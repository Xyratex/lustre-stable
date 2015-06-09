/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2011 Whamcloud, Inc.
 */
/*
 * Copyright (c) 2011 Xyratex, Inc.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/mdd/mdd_device.c
 *
 * Lustre Metadata Server (mdd) routines
 *
 * Author: Wang Di <wangdi@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_MDS

#include <obd_class.h>
#include <lprocfs_status.h>
#include <lustre_fid.h>
#include <lustre_mds.h>
#include <lustre_disk.h>      /* for changelogs */
#include <lustre_param.h>
#include <lustre_fid.h>

#include "mdd_internal.h"

const struct md_device_operations mdd_ops;
static struct lu_device_type mdd_device_type;

static const char mdd_root_dir_name[] = "ROOT";
static const char mdd_obf_dir_name[] = "fid";

/* Slab for MDD object allocation */
cfs_mem_cache_t *mdd_object_kmem;

static struct lu_kmem_descr mdd_caches[] = {
	{
		.ckd_cache = &mdd_object_kmem,
		.ckd_name  = "mdd_obj",
		.ckd_size  = sizeof(struct mdd_object)
	},
	{
		.ckd_cache = NULL
	}
};

static int mdd_device_init(const struct lu_env *env, struct lu_device *d,
                           const char *name, struct lu_device *next)
{
        struct mdd_device *mdd = lu2mdd_dev(d);
        int rc;
        ENTRY;

        mdd->mdd_child = lu2dt_dev(next);

        /* Prepare transactions callbacks. */
        mdd->mdd_txn_cb.dtc_txn_start = mdd_txn_start_cb;
        mdd->mdd_txn_cb.dtc_txn_stop = mdd_txn_stop_cb;
        mdd->mdd_txn_cb.dtc_txn_commit = NULL;
        mdd->mdd_txn_cb.dtc_cookie = mdd;
        mdd->mdd_txn_cb.dtc_tag = LCT_MD_THREAD;
        CFS_INIT_LIST_HEAD(&mdd->mdd_txn_cb.dtc_linkage);

	dt_txn_callback_add(mdd->mdd_child, &mdd->mdd_txn_cb);

        mdd->mdd_atime_diff = MAX_ATIME_DIFF;
        /* sync permission changes */
        mdd->mdd_sync_permission = 1;

        rc = mdd_procfs_init(mdd, name);
        RETURN(rc);
}

static struct lu_device *mdd_device_fini(const struct lu_env *env,
                                         struct lu_device *d)
{
        struct mdd_device *mdd = lu2mdd_dev(d);
        struct lu_device *next = &mdd->mdd_child->dd_lu_dev;
        int rc;

        rc = mdd_procfs_fini(mdd);
        if (rc) {
                CERROR("proc fini error %d \n", rc);
                return ERR_PTR(rc);
        }
        return next;
}

static void mdd_changelog_fini(const struct lu_env *env,
                               struct mdd_device *mdd);

static void mdd_device_shutdown(const struct lu_env *env,
                                struct mdd_device *m, struct lustre_cfg *cfg)
{
        ENTRY;
        mdd_changelog_fini(env, m);
        dt_txn_callback_del(m->mdd_child, &m->mdd_txn_cb);
        if (m->mdd_dot_lustre_objs.mdd_obf)
                mdd_object_put(env, m->mdd_dot_lustre_objs.mdd_obf);
        if (m->mdd_dot_lustre)
                mdd_object_put(env, m->mdd_dot_lustre);
        if (m->mdd_obd_dev)
                mdd_fini_obd(env, m, cfg);
        orph_index_fini(env, m);
        /* remove upcall device*/
        md_upcall_fini(&m->mdd_md_dev);
        EXIT;
}

static int changelog_init_cb(struct llog_handle *llh, struct llog_rec_hdr *hdr,
                             void *data)
{
        struct mdd_device *mdd = (struct mdd_device *)data;
        struct llog_changelog_rec *rec = (struct llog_changelog_rec *)hdr;
        ENTRY;

        LASSERT(llh->lgh_hdr->llh_flags & LLOG_F_IS_PLAIN);
        LASSERT(rec->cr_hdr.lrh_type == CHANGELOG_REC);

        CDEBUG(D_INFO,
               "seeing record at index %d/%d/"LPU64" t=%x %.*s in log "LPX64"\n",
               hdr->lrh_index, rec->cr_hdr.lrh_index, rec->cr.cr_index,
               rec->cr.cr_type, rec->cr.cr_namelen, rec->cr.cr_name,
               llh->lgh_id.lgl_oid);

        mdd->mdd_cl.mc_index = rec->cr.cr_index;
        RETURN(LLOG_PROC_BREAK);
}

static int changelog_user_init_cb(struct llog_handle *llh,
                                  struct llog_rec_hdr *hdr, void *data)
{
        struct mdd_device *mdd = (struct mdd_device *)data;
        struct llog_changelog_user_rec *rec =
                (struct llog_changelog_user_rec *)hdr;
        ENTRY;

        LASSERT(llh->lgh_hdr->llh_flags & LLOG_F_IS_PLAIN);
        LASSERT(rec->cur_hdr.lrh_type == CHANGELOG_USER_REC);

        CDEBUG(D_INFO, "seeing user at index %d/%d id=%d endrec="LPU64
               " in log "LPX64"\n", hdr->lrh_index, rec->cur_hdr.lrh_index,
               rec->cur_id, rec->cur_endrec, llh->lgh_id.lgl_oid);

        cfs_spin_lock(&mdd->mdd_cl.mc_user_lock);
        mdd->mdd_cl.mc_lastuser = rec->cur_id;
	if (rec->cur_endrec > mdd->mdd_cl.mc_index)
		mdd->mdd_cl.mc_index = rec->cur_endrec;
        cfs_spin_unlock(&mdd->mdd_cl.mc_user_lock);

        RETURN(LLOG_PROC_BREAK);
}


static int mdd_changelog_llog_init(const struct lu_env *env, struct mdd_device *mdd)
{
        struct obd_device *obd = mdd2obd_dev(mdd);
        struct llog_ctxt *ctxt;
        int rc;

        /* Find last changelog entry number */
        ctxt = llog_get_context(obd, LLOG_CHANGELOG_ORIG_CTXT);
        if (ctxt == NULL) {
                CERROR("no changelog context\n");
                return -EINVAL;
        }
        if (!ctxt->loc_handle) {
                llog_ctxt_put(ctxt);
                return -EINVAL;
        }

        rc = llog_cat_reverse_process(ctxt->loc_handle, changelog_init_cb, mdd);
        llog_ctxt_put(ctxt);

        if (rc < 0) {
                CERROR("changelog init failed: %d\n", rc);
                return rc;
        }
        CDEBUG(D_IOCTL, "changelog starting index="LPU64"\n",
               mdd->mdd_cl.mc_index);

        /* Find last changelog user id */
        ctxt = llog_get_context(obd, LLOG_CHANGELOG_USER_ORIG_CTXT);
        if (ctxt == NULL) {
                CERROR("no changelog user context\n");
                return -EINVAL;
        }
        if (!ctxt->loc_handle) {
                llog_ctxt_put(ctxt);
                return -EINVAL;
        }

        rc = llog_cat_reverse_process(ctxt->loc_handle, changelog_user_init_cb,
                                      mdd);
        llog_ctxt_put(ctxt);

        if (rc < 0) {
                CERROR("changelog user init failed: %d\n", rc);
                return rc;
        }

        /* If we have registered users, assume we want changelogs on */
        if (mdd->mdd_cl.mc_lastuser > 0)
		rc = mdd_changelog_on(env, mdd, 1);

        return rc;
}

static int mdd_changelog_init(const struct lu_env *env, struct mdd_device *mdd)
{
        int rc;

        mdd->mdd_cl.mc_index = 0;
        cfs_spin_lock_init(&mdd->mdd_cl.mc_lock);
        mdd->mdd_cl.mc_starttime = cfs_time_current_64();
        mdd->mdd_cl.mc_flags = 0; /* off by default */
        mdd->mdd_cl.mc_mask = CHANGELOG_DEFMASK;
        cfs_spin_lock_init(&mdd->mdd_cl.mc_user_lock);
        mdd->mdd_cl.mc_lastuser = 0;

	rc = mdd_changelog_llog_init(env, mdd);
        if (rc) {
                CERROR("Changelog setup during init failed %d\n", rc);
                mdd->mdd_cl.mc_flags |= CLM_ERR;
        }

        return rc;
}

static void mdd_changelog_fini(const struct lu_env *env, struct mdd_device *mdd)
{
        mdd->mdd_cl.mc_flags = 0;
}

/* Start / stop recording */
int mdd_changelog_on(const struct lu_env *env, struct mdd_device *mdd, int on)
{
        int rc = 0;

        if ((on == 1) && ((mdd->mdd_cl.mc_flags & CLM_ON) == 0)) {
                LCONSOLE_INFO("%s: changelog on\n", mdd2obd_dev(mdd)->obd_name);
                if (mdd->mdd_cl.mc_flags & CLM_ERR) {
                        CERROR("Changelogs cannot be enabled due to error "
                               "condition (see %s log).\n",
                               mdd2obd_dev(mdd)->obd_name);
                        rc = -ESRCH;
                } else {
                        cfs_spin_lock(&mdd->mdd_cl.mc_lock);
                        mdd->mdd_cl.mc_flags |= CLM_ON;
                        cfs_spin_unlock(&mdd->mdd_cl.mc_lock);
			rc = mdd_changelog_write_header(env, mdd, CLM_START);
                }
        } else if ((on == 0) && ((mdd->mdd_cl.mc_flags & CLM_ON) == CLM_ON)) {
                LCONSOLE_INFO("%s: changelog off\n",mdd2obd_dev(mdd)->obd_name);
                rc = mdd_changelog_write_header(env, mdd, CLM_FINI);
                cfs_spin_lock(&mdd->mdd_cl.mc_lock);
                mdd->mdd_cl.mc_flags &= ~CLM_ON;
                cfs_spin_unlock(&mdd->mdd_cl.mc_lock);
        }
        return rc;
}

static __u64 cl_time(void) {
        cfs_fs_time_t time;

        cfs_fs_time_current(&time);
        return (((__u64)time.tv_sec) << 30) + time.tv_nsec;
}

/** Add a changelog entry \a rec to the changelog llog
 * \param mdd
 * \param rec
 * \param handle - currently ignored since llogs start their own transaction;
 *                 this will hopefully be fixed in llog rewrite
 * \retval 0 ok
 */
int mdd_changelog_llog_write(struct mdd_device         *mdd,
                             struct llog_changelog_rec *rec,
                             struct thandle            *handle)
{
        struct obd_device *obd = mdd2obd_dev(mdd);
        struct llog_ctxt *ctxt;
        int rc;

        rec->cr_hdr.lrh_len = llog_data_len(sizeof(*rec) + rec->cr.cr_namelen);
        /* llog_lvfs_write_rec sets the llog tail len */
        rec->cr_hdr.lrh_type = CHANGELOG_REC;
        rec->cr.cr_time = cl_time();
        cfs_spin_lock(&mdd->mdd_cl.mc_lock);
        /* NB: I suppose it's possible llog_add adds out of order wrt cr_index,
           but as long as the MDD transactions are ordered correctly for e.g.
           rename conflicts, I don't think this should matter. */
        rec->cr.cr_index = ++mdd->mdd_cl.mc_index;
        cfs_spin_unlock(&mdd->mdd_cl.mc_lock);
        ctxt = llog_get_context(obd, LLOG_CHANGELOG_ORIG_CTXT);
        if (ctxt == NULL)
                return -ENXIO;

        /* nested journal transaction */
        rc = llog_add(ctxt, &rec->cr_hdr, NULL, NULL, 0);
        llog_ctxt_put(ctxt);

        return rc;
}

/** Add a changelog_ext entry \a rec to the changelog llog
 * \param mdd
 * \param rec
 * \param handle - currently ignored since llogs start their own transaction;
 *		this will hopefully be fixed in llog rewrite
 * \retval 0 ok
 */
int mdd_changelog_ext_llog_write(struct mdd_device *mdd,
				 struct llog_changelog_ext_rec *rec,
				 struct thandle *handle)
{
	struct obd_device *obd = mdd2obd_dev(mdd);
	struct llog_ctxt *ctxt;
	int rc;

	rec->cr_hdr.lrh_len = llog_data_len(sizeof(*rec) + rec->cr.cr_namelen);
	/* llog_lvfs_write_rec sets the llog tail len */
	rec->cr_hdr.lrh_type = CHANGELOG_REC;
	rec->cr.cr_time = cl_time();
	cfs_spin_lock(&mdd->mdd_cl.mc_lock);
	/* NB: I suppose it's possible llog_add adds out of order wrt cr_index,
	 * but as long as the MDD transactions are ordered correctly for e.g.
	 * rename conflicts, I don't think this should matter. */
	rec->cr.cr_index = ++mdd->mdd_cl.mc_index;
	cfs_spin_unlock(&mdd->mdd_cl.mc_lock);
	ctxt = llog_get_context(obd, LLOG_CHANGELOG_ORIG_CTXT);
	if (ctxt == NULL)
		return -ENXIO;

	/* nested journal transaction */
	rc = llog_add(ctxt, &rec->cr_hdr, NULL, NULL, 0);
	llog_ctxt_put(ctxt);

	return rc;
}

/** Remove entries with indicies up to and including \a endrec from the
 *  changelog
 * \param mdd
 * \param endrec
 * \retval 0 ok
 */
int mdd_changelog_llog_cancel(const struct lu_env *env,
			      struct mdd_device *mdd, long long endrec)
{
        struct obd_device *obd = mdd2obd_dev(mdd);
        struct llog_ctxt *ctxt;
        long long unsigned cur;
        int rc;

        ctxt = llog_get_context(obd, LLOG_CHANGELOG_ORIG_CTXT);
        if (ctxt == NULL)
                return -ENXIO;

        cfs_mutex_lock(&ctxt->loc_mutex);
        cfs_spin_lock(&mdd->mdd_cl.mc_lock);
        cur = (long long)mdd->mdd_cl.mc_index;
        cfs_spin_unlock(&mdd->mdd_cl.mc_lock);
        if (endrec > cur)
                endrec = cur;

        /* purge to "0" is shorthand for everything */
        if (endrec == 0)
                endrec = cur;

        /* If purging all records, write a header entry so we don't have an
           empty catalog and we're sure to have a valid starting index next
           time.  In case of crash, we just restart with old log so we're
           allright. */
        if (endrec == cur) {
		rc = mdd_changelog_write_header(env, mdd, CLM_PURGE);
                if (rc)
                      goto out;
        }

        /* Some records were purged, so reset repeat-access time (so we
           record new mtime update records, so users can see a file has been
           changed since the last purge) */
        mdd->mdd_cl.mc_starttime = cfs_time_current_64();

        rc = llog_cancel(ctxt, NULL, 1, (struct llog_cookie *)&endrec, 0);
out:
        cfs_mutex_unlock(&ctxt->loc_mutex);
        llog_ctxt_put(ctxt);
        return rc;
}

/** Add a CL_MARK record to the changelog
 * \param mdd
 * \param markerflags - CLM_*
 * \retval 0 ok
 */
int mdd_changelog_write_header(const struct lu_env *env,
			       struct mdd_device *mdd, int markerflags)
{
        struct obd_device *obd = mdd2obd_dev(mdd);
        struct llog_changelog_rec *rec;
        int reclen;
	struct thandle *handle;
        int len = strlen(obd->obd_name);
	int rc = 0;
        ENTRY;

        reclen = llog_data_len(sizeof(*rec) + len);
        OBD_ALLOC(rec, reclen);
        if (rec == NULL)
                RETURN(-ENOMEM);

        rec->cr.cr_flags = CLF_VERSION;
        rec->cr.cr_type = CL_MARK;
        rec->cr.cr_namelen = len;
        memcpy(rec->cr.cr_name, obd->obd_name, rec->cr.cr_namelen);
        /* Status and action flags */
        rec->cr.cr_markerflags = mdd->mdd_cl.mc_flags | markerflags;

	if (mdd->mdd_cl.mc_mask & (1 << CL_MARK)) {
		mdd_txn_param_build(env, mdd, MDD_TXN_NOP, 1);
		handle = mdd_trans_start(env, mdd);
		if (IS_ERR(handle)) {
			rc = PTR_ERR(handle);
		} else {
			rc = mdd_changelog_llog_write(mdd, rec, handle);
			mdd_trans_stop(env, mdd, rc, handle);
		}
	}

        /* assume on or off event; reset repeat-access time */
        mdd->mdd_cl.mc_starttime = cfs_time_current_64();

        OBD_FREE(rec, reclen);
        RETURN(rc);
}

/**
 * Create ".lustre" directory.
 */
static int create_dot_lustre_dir(const struct lu_env *env, struct mdd_device *m)
{
        struct lu_fid *fid = &mdd_env_info(env)->mti_fid;
        struct md_object *mdo;
        int rc;

        memcpy(fid, &LU_DOT_LUSTRE_FID, sizeof(struct lu_fid));
        mdo = llo_store_create_index(env, &m->mdd_md_dev, m->mdd_child,
                                     mdd_root_dir_name, dot_lustre_name,
                                     fid, &dt_directory_features);
        /* .lustre dir may be already present */
        if (IS_ERR(mdo) && PTR_ERR(mdo) != -EEXIST) {
                rc = PTR_ERR(mdo);
                CERROR("creating obj [%s] fid = "DFID" rc = %d\n",
                        dot_lustre_name, PFID(fid), rc);
                RETURN(rc);
        }

        if (!IS_ERR(mdo))
                lu_object_put(env, &mdo->mo_lu);

        return 0;
}


static int dot_lustre_mdd_permission(const struct lu_env *env,
                                     struct md_object *pobj,
                                     struct md_object *cobj,
                                     struct md_attr *attr, int mask)
{
        if (mask & ~(MAY_READ | MAY_EXEC))
                return -EPERM;
        else
                return 0;
}

static int dot_lustre_mdd_xattr_get(const struct lu_env *env,
                                    struct md_object *obj, struct lu_buf *buf,
                                    const char *name)
{
        return 0;
}

static int dot_lustre_mdd_xattr_list(const struct lu_env *env,
                                     struct md_object *obj, struct lu_buf *buf)
{
        return 0;
}

static int dot_lustre_mdd_xattr_set(const struct lu_env *env,
                                    struct md_object *obj,
                                    const struct lu_buf *buf, const char *name,
                                    int fl)
{
        return -EPERM;
}

static int dot_lustre_mdd_xattr_del(const struct lu_env *env,
                                    struct md_object *obj,
                                    const char *name)
{
        return -EPERM;
}

static int dot_lustre_mdd_readlink(const struct lu_env *env,
                                   struct md_object *obj, struct lu_buf *buf)
{
        return 0;
}

static int dot_lustre_mdd_object_create(const struct lu_env *env,
                                        struct md_object *obj,
                                        const struct md_op_spec *spec,
                                        struct md_attr *ma)
{
        return -EPERM;
}

static int dot_lustre_mdd_ref_add(const struct lu_env *env,
                                  struct md_object *obj,
                                  const struct md_attr *ma)
{
        return -EPERM;
}

static int dot_lustre_mdd_ref_del(const struct lu_env *env,
                                  struct md_object *obj,
                                  struct md_attr *ma)
{
        return -EPERM;
}

static int dot_lustre_mdd_open(const struct lu_env *env, struct md_object *obj,
                               int flags)
{
        struct mdd_object *mdd_obj = md2mdd_obj(obj);

        mdd_write_lock(env, mdd_obj, MOR_TGT_CHILD);
        mdd_obj->mod_count++;
        mdd_write_unlock(env, mdd_obj);

        return 0;
}

static inline int dot_lustre_mdd_close_get_req_sz(const struct lu_env *env,
						  struct md_object *mo,
						  int *md_size,
						  int *logcookies_size)
{
	*md_size = 0;
	*logcookies_size = 0;

	return 0;
}

static int dot_lustre_mdd_close(const struct lu_env *env, struct md_object *obj,
                                struct md_attr *ma, int mode)
{
        struct mdd_object *mdd_obj = md2mdd_obj(obj);

        mdd_write_lock(env, mdd_obj, MOR_TGT_CHILD);
        mdd_obj->mod_count--;
        mdd_write_unlock(env, mdd_obj);

        return 0;
}

static int dot_lustre_mdd_object_sync(const struct lu_env *env,
                                      struct md_object *obj)
{
        return -ENOSYS;
}

static dt_obj_version_t dot_lustre_mdd_version_get(const struct lu_env *env,
                                                   struct md_object *obj)
{
        return 0;
}

static void dot_lustre_mdd_version_set(const struct lu_env *env,
                                       struct md_object *obj,
                                       dt_obj_version_t version)
{
        return;
}

static int dot_lustre_mdd_path(const struct lu_env *env, struct md_object *obj,
                           char *path, int pathlen, __u64 *recno, int *linkno)
{
        return -ENOSYS;
}

static int dot_file_lock(const struct lu_env *env, struct md_object *obj,
                         struct lov_mds_md *lmm, struct ldlm_extent *extent,
                         struct lustre_handle *lockh)
{
        return -ENOSYS;
}

static int dot_file_unlock(const struct lu_env *env, struct md_object *obj,
                           struct lov_mds_md *lmm, struct lustre_handle *lockh)
{
        return -ENOSYS;
}

static struct md_object_operations mdd_dot_lustre_obj_ops = {
        .moo_permission    = dot_lustre_mdd_permission,
	.moo_attr_get      = mdd_attr_get,
	.moo_attr_set      = mdd_attr_set,
        .moo_xattr_get     = dot_lustre_mdd_xattr_get,
        .moo_xattr_list    = dot_lustre_mdd_xattr_list,
        .moo_xattr_set     = dot_lustre_mdd_xattr_set,
        .moo_xattr_del     = dot_lustre_mdd_xattr_del,
        .moo_readpage      = mdd_readpage,
        .moo_readlink      = dot_lustre_mdd_readlink,
        .moo_object_create = dot_lustre_mdd_object_create,
        .moo_ref_add       = dot_lustre_mdd_ref_add,
        .moo_ref_del       = dot_lustre_mdd_ref_del,
        .moo_open          = dot_lustre_mdd_open,
	.moo_close_get_sz  = dot_lustre_mdd_close_get_req_sz,
        .moo_close         = dot_lustre_mdd_close,
        .moo_capa_get      = mdd_capa_get,
        .moo_object_sync   = dot_lustre_mdd_object_sync,
        .moo_version_get   = dot_lustre_mdd_version_get,
        .moo_version_set   = dot_lustre_mdd_version_set,
        .moo_path          = dot_lustre_mdd_path,
        .moo_file_lock     = dot_file_lock,
        .moo_file_unlock   = dot_file_unlock,
};


static int dot_lustre_mdd_lookup(const struct lu_env *env, struct md_object *p,
                                 const struct lu_name *lname, struct lu_fid *f,
                                 struct md_op_spec *spec)
{
        if (strcmp(lname->ln_name, mdd_obf_dir_name) == 0)
                *f = LU_OBF_FID;
        else
                return -ENOENT;

        return 0;
}

static mdl_mode_t dot_lustre_mdd_lock_mode(const struct lu_env *env,
                                           struct md_object *obj,
                                           mdl_mode_t mode)
{
        return MDL_MINMODE;
}

static int dot_lustre_mdd_create(const struct lu_env *env,
                                 struct md_object *pobj,
                                 const struct lu_name *lname,
                                 struct md_object *child,
                                 struct md_op_spec *spec,
                                 struct md_attr* ma)
{
        return -EPERM;
}

static int dot_lustre_mdd_create_data(const struct lu_env *env,
                                      struct md_object *p,
                                      struct md_object *o,
                                      const struct md_op_spec *spec,
                                      struct md_attr *ma)
{
        return -EPERM;
}

static int dot_lustre_mdd_rename(const struct lu_env *env,
                                 struct md_object *src_pobj,
                                 struct md_object *tgt_pobj,
                                 const struct lu_fid *lf,
                                 const struct lu_name *lsname,
                                 struct md_object *tobj,
                                 const struct lu_name *ltname,
                                 struct md_attr *ma)
{
        return -EPERM;
}

static int dot_lustre_mdd_link(const struct lu_env *env,
                               struct md_object *tgt_obj,
                               struct md_object *src_obj,
                               const struct lu_name *lname,
                               struct md_attr *ma)
{
        return -EPERM;
}

static int dot_lustre_mdd_unlink(const struct lu_env *env,
                                 struct md_object *pobj,
                                 struct md_object *cobj,
                                 const struct lu_name *lname,
                                 struct md_attr *ma)
{
        return -EPERM;
}

static int dot_lustre_mdd_name_insert(const struct lu_env *env,
                                      struct md_object *obj,
                                      const struct lu_name *lname,
                                      const struct lu_fid *fid,
                                      const struct md_attr *ma)
{
        return -EPERM;
}

static int dot_lustre_mdd_name_remove(const struct lu_env *env,
                                      struct md_object *obj,
                                      const struct lu_name *lname,
                                      const struct md_attr *ma)
{
        return -EPERM;
}

static int dot_lustre_mdd_rename_tgt(const struct lu_env *env,
                                     struct md_object *pobj,
                                     struct md_object *tobj,
                                     const struct lu_fid *fid,
                                     const struct lu_name *lname,
                                     struct md_attr *ma)
{
        return -EPERM;
}


static struct md_dir_operations mdd_dot_lustre_dir_ops = {
        .mdo_is_subdir   = mdd_is_subdir,
        .mdo_lookup      = dot_lustre_mdd_lookup,
        .mdo_lock_mode   = dot_lustre_mdd_lock_mode,
        .mdo_create      = dot_lustre_mdd_create,
        .mdo_create_data = dot_lustre_mdd_create_data,
        .mdo_rename      = dot_lustre_mdd_rename,
        .mdo_link        = dot_lustre_mdd_link,
        .mdo_unlink      = dot_lustre_mdd_unlink,
        .mdo_name_insert = dot_lustre_mdd_name_insert,
        .mdo_name_remove = dot_lustre_mdd_name_remove,
        .mdo_rename_tgt  = dot_lustre_mdd_rename_tgt,
};

static int obf_attr_get(const struct lu_env *env, struct md_object *obj,
                        struct md_attr *ma)
{
        int rc = 0;

        if (ma->ma_need & MA_INODE) {
                struct mdd_device *mdd = mdo2mdd(obj);

                /* "fid" is a virtual object and hence does not have any "real"
                 * attributes. So we reuse attributes of .lustre for "fid" dir */
                ma->ma_need |= MA_INODE;
		rc = mdd_attr_get(env, &mdd->mdd_dot_lustre->mod_obj, ma);
                if (rc)
                        return rc;
                ma->ma_valid |= MA_INODE;
        }

        /* "fid" directory does not have any striping information. */
        if (ma->ma_need & MA_LOV) {
                struct mdd_object *mdd_obj = md2mdd_obj(obj);

                if (ma->ma_valid & MA_LOV)
                        return 0;

                if (!(S_ISREG(mdd_object_type(mdd_obj)) ||
                      S_ISDIR(mdd_object_type(mdd_obj))))
                        return 0;

                if (ma->ma_need & MA_LOV_DEF) {
                        rc = mdd_get_default_md(mdd_obj, ma->ma_lmm);
                        if (rc > 0) {
                                ma->ma_lmm_size = rc;
                                ma->ma_valid |= MA_LOV;
                                rc = 0;
                        }
                }
        }

        return rc;
}

static int obf_attr_set(const struct lu_env *env, struct md_object *obj,
                        const struct md_attr *ma)
{
        return -EPERM;
}

static int obf_xattr_list(const struct lu_env *env,
			  struct md_object *obj, struct lu_buf *buf)
{
	return -ENODATA;
}

static int obf_xattr_get(const struct lu_env *env,
                         struct md_object *obj, struct lu_buf *buf,
                         const char *name)
{
	return -ENODATA;
}

static int obf_xattr_set(const struct lu_env *env,
			 struct md_object *obj,
			 const struct lu_buf *buf, const char *name,
			 int fl)
{
	return -EPERM;
}

static int obf_xattr_del(const struct lu_env *env,
			 struct md_object *obj,
			 const char *name)
{
	return -EPERM;
}

static int obf_mdd_open(const struct lu_env *env, struct md_object *obj,
                        int flags)
{
        struct mdd_object *mdd_obj = md2mdd_obj(obj);

        mdd_write_lock(env, mdd_obj, MOR_TGT_CHILD);
        mdd_obj->mod_count++;
        mdd_write_unlock(env, mdd_obj);

        return 0;
}

static inline int obf_mdd_close_get_req_sz(const struct lu_env *env,
					   struct md_object *mo,
					   int *md_size, int *logcookies_size)
{
	*md_size = 0;
	*logcookies_size = 0;

	return 0;
}

static int obf_mdd_close(const struct lu_env *env, struct md_object *obj,
                         struct md_attr *ma, int mode)
{
        struct mdd_object *mdd_obj = md2mdd_obj(obj);

        mdd_write_lock(env, mdd_obj, MOR_TGT_CHILD);
        mdd_obj->mod_count--;
        mdd_write_unlock(env, mdd_obj);

        return 0;
}

/** Nothing to list in "fid" directory */
static int obf_mdd_readpage(const struct lu_env *env, struct md_object *obj,
                            const struct lu_rdpg *rdpg)
{
        return -EPERM;
}

static int obf_path(const struct lu_env *env, struct md_object *obj,
                    char *path, int pathlen, __u64 *recno, int *linkno)
{
        return -ENOSYS;
}

static struct md_object_operations mdd_obf_obj_ops = {
	.moo_attr_get    = obf_attr_get,
	.moo_attr_set    = obf_attr_set,
	.moo_xattr_list  = obf_xattr_list,
	.moo_xattr_get   = obf_xattr_get,
	.moo_xattr_set   = obf_xattr_set,
	.moo_xattr_del   = obf_xattr_del,
	.moo_open        = obf_mdd_open,
	.moo_close_get_sz= obf_mdd_close_get_req_sz,
	.moo_close       = obf_mdd_close,
	.moo_readpage    = obf_mdd_readpage,
	.moo_path        = obf_path
};

/**
 * Lookup method for "fid" object. Only filenames with correct SEQ:OID format
 * are valid. We also check if object with passed fid exists or not.
 */
static int obf_lookup(const struct lu_env *env, struct md_object *p,
                      const struct lu_name *lname, struct lu_fid *f,
                      struct md_op_spec *spec)
{
        char *name = (char *)lname->ln_name;
        struct mdd_device *mdd = mdo2mdd(p);
        struct mdd_object *child;
        int rc = 0;

        while (*name == '[')
                name++;

        sscanf(name, SFID, RFID(f));
        if (!fid_is_sane(f)) {
		CWARN("%s: bad FID format [%s], should be "DFID"\n",
		      mdd->mdd_obd_dev->obd_name, lname->ln_name,
		      (__u64)FID_SEQ_NORMAL, 1, 0);
                GOTO(out, rc = -EINVAL);
        }

	if (!fid_is_norm(f)) {
		CWARN("%s: "DFID" is invalid, sequence should be "
		      ">= "LPX64"\n", mdd->mdd_obd_dev->obd_name, PFID(f),
		      (__u64)FID_SEQ_NORMAL);
		GOTO(out, rc = -EINVAL);
	}

        /* Check if object with this fid exists */
        child = mdd_object_find(env, mdd, f);
        if (child == NULL)
                GOTO(out, rc = 0);
        if (IS_ERR(child))
                GOTO(out, rc = PTR_ERR(child));

        if (mdd_object_exists(child) == 0)
                rc = -ENOENT;

        mdd_object_put(env, child);

out:
        return rc;
}

static int obf_create(const struct lu_env *env, struct md_object *pobj,
                      const struct lu_name *lname, struct md_object *child,
                      struct md_op_spec *spec, struct md_attr* ma)
{
        return -EPERM;
}

static int obf_rename(const struct lu_env *env,
                      struct md_object *src_pobj, struct md_object *tgt_pobj,
                      const struct lu_fid *lf, const struct lu_name *lsname,
                      struct md_object *tobj, const struct lu_name *ltname,
                      struct md_attr *ma)
{
        return -EPERM;
}

static int obf_link(const struct lu_env *env, struct md_object *tgt_obj,
                    struct md_object *src_obj, const struct lu_name *lname,
                    struct md_attr *ma)
{
        return -EPERM;
}

static int obf_unlink(const struct lu_env *env, struct md_object *pobj,
                      struct md_object *cobj, const struct lu_name *lname,
                      struct md_attr *ma)
{
        return -EPERM;
}

static struct md_dir_operations mdd_obf_dir_ops = {
        .mdo_lookup = obf_lookup,
        .mdo_create = obf_create,
        .mdo_rename = obf_rename,
        .mdo_link   = obf_link,
        .mdo_unlink = obf_unlink
};

/**
 * Create special in-memory "fid" object for open-by-fid.
 */
static int mdd_obf_setup(const struct lu_env *env, struct mdd_device *m)
{
        struct mdd_object *mdd_obf;
        struct lu_object *obf_lu_obj;
        int rc = 0;

        m->mdd_dot_lustre_objs.mdd_obf = mdd_object_find(env, m,
                                                         &LU_OBF_FID);
        if (m->mdd_dot_lustre_objs.mdd_obf == NULL ||
            IS_ERR(m->mdd_dot_lustre_objs.mdd_obf))
                GOTO(out, rc = -ENOENT);

        mdd_obf = m->mdd_dot_lustre_objs.mdd_obf;
        mdd_obf->mod_obj.mo_dir_ops = &mdd_obf_dir_ops;
        mdd_obf->mod_obj.mo_ops = &mdd_obf_obj_ops;
        /* Don't allow objects to be created in "fid" dir */
        mdd_obf->mod_flags |= IMMUTE_OBJ;

        obf_lu_obj = mdd2lu_obj(mdd_obf);
        obf_lu_obj->lo_header->loh_attr |= (LOHA_EXISTS | S_IFDIR);

out:
        return rc;
}

/** Setup ".lustre" directory object */
static int mdd_dot_lustre_setup(const struct lu_env *env, struct mdd_device *m)
{
        struct dt_object *dt_dot_lustre;
        struct lu_fid *fid = &mdd_env_info(env)->mti_fid;
        int rc;

        rc = create_dot_lustre_dir(env, m);
        if (rc)
                return rc;

        dt_dot_lustre = dt_store_open(env, m->mdd_child, mdd_root_dir_name,
                                      dot_lustre_name, fid);
        if (IS_ERR(dt_dot_lustre)) {
                rc = PTR_ERR(dt_dot_lustre);
                GOTO(out, rc);
        }

        /* references are released in mdd_device_shutdown() */
        m->mdd_dot_lustre = lu2mdd_obj(lu_object_locate(dt_dot_lustre->do_lu.lo_header,
                                                        &mdd_device_type));

        m->mdd_dot_lustre->mod_obj.mo_dir_ops = &mdd_dot_lustre_dir_ops;
        m->mdd_dot_lustre->mod_obj.mo_ops = &mdd_dot_lustre_obj_ops;

        rc = mdd_obf_setup(env, m);
        if (rc)
                CERROR("Error initializing \"fid\" object - %d.\n", rc);

out:
        RETURN(rc);
}

static int mdd_process_config(const struct lu_env *env,
                              struct lu_device *d, struct lustre_cfg *cfg)
{
        struct mdd_device *m    = lu2mdd_dev(d);
        struct dt_device  *dt   = m->mdd_child;
        struct lu_device  *next = &dt->dd_lu_dev;
        int rc;
        ENTRY;

        switch (cfg->lcfg_command) {
        case LCFG_PARAM: {
                struct lprocfs_static_vars lvars;

                lprocfs_mdd_init_vars(&lvars);
                rc = class_process_proc_param(PARAM_MDD, lvars.obd_vars, cfg,m);
                if (rc > 0 || rc == -ENOSYS)
                        /* we don't understand; pass it on */
                        rc = next->ld_ops->ldo_process_config(env, next, cfg);
                break;
        }
        case LCFG_SETUP:
                rc = next->ld_ops->ldo_process_config(env, next, cfg);
                if (rc)
                        GOTO(out, rc);
                dt->dd_ops->dt_conf_get(env, dt, &m->mdd_dt_conf);

                rc = mdd_init_obd(env, m, cfg);
                if (rc) {
                        CERROR("lov init error %d \n", rc);
                        GOTO(out, rc);
                }
                rc = mdd_txn_init_credits(env, m);
                if (rc)
                        break;

                mdd_changelog_init(env, m);
                break;
        case LCFG_CLEANUP:
		lu_dev_del_linkage(d->ld_site, d);
                mdd_device_shutdown(env, m, cfg);
        default:
                rc = next->ld_ops->ldo_process_config(env, next, cfg);
                break;
        }
out:
        RETURN(rc);
}

#if 0
static int mdd_lov_set_nextid(const struct lu_env *env,
                              struct mdd_device *mdd)
{
        struct mds_obd *mds = &mdd->mdd_obd_dev->u.mds;
        int rc;
        ENTRY;

        LASSERT(mds->mds_lov_objids != NULL);
        rc = obd_set_info_async(mds->mds_lov_exp, strlen(KEY_NEXT_ID),
                                KEY_NEXT_ID, mds->mds_lov_desc.ld_tgt_count,
                                mds->mds_lov_objids, NULL);

        RETURN(rc);
}

static int mdd_cleanup_unlink_llog(const struct lu_env *env,
                                   struct mdd_device *mdd)
{
        /* XXX: to be implemented! */
        return 0;
}
#endif

static int mdd_recovery_complete(const struct lu_env *env,
                                 struct lu_device *d)
{
        struct mdd_device *mdd = lu2mdd_dev(d);
        struct lu_device *next = &mdd->mdd_child->dd_lu_dev;
        struct obd_device *obd = mdd2obd_dev(mdd);
        int rc;
        ENTRY;

        LASSERT(mdd != NULL);
        LASSERT(obd != NULL);
#if 0
        /* XXX: Do we need this in new stack? */
        rc = mdd_lov_set_nextid(env, mdd);
        if (rc) {
                CERROR("mdd_lov_set_nextid() failed %d\n",
                       rc);
                RETURN(rc);
        }

        /* XXX: cleanup unlink. */
        rc = mdd_cleanup_unlink_llog(env, mdd);
        if (rc) {
                CERROR("mdd_cleanup_unlink_llog() failed %d\n",
                       rc);
                RETURN(rc);
        }
#endif
        /* Call that with obd_recovering = 1 just to update objids */
        obd_notify(obd->u.mds.mds_lov_obd, NULL, (obd->obd_async_recov ?
                    OBD_NOTIFY_SYNC_NONBLOCK : OBD_NOTIFY_SYNC), NULL);

        /* Drop obd_recovering to 0 and call o_postrecov to recover mds_lov */
        cfs_spin_lock(&obd->obd_dev_lock);
        obd->obd_recovering = 0;
        cfs_spin_unlock(&obd->obd_dev_lock);
        obd->obd_type->typ_dt_ops->o_postrecov(obd);

        /* XXX: orphans handling. */
        __mdd_orphan_cleanup(env, mdd);
        rc = next->ld_ops->ldo_recovery_complete(env, next);

        RETURN(rc);
}

#define MDD_REBUILD_THREAD_NR cfs_num_online_cpus()

/* mdd_rebuild_info flags */
/* Let the threads to start the rebuild once this flag is dropped. */
#define MRT_PREPARE     LDF_REBUILD_LAST
/* Set this flag to interrupt the rebuild work. */
#define MRT_STOPPING    LDF_REBUILD_LAST << 1

/* A common rebuild info for all the threads. */
struct mdd_rebuild_info {
        struct mdd_device      *ri_mdd;

        /** The sequence manager for the new FID allocation. */
        struct lu_client_seq   *ri_seq;

        /** The lock for the thread synchronization */
        cfs_spinlock_t          ri_lock;

        /** The wait queue for the rebuild work synchronization */
        cfs_waitq_t             ri_waitq;

        /** The list of all the rebuild threads. */
        cfs_list_t              ri_threads;
        int                     ri_thread_count;

        /** The list of items to be rebuilt */
        cfs_list_t              ri_head;

        /**
         * An amount of allocated items, not necessary in the
         * mdd_rebuild_info::ri_head. For sanity checking. */
        cfs_atomic_t            ri_count;

        /** Amount of busy threads */
        cfs_atomic_t            ri_busy;

        /** Rebuild flags */
        __u32                   ri_flags;

        /** Rebuild error code */
        int                     ri_error;

};

#define THREAD_NAME_LEN 32

/* The per-thread rebuild info. */
struct mdd_rebuild_thread {
        /** A pointer to the common rebuild info. */
        struct mdd_rebuild_info *mr_info;

        /** linked list of threads */
        cfs_list_t              mr_list;

        /** A new allocated fid to rebuild the current object with */
        struct lu_fid           mr_new_fid;
        /** The current object fid. */
        struct lu_fid           mr_cur_fid;
        /** The last generated fid. */
        struct lu_fid           mr_last_fid;

        /** The thread name */
        char                    mr_name[THREAD_NAME_LEN];

        struct lu_dirent        mr_dirent;
        char                    mr_dirent_name[NAME_MAX];

#if defined(CONFIG_SMP)
        /** The cpu index. */
        int                     mr_cpu;
#endif
        /** The statistics counter. */
        __u32                   mr_processed;
};

/* The rebuild item sitting in the mdd_rebuild_info::ri_head list. */
struct mdd_rebuild_ent {
        /** The parent pointer to the parent rebuild item. */
        struct mdd_rebuild_ent  *re_parent;

        /** The object referenced by the rebuild item. Directory only. */
        struct dt_object        *re_obj;

        /** The original FID. Directory only. */
        struct lu_fid           re_oldfid;

        /** The index iterator on the object. */
        struct dt_it            *re_di;

        /** The link to the rebuild list. */
        cfs_list_t              re_list;

        /**
         * The reference counter. Taken by the children rebuild items and
         * by the handling threads.
         */
        cfs_atomic_t            re_refs;
};

/**
 * Allocate and initialise the rebuild item.
 * A reference on the parent is taken if \a parent is given.
 */
static struct mdd_rebuild_ent *
mdd_rebuild_ent_alloc(const struct lu_env *env,
                      struct mdd_rebuild_info *ri,
                      struct dt_object *obj,
                      struct mdd_rebuild_ent *parent)
{
        struct mdd_rebuild_ent *rent;
        const struct dt_it_ops *iops;
        int rc;
        ENTRY;

        if (!dt_try_as_dir(env, obj)) {
                CERROR("failed to init %p/"DFID"\n", parent,
                       PFID(lu_object_fid(&obj->do_lu)));
                RETURN(ERR_PTR(-ENOTDIR));
        }

        OBD_ALLOC_PTR(rent);
        if (rent == NULL)
                return ERR_PTR(-ENOMEM);

        CFS_INIT_LIST_HEAD(&rent->re_list);
        rent->re_obj = obj;

        iops = &obj->do_index_ops->dio_it;

        rent->re_di = iops->init(env, obj, 0, II_MULTI_THREAD, BYPASS_CAPA);
        if (IS_ERR(rent->re_di))
                GOTO(free, rc = PTR_ERR(rent->re_di));

        rc = iops->get(env, rent->re_di, (const void *)"");
        if (rc < 0)
                GOTO(fini, rc);

        lu_object_get(&rent->re_obj->do_lu);
        cfs_atomic_set(&rent->re_refs, 1);
        cfs_atomic_inc(&ri->ri_count);

        if (parent) {
                rent->re_parent = parent;
                cfs_atomic_inc(&parent->re_refs);
        }

        CDEBUG(D_HA, "alloc rebuild %p/%p/"DFID"\n", parent,
               rent, PFID(lu_object_fid(&rent->re_obj->do_lu)));

        RETURN(rent);
fini:
        iops->fini(env, rent->re_di);
free:
        OBD_FREE(rent, CFS_PAGE_SIZE);
        return ERR_PTR(rc);
}

/**
 * Free the rebuild item
 * \pre  not in the mdd_rebuild_info::ri_head list
 */
static void mdd_rebuild_ent_free(const struct lu_env *env,
                                 struct mdd_rebuild_info *ri,
                                 struct mdd_rebuild_ent *rent)
{
        LASSERT(rent != NULL);

        cfs_atomic_dec(&ri->ri_count);
        LASSERT(cfs_list_empty(&rent->re_list));
        rent->re_obj->do_index_ops->dio_it.put(env, rent->re_di);
        rent->re_obj->do_index_ops->dio_it.fini(env, rent->re_di);
        lu_object_put(env, &rent->re_obj->do_lu);
        OBD_FREE_PTR(rent);
}

/**
 * Take the rebuild item from the mdd_rebuild_info::ri_head list into
 * processing.
 *
 * \pre  mdd_rebuild_info::ri_lock is hold
 *
 * mdd_rebuild_ent::re_refs is not changed, in fact, -1 for removing
 * from the list, +1 for the handling thread.
 */
static void mdd_rebuild_ent_get_locked(struct mdd_rebuild_info *ri,
                                       struct mdd_rebuild_ent *rent)
{
        LASSERT(rent->re_obj != NULL);
        LASSERT(cfs_spin_is_locked(&ri->ri_lock));
        /* The object is sitting in the TODO list, so there is a reference */
        LASSERT(cfs_atomic_read(&rent->re_refs) >= 1);
        LASSERT(S_ISDIR(lu_object_attr(&rent->re_obj->do_lu)));
        cfs_list_del_init(&rent->re_list);

        CDEBUG(D_HA, "get rebuild %p/%p/"DFID"\n", rent->re_parent,
               rent, PFID(lu_object_fid(&rent->re_obj->do_lu)));
}

/**
 * Put the rebuild item back to the mdd_rebuild_info::ri_head list after
 * a partial processing.
 *
 * mdd_rebuild_ent::re_refs is not changed, in fact, +1 for adding to the
 * list, -1 for the handling thread.
 */
static void mdd_rebuild_ent_put(struct mdd_rebuild_info *ri,
                                struct mdd_rebuild_ent *rent)
{
        LASSERT(rent != NULL);
        LASSERT(rent->re_obj != NULL);

        /* The rebuild is not completed, so there is a reference. */
        CDEBUG(D_HA, "put rebuild %p/%p/"DFID"\n", rent->re_parent,
               rent, PFID(lu_object_fid(&rent->re_obj->do_lu)));
        LASSERT(cfs_atomic_read(&rent->re_refs) >= 1);
        LASSERT(S_ISDIR(lu_object_attr(&rent->re_obj->do_lu)));
        cfs_spin_lock(&ri->ri_lock);
        /* Add to the head so that the recent items would be handled first. */
        cfs_list_add(&rent->re_list, &ri->ri_head);
        cfs_spin_unlock(&ri->ri_lock);
        cfs_waitq_signal(&ri->ri_waitq);
}

/**
 * Release a reference on the rebuild item \a rent.
 * This may lead to the item deallocation if there is no other references.
 * Deallocation involves a parent reference decrement.
 *
 * No locking, only one thread is the last one which will free the item,
 * it is protected by the atomic mdd_rebuild_ent::re_refs.
 */
static void mdd_rebuild_ent_release(struct lu_env *env,
                                    struct mdd_rebuild_info *ri,
                                    struct mdd_rebuild_ent *rent)
{

        while (rent) {
                int refs = cfs_atomic_read(&rent->re_refs);
                struct mdd_rebuild_ent *child = rent;

                /* Each item in the up path has a taken reference. */
                LASSERT(refs >= 1);
                CDEBUG(D_HA, "release rebuild %p/%p/"DFID" refs %u\n",
                       rent->re_parent, rent,
                       PFID(lu_object_fid(&rent->re_obj->do_lu)), refs);

                if (cfs_atomic_dec_and_test(&rent->re_refs) == 0)
                        break;

                /* No refs means the entry is not in the rebuild list. */
                LASSERT(cfs_list_empty(&rent->re_list));
                rent = rent->re_parent;
                mdd_rebuild_ent_free(env, ri, child);
        }
}

/**
 * Read and rebuild the directory entry and the object pointed by it.
 */
static inline int mdd_rebuild_ent(struct lu_env *env,
                                  struct mdd_rebuild_thread *th,
                                  struct mdd_rebuild_ent *rent)
{
        struct thandle *handle;
        const struct dt_it_ops *iops;
        __u32 flags = th->mr_info->ri_flags;
        struct mdd_device *mdd = th->mr_info->ri_mdd;
        int rc;

        mdd_txn_param_build(env, mdd, MDD_TXN_REBUILD_OP, 0);
        handle = mdd_trans_start(env, mdd);
        if (IS_ERR(handle))
                RETURN(PTR_ERR(handle));

        flags |= rent->re_parent == NULL ? LDF_REBUILD_NO_PARENT : 0;
        iops = &rent->re_obj->do_index_ops->dio_it;
        rc = iops->rebuild(env, rent->re_di, &th->mr_dirent,
                           &th->mr_new_fid, handle, flags, 0);
        mdd_trans_stop(env, mdd, rc, handle);

        fid_le_to_cpu(&th->mr_cur_fid, &th->mr_dirent.lde_fid);
        CDEBUG(D_HA, "rebuild "DFID"/'%.*s': "DFID" flags 0x%x "
               "rc %d\n", PFID(lu_object_fid(&rent->re_obj->do_lu)),
               le16_to_cpu(th->mr_dirent.lde_namelen),
               th->mr_dirent.lde_name, PFID(&th->mr_cur_fid), flags, rc);

        return rc;
}

/**
 * Rebuild the LinkEA for the given object \a obj.
 */
static int mdd_rebuild_ent_links(struct lu_env *env,
                                 struct mdd_device *mdd,
                                 struct mdd_object *obj,
                                 struct mdd_rebuild_ent *parent,
                                 struct lu_dirent *ent,
                                 __u32 flags)
{
        const struct lu_fid *newfid, *oldfid;
        struct thandle *handle;
        struct lu_name *lname;
        int rc;

        LASSERT(obj != NULL);
        LASSERT(parent != NULL);

        if (!(flags & LDF_REBUILD_LINKEA))
                return 0;

        mdd_txn_param_build(env, mdd, MDD_TXN_LINK_OP, 0);
        handle = mdd_trans_start(env, mdd);
        if (IS_ERR(handle))
                RETURN(PTR_ERR(handle));

        oldfid = fid_is_sane(&parent->re_oldfid) ? &parent->re_oldfid : NULL;
        newfid = lu_object_fid(&parent->re_obj->do_lu);

        lname = mdd_name(env, ent->lde_name, le16_to_cpu(ent->lde_namelen));
        mdd_write_lock(env, obj, MOR_TGT_CHILD);
        rc = mdd_links_rename_check(env, obj, oldfid, lname,
                                    newfid, lname, handle);
        mdd_write_unlock(env, obj);

        mdd_trans_stop(env, mdd, rc, handle);
        return rc;
}

/**
 * Allocate a child rebuild item for the object pointer by the directory
 * entry \a ent, if the child is a directory.
 *
 * While the child object is accessed, its LinkEA is also rebuilt.
 *
 * \retval  the new allocated item      success
 * \retval  0 if not a directory        success
 * \retval -ve                          failure
 */
static struct mdd_rebuild_ent *
mdd_rebuild_get_child(struct lu_env *env,
                      struct mdd_rebuild_thread *th,
                      struct mdd_rebuild_ent *parent,
                      struct lu_dirent *ent)
{
        struct mdd_rebuild_info *ri = th->mr_info;
        struct mdd_rebuild_ent *child = NULL;
        struct dt_object *next;
        struct mdd_object *mo;
        int rc;
        ENTRY;

        mo = mdd_object_by_fid(env, ri->ri_mdd, &th->mr_cur_fid);
        if (IS_ERR(mo))
                RETURN((void *)mo);

        /* Get index ops installed. */
        next = mdd_object_child(mo);
        rc = mdd_rebuild_ent_links(env, ri->ri_mdd, mo, parent,
                                   ent, ri->ri_flags);
        if (rc)
                GOTO(error, child = ERR_PTR(rc));

        if (S_ISDIR(mdd_object_type(mo))) {
                child = mdd_rebuild_ent_alloc(env, ri, next, parent);
                if (IS_ERR(child))
                        GOTO(error, child);
        }
        EXIT;
error:
        mdd_object_put(env, mo);
        return child;
}

/**
 * Follow parent links from the current item \a rent and return the first
 * not being handled by any thread.
 *
 * \retval the found parent     success
 */
static struct mdd_rebuild_ent *
mdd_rebuild_get_parent(struct mdd_rebuild_info *ri,
                       struct mdd_rebuild_ent *rent)
{
        struct mdd_rebuild_ent *parent;
        ENTRY;

        LASSERT(cfs_list_empty(&rent->re_list));
        cfs_spin_lock(&ri->ri_lock);
        parent = rent->re_parent;
        /* Take the first parent item sitting in the rebuild list, i.e.
         * not being handled by any thread. */
        while (parent && cfs_list_empty(&parent->re_list))
                parent = parent->re_parent;

        if (parent) {
                /* Item sitting in the rebuild list and having children
                 * must have at least 2 taken references */
                LASSERT(cfs_atomic_read(&parent->re_refs) >= 2);
                mdd_rebuild_ent_get_locked(ri, parent);
        }

        cfs_spin_unlock(&ri->ri_lock);
        RETURN(parent);
}

/**
 * Traverse the semantic tree and rebuild the objects.
 *
 * The traverse starts from the first item in the mdd_rebuild_info::ri_head
 * list. Originally, only the root item exists there. Once an item is taken
 * into processing, it is removed from the list. The traverse goes in child-
 * -first order. If a thread switches to a child, the parent is returned back
 * to the list so that other thread could proceed with it. Once a directory
 * is rebuilt completely with all its children, the thread takes its parent
 * following mdd_rebuild_ent::re_parent, precisely the nearest parent waiting
 * for the rebuild in the mdd_rebuild_info::ri_head list. If all the parents
 * are being handled by other threads, the traverse exists. The caller is
 * responsible for the full rebuild completion.
 *
 */
static int mdd_rebuild_handler(struct lu_env *env,
                               struct mdd_rebuild_thread *th)
{
        struct mdd_rebuild_info *ri = th->mr_info;
        struct mdd_rebuild_ent *rent;
        const struct dt_it_ops *iops;
        int rc = 0;
        ENTRY;

        /* Take the first entity to be rebuilt.
         * Only directories are added to the list, by taking it from
         * the list, we let ony 1 thread to handle 1 directory at a time. */
        cfs_spin_lock(&ri->ri_lock);
        if (cfs_list_empty(&ri->ri_head)) {
                cfs_spin_unlock(&ri->ri_lock);
                RETURN(0);
        }
        rent = cfs_list_entry(ri->ri_head.next,
                              struct mdd_rebuild_ent, re_list);
        mdd_rebuild_ent_get_locked(ri, rent);
        cfs_spin_unlock(&ri->ri_lock);

        while (rent != NULL) {
                struct mdd_rebuild_ent *next;

                if (ri->ri_flags & MRT_STOPPING) {
                        rc = 0;
                        break;
                }

                iops = &rent->re_obj->do_index_ops->dio_it;
                rc = iops->next(env, rent->re_di);
                if (rc < 0)
                        GOTO(error, rc);

                /* If the item is rebuilt completely, move to the parent. */
                if (rc > 0) {
                        next = mdd_rebuild_get_parent(ri, rent);
                        if (IS_ERR(next)) {
                                GOTO(error, rc = PTR_ERR(next));
                        }
                        /* Release the completed child and switch to
                         * the parent. */
                        mdd_rebuild_ent_release(env, ri, rent);
                        rent = next;
                        continue;
                }

                rc = mdd_rebuild_ent(env, th, rent);
                if (rc < 0)
                        GOTO(error, rc);

                if (le16_to_cpu(th->mr_dirent.lde_namelen) == 1 &&
                    strncmp(th->mr_dirent.lde_name, dot, 1) == 0)
                        continue;

                if (le16_to_cpu(th->mr_dirent.lde_namelen) == 2 &&
                    strncmp(th->mr_dirent.lde_name, dotdot, 2) == 0)
                        continue;

                th->mr_processed++;
                /* Let's try to switch to the child if it is a directory. */
                next = mdd_rebuild_get_child(env, th, rent, &th->mr_dirent);
                if (IS_ERR(next)) {
                        GOTO(error, rc = PTR_ERR(next));
                } else if (next != NULL) {
                        /* Put the parent back to the rebuild list and
                         * switch to the child. */
                        mdd_rebuild_ent_put(ri, rent);
                        rent = next;
                        lprocfs_counter_incr(ri->ri_mdd->mdd_stats,
                                             LPROC_MDD_REBUILD_DIRS);
                } else {
                        lprocfs_counter_incr(ri->ri_mdd->mdd_stats,
                                             LPROC_MDD_REBUILD_FILES);
                }

                /* If the child object has the same fid as the new generated
                 * one, the new fid was used during the ->rebuild() process
                 * and a new one is to be genarated. The value in the \a
                 * th->mr_new_fid was replaced by the previous object fid,
                 * store it in \a rent */
                if (lu_fid_eq(&th->mr_last_fid, &th->mr_cur_fid)) {
                        if (next != NULL)
                                rent->re_oldfid = th->mr_new_fid;
                        seq_client_alloc_fid(env, ri->ri_seq, &th->mr_new_fid);
                        th->mr_last_fid = th->mr_new_fid;
                }
        }

        EXIT;
error:
        if (rent) {
                LASSERT(!IS_ERR(rent));
                if (rc < 0) {
                        CERROR("Failed to rebuild "DFID" : %d\n",
                               PFID(lu_object_fid(&rent->re_obj->do_lu)), rc);
                }
                mdd_rebuild_ent_release(env, ri, rent);
        }
        return rc < 0 ? rc : 0;
}

/**
 * The main rebuild thread handler.
 * Threads are woken up if there is an item in the mdd_rebuild_info::ri_head
 * list, and the semantic traverse starts at this item.
 */
static int mdd_rebuild_main(void *arg)
{
        struct mdd_rebuild_thread *th = (struct mdd_rebuild_thread *)arg;
        struct mdd_rebuild_info *ri = th->mr_info;
        struct l_wait_info lwi = { 0 };
        struct lu_env env;
        int rc;

        cfs_daemonize(th->mr_name);
#if defined(CONFIG_SMP)
        set_cpus_allowed_ptr(cfs_current(),
                             cpumask_of_node(cpu_to_node(th->mr_cpu)));
#endif

        CDEBUG(D_HA, "MDD recovery thread started, pid %d\n",
               cfs_curproc_pid());

        /* Wait when the caller completes the mdd_rebuild_info initialisation */
        l_wait_event(ri->ri_waitq, (!(ri->ri_flags & MRT_PREPARE)), &lwi);

        rc = lu_env_init(&env, LCT_MD_THREAD | LCT_NOREF | LCT_REMEMBER);
        if (rc)
                RETURN(rc);

        seq_client_alloc_fid(&env, ri->ri_seq, &th->mr_new_fid);
        th->mr_last_fid = th->mr_new_fid;

        while (1) {
                /* Waits for some items to be rebuilt or
                 * an indicator the work is to be interrupted or
                 * if all the items are idle and there is nothing left in the
                 * rebuild list -- i.e. the rebuild is completed. */
                l_wait_event(ri->ri_waitq,
                             !cfs_list_empty(&ri->ri_head) ||
                             cfs_atomic_read(&ri->ri_busy) == 0 ||
                             ri->ri_flags & MRT_STOPPING, &lwi);

                /* A request to stop the rebuild is given. */
                if (ri->ri_flags & MRT_STOPPING)
                        break;

                /* The rebuild is completed. */
                if (cfs_list_empty(&ri->ri_head) &&
                    cfs_atomic_read(&ri->ri_busy) == 0)
                        break;

                cfs_atomic_inc(&ri->ri_busy);
                rc = mdd_rebuild_handler(&env, th);
                cfs_atomic_dec(&ri->ri_busy);
                if (rc) {
                        cfs_spin_lock(&ri->ri_lock);
                        ri->ri_flags |= MRT_STOPPING;
                        ri->ri_error = rc;
                        cfs_spin_unlock(&ri->ri_lock);
                }
        }

        lu_env_fini(&env);

        CDEBUG(D_HA, "MDD recovery thread completed, pid %d, processed %u\n",
               cfs_curproc_pid(), th->mr_processed);

        /* Some threads are probably still sleeping, wake them up. */
        cfs_waitq_signal(&ri->ri_waitq);
        cfs_spin_lock(&ri->ri_lock);
        cfs_list_del(&th->mr_list);
        OBD_FREE_PTR(th);
        /* Wake up the parent to proceed with mount. Do it before unlocking
         * to avoid a race with possible deallocation of mdd_rebuild_info
         * before signal is sent at all. */
        if (cfs_list_empty(&ri->ri_threads))
                cfs_waitq_signal(&ri->ri_waitq);
        cfs_spin_unlock(&ri->ri_lock);

        return rc;
}

/**
 * Allocate and initialise the rebuild client sequence manager.
 * It is needed to get new unused FIDs for the rebuild.
 */
static int mdd_client_seq_init(struct mdd_rebuild_info *ri)
{
        struct md_site *ms;
        int rc;
        ENTRY;

        ms = lu_site2md(mdd2lu_dev(ri->ri_mdd)->ld_site);
        OBD_ALLOC_PTR(ri->ri_seq);
        if (ri->ri_seq == NULL)
                RETURN(-ENOMEM);

        /* Init client side sequence-manager */
        rc = seq_client_init(ri->ri_seq, NULL, LUSTRE_SEQ_METADATA,
                             "mdd-cl-seq", ms->ms_server_seq);
        if (rc) {
                OBD_FREE_PTR(ri->ri_seq);
                ri->ri_seq = NULL;
        }

        RETURN(rc);
}

/**
 * Finalize and free the rebuild client sequence manager.
 */
static void mdd_client_seq_fini(struct mdd_rebuild_info *ri)
{
        seq_client_fini(ri->ri_seq);
        OBD_FREE_PTR(ri->ri_seq);
        ri->ri_seq = NULL;
}

/**
 * Wait for the rebuild thread completion, finalise and free all the resources.
 * In an error case, send an MRT_STOPPING flag to the rebuild threads so that
 * they would interrupt their work and complete the work.
 */
static int mdd_rebuild_stop_threads(const struct lu_env *env,
                                    struct mdd_rebuild_info *ri, int rc)
{
        struct l_wait_info lwi = { 0 };

        if (rc) {
                cfs_spin_lock(&ri->ri_lock);
                ri->ri_flags &= ~MRT_PREPARE;
                ri->ri_flags |= MRT_STOPPING;
                cfs_spin_unlock(&ri->ri_lock);
                cfs_waitq_broadcast(&ri->ri_waitq);
        }
        l_wait_event(ri->ri_waitq, cfs_list_empty(&ri->ri_threads), &lwi);

        /* If the job was interrupted, cleanup the rebuild list.
         * Make mdd_rebuild_ent_free() happy, take the spinlock.
         * This spinlock is also needed due to mdd_rebuild_main()
         * unlocking after waking up the parent. */
        cfs_spin_lock(&ri->ri_lock);
        while (!cfs_list_empty(&ri->ri_head)) {
                struct mdd_rebuild_ent *rent;
                rent = cfs_list_entry(ri->ri_head.next,
                                      struct mdd_rebuild_ent, re_list);
                cfs_list_del_init(&rent->re_list);
                /* It may happen that its parent has been already processed
                 * and not sitting in this list anymore, add it to the tail */
                if (rent->re_parent &&
                    cfs_list_empty(&rent->re_parent->re_list))
                {
                        cfs_list_add_tail(&rent->re_parent->re_list,
                                          &ri->ri_head);
                }
                mdd_rebuild_ent_free(env, ri, rent);
        }
        cfs_spin_unlock(&ri->ri_lock);
        RETURN(rc);
}

/**
 * Alloc and init the rebuild info
 */
static struct mdd_rebuild_info *mdd_rebuild_info_alloc(struct mdd_device *mdd,
                                                        __u32 flags)
{
        struct mdd_rebuild_info *ri;
        int rc;

        OBD_ALLOC_PTR(ri);
        if (ri == NULL)
                return ERR_PTR(-ENOMEM);

        ri->ri_mdd = mdd;
        cfs_spin_lock_init(&ri->ri_lock);
        CFS_INIT_LIST_HEAD(&ri->ri_threads);
        CFS_INIT_LIST_HEAD(&ri->ri_head);
        cfs_waitq_init(&ri->ri_waitq);
        cfs_atomic_set(&ri->ri_busy, 0);
        cfs_atomic_set(&ri->ri_count, 0);
        ri->ri_flags = flags | MRT_PREPARE;
        ri->ri_thread_count = MDD_REBUILD_THREAD_NR;

        rc = mdd_client_seq_init(ri);
        if (rc) {
                OBD_FREE_PTR(ri);
                return ERR_PTR(rc);
        }
        return ri;
}

static int mdd_rebuild_info_free(struct mdd_rebuild_info *ri)
{
        int rc;

        mdd_client_seq_fini(ri);
        rc = ri->ri_error;

        /* Check that there is no leak of items not sitting in the list
         * at the time of cleanup. */
        LASSERTF(cfs_atomic_read(&ri->ri_count) == 0,
                 "count %d, list empty %d\n",
                 cfs_atomic_read(&ri->ri_count),
                 cfs_list_empty(&ri->ri_head) ? 1 : 0);
        OBD_FREE_PTR(ri);

        return rc;
}

/**
 * Allocate the rebuild threads and assign/initialise all the needed resources.
 */
static struct mdd_rebuild_info *
mdd_rebuild_start_threads(const struct lu_env *env,
                          struct mdd_rebuild_info *ri)
{
        int rc, i, cpu;
        ENTRY;

        for (i = 0, cpu = 0; i < ri->ri_thread_count; i++) {
                struct mdd_rebuild_thread *thread;

                OBD_ALLOC_PTR(thread);
                if (thread == NULL)
                        GOTO(free, rc = -ENOMEM);

                thread->mr_info = ri;

#if defined(CONFIG_SMP)
                while (!cpu_online(cpu)) {
                        cpu++;
                        if (cpu >= cfs_num_possible_cpus())
                                cpu = 0;
                }
                thread->mr_cpu = cpu++;
#endif

                CFS_INIT_LIST_HEAD(&thread->mr_list);
                sprintf(thread->mr_name, "rebuild_%02d", i);

                cfs_spin_lock(&ri->ri_lock);
                cfs_list_add(&thread->mr_list, &ri->ri_threads);
                cfs_spin_unlock(&ri->ri_lock);
                rc = cfs_create_thread(mdd_rebuild_main, thread,
                                       CLONE_VM | CLONE_FILES);
                if (rc < 0) {
                        CERROR("cannot start thread %s, rc = %d\n",
                               thread->mr_name, rc);
                        cfs_spin_lock(&ri->ri_lock);
                        cfs_list_del(&thread->mr_list);
                        cfs_spin_unlock(&ri->ri_lock);
                        OBD_FREE_PTR(thread);
                        GOTO(free, rc);
                }
        }
        RETURN(ri);
free:
        mdd_rebuild_stop_threads(env, ri, rc);
        return ERR_PTR(rc);
}

/**
 * Rebuilds the MDD_ROOT object.
 */
static int mdd_root_rebuild(const struct lu_env *env,
                            struct mdd_rebuild_info *ri)
{
        struct lu_fid root_fid, fid;
        struct dt_object *dir;
        int rc;
        ENTRY;

        if (!(ri->ri_flags & (LDF_REBUILD_OI | LDF_REBUILD_LMA)))
                RETURN(0);

        dir = dt_store_open(env, ri->ri_mdd->mdd_child, "", ".", &fid);
        if (IS_ERR(dir))
                RETURN(PTR_ERR(dir));

        if (dt_try_as_dir(env, dir)) {
                struct thandle *handle;

                seq_client_alloc_fid(env, ri->ri_seq, &root_fid);
                mdd_txn_param_build(env, ri->ri_mdd, MDD_TXN_REBUILD_OP, 0);
                handle = mdd_trans_start(env, ri->ri_mdd);
                if (IS_ERR(handle))
                        RETURN(PTR_ERR(handle));

                rc = dir->do_index_ops->dio_rebuild(env, dir,
                                                    (struct dt_rec *)&fid,
                                                    (struct dt_rec *)&root_fid,
                                                    (const struct dt_key *)
                                                    mdd_root_dir_name,
                                                    handle, ri->ri_flags);
                mdd_trans_stop(env, ri->ri_mdd, rc, handle);
        } else {
                rc = -ENOTDIR;
        }

        if (rc == 0)
                lprocfs_counter_incr(ri->ri_mdd->mdd_stats,
                                     LPROC_MDD_REBUILD_DIRS);

        lu_object_put(env, &dir->do_lu);
        RETURN(rc);
}

/**
 * Start rebuild threads, giv them the root object and wait for their
 * completion, which will happen either if the process will be interrupted
 * or the whole semantic tree will be traversed and rebuilt.
 */
static int mdd_fs_rebuild(const struct lu_env *env,
                          struct mdd_rebuild_info *ri)
{
        struct mdd_rebuild_ent  *rent;
        struct dt_object        *root;
        int                     rc = 0;
        ENTRY;

        ri = mdd_rebuild_start_threads(env, ri);
        if (IS_ERR(ri))
                GOTO(error, rc = PTR_ERR(ri));

        root = dt_store_open(env, ri->ri_mdd->mdd_child, "",
                             mdd_root_dir_name, &ri->ri_mdd->mdd_root_fid);
        if (IS_ERR(root))
                GOTO(error, rc = PTR_ERR(root));
        else
                LASSERT(root != NULL);

        rent = mdd_rebuild_ent_alloc(env, ri, root, NULL);
        if (IS_ERR(rent))
                GOTO(put_root, rc = PTR_ERR(rent));

        /* Add the root item and let the threads to start the rebuild. */
        mdd_rebuild_ent_put(ri, rent);

        cfs_spin_lock(&ri->ri_lock);
        ri->ri_flags &= ~MRT_PREPARE;
        cfs_spin_unlock(&ri->ri_lock);
        cfs_waitq_broadcast(&ri->ri_waitq);
        EXIT;
put_root:
        lu_object_put(env, &root->do_lu);
error:
        rc = mdd_rebuild_stop_threads(env, ri, rc);
        return rc;
}


static int mdd_prepare(const struct lu_env *env,
                       struct lu_device *pdev,
                       struct lu_device *cdev)
{
        struct mdd_device *mdd = lu2mdd_dev(cdev);
        struct lu_device *next = &mdd->mdd_child->dd_lu_dev;
        struct dt_object *root;
        int rc;

        ENTRY;
        rc = next->ld_ops->ldo_prepare(env, cdev, next);
        if (rc)
                GOTO(out, rc);

        root = dt_store_open(env, mdd->mdd_child, "", mdd_root_dir_name,
                             &mdd->mdd_root_fid);
        if (!IS_ERR(root)) {
                LASSERT(root != NULL);
                lu_object_put(env, &root->do_lu);
                rc = orph_index_init(env, mdd);
        } else {
                rc = PTR_ERR(root);
        }
        if (rc)
                GOTO(out, rc);

        rc = mdd_dot_lustre_setup(env, mdd);
        if (rc) {
                CERROR("Error(%d) initializing .lustre objects\n", rc);
                GOTO(out, rc);
        }

out:
        RETURN(rc);
}

static int mdd_rebuild(const struct lu_env *env,
                       struct lu_device *cdev,
                       __u32 flags)
{
        struct mdd_device *mdd = lu2mdd_dev(cdev);
        struct mdd_rebuild_info *ri;
        int rc, rc2;
        ENTRY;

        if (!(flags & LDF_REBUILD_DEFAULT))
                RETURN(0);

        ri = mdd_rebuild_info_alloc(mdd, flags);
        if (IS_ERR(ri))
                RETURN(PTR_ERR(ri));

        rc = mdd_root_rebuild(env, ri);
        if (rc)
                GOTO(error, rc);

        rc = mdd_fs_rebuild(env, ri);

        EXIT;
error:
        rc2 = mdd_rebuild_info_free(ri);
        rc = rc ? : rc2;
        if (rc)
                CERROR("Failed to rebuild 2.x fs format: %d,%d\n", rc, rc2);
        return rc;
}

const struct lu_device_operations mdd_lu_ops = {
        .ldo_object_alloc      = mdd_object_alloc,
        .ldo_process_config    = mdd_process_config,
        .ldo_recovery_complete = mdd_recovery_complete,
        .ldo_prepare           = mdd_prepare,
        .ldo_rebuild           = mdd_rebuild,
};

/*
 * No permission check is needed.
 */
static int mdd_root_get(const struct lu_env *env,
                        struct md_device *m, struct lu_fid *f)
{
        struct mdd_device *mdd = lu2mdd_dev(&m->md_lu_dev);

        ENTRY;
        *f = mdd->mdd_root_fid;
        RETURN(0);
}

/*
 * No permission check is needed.
 */
static int mdd_statfs(const struct lu_env *env, struct md_device *m,
                      struct obd_statfs *sfs)
{
        struct mdd_device *mdd = lu2mdd_dev(&m->md_lu_dev);
        int rc;

        ENTRY;

        rc = mdd_child_ops(mdd)->dt_statfs(env, mdd->mdd_child, sfs);

        RETURN(rc);
}

/*
 * No permission check is needed.
 */
static int mdd_maxsize_get(const struct lu_env *env, struct md_device *m,
                           int *md_size, int *cookie_size)
{
        struct mdd_device *mdd = lu2mdd_dev(&m->md_lu_dev);
        ENTRY;

        *md_size = mdd_lov_mdsize(env, mdd);
        *cookie_size = mdd_lov_cookiesize(env, mdd);

        RETURN(0);
}

static int mdd_maxeasize_get(const struct lu_env *env, struct md_device *m,
				int *easize)
{
	struct mdd_device *mdd = lu2mdd_dev(&m->md_lu_dev);
	ENTRY;

	*easize = mdd->mdd_dt_conf.ddp_max_ea_size;

	RETURN(0);
}

static int mdd_init_capa_ctxt(const struct lu_env *env, struct md_device *m,
                              int mode, unsigned long timeout, __u32 alg,
                              struct lustre_capa_key *keys)
{
        struct mdd_device *mdd = lu2mdd_dev(&m->md_lu_dev);
        struct mds_obd    *mds = &mdd2obd_dev(mdd)->u.mds;
        int rc;
        ENTRY;

        /* need barrier for mds_capa_keys access. */
        cfs_down_write(&mds->mds_notify_lock);
        mds->mds_capa_keys = keys;
        cfs_up_write(&mds->mds_notify_lock);

        rc = mdd_child_ops(mdd)->dt_init_capa_ctxt(env, mdd->mdd_child, mode,
                                                   timeout, alg, keys);
        RETURN(rc);
}

static int mdd_update_capa_key(const struct lu_env *env,
                               struct md_device *m,
                               struct lustre_capa_key *key)
{
        struct mds_capa_info info = { .uuid = NULL, .capa = key };
        struct mdd_device *mdd = lu2mdd_dev(&m->md_lu_dev);
        struct obd_export *lov_exp = mdd2obd_dev(mdd)->u.mds.mds_lov_exp;
        int rc;
        ENTRY;

        rc = obd_set_info_async(lov_exp, sizeof(KEY_CAPA_KEY), KEY_CAPA_KEY,
                                sizeof(info), &info, NULL);
        RETURN(rc);
}

static int mdd_llog_ctxt_get(const struct lu_env *env, struct md_device *m,
                             int idx, void **h)
{
        struct mdd_device *mdd = lu2mdd_dev(&m->md_lu_dev);

        *h = llog_group_get_ctxt(&mdd2obd_dev(mdd)->obd_olg, idx);
        return (*h == NULL ? -ENOENT : 0);
}

static struct lu_device *mdd_device_alloc(const struct lu_env *env,
                                          struct lu_device_type *t,
                                          struct lustre_cfg *lcfg)
{
        struct lu_device  *l;
        struct mdd_device *m;

        OBD_ALLOC_PTR(m);
        if (m == NULL) {
                l = ERR_PTR(-ENOMEM);
        } else {
                md_device_init(&m->mdd_md_dev, t);
                l = mdd2lu_dev(m);
                l->ld_ops = &mdd_lu_ops;
                m->mdd_md_dev.md_ops = &mdd_ops;
                md_upcall_init(&m->mdd_md_dev, NULL);
        }

        return l;
}

static struct lu_device *mdd_device_free(const struct lu_env *env,
                                         struct lu_device *lu)
{
        struct mdd_device *m = lu2mdd_dev(lu);
        struct lu_device  *next = &m->mdd_child->dd_lu_dev;
        ENTRY;

        LASSERT(cfs_atomic_read(&lu->ld_ref) == 0);
        md_device_fini(&m->mdd_md_dev);
        OBD_FREE_PTR(m);
        RETURN(next);
}

static struct obd_ops mdd_obd_device_ops = {
        .o_owner = THIS_MODULE
};

/*
 * context key constructor/destructor:
 * mdd_quota_key_init, mdd_quota_key_fini
 */
LU_KEY_INIT_FINI(mdd_quota, struct md_quota);

struct lu_context_key mdd_quota_key = {
        .lct_tags = LCT_SERVER_SESSION,
        .lct_init = mdd_quota_key_init,
        .lct_fini = mdd_quota_key_fini
};

struct md_quota *md_quota(const struct lu_env *env)
{
        LASSERT(env->le_ses != NULL);
        return lu_context_key_get(env->le_ses, &mdd_quota_key);
}
EXPORT_SYMBOL(md_quota);

static int mdd_changelog_user_register(const struct lu_env *env,
				       struct mdd_device *mdd, int *id)
{
        struct llog_ctxt *ctxt;
        struct llog_changelog_user_rec *rec;
	struct thandle *handle;
        int rc;
        ENTRY;

        ctxt = llog_get_context(mdd2obd_dev(mdd),LLOG_CHANGELOG_USER_ORIG_CTXT);
        if (ctxt == NULL)
                RETURN(-ENXIO);

        OBD_ALLOC_PTR(rec);
        if (rec == NULL) {
                llog_ctxt_put(ctxt);
                RETURN(-ENOMEM);
        }

        /* Assume we want it on since somebody registered */
	rc = mdd_changelog_on(env, mdd, 1);
        if (rc)
                GOTO(out, rc);

        rec->cur_hdr.lrh_len = sizeof(*rec);
        rec->cur_hdr.lrh_type = CHANGELOG_USER_REC;
        cfs_spin_lock(&mdd->mdd_cl.mc_user_lock);
        if (mdd->mdd_cl.mc_lastuser == (unsigned int)(-1)) {
                cfs_spin_unlock(&mdd->mdd_cl.mc_user_lock);
                CERROR("Maximum number of changelog users exceeded!\n");
                GOTO(out, rc = -EOVERFLOW);
        }
        *id = rec->cur_id = ++mdd->mdd_cl.mc_lastuser;
        rec->cur_endrec = mdd->mdd_cl.mc_index;
        cfs_spin_unlock(&mdd->mdd_cl.mc_user_lock);

	mdd_txn_param_build(env, mdd, MDD_TXN_NOP, 1);
	handle = mdd_trans_start(env, mdd);
	if (IS_ERR(handle))
		GOTO(out, rc = PTR_ERR(handle));
        rc = llog_add(ctxt, &rec->cur_hdr, NULL, NULL, 0);
	mdd_trans_stop(env, mdd, rc, handle);

        CDEBUG(D_IOCTL, "Registered changelog user %d\n", *id);
out:
        OBD_FREE_PTR(rec);
        llog_ctxt_put(ctxt);
        RETURN(rc);
}

struct mdd_changelog_user_data {
        __u64 mcud_endrec; /**< purge record for this user */
        __u64 mcud_minrec; /**< lowest changelog recno still referenced */
        __u32 mcud_id;
        __u32 mcud_minid;  /**< user id with lowest rec reference */
        __u32 mcud_usercount;
	unsigned int mcud_found:1;
        struct mdd_device   *mcud_mdd;
        const struct lu_env *mcud_env;
};
#define MCUD_UNREGISTER -1LL

/** Two things:
 * 1. Find the smallest record everyone is willing to purge
 * 2. Update the last purgeable record for this user
 */
static int mdd_changelog_user_purge_cb(struct llog_handle *llh,
                                       struct llog_rec_hdr *hdr, void *data)
{
        struct llog_changelog_user_rec *rec;
        struct mdd_changelog_user_data *mcud =
                (struct mdd_changelog_user_data *)data;
	struct mdd_device *mdd = mcud->mcud_mdd;
	void *trans_h;
	int rc;
        ENTRY;

        LASSERT(llh->lgh_hdr->llh_flags & LLOG_F_IS_PLAIN);

        rec = (struct llog_changelog_user_rec *)hdr;

        mcud->mcud_usercount++;

        /* If we have a new endrec for this id, use it for the following
           min check instead of its old value */
        if (rec->cur_id == mcud->mcud_id)
                rec->cur_endrec = max(rec->cur_endrec, mcud->mcud_endrec);

        /* Track the minimum referenced record */
        if (mcud->mcud_minid == 0 || mcud->mcud_minrec > rec->cur_endrec) {
                mcud->mcud_minid = rec->cur_id;
                mcud->mcud_minrec = rec->cur_endrec;
        }

        if (rec->cur_id != mcud->mcud_id)
                RETURN(0);

        /* Update this user's record */
        mcud->mcud_found = 1;

        /* Special case: unregister this user */
        if (mcud->mcud_endrec == MCUD_UNREGISTER) {
                struct llog_cookie cookie;

                cookie.lgc_lgl = llh->lgh_id;
                cookie.lgc_index = hdr->lrh_index;

                /* XXX This is a workaround for the deadlock of changelog
                 * adding vs. changelog cancelling. LU-81. */
                mdd_txn_param_build(mcud->mcud_env, mdd, MDD_TXN_UNLINK_OP, 0);
                trans_h = mdd_trans_start(mcud->mcud_env, mdd);
                if (IS_ERR(trans_h)) {
                        CERROR("fsfilt_start_log failed: %ld\n",
                               PTR_ERR(trans_h));
                        RETURN(PTR_ERR(trans_h));
                }

                rc = llog_cat_cancel_record(llh->u.phd.phd_cat_handle,
                                            &cookie);
                if (rc == 0)
                        mcud->mcud_usercount--;

                mdd_trans_stop(mcud->mcud_env, mdd, rc, trans_h);
                RETURN(rc);
        }

        /* Update the endrec */
        CDEBUG(D_IOCTL, "Rewriting changelog user %d endrec to "LPU64"\n",
               mcud->mcud_id, rec->cur_endrec);

	mdd_txn_param_build(mcud->mcud_env, mdd, MDD_TXN_NOP, 1);
	trans_h = mdd_trans_start(mcud->mcud_env, mdd);
	if (IS_ERR(trans_h))
		RETURN(PTR_ERR(trans_h));

        /* hdr+1 is loc of data */
        hdr->lrh_len -= sizeof(*hdr) + sizeof(struct llog_rec_tail);
        rc = llog_write_rec(llh, hdr, NULL, 0, (void *)(hdr + 1),
                            hdr->lrh_index);
	mdd_trans_stop(mcud->mcud_env, mdd, rc, trans_h);

        RETURN(rc);
}

static int mdd_changelog_user_purge(const struct lu_env *env,
                                    struct mdd_device *mdd, int id,
                                    long long endrec)
{
        struct mdd_changelog_user_data data;
        struct llog_ctxt *ctxt;
        int rc;
        ENTRY;

        CDEBUG(D_IOCTL, "Purge request: id=%d, endrec=%lld\n", id, endrec);

        data.mcud_id = id;
        data.mcud_minid = 0;
        data.mcud_minrec = 0;
        data.mcud_usercount = 0;
        data.mcud_endrec = endrec;
        data.mcud_mdd = mdd;
        data.mcud_env = env;
        cfs_spin_lock(&mdd->mdd_cl.mc_lock);
        endrec = mdd->mdd_cl.mc_index;
        cfs_spin_unlock(&mdd->mdd_cl.mc_lock);
        if ((data.mcud_endrec == 0) ||
            ((data.mcud_endrec > endrec) &&
             (data.mcud_endrec != MCUD_UNREGISTER)))
                data.mcud_endrec = endrec;

        ctxt = llog_get_context(mdd2obd_dev(mdd),LLOG_CHANGELOG_USER_ORIG_CTXT);
        if (ctxt == NULL)
                return -ENXIO;
        LASSERT(ctxt->loc_handle->lgh_hdr->llh_flags & LLOG_F_IS_CAT);

        rc = llog_cat_process(ctxt->loc_handle, mdd_changelog_user_purge_cb,
                              (void *)&data, 0, 0);
        if ((rc >= 0) && (data.mcud_minrec > 0)) {
                CDEBUG(D_IOCTL, "Purging changelog entries up to "LPD64
                       ", referenced by "CHANGELOG_USER_PREFIX"%d\n",
                       data.mcud_minrec, data.mcud_minid);
		rc = mdd_changelog_llog_cancel(env, mdd, data.mcud_minrec);
        } else {
                CWARN("Could not determine changelog records to purge; rc=%d\n",
                      rc);
        }

        llog_ctxt_put(ctxt);

        if (!data.mcud_found) {
                CWARN("No entry for user %d.  Last changelog reference is "
                      LPD64" by changelog user %d\n", data.mcud_id,
                      data.mcud_minrec, data.mcud_minid);
               rc = -ENOENT;
        }

        if (!rc && data.mcud_usercount == 0)
                /* No more users; turn changelogs off */
		rc = mdd_changelog_on(env, mdd, 0);

        RETURN (rc);
}

/** mdd_iocontrol
 * May be called remotely from mdt_iocontrol_handle or locally from
 * mdt_iocontrol. Data may be freeform - remote handling doesn't enforce
 * an obd_ioctl_data format (but local ioctl handler does).
 * \param cmd - ioc
 * \param len - data len
 * \param karg - ioctl data, in kernel space
 */
static int mdd_iocontrol(const struct lu_env *env, struct md_device *m,
                         unsigned int cmd, int len, void *karg)
{
        struct mdd_device *mdd;
        struct obd_ioctl_data *data = karg;
        int rc;
        ENTRY;

        mdd = lu2mdd_dev(&m->md_lu_dev);

        /* Doesn't use obd_ioctl_data */
        switch (cmd) {
        case OBD_IOC_CHANGELOG_CLEAR: {
                struct changelog_setinfo *cs = karg;
                rc = mdd_changelog_user_purge(env, mdd, cs->cs_id,
                                              cs->cs_recno);
                RETURN(rc);
        }
        case OBD_IOC_GET_MNTOPT: {
                mntopt_t *mntopts = (mntopt_t *)karg;
                *mntopts = mdd->mdd_dt_conf.ddp_mntopts;
                RETURN(0);
        }
        }

        /* Below ioctls use obd_ioctl_data */
        if (len != sizeof(*data)) {
                CERROR("Bad ioctl size %d\n", len);
                RETURN(-EINVAL);
        }
        if (data->ioc_version != OBD_IOCTL_VERSION) {
                CERROR("Bad magic %x != %x\n", data->ioc_version,
                       OBD_IOCTL_VERSION);
                RETURN(-EINVAL);
        }

        switch (cmd) {
        case OBD_IOC_CHANGELOG_REG:
		rc = mdd_changelog_user_register(env, mdd, &data->ioc_u32_1);
                break;
        case OBD_IOC_CHANGELOG_DEREG:
                rc = mdd_changelog_user_purge(env, mdd, data->ioc_u32_1,
                                              MCUD_UNREGISTER);
                break;
        default:
                rc = -ENOTTY;
        }

        RETURN (rc);
}

/* type constructor/destructor: mdd_type_init, mdd_type_fini */
LU_TYPE_INIT_FINI(mdd, &mdd_thread_key, &mdd_quota_key);

const struct md_device_operations mdd_ops = {
        .mdo_statfs         = mdd_statfs,
        .mdo_root_get       = mdd_root_get,
        .mdo_maxsize_get    = mdd_maxsize_get,
        .mdo_maxeasize_get  = mdd_maxeasize_get,
        .mdo_init_capa_ctxt = mdd_init_capa_ctxt,
        .mdo_update_capa_key= mdd_update_capa_key,
        .mdo_llog_ctxt_get  = mdd_llog_ctxt_get,
        .mdo_iocontrol      = mdd_iocontrol,
#ifdef HAVE_QUOTA_SUPPORT
        .mdo_quota          = {
                .mqo_notify      = mdd_quota_notify,
                .mqo_setup       = mdd_quota_setup,
                .mqo_cleanup     = mdd_quota_cleanup,
                .mqo_recovery    = mdd_quota_recovery,
                .mqo_check       = mdd_quota_check,
                .mqo_on          = mdd_quota_on,
                .mqo_off         = mdd_quota_off,
                .mqo_setinfo     = mdd_quota_setinfo,
                .mqo_getinfo     = mdd_quota_getinfo,
                .mqo_setquota    = mdd_quota_setquota,
                .mqo_getquota    = mdd_quota_getquota,
                .mqo_getoinfo    = mdd_quota_getoinfo,
                .mqo_getoquota   = mdd_quota_getoquota,
                .mqo_invalidate  = mdd_quota_invalidate,
                .mqo_finvalidate = mdd_quota_finvalidate
        }
#endif
};

static struct lu_device_type_operations mdd_device_type_ops = {
        .ldto_init = mdd_type_init,
        .ldto_fini = mdd_type_fini,

        .ldto_start = mdd_type_start,
        .ldto_stop  = mdd_type_stop,

        .ldto_device_alloc = mdd_device_alloc,
        .ldto_device_free  = mdd_device_free,

        .ldto_device_init    = mdd_device_init,
        .ldto_device_fini    = mdd_device_fini
};

static struct lu_device_type mdd_device_type = {
        .ldt_tags     = LU_DEVICE_MD,
        .ldt_name     = LUSTRE_MDD_NAME,
        .ldt_ops      = &mdd_device_type_ops,
        .ldt_ctx_tags = LCT_MD_THREAD
};

/* context key constructor: mdd_key_init */
LU_KEY_INIT(mdd, struct mdd_thread_info);

static void mdd_key_fini(const struct lu_context *ctx,
                         struct lu_context_key *key, void *data)
{
        struct mdd_thread_info *info = data;
        if (info->mti_max_lmm != NULL)
                OBD_FREE_LARGE(info->mti_max_lmm, info->mti_max_lmm_size);
        if (info->mti_max_cookie != NULL)
                OBD_FREE_LARGE(info->mti_max_cookie, info->mti_max_cookie_size);
        mdd_buf_put(&info->mti_big_buf);

        OBD_FREE_PTR(info);
}

/* context key: mdd_thread_key */
LU_CONTEXT_KEY_DEFINE(mdd, LCT_MD_THREAD);

static struct lu_local_obj_desc llod_capa_key = {
        .llod_name      = CAPA_KEYS,
        .llod_oid       = MDD_CAPA_KEYS_OID,
        .llod_is_index  = 0,
};

static struct lu_local_obj_desc llod_mdd_orphan = {
        .llod_name      = orph_index_name,
        .llod_oid       = MDD_ORPHAN_OID,
        .llod_is_index  = 1,
        .llod_feat      = &dt_directory_features,
};

static struct lu_local_obj_desc llod_mdd_root = {
        .llod_name      = mdd_root_dir_name,
        .llod_oid       = MDD_ROOT_INDEX_OID,
        .llod_is_index  = 1,
        .llod_feat      = &dt_directory_features,
};

static int __init mdd_mod_init(void)
{
        struct lprocfs_static_vars lvars;
	int rc;

        lprocfs_mdd_init_vars(&lvars);

	rc = lu_kmem_init(mdd_caches);
	if (rc)
		return rc;

        llo_local_obj_register(&llod_capa_key);
        llo_local_obj_register(&llod_mdd_orphan);
        llo_local_obj_register(&llod_mdd_root);

	rc = class_register_type(&mdd_obd_device_ops, NULL, lvars.module_vars,
				 LUSTRE_MDD_NAME, &mdd_device_type);
	if (rc)
		lu_kmem_fini(mdd_caches);
	return rc;
}

static void __exit mdd_mod_exit(void)
{
        llo_local_obj_unregister(&llod_capa_key);
        llo_local_obj_unregister(&llod_mdd_orphan);
        llo_local_obj_unregister(&llod_mdd_root);

	class_unregister_type(LUSTRE_MDD_NAME);
	lu_kmem_fini(mdd_caches);
}

MODULE_AUTHOR("Sun Microsystems, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Meta-data Device Prototype ("LUSTRE_MDD_NAME")");
MODULE_LICENSE("GPL");

cfs_module(mdd, "0.1.0", mdd_mod_init, mdd_mod_exit);

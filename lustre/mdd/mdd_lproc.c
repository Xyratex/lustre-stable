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
 * Copyright (c) 2011 Xyratex, Inc.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/mdd/mdd_lproc.c
 *
 * Lustre Metadata Server (mdd) routines
 *
 * Author: Wang Di <wangdi@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_MDS

#include <linux/module.h>
#include <linux/poll.h>
#include <obd.h>
#include <obd_class.h>
#include <lustre_ver.h>
#include <obd_support.h>
#include <lprocfs_status.h>
#include <lu_time.h>
#include <lustre_log.h>
#include <lustre/lustre_idl.h>
#include <libcfs/libcfs_string.h>

#include "mdd_internal.h"

struct mdd_lproc_cntr {
        __u32       code;
        __u32       conf;
        const char *name;
        const char *unit;
} mdd_lproc_table[LPROC_MDD_NR] = {
        { LPROC_MDD_REBUILD_LINKEA_ADD, 0, "LinkEA added", "" },
        { LPROC_MDD_REBUILD_LINKEA_RM, 0, "LinkEA removed", "" },
        { LPROC_MDD_REBUILD_FILES, 0, "files handled", "" },
        { LPROC_MDD_REBUILD_DIRS, 0, "dirs handled", "" }
};

int mdd_procfs_init(struct mdd_device *mdd, const char *name)
{
        struct lprocfs_static_vars lvars;
        struct lu_device    *ld = &mdd->mdd_md_dev.md_lu_dev;
        struct obd_type     *type;
        int                  rc, i;
        ENTRY;

        type = ld->ld_type->ldt_obd_type;

        LASSERT(name != NULL);
        LASSERT(type != NULL);

        /* Find the type procroot and add the proc entry for this device */
        lprocfs_mdd_init_vars(&lvars);
        mdd->mdd_proc_entry = lprocfs_register(name, type->typ_procroot,
                                               lvars.obd_vars, mdd);
        if (IS_ERR(mdd->mdd_proc_entry)) {
                rc = PTR_ERR(mdd->mdd_proc_entry);
                CERROR("Error %d setting up lprocfs for %s\n",
                       rc, name);
                mdd->mdd_proc_entry = NULL;
                GOTO(out, rc);
        }

        mdd->mdd_stats = lprocfs_alloc_stats(LPROC_MDD_NR,
                                             LPROCFS_STATS_FLAG_NONE);
        if (mdd->mdd_stats == NULL)
                GOTO(out, rc = -ENOMEM);

        for (i = 0; i < LPROC_MDD_NR; ++i)
                lprocfs_counter_init(mdd->mdd_stats,
                                     mdd_lproc_table[i].code,
                                     mdd_lproc_table[i].conf,
                                     mdd_lproc_table[i].name,
                                     mdd_lproc_table[i].unit);
        rc = lprocfs_register_stats(mdd->mdd_proc_entry, "lu_stat",
                                    mdd->mdd_stats);

        EXIT;
out:
        if (rc)
               mdd_procfs_fini(mdd);
        return rc;
}

int mdd_procfs_fini(struct mdd_device *mdd)
{
        if (mdd->mdd_stats)
                lu_time_fini(&mdd->mdd_stats);

        if (mdd->mdd_proc_entry) {
                 lprocfs_remove(&mdd->mdd_proc_entry);
                 mdd->mdd_proc_entry = NULL;
        }
        RETURN(0);
}

void mdd_lprocfs_time_start(const struct lu_env *env)
{
        lu_lprocfs_time_start(env);
}

void mdd_lprocfs_time_end(const struct lu_env *env, struct mdd_device *mdd,
                          int idx)
{
        lu_lprocfs_time_end(env, mdd->mdd_stats, idx);
}

static int lprocfs_wr_atime_diff(struct file *file, const char *buffer,
                                 unsigned long count, void *data)
{
        struct mdd_device *mdd = data;
        char kernbuf[20], *end;
        unsigned long diff = 0;

        if (count > (sizeof(kernbuf) - 1))
                return -EINVAL;

        if (cfs_copy_from_user(kernbuf, buffer, count))
                return -EFAULT;

        kernbuf[count] = '\0';

        diff = simple_strtoul(kernbuf, &end, 0);
        if (kernbuf == end)
                return -EINVAL;

        mdd->mdd_atime_diff = diff;
        return count;
}

static int lprocfs_rd_atime_diff(char *page, char **start, off_t off,
                                 int count, int *eof, void *data)
{
        struct mdd_device *mdd = data;

        *eof = 1;
        return snprintf(page, count, "%lu\n", mdd->mdd_atime_diff);
}


/**** changelogs ****/
static int lprocfs_rd_changelog_mask(char *page, char **start, off_t off,
                                     int count, int *eof, void *data)
{
        struct mdd_device *mdd = data;
        int i = 0, rc = 0;

        *eof = 1;
        while (i < CL_LAST) {
                if (mdd->mdd_cl.mc_mask & (1 << i))
                        rc += snprintf(page + rc, count - rc, "%s ",
                                       changelog_type2str(i));
                i++;
        }
        return rc;
}

static int lprocfs_wr_changelog_mask(struct file *file, const char *buffer,
                                     unsigned long count, void *data)
{
        struct mdd_device *mdd = data;
        char *kernbuf;
        int rc;
        ENTRY;

        if (count >= CFS_PAGE_SIZE)
                RETURN(-EINVAL);
        OBD_ALLOC(kernbuf, CFS_PAGE_SIZE);
        if (kernbuf == NULL)
                RETURN(-ENOMEM);
        if (cfs_copy_from_user(kernbuf, buffer, count))
                GOTO(out, rc = -EFAULT);
        kernbuf[count] = 0;

        rc = cfs_str2mask(kernbuf, changelog_type2str, &mdd->mdd_cl.mc_mask,
                          CHANGELOG_MINMASK, CHANGELOG_ALLMASK);
        if (rc == 0)
                rc = count;
out:
        OBD_FREE(kernbuf, CFS_PAGE_SIZE);
        return rc;
}

struct cucb_data {
        char *page;
        int count;
        int idx;
};

static int lprocfs_changelog_users_cb(struct llog_handle *llh,
                                      struct llog_rec_hdr *hdr, void *data)
{
        struct llog_changelog_user_rec *rec;
        struct cucb_data *cucb = (struct cucb_data *)data;

        LASSERT(llh->lgh_hdr->llh_flags & LLOG_F_IS_PLAIN);

        rec = (struct llog_changelog_user_rec *)hdr;

        cucb->idx += snprintf(cucb->page + cucb->idx, cucb->count - cucb->idx,
                              CHANGELOG_USER_PREFIX"%-3d "LPU64"\n",
                              rec->cur_id, rec->cur_endrec);
        if (cucb->idx >= cucb->count)
                return -ENOSPC;

        return 0;
}

static int lprocfs_rd_changelog_users(char *page, char **start, off_t off,
                                      int count, int *eof, void *data)
{
        struct mdd_device *mdd = data;
        struct llog_ctxt *ctxt;
        struct cucb_data cucb;
        __u64 cur;

        *eof = 1;

        ctxt = llog_get_context(mdd2obd_dev(mdd),LLOG_CHANGELOG_USER_ORIG_CTXT);
        if (ctxt == NULL)
                return -ENXIO;
        LASSERT(ctxt->loc_handle->lgh_hdr->llh_flags & LLOG_F_IS_CAT);

        cfs_spin_lock(&mdd->mdd_cl.mc_lock);
        cur = mdd->mdd_cl.mc_index;
        cfs_spin_unlock(&mdd->mdd_cl.mc_lock);

        cucb.count = count;
        cucb.page = page;
        cucb.idx = 0;

        cucb.idx += snprintf(cucb.page + cucb.idx, cucb.count - cucb.idx,
                              "current index: "LPU64"\n", cur);

        cucb.idx += snprintf(cucb.page + cucb.idx, cucb.count - cucb.idx,
                              "%-5s %s\n", "ID", "index");

        llog_cat_process(ctxt->loc_handle, lprocfs_changelog_users_cb,
                         &cucb, 0, 0);

        llog_ctxt_put(ctxt);
        return cucb.idx;
}

#ifdef HAVE_QUOTA_SUPPORT
static int mdd_lprocfs_quota_rd_type(char *page, char **start, off_t off,
                                     int count, int *eof, void *data)
{
        struct mdd_device *mdd = data;
        return lprocfs_quota_rd_type(page, start, off, count, eof,
                                     mdd->mdd_obd_dev);
}

static int mdd_lprocfs_quota_wr_type(struct file *file, const char *buffer,
                                     unsigned long count, void *data)
{
        struct mdd_device *mdd = data;
        return lprocfs_quota_wr_type(file, buffer, count, mdd->mdd_obd_dev);
}
#endif

static int lprocfs_rd_sync_perm(char *page, char **start, off_t off,
                                int count, int *eof, void *data)
{
        struct mdd_device *mdd = data;

        LASSERT(mdd != NULL);
        return snprintf(page, count, "%d\n", mdd->mdd_sync_permission);
}

static int lprocfs_wr_sync_perm(struct file *file, const char *buffer,
                                unsigned long count, void *data)
{
        struct mdd_device *mdd = data;
        int val, rc;

        LASSERT(mdd != NULL);
        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        mdd->mdd_sync_permission = !!val;
        return count;
}

static int lprocfs_rd_percpu_rebuild(char *page, char **start, off_t off,
                                     int count, int *eof, void *data)
{
        struct mdd_device *mdd = data;
        int i, len = 0;

        LASSERT(mdd != NULL);
        len += snprintf(page, count, "dirs / files handled per-cpu\n");
        for (i = 0; i < cfs_num_possible_cpus(); i++) {
                struct lprocfs_stats *stats = mdd->mdd_stats;
                struct lprocfs_counter *lcf, *lcd;
                __s64 dirs, files;

                lcf = &(stats->ls_percpu[i]->lp_cntr[LPROC_MDD_REBUILD_FILES]);
                lcd = &(stats->ls_percpu[i]->lp_cntr[LPROC_MDD_REBUILD_DIRS]);
                files = lprocfs_read_helper(lcf, LPROCFS_FIELDS_FLAGS_COUNT);
                dirs = lprocfs_read_helper(lcd, LPROCFS_FIELDS_FLAGS_COUNT);

                len += snprintf(page + len, count, "%lld %lld\n", dirs, files);
        }
        return len;
}

static struct lprocfs_vars lprocfs_mdd_obd_vars[] = {
        { "atime_diff",      lprocfs_rd_atime_diff, lprocfs_wr_atime_diff, 0 },
        { "changelog_mask",  lprocfs_rd_changelog_mask,
                             lprocfs_wr_changelog_mask, 0 },
        { "changelog_users", lprocfs_rd_changelog_users, 0, 0},
#ifdef HAVE_QUOTA_SUPPORT
        { "quota_type",      mdd_lprocfs_quota_rd_type,
                             mdd_lprocfs_quota_wr_type, 0 },
#endif
        { "sync_permission", lprocfs_rd_sync_perm, lprocfs_wr_sync_perm, 0 },
        { "per_cpu_rebuild", lprocfs_rd_percpu_rebuild, 0, 0 },
        { 0 }
};

static struct lprocfs_vars lprocfs_mdd_module_vars[] = {
        { "num_refs",   lprocfs_rd_numrefs, 0, 0 },
        { 0 }
};

void lprocfs_mdd_init_vars(struct lprocfs_static_vars *lvars)
{
        lvars->module_vars  = lprocfs_mdd_module_vars;
        lvars->obd_vars     = lprocfs_mdd_obd_vars;
}


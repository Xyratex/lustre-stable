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
 *
 * Copyright (c) 2014, Intel Corporation.
 */
/*
 * lustre/osp/osp_object.c
 *
 * Lustre OST Proxy Device (OSP) is the agent on the local MDT for the OST
 * or remote MDT.
 *
 * OSP object attributes cache
 * ---------------------------
 * OSP object is the stub of the remote OST-object or MDT-object. Both the
 * attribute and the extended attributes are stored on the peer side remotely.
 * It is inefficient to send RPC to peer to fetch those attributes when every
 * get_attr()/get_xattr() called. For a large system, the LFSCK synchronous
 * mode scanning is prohibitively inefficient.
 *
 * So the OSP maintains the OSP object attributes cache to cache some
 * attributes on the local MDT. The cache is organized against the OSP
 * object as follows:
 *
 * struct osp_xattr_entry {
 *	struct list_head	 oxe_list;
 *	atomic_t		 oxe_ref;
 *	void			*oxe_value;
 *	int			 oxe_buflen;
 *	int			 oxe_namelen;
 *	int			 oxe_vallen;
 *	unsigned int		 oxe_exist:1,
 *				 oxe_ready:1;
 *	char			 oxe_buf[0];
 * };
 *
 * struct osp_object_attr {
 *	struct lu_attr		ooa_attr;
 *	struct list_head	ooa_xattr_list;
 * };
 *
 * struct osp_object {
 *	...
 *	struct osp_object_attr *opo_ooa;
 *	spinlock_t		opo_lock;
 *	...
 * };
 *
 * The basic attributes, such as owner/mode/flags, are stored in the
 * osp_object_attr::ooa_attr. The extended attributes will be stored
 * as osp_xattr_entry. Every extended attribute has an independent
 * osp_xattr_entry, and all the osp_xattr_entry are linked into the
 * osp_object_attr::ooa_xattr_list. The OSP object attributes cache
 * is protected by the osp_object::opo_lock.
 *
 * Not all OSP objects have an attributes cache because maintaining
 * the cache requires some resources. Currently, the OSP object
 * attributes cache will be initialized when the attributes or the
 * extended attributes are pre-fetched via osp_declare_attr_get()
 * or osp_declare_xattr_get(). That is usually for LFSCK purpose,
 * but it also can be shared by others.
 *
 * Author: Alex Zhuravlev <alexey.zhuravlev@intel.com>
 * Author: Mikhail Pershin <mike.tappro@intel.com>
 */

#define DEBUG_SUBSYSTEM S_MDS

#include "osp_internal.h"

/**
 * Assign FID to the OST object.
 *
 * This function will assign the FID to the OST object of a striped file.
 *
 * \param[in] env	pointer to the thread context
 * \param[in] d		pointer to the OSP device
 * \param[in] o		pointer to the OSP object that the FID will be
 *			assigned to
 */
static void osp_object_assign_fid(const struct lu_env *env,
				 struct osp_device *d, struct osp_object *o)
{
	struct osp_thread_info *osi = osp_env_info(env);

	LASSERT(fid_is_zero(lu_object_fid(&o->opo_obj.do_lu)));
	LASSERT(o->opo_reserved);
	o->opo_reserved = 0;

	osp_precreate_get_fid(env, d, &osi->osi_fid);

	lu_object_assign_fid(env, &o->opo_obj.do_lu, &osi->osi_fid);
}

/**
 * Implement OSP layer dt_object_operations::do_declare_attr_set() interface.
 * XXX: NOT prepare set_{attr,xattr} RPC for remote transaction.
 *
 * According to our current transaction/dt_object_lock framework (to make
 * the cross-MDTs modification for DNE1 to be workable), the transaction
 * sponsor will start the transaction firstly, then try to acquire related
 * dt_object_lock if needed. Under such rules, if we want to prepare the
 * set_{attr,xattr} RPC in the RPC declare phase, then related attr/xattr
 * should be known without dt_object_lock. But such condition maybe not
 * true for some remote transaction case. For example:
 *
 * For linkEA repairing (by LFSCK) case, before the LFSCK thread obtained
 * the dt_object_lock on the target MDT-object, it cannot know whether
 * the MDT-object has linkEA or not, neither invalid or not.
 *
 * Since the LFSCK thread cannot hold dt_object_lock before the (remote)
 * transaction start (otherwise there will be some potential deadlock),
 * it cannot prepare related RPC for repairing during the declare phase
 * as other normal transactions do.
 *
 * To resolve the trouble, we will make OSP to prepare related RPC
 * (set_attr/set_xattr/del_xattr) after remote transaction started,
 * and trigger the remote updating (RPC sending) when trans_stop.
 * Then the up layer users, such as LFSCK, can follow the general
 * rule to handle trans_start/dt_object_lock for repairing linkEA
 * inconsistency without distinguishing remote MDT-object.
 *
 * In fact, above solution for remote transaction should be the normal
 * model without considering DNE1. The trouble brought by DNE1 will be
 * resolved in DNE2. At that time, this patch can be removed.
 *
 * \param[in] env	pointer to the thread context
 * \param[in] dt	pointer to the OSP layer dt_object
 * \param[in] attr	pointer to the attribute to be set
 * \param[in] th	pointer to the transaction handler
 *
 * \retval		0 for success
 * \retval		negative error number on failure
 */
static int osp_declare_attr_set(const struct lu_env *env, struct dt_object *dt,
				const struct lu_attr *attr, struct thandle *th)
{
	struct osp_device	*d = lu2osp_dev(dt->do_lu.lo_dev);
	struct osp_object	*o = dt2osp_obj(dt);
	int			 rc = 0;

	ENTRY;

	/*
	 * Usually we don't allow server stack to manipulate size
	 * but there is a special case when striping is created
	 * late, after stripeless file got truncated to non-zero.
	 *
	 * In this case we do the following:
	 *
	 * 1) grab id in declare - this can lead to leaked OST objects
	 *    but we don't currently have proper mechanism and the only
	 *    options we have are to do truncate RPC holding transaction
	 *    open (very bad) or to grab id in declare at cost of leaked
	 *    OST object in same very rare unfortunate case (just bad)
	 *    notice 1.6-2.0 do assignment outside of running transaction
	 *    all the time, meaning many more chances for leaked objects.
	 *
	 * 2) send synchronous truncate RPC with just assigned id
	 */

	/* there are few places in MDD code still passing NULL
	 * XXX: to be fixed soon */
	if (attr == NULL)
		RETURN(0);

	if (attr->la_valid & LA_SIZE && attr->la_size > 0 &&
	    fid_is_zero(lu_object_fid(&o->opo_obj.do_lu))) {
		LASSERT(!dt_object_exists(dt));
		osp_object_assign_fid(env, d, o);
		rc = osp_object_truncate(env, dt, attr->la_size);
		if (rc)
			RETURN(rc);
	}

	if (!(attr->la_valid & (LA_UID | LA_GID)))
		RETURN(0);

	/*
	 * track all UID/GID changes via llog
	 */
	rc = osp_sync_declare_add(env, o, MDS_SETATTR64_REC, th);

	RETURN(rc);
}

/**
 * Implement OSP layer dt_object_operations::do_attr_set() interface.
 *
 * Set attribute to the specified OST object.
 *
 * If the transaction is a remote transaction, then related modification
 * sub-request has been added in the declare phase and related OUT RPC
 * has been triggered at transaction start. Otherwise it will generate
 * a MDS_SETATTR64_REC record in the llog. There is a dedicated thread
 * to handle the llog asynchronously.
 *
 * If the attribute entry exists in the OSP object attributes cache,
 * then update the cached attribute according to given attribute.
 *
 * \param[in] env	pointer to the thread context
 * \param[in] dt	pointer to the OSP layer dt_object
 * \param[in] attr	pointer to the attribute to be set
 * \param[in] th	pointer to the transaction handler
 * \param[in] capa	the capability for this operation
 *
 * \retval		0 for success
 * \retval		negative error number on failure
 */
static int osp_attr_set(const struct lu_env *env, struct dt_object *dt,
			const struct lu_attr *attr, struct thandle *th,
			struct lustre_capa *capa)
{
	struct osp_object	*o = dt2osp_obj(dt);
	int			 rc = 0;

	ENTRY;

	/* we're interested in uid/gid changes only */
	if (!(attr->la_valid & (LA_UID | LA_GID)))
		RETURN(0);

	/*
	 * once transaction is committed put proper command on
	 * the queue going to our OST
	 */
	rc = osp_sync_add(env, o, MDS_SETATTR64_REC, th, attr);

	/* XXX: send new uid/gid to OST ASAP? */

	RETURN(rc);
}

/**
 * Implement OSP layer dt_object_operations::do_declare_create() interface.
 *
 * Declare that the caller will create the OST object.
 *
 * If the transaction is a remote transaction (please refer to the
 * comment of osp_trans_create() for remote transaction), then the FID
 * for the OST object has been assigned already, and will be handled
 * as create (remote) MDT object via osp_md_declare_object_create().
 * This function is usually used for LFSCK to re-create the lost OST
 * object. Otherwise, if it is not replay case, the OSP will reserve
 * pre-created object for the subsequent create operation; if the MDT
 * side cached pre-created objects are less than some threshold, then
 * it will wakeup the pre-create thread.
 *
 * \param[in] env	pointer to the thread context
 * \param[in] dt	pointer to the OSP layer dt_object
 * \param[in] attr	the attribute for the object to be created
 * \param[in] hint	pointer to the hint for creating the object, such as
 *			the parent object
 * \param[in] dof	pointer to the dt_object_format for help the creation
 * \param[in] th	pointer to the transaction handler
 *
 * \retval		0 for success
 * \retval		negative error number on failure
 */
static int osp_declare_object_create(const struct lu_env *env,
				     struct dt_object *dt,
				     struct lu_attr *attr,
				     struct dt_allocation_hint *hint,
				     struct dt_object_format *dof,
				     struct thandle *th)
{
	struct osp_thread_info	*osi = osp_env_info(env);
	struct osp_device	*d = lu2osp_dev(dt->do_lu.lo_dev);
	struct osp_object	*o = dt2osp_obj(dt);
	const struct lu_fid	*fid;
	int			 rc = 0;

	ENTRY;

	/* should happen to non-0 OSP only so that at least one object
	 * has been already declared in the scenario and LOD should
	 * cleanup that */
	if (OBD_FAIL_CHECK(OBD_FAIL_MDS_OSC_CREATE_FAIL) && d->opd_index == 1)
		RETURN(-ENOSPC);

	LASSERT(d->opd_last_used_oid_file);
	fid = lu_object_fid(&dt->do_lu);

	/*
	 * There can be gaps in precreated ids and record to unlink llog
	 * XXX: we do not handle gaps yet, implemented before solution
	 *	was found to be racy, so we disabled that. there is no
	 *	point in making useless but expensive llog declaration.
	 */
	/* rc = osp_sync_declare_add(env, o, MDS_UNLINK64_REC, th); */

	if (unlikely(!fid_is_zero(fid))) {
		/* replay case: caller knows fid */
		osi->osi_off = sizeof(osi->osi_id) * d->opd_index;
		rc = dt_declare_record_write(env, d->opd_last_used_oid_file,
					     sizeof(osi->osi_id), osi->osi_off,
					     th);
		RETURN(rc);
	}

	/*
	 * in declaration we need to reserve object so that we don't block
	 * awaiting precreation RPC to complete
	 */
	rc = osp_precreate_reserve(env, d);
	/*
	 * we also need to declare update to local "last used id" file for
	 * recovery if object isn't used for a reason, we need to release
	 * reservation, this can be made in osd_object_release()
	 */
	if (rc == 0) {
		/* mark id is reserved: in create we don't want to talk
		 * to OST */
		LASSERT(o->opo_reserved == 0);
		o->opo_reserved = 1;

		/* common for all OSPs file hystorically */
		osi->osi_off = sizeof(osi->osi_id) * d->opd_index;
		rc = dt_declare_record_write(env, d->opd_last_used_oid_file,
					     sizeof(osi->osi_id), osi->osi_off,
					     th);
	} else {
		/* not needed in the cache anymore */
		set_bit(LU_OBJECT_HEARD_BANSHEE,
			    &dt->do_lu.lo_header->loh_flags);
	}
	RETURN(rc);
}

/**
 * Implement OSP layer dt_object_operations::do_create() interface.
 *
 * Create the OST object.
 *
 * For remote transaction case, the real create sub-request has been
 * added in the declare phase and related (OUT) RPC has been triggered
 * at transaction start. Here, like creating (remote) MDT object, the
 * OSP will mark the object existence via osp_md_object_create().
 *
 * For non-remote transaction case, the OSP will assign FID to the
 * object to be created, and update last_used Object ID (OID) file.
 *
 * \param[in] env	pointer to the thread context
 * \param[in] dt	pointer to the OSP layer dt_object
 * \param[in] attr	the attribute for the object to be created
 * \param[in] hint	pointer to the hint for creating the object, such as
 *			the parent object
 * \param[in] dof	pointer to the dt_object_format for help the creation
 * \param[in] th	pointer to the transaction handler
 *
 * \retval		0 for success
 * \retval		negative error number on failure
 */
static int osp_object_create(const struct lu_env *env, struct dt_object *dt,
			     struct lu_attr *attr,
			     struct dt_allocation_hint *hint,
			     struct dt_object_format *dof, struct thandle *th)
{
	struct osp_thread_info	*osi = osp_env_info(env);
	struct osp_device	*d = lu2osp_dev(dt->do_lu.lo_dev);
	struct osp_object	*o = dt2osp_obj(dt);
	int			rc = 0;
	struct lu_fid		*fid = &osi->osi_fid;
	struct lu_fid		*last_fid = &d->opd_last_used_fid;
	ENTRY;

	if (o->opo_reserved) {
		/* regular case, fid is assigned holding transaction open */
		 osp_object_assign_fid(env, d, o);
	}

	memcpy(fid, lu_object_fid(&dt->do_lu), sizeof(*fid));

	LASSERTF(fid_is_sane(fid), "fid for osp_object %p is insane"DFID"!\n",
		 o, PFID(fid));

	if (!o->opo_reserved) {
		/* special case, id was assigned outside of transaction
		 * see comments in osp_declare_attr_set */
		spin_lock(&d->opd_pre_lock);
		osp_update_last_fid(d, fid);
		spin_unlock(&d->opd_pre_lock);
	}

	CDEBUG(D_INODE, "fid for osp_object %p is "DFID"\n", o, PFID(fid));

	/* If the precreate ends, it means it will be ready to rollover to
	 * the new sequence soon, all the creation should be synchronized,
	 * otherwise during replay, the replay fid will be inconsistent with
	 * last_used/create fid */
	if (osp_precreate_end_seq(env, d) && osp_is_fid_client(d))
		th->th_sync = 1;

	/*
	 * it's OK if the import is inactive by this moment - id was created
	 * by OST earlier, we just need to maintain it consistently on the disk
	 * once import is reconnected, OSP will claim this and other objects
	 * used and OST either keep them, if they exist or recreate
	 */

	/* we might have lost precreated objects */
	if (unlikely(d->opd_gap_count) > 0) {
		spin_lock(&d->opd_pre_lock);
		if (d->opd_gap_count > 0) {
			int count = d->opd_gap_count;

			ostid_set_id(&osi->osi_oi,
				     fid_oid(&d->opd_gap_start_fid));
			d->opd_gap_count = 0;
			spin_unlock(&d->opd_pre_lock);

			CDEBUG(D_HA, "Writing gap "DFID"+%d in llog\n",
			       PFID(&d->opd_gap_start_fid), count);
			/* real gap handling is disabled intil ORI-692 will be
			 * fixed, now we only report gaps */
		} else {
			spin_unlock(&d->opd_pre_lock);
		}
	}

	/* Only need update last_used oid file, seq file will only be update
	 * during seq rollover */
	if (fid_is_idif((last_fid)))
		osi->osi_id = fid_idif_id(fid_seq(last_fid),
					  fid_oid(last_fid), fid_ver(last_fid));
	else
		osi->osi_id = fid_oid(last_fid);
	osp_objid_buf_prep(&osi->osi_lb, &osi->osi_off,
			   &osi->osi_id, d->opd_index);

	rc = dt_record_write(env, d->opd_last_used_oid_file, &osi->osi_lb,
			     &osi->osi_off, th);

	CDEBUG(D_HA, "%s: Wrote last used FID: "DFID", index %d: %d\n",
	       d->opd_obd->obd_name, PFID(fid), d->opd_index, rc);

	RETURN(rc);
}

/**
 * Implement OSP layer dt_object_operations::do_declare_destroy() interface.
 *
 * Declare that the caller will destroy the specified OST object.
 *
 * The OST object destroy will be handled via llog asynchronously. This
 * function will declare the credits for generating MDS_UNLINK64_REC llog.
 *
 * \param[in] env	pointer to the thread context
 * \param[in] dt	pointer to the OSP layer dt_object to be destroyed
 * \param[in] th	pointer to the transaction handler
 *
 * \retval		0 for success
 * \retval		negative error number on failure
 */
static int osp_declare_object_destroy(const struct lu_env *env,
				      struct dt_object *dt,
				      struct thandle *th)
{
	struct osp_object	*o = dt2osp_obj(dt);
	int			 rc = 0;

	ENTRY;

	/*
	 * track objects to be destroyed via llog
	 */
	rc = osp_sync_declare_add(env, o, MDS_UNLINK64_REC, th);

	RETURN(rc);
}

/**
 * Implement OSP layer dt_object_operations::do_destroy() interface.
 *
 * Destroy the specified OST object.
 *
 * The OSP generates a MDS_UNLINK64_REC record in the llog. There
 * will be some dedicated thread to handle the llog asynchronously.
 *
 * It also marks the object as non-cached.
 *
 * \param[in] env	pointer to the thread context
 * \param[in] dt	pointer to the OSP layer dt_object to be destroyed
 * \param[in] th	pointer to the transaction handler
 *
 * \retval		0 for success
 * \retval		negative error number on failure
 */
static int osp_object_destroy(const struct lu_env *env, struct dt_object *dt,
			      struct thandle *th)
{
	struct osp_object	*o = dt2osp_obj(dt);
	int			 rc = 0;

	ENTRY;

	/*
	 * once transaction is committed put proper command on
	 * the queue going to our OST
	 */
	rc = osp_sync_add(env, o, MDS_UNLINK64_REC, th, NULL);

	/* not needed in cache any more */
	set_bit(LU_OBJECT_HEARD_BANSHEE, &dt->do_lu.lo_header->loh_flags);

	RETURN(rc);
}

struct dt_object_operations osp_obj_ops = {
	.do_declare_attr_set	= osp_declare_attr_set,
	.do_attr_set		= osp_attr_set,
	.do_declare_create	= osp_declare_object_create,
	.do_create		= osp_object_create,
	.do_declare_destroy	= osp_declare_object_destroy,
	.do_destroy		= osp_object_destroy,
};

static int is_ost_obj(struct lu_object *lo)
{
	struct osp_device  *osp  = lu2osp_dev(lo->lo_dev);

	return !osp->opd_connect_mdt;
}

/**
 * Implement OSP layer lu_object_operations::loo_object_init() interface.
 *
 * Initialize the object.
 *
 * If it is a remote MDT object, then call do_attr_get() to fetch
 * the attribute from the peer.
 *
 * \param[in] env	pointer to the thread context
 * \param[in] o		pointer to the OSP layer lu_object
 * \param[in] conf	unused
 *
 * \retval		0 for success
 * \retval		negative error number on failure
 */
static int osp_object_init(const struct lu_env *env, struct lu_object *o,
			   const struct lu_object_conf *conf)
{
	struct osp_object	*po = lu2osp_obj(o);
	int			rc = 0;
	ENTRY;

	if (is_ost_obj(o)) {
		po->opo_obj.do_ops = &osp_obj_ops;
	} else {
		struct lu_attr		*la = &osp_env_info(env)->osi_attr;

		po->opo_obj.do_ops = &osp_md_obj_ops;
		o->lo_header->loh_attr |= LOHA_REMOTE;
		rc = po->opo_obj.do_ops->do_attr_get(env, lu2dt_obj(o),
						     la, NULL);
		if (rc == 0)
			o->lo_header->loh_attr |=
				LOHA_EXISTS | (la->la_mode & S_IFMT);
		if (rc == -ENOENT)
			rc = 0;
		init_rwsem(&po->opo_sem);
	}
	RETURN(rc);
}

/**
 * Implement OSP layer lu_object_operations::loo_object_free() interface.
 *
 * Finalize the object.
 *
 * If the OSP object has attributes cache, then destroy the cache.
 * Free the object finally.
 *
 * \param[in] env	pointer to the thread context
 * \param[in] o		pointer to the OSP layer lu_object
 */
static void osp_object_free(const struct lu_env *env, struct lu_object *o)
{
	struct osp_object	*obj = lu2osp_obj(o);
	struct lu_object_header	*h = o->lo_header;

	dt_object_fini(&obj->opo_obj);
	lu_object_header_fini(h);
	OBD_SLAB_FREE_PTR(obj, osp_object_kmem);
}

/**
 * Implement OSP layer lu_object_operations::loo_object_release() interface.
 *
 * Cleanup (not free) the object.
 *
 * If it is a reserved object but failed to be created, or it is an OST
 * object, then mark the object as non-cached.
 *
 * \param[in] env	pointer to the thread context
 * \param[in] o		pointer to the OSP layer lu_object
 */
static void osp_object_release(const struct lu_env *env, struct lu_object *o)
{
	struct osp_object	*po = lu2osp_obj(o);
	struct osp_device	*d  = lu2osp_dev(o->lo_dev);

	ENTRY;

	/*
	 * release reservation if object was declared but not created
	 * this may require lu_object_put() in LOD
	 */
	if (unlikely(po->opo_reserved)) {
		LASSERT(d->opd_pre_reserved > 0);
		spin_lock(&d->opd_pre_lock);
		d->opd_pre_reserved--;
		spin_unlock(&d->opd_pre_lock);
		/*
		 * Check that osp_precreate_cleanup_orphans is not blocked
		 * due to opd_pre_reserved > 0.
		 */
		if (unlikely(d->opd_pre_reserved == 0 &&
			     (d->opd_pre_recovering || d->opd_pre_status)))
			wake_up(&d->opd_pre_waitq);

		/* not needed in cache any more */
		set_bit(LU_OBJECT_HEARD_BANSHEE, &o->lo_header->loh_flags);
	}
	EXIT;
}

static int osp_object_print(const struct lu_env *env, void *cookie,
			    lu_printer_t p, const struct lu_object *l)
{
	const struct osp_object *o = lu2osp_obj((struct lu_object *)l);

	return (*p)(env, cookie, LUSTRE_OSP_NAME"-object@%p", o);
}

static int osp_object_invariant(const struct lu_object *o)
{
	LBUG();
}

struct lu_object_operations osp_lu_obj_ops = {
	.loo_object_init	= osp_object_init,
	.loo_object_free	= osp_object_free,
	.loo_object_release	= osp_object_release,
	.loo_object_print	= osp_object_print,
	.loo_object_invariant	= osp_object_invariant
};

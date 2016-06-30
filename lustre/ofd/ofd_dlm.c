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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2014 Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ofd/ofd_dlm.c
 *
 * This file contains OBD Filter Device (OFD) LDLM-related code which is just
 * intent handling for glimpse lock.
 *
 * Author: Andreas Dilger <andreas.dilger@intel.com>
 * Author: Jinshan Xiong <jinshan.xiong@intel.com>
 * Author: Alexey Zhuravlev <alexey.zhuravlev@intel.com>
 * Author: Mikhail Pershin <mike.pershin@intel.com>
 */

#define DEBUG_SUBSYSTEM S_FILTER

#include "ofd_internal.h"

struct ofd_intent_args {
	struct list_head	gl_list;
	__u64			 size;
	int			liblustre;
	int			no_glimpse_ast;
};

int ofd_dlm_init(void)
{
	ldlm_glimpse_work_kmem = kmem_cache_create("ldlm_glimpse_work_kmem",
					  sizeof(struct ldlm_glimpse_work),
					  0, 0, NULL);
	if (ldlm_glimpse_work_kmem == NULL)
		return -ENOMEM;
	else
		return 0;
}

void ofd_dlm_exit(void)
{
	if (ldlm_glimpse_work_kmem) {
		kmem_cache_destroy(ldlm_glimpse_work_kmem);
		ldlm_glimpse_work_kmem = NULL;
	}
}

/**
 * OFD interval callback.
 *
 * The interval_callback_t is part of interval_iterate_reverse() and is called
 * for each interval in tree. The OFD interval callback searches for locks
 * covering extents beyond the given args->size. This is used to decide if LVB
 * data is outdated.
 *
 * It finds the highest lock (by starting point) in this interval, and adds it
 * to the list of locks to glimpse.  We must glimpse a list of locks - rather
 * than only the highest lock on the file - because lock ahead creates extent
 * locks in advance of IO, and so breaks the assumption that the holder of the
 * highest lock knows the current file size.
 *
 * This assumption is normally true because locks which are created as part of
 * IO - rather than in advance of it - are guaranteed to be 'active', IE,
 * involved in IO, and the highest 'active' lock always knows the current file
 * size, because it is either not changing or the holder of that lock is
 * responsible for updating it.
 *
 * So we need only glimpse until we find the first 'active' lock.
 * Unfortunately, there is no way to know if a lock ahead/speculative lock is
 * 'active' from the server side.  So we must glimpse all speculative locks we
 * find and merge their LVBs.
 *
 * However, *all* non-speculative locks are active.  So we can stop glimpsing
 * as soon as we find a non-speculative lock.  Currently, all speculative locks
 * have LDLM_FL_NO_EXPANSION set, and we use this to identify them.  This is
 * enforced by an assertion in osc_lock_init, which references this comment.
 *
 * If that ever changes, we will either need to find a new way to identify
 * active locks or we will need to glimpse all PW locks.
 *
 * Note that it is safe to glimpse only the 'top' lock from each interval
 * because ofd_intent_cb is only called for PW extent locks, and for PW locks,
 * there is only one lock per interval.
 *
 * \param[in] n		interval node
 * \param[in,out] args	intent arguments, gl work list for identified locks
 *
 * \retval		INTERVAL_ITER_STOP if the interval is lower than
 *			file size, caller stops execution
 * \retval		INTERVAL_ITER_CONT if callback finished successfully
 *			and caller may continue execution
 */
static enum interval_iter ofd_intent_cb(struct interval_node *n, void *args)
{
	struct ldlm_interval	 *node = (struct ldlm_interval *)n;
	struct ofd_intent_args	 *arg = args;
	__u64			  size = arg->size;
	struct ldlm_lock	*v = NULL;
	struct ldlm_lock	 *lck;
	struct ldlm_glimpse_work *gl_work;

	/* If the interval is lower than the current file size, just break. */
	if (interval_high(n) <= size)
		return INTERVAL_ITER_STOP;

	/* Find the 'victim' lock from this interval */
	list_for_each_entry(lck, &node->li_group, l_sl_policy) {
		/* Don't send glimpse ASTs to liblustre clients.
		 * They aren't listening for them, and they do
		 * entirely synchronous I/O anyways. */
		if (lck->l_export == NULL || lck->l_export->exp_libclient)
			continue;

		if (arg->liblustre)
			arg->liblustre = 0;

		v = LDLM_LOCK_GET(lck);

		/* the same policy group - every lock has the
		 * same extent, so needn't do it any more */
		break;
	}

	/* l_export can be null in race with eviction - In that case, we will
	   not find any locks in this interval */
	if (!v) {
		CDEBUG(D_DLMTRACE, "No lock found for interval - probably due"
				   " to an eviction.\n");
		return INTERVAL_ITER_CONT;
	}

	/*
	 * This check is for lock taken in ofd_destroy_by_fid() that does
	 * not have l_glimpse_ast set. So the logic is: if there is a lock
	 * with no l_glimpse_ast set, this object is being destroyed already.
	 * Hence, if you are grabbing DLM locks on the server, always set
	 * non-NULL glimpse_ast (e.g., ldlm_request.c::ldlm_glimpse_ast()).
	 */
	if (v->l_glimpse_ast == NULL) {
		LDLM_DEBUG(v, "no l_glimpse_ast");
		arg->no_glimpse_ast = 1;
		LDLM_LOCK_RELEASE(v);
		return INTERVAL_ITER_STOP;
	}

	OBD_SLAB_ALLOC_PTR_GFP(gl_work, ldlm_glimpse_work_kmem, GFP_ATOMIC);

	/* Populate the gl_work structure. */
	gl_work->gl_lock = v;
	list_add_tail(&gl_work->gl_list, &arg->gl_list);
	/* There is actually no need for a glimpse descriptor when glimpsing
	 * extent locks */
	gl_work->gl_desc = NULL;
	/* This tells ldlm_work_gl_ast_lock this was allocated from a slab and
	 * must be freed in a slab-aware manner. */
	gl_work->gl_flags = LDLM_GL_WORK_SLAB_ALLOCATED;

	/* If NO_EXPANSION is not set, this is an active lock, and we don't need
	 * to glimpse any further.  See comment above this function. */
	if (!(v->l_flags & LDLM_FL_NO_EXPANSION))
		return INTERVAL_ITER_STOP;
	else
		return INTERVAL_ITER_CONT;
}

/**
 * OFD lock intent policy
 *
 * This defines ldlm_namespace::ns_policy interface for OFD.
 * Intent policy is called when lock has an intent, for OFD that
 * means glimpse lock and policy fills Lock Value Block (LVB).
 *
 * If already granted lock is found it will be placed in \a lockp and
 * returned back to caller function.
 *
 * \param[in] ns	 namespace
 * \param[in,out] lockp	 pointer to the lock
 * \param[in] req_cookie incoming request
 * \param[in] mode	 LDLM mode
 * \param[in] flags	 LDLM flags
 * \param[in] data	 opaque data, not used in OFD policy
 *
 * \retval		ELDLM_LOCK_REPLACED if already granted lock was found
 *			and placed in \a lockp
 * \retval		ELDLM_LOCK_ABORTED in other cases except error
 * \retval		negative value on error
 */
int ofd_intent_policy(struct ldlm_namespace *ns, struct ldlm_lock **lockp,
		      void *req_cookie, ldlm_mode_t mode, __u64 flags,
		      void *data)
{
	struct ptlrpc_request		*req = req_cookie;
	struct ldlm_lock		*lock = *lockp;
	struct ldlm_resource		*res = lock->l_resource;
	ldlm_processing_policy		 policy;
	struct ost_lvb			*res_lvb, *reply_lvb;
	struct ldlm_reply		*rep;
	ldlm_error_t			 err;
	int				 idx, rc;
	struct ldlm_interval_tree	*tree;
	struct ofd_intent_args		 arg;
	__u32				 repsize[3] = {
		[MSG_PTLRPC_BODY_OFF] = sizeof(struct ptlrpc_body),
		[DLM_LOCKREPLY_OFF]   = sizeof(*rep),
		[DLM_REPLY_REC_OFF]   = sizeof(*reply_lvb)
	};
	struct ldlm_glimpse_work	 *pos, *tmp;
	ENTRY;

	INIT_LIST_HEAD(&arg.gl_list);
	arg.no_glimpse_ast = 0;
	arg.liblustre = 1;
	lock->l_lvb_type = LVB_T_OST;
	policy = ldlm_get_processing_policy(res);
	LASSERT(policy != NULL);
	LASSERT(req != NULL);

	rc = lustre_pack_reply(req, 3, repsize, NULL);
	if (rc)
		RETURN(req->rq_status = rc);

	rep = lustre_msg_buf(req->rq_repmsg, DLM_LOCKREPLY_OFF, sizeof(*rep));
	LASSERT(rep != NULL);

	reply_lvb = lustre_msg_buf(req->rq_repmsg, DLM_REPLY_REC_OFF,
				   sizeof(*reply_lvb));
	LASSERT(reply_lvb != NULL);

	/* Call the extent policy function to see if our request can be
	 * granted, or is blocked.
	 * If the OST lock has LDLM_FL_HAS_INTENT set, it means a glimpse
	 * lock, and should not be granted if the lock will be blocked.
	 */

	if (flags & LDLM_FL_BLOCK_NOWAIT) {
		OBD_FAIL_TIMEOUT(OBD_FAIL_LDLM_AGL_DELAY, 5);

		if (OBD_FAIL_CHECK(OBD_FAIL_LDLM_AGL_NOLOCK))
			RETURN(ELDLM_LOCK_ABORTED);
	}

	LASSERT(ns == ldlm_res_to_ns(res));
	lock_res(res);

	/* Check if this is a resend case (MSG_RESENT is set on RPC) and a
	 * lock was found by ldlm_handle_enqueue(); if so no need to grant
	 * it again. */
	if (flags & LDLM_FL_RESENT) {
		rc = LDLM_ITER_CONTINUE;
	} else {
		__u64 tmpflags = LDLM_FL_INTENT_ONLY;
		rc = policy(lock, &tmpflags, 0, &err, NULL);
		check_res_locked(res);
	}

	/* The lock met with no resistance; we're finished. */
	if (rc == LDLM_ITER_CONTINUE) {
		/* do not grant locks to the liblustre clients: they cannot
		 * handle ASTs robustly.  We need to do this while still
		 * holding ns_lock to avoid the lock remaining on the res_link
		 * list (and potentially being added to l_pending_list by an
		 * AST) when we are going to drop this lock ASAP. */
		if (lock->l_export->exp_libclient ||
		    OBD_FAIL_TIMEOUT(OBD_FAIL_LDLM_GLIMPSE, 2)) {
			ldlm_resource_unlink_lock(lock);
			err = ELDLM_LOCK_ABORTED;
		} else {
			err = ELDLM_LOCK_REPLACED;
		}
		unlock_res(res);
		RETURN(err);
	} else if (flags & LDLM_FL_BLOCK_NOWAIT) {
		/* LDLM_FL_BLOCK_NOWAIT means it is for AGL. Do not send glimpse
		 * callback for glimpse size. The real size user will trigger
		 * the glimpse callback when necessary. */
		unlock_res(res);
		RETURN(ELDLM_LOCK_ABORTED);
	}

	/* Do not grant any lock, but instead send GL callbacks.  The extent
	 * policy nicely created a list of all PW locks for us.  We will choose
	 * the highest of those which are larger than the size in the LVB, if
	 * any, and perform a glimpse callback. */
	res_lvb = res->lr_lvb_data;
	LASSERT(res_lvb != NULL);
	*reply_lvb = *res_lvb;

	/*
	 * ->ns_lock guarantees that no new locks are granted, and,
	 *  therefore, that res->lr_lvb_data cannot increase beyond the
	 *  end of already granted lock. As a result, it is safe to
	 *  check against "stale" reply_lvb->lvb_size value without
	 *  res->lr_lvb_sem.
	 */
	arg.size = reply_lvb->lvb_size;

	/* Check for PW locks beyond the size in the LVB, build the list
	 * of locks to glimpse (arg.gl_list) */
	for (idx = 0; idx < LCK_MODE_NUM; idx++) {
		tree = &res->lr_itree[idx];
		if (tree->lit_mode == LCK_PR)
			continue;

		interval_iterate_reverse(tree->lit_root, ofd_intent_cb, &arg);
	}
	unlock_res(res);

	/* There were no PW locks beyond the size in the LVB; finished. */
	if (list_empty(&arg.gl_list)) {
		if (arg.liblustre) {
			/* If we discovered a liblustre client with a PW lock,
			 * however, the LVB may be out of date!  The LVB is
			 * updated only on glimpse (which we don't do for
			 * liblustre clients) and cancel (which the client
			 * obviously has not yet done).  So if it has written
			 * data but kept the lock, the LVB is stale and needs
			 * to be updated from disk.
			 *
			 * Of course, this will all disappear when we switch to
			 * taking liblustre locks on the OST. */
			ldlm_res_lvbo_update(res, NULL, 1);
		}
		RETURN(ELDLM_LOCK_ABORTED);
	}

	if (arg.no_glimpse_ast) {
		/* We are racing with unlink(); just return -ENOENT */
		rep->lock_policy_res1 = ptlrpc_status_hton(-ENOENT);
		GOTO(out, ELDLM_LOCK_ABORTED);
	}

	/* this will update the LVB */
	rc = ldlm_glimpse_locks(res, &arg.gl_list);

	lock_res(res);
	*reply_lvb = *res_lvb;
	unlock_res(res);

out:
	/* If the list is not empty, we failed to glimpse some locks and
	 * must clean up.  Usually due to a race with unlink.*/
	list_for_each_entry_safe(pos, tmp, &arg.gl_list, gl_list) {
		list_del(&pos->gl_list);
		LDLM_LOCK_RELEASE(pos->gl_lock);
		OBD_SLAB_FREE_PTR(pos, ldlm_glimpse_work_kmem);
	}

	RETURN(ELDLM_LOCK_ABORTED);
}


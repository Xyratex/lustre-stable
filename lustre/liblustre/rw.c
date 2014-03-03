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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.lustre.org/lustre/docs/GPLv2.pdf
 *
 * Please contact Xyratex Technology, Ltd., Langstone Road, Havant, Hampshire.
 * PO9 1SA, U.K. or visit www.xyratex.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2013, Xyratex Technology, Ltd . All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Some portions of Lustre® software are subject to copyrights help by Intel Corp.
 * Copyright (c) 2011-2013 Intel Corporation, Inc.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre® and the Lustre logo are registered trademarks of
 * Xyratex Technology, Ltd  in the United States and/or other countries.
 *
 * lustre/liblustre/rw.c
 *
 * Lustre Light block IO
 */

#define DEBUG_SUBSYSTEM S_LLITE

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <fcntl.h>
#include <sys/uio.h>

#include <sysio.h>
#ifdef HAVE_XTIO_H
#include <xtio.h>
#endif
#include <fs.h>
#include <mount.h>
#include <inode.h>
#ifdef HAVE_FILE_H
#include <file.h>
#endif

#undef LIST_HEAD

#include "llite_lib.h"

struct llu_io_group
{
        struct obd_io_group    *lig_oig;
        struct inode           *lig_inode;
        struct lustre_rw_params *lig_params;
        int                     lig_maxpages;
        int                     lig_npages;
        __u64                   lig_rwcount;
        struct ll_async_page   *lig_llaps;
        struct page            *lig_pages;
        void                   *lig_llap_cookies;
};

#define LLU_IO_GROUP_SIZE(x) \
        (sizeof(struct llu_io_group) + \
         (sizeof(struct ll_async_page) + \
          sizeof(struct page) + \
          llap_cookie_size) * (x))

struct llu_io_session
{
        struct inode           *lis_inode;
        int                     lis_cmd;
        int                     lis_max_groups;
        int                     lis_ngroups;
        struct llu_io_group    *lis_groups[0];
};
#define LLU_IO_SESSION_SIZE(x)  \
        (sizeof(struct llu_io_session) + (x) * 2 * sizeof(void *))


typedef ssize_t llu_file_piov_t(const struct iovec *iovec, int iovlen,
                                _SYSIO_OFF_T pos, ssize_t len,
                                void *private);

size_t llap_cookie_size;

static int llu_lock_to_stripe_offset(struct inode *inode, struct ldlm_lock *lock)
{
        struct llu_inode_info *lli = llu_i2info(inode);
        struct lov_stripe_md *lsm = lli->lli_smd;
        struct obd_export *exp = llu_i2obdexp(inode);
        struct {
                char name[16];
                struct ldlm_lock *lock;
        } key = { .name = KEY_LOCK_TO_STRIPE, .lock = lock };
        __u32 stripe, vallen = sizeof(stripe);
        int rc;
        ENTRY;

        if (lsm->lsm_stripe_count == 1)
                RETURN(0);

        /* get our offset in the lov */
        rc = obd_get_info(exp, sizeof(key), &key, &vallen, &stripe, lsm);
        if (rc != 0) {
                CERROR("obd_get_info: rc = %d\n", rc);
                LBUG();
        }
        LASSERT(stripe < lsm->lsm_stripe_count);
        RETURN(stripe);
}

int llu_extent_lock_cancel_cb(struct ldlm_lock *lock,
                                    struct ldlm_lock_desc *new, void *data,
                                    int flag)
{
        struct lustre_handle lockh = { 0 };
        int rc;
        ENTRY;

        if ((unsigned long)data > 0 && (unsigned long)data < 0x1000) {
                LDLM_ERROR(lock, "cancelling lock with bad data %p", data);
                LBUG();
        }

        switch (flag) {
        case LDLM_CB_BLOCKING:
                ldlm_lock2handle(lock, &lockh);
                rc = ldlm_cli_cancel(&lockh);
                if (rc != ELDLM_OK)
                        CERROR("ldlm_cli_cancel failed: %d\n", rc);
                break;
        case LDLM_CB_CANCELING: {
                struct inode *inode;
                struct llu_inode_info *lli;
                struct lov_stripe_md *lsm;
                __u32 stripe;
                __u64 kms;

                /* This lock wasn't granted, don't try to evict pages */
                if (lock->l_req_mode != lock->l_granted_mode)
                        RETURN(0);

                inode = llu_inode_from_resource(lock);
                if (!inode)
                        RETURN(0);
                lli= llu_i2info(inode);
                if (!lli)
                        goto iput;
                if (!lli->lli_smd)
                        goto iput;
                lsm = lli->lli_smd;

                stripe = llu_lock_to_stripe_offset(inode, lock);
                lock_res_and_lock(lock);
                kms = ldlm_extent_shift_kms(lock,
                                            lsm->lsm_oinfo[stripe]->loi_kms);
                unlock_res_and_lock(lock);
                if (lsm->lsm_oinfo[stripe]->loi_kms != kms)
                        LDLM_DEBUG(lock, "updating kms from "LPU64" to "LPU64,
                                   lsm->lsm_oinfo[stripe]->loi_kms, kms);
                lsm->lsm_oinfo[stripe]->loi_kms = kms;
iput:
                I_RELE(inode);
                break;
        }
        default:
                LBUG();
        }

        RETURN(0);
}

static int llu_glimpse_callback(struct ldlm_lock *lock, void *reqp)
{
        struct ptlrpc_request *req = reqp;
        struct inode *inode = llu_inode_from_resource(lock);
        struct llu_inode_info *lli;
        struct ost_lvb *lvb;
        __u32 size[2] = { sizeof(struct ptlrpc_body), sizeof(*lvb) };
        int rc, stripe = 0;
        ENTRY;

        if (inode == NULL)
                GOTO(out, rc = -ELDLM_NO_LOCK_DATA);
        lli = llu_i2info(inode);
        if (lli == NULL)
                GOTO(iput, rc = -ELDLM_NO_LOCK_DATA);
        if (lli->lli_smd == NULL)
                GOTO(iput, rc = -ELDLM_NO_LOCK_DATA);

        /* First, find out which stripe index this lock corresponds to. */
        if (lli->lli_smd->lsm_stripe_count > 1)
                stripe = llu_lock_to_stripe_offset(inode, lock);

        rc = lustre_pack_reply(req, 2, size, NULL);
        if (rc)
                GOTO(iput, rc);

        lvb = lustre_msg_buf(req->rq_repmsg, REPLY_REC_OFF, sizeof(*lvb));
        lvb->lvb_size = lli->lli_smd->lsm_oinfo[stripe]->loi_kms;

        LDLM_DEBUG(lock, "i_size: %llu -> stripe number %u -> kms "LPU64,
                   (long long)llu_i2stat(inode)->st_size, stripe,lvb->lvb_size);
 iput:
        I_RELE(inode);
 out:
        /* These errors are normal races, so we don't want to fill the console
         * with messages by calling ptlrpc_error() */
        if (rc == -ELDLM_NO_LOCK_DATA)
                lustre_pack_reply(req, 1, NULL, NULL);

        req->rq_status = rc;
        return rc;
}

/* NB: lov_merge_size will prefer locally cached writes if they extend the
 * file (because it prefers KMS over RSS when larger) */
int llu_glimpse_size(struct inode *inode)
{
        struct llu_inode_info *lli = llu_i2info(inode);
        struct intnl_stat *st = llu_i2stat(inode);
        struct llu_sb_info *sbi = llu_i2sbi(inode);
        struct lustre_handle lockh = { 0 };
        struct ldlm_enqueue_info einfo = { 0 };
        struct obd_info oinfo = { { { 0 } } };
        struct ost_lvb lvb;
        int rc;
        ENTRY;

        CDEBUG(D_DLMTRACE, "Glimpsing inode %llu\n", (long long)st->st_ino);

        if (!lli->lli_smd) {
                CDEBUG(D_DLMTRACE, "No objects for inode %llu\n", 
                       (long long)st->st_ino);
                RETURN(0);
        }

        einfo.ei_type = LDLM_EXTENT;
        einfo.ei_mode = LCK_PR;
        einfo.ei_cb_bl = osc_extent_blocking_cb;
        einfo.ei_cb_cp = ldlm_completion_ast;
        einfo.ei_cb_gl = llu_glimpse_callback;
        einfo.ei_cbdata = inode;

        oinfo.oi_policy.l_extent.end = OBD_OBJECT_EOF;
        oinfo.oi_lockh = &lockh;
        oinfo.oi_md = lli->lli_smd;
        oinfo.oi_flags = LDLM_FL_HAS_INTENT;

        rc = obd_enqueue_rqset(sbi->ll_osc_exp, &oinfo, &einfo);
        if (rc) {
                CERROR("obd_enqueue returned rc %d, returning -EIO\n", rc);
                RETURN(rc > 0 ? -EIO : rc);
        }

        lov_stripe_lock(lli->lli_smd);
        inode_init_lvb(inode, &lvb);
        /* merge timestamps the most recently obtained from mds with
           timestamps obtained from osts */
        lvb = lli->lli_lvb;
        rc = obd_merge_lvb(sbi->ll_osc_exp, lli->lli_smd, &lvb, 0);
        st->st_size = lvb.lvb_size;
        st->st_blocks = lvb.lvb_blocks;
        /* handle st_blocks overflow gracefully */
        if (st->st_blocks < lvb.lvb_blocks)
                st->st_blocks = ~0UL;
        st->st_mtime = lvb.lvb_mtime;
        st->st_atime = lvb.lvb_atime;
        st->st_ctime = lvb.lvb_ctime;
        lov_stripe_unlock(lli->lli_smd);

        CDEBUG(D_DLMTRACE, "glimpse: size: "LPU64", blocks: "LPU64"\n",
               (__u64)st->st_size, (__u64)st->st_blocks);

        RETURN(rc);
}

int llu_extent_lock(struct ll_file_data *fd, struct inode *inode,
                    struct lov_stripe_md *lsm, int mode,
                    ldlm_policy_data_t *policy, struct lustre_handle *lockh,
                    int ast_flags)
{
        struct llu_sb_info *sbi = llu_i2sbi(inode);
        struct intnl_stat *st = llu_i2stat(inode);
        struct ldlm_enqueue_info einfo = { 0 };
        struct obd_info oinfo = { { { 0 } } };
        struct ost_lvb lvb;
        int rc;
        ENTRY;

        LASSERT(!lustre_handle_is_used(lockh));
        CLASSERT(ELDLM_OK == 0);

        /* XXX phil: can we do this?  won't it screw the file size up? */
        if ((fd && (fd->fd_flags & LL_FILE_IGNORE_LOCK)) ||
            (sbi->ll_flags & LL_SBI_NOLCK) || mode == LCK_NL)
                RETURN(0);

        CDEBUG(D_DLMTRACE, "Locking inode %llu, start "LPU64" end "LPU64"\n",
               (long long)st->st_ino, policy->l_extent.start,
               policy->l_extent.end);

        einfo.ei_type = LDLM_EXTENT;
        einfo.ei_mode = mode;
        einfo.ei_cb_bl = osc_extent_blocking_cb;
        einfo.ei_cb_cp = ldlm_completion_ast;
        einfo.ei_cb_gl = llu_glimpse_callback;
        einfo.ei_cbdata = inode;

        oinfo.oi_policy = *policy;
        oinfo.oi_lockh = lockh;
        oinfo.oi_md = lsm;
        oinfo.oi_flags = ast_flags;

        rc = obd_enqueue(sbi->ll_osc_exp, &oinfo, &einfo, NULL);
        *policy = oinfo.oi_policy;
        if (rc > 0)
                rc = -EIO;

        inode_init_lvb(inode, &lvb);
        obd_merge_lvb(sbi->ll_osc_exp, lsm, &lvb, 1);
        if (policy->l_extent.start == 0 &&
            policy->l_extent.end == OBD_OBJECT_EOF)
                st->st_size = lvb.lvb_size;

        if (rc == 0) {
                st->st_mtime = lvb.lvb_mtime;
                st->st_atime = lvb.lvb_atime;
                st->st_ctime = lvb.lvb_ctime;
        }

        RETURN(rc);
}

int llu_extent_unlock(struct ll_file_data *fd, struct inode *inode,
                struct lov_stripe_md *lsm, int mode,
                struct lustre_handle *lockh)
{
        struct llu_sb_info *sbi = llu_i2sbi(inode);
        int rc;
        ENTRY;

        CLASSERT(ELDLM_OK == 0);

        /* XXX phil: can we do this?  won't it screw the file size up? */
        if ((fd && (fd->fd_flags & LL_FILE_IGNORE_LOCK)) ||
            (sbi->ll_flags & LL_SBI_NOLCK) || mode == LCK_NL)
                RETURN(0);

        rc = obd_cancel(sbi->ll_osc_exp, lsm, mode, lockh, 0, 0);

        RETURN(rc);
}

#define LLAP_MAGIC 12346789

struct ll_async_page {
        int             llap_magic;
        void           *llap_cookie;
        int             llap_queued;
        struct page    *llap_page;
        struct inode   *llap_inode;
};

static void llu_ap_fill_obdo(void *data, int cmd, struct obdo *oa)
{
        struct ll_async_page *llap;
        struct inode *inode;
        struct lov_stripe_md *lsm;
        obd_flag valid_flags;
        ENTRY;

        llap = LLAP_FROM_COOKIE(data);
        inode = llap->llap_inode;
        lsm = llu_i2info(inode)->lli_smd;

        oa->o_id = lsm->lsm_object_id;
        oa->o_valid = OBD_MD_FLID;
        valid_flags = OBD_MD_FLTYPE | OBD_MD_FLATIME;
        if (cmd & OBD_BRW_WRITE)
                valid_flags |= OBD_MD_FLMTIME | OBD_MD_FLCTIME |
                        OBD_MD_FLUID | OBD_MD_FLGID |
                        OBD_MD_FLFID | OBD_MD_FLGENER;

        obdo_from_inode(oa, inode, valid_flags);
        EXIT;
}

static void llu_ap_update_obdo(void *data, int cmd, struct obdo *oa,
                               obd_valid valid)
{
        struct ll_async_page *llap;
        ENTRY;

        llap = LLAP_FROM_COOKIE(data);
        obdo_from_inode(oa, llap->llap_inode, valid);

        EXIT;
}

/* called for each page in a completed rpc.*/
static int llu_ap_completion(void *data, int cmd, struct obdo *oa, int rc)
{
        struct ll_async_page *llap;
        struct page *page;
        ENTRY;

        llap = LLAP_FROM_COOKIE(data);
        llap->llap_queued = 0;
        page = llap->llap_page;

        if (rc != 0) {
                if (cmd & OBD_BRW_WRITE)
                        CERROR("writeback error on page %p index %ld: %d\n",
                               page, page->index, rc);
        }
        RETURN(0);
}

static struct obd_async_page_ops llu_async_page_ops = {
        .ap_make_ready =        NULL,
        .ap_refresh_count =     NULL,
        .ap_fill_obdo =         llu_ap_fill_obdo,
        .ap_update_obdo =       llu_ap_update_obdo,
        .ap_completion =        llu_ap_completion,
};

static int llu_queue_pio(int cmd, struct llu_io_group *group,
                         char *buf, size_t count, loff_t pos)
{
        struct llu_inode_info *lli = llu_i2info(group->lig_inode);
        struct intnl_stat *st = llu_i2stat(group->lig_inode);
        struct lov_stripe_md *lsm = lli->lli_smd;
        struct obd_export *exp = llu_i2obdexp(group->lig_inode);
        struct page *pages = &group->lig_pages[group->lig_npages],*page = pages;
        struct ll_async_page *llap = &group->lig_llaps[group->lig_npages];
        void *llap_cookie = group->lig_llap_cookies +
                llap_cookie_size * group->lig_npages;
        int i, rc, npages = 0, ret_bytes = 0;
        int local_lock;
        ENTRY;

        if (!exp)
                RETURN(-EINVAL);

        local_lock = group->lig_params->lrp_lock_mode != LCK_NL;
        /* prepare the pages array */
	do {
                unsigned long index, offset, bytes;

                offset = (pos & ~CFS_PAGE_MASK);
                index = pos >> CFS_PAGE_SHIFT;
                bytes = CFS_PAGE_SIZE - offset;
                if (bytes > count)
                        bytes = count;

                /* prevent read beyond file range */
                if (/* local_lock && */
                    cmd == OBD_BRW_READ && pos + bytes >= st->st_size) {
                        if (pos >= st->st_size)
                                break;
                        bytes = st->st_size - pos;
                }

                /* prepare page for this index */
                page->index = index;
                page->addr = buf - offset;

                page->_offset = offset;
                page->_count = bytes;

                page++;
                npages++;
                count -= bytes;
                pos += bytes;
                buf += bytes;

                group->lig_rwcount += bytes;
                ret_bytes += bytes;
        } while (count);

        group->lig_npages += npages;

        for (i = 0, page = pages; i < npages;
             i++, page++, llap++, llap_cookie += llap_cookie_size){
                llap->llap_magic = LLAP_MAGIC;
                llap->llap_cookie = llap_cookie;
                rc = obd_prep_async_page(exp, lsm, NULL, page,
                                         (obd_off)page->index << CFS_PAGE_SHIFT,
                                         &llu_async_page_ops,
                                         llap, &llap->llap_cookie,
                                         /* no cache in liblustre at all */
                                         OBD_PAGE_NO_CACHE,
                                         NULL);
                if (rc) {
                        LASSERT(rc < 0);
                        llap->llap_cookie = NULL;
                        RETURN(rc);
                }
                CDEBUG(D_CACHE, "llap %p page %p group %p obj off "LPU64"\n",
                       llap, page, llap->llap_cookie,
                       (obd_off)pages->index << CFS_PAGE_SHIFT);
                page->private = (unsigned long)llap;
                llap->llap_page = page;
                llap->llap_inode = group->lig_inode;

                rc = obd_queue_group_io(exp, lsm, NULL, group->lig_oig,
                                        llap->llap_cookie, cmd,
                                        page->_offset, page->_count,
                                        group->lig_params->lrp_brw_flags,
                                        ASYNC_READY | ASYNC_URGENT |
                                        ASYNC_COUNT_STABLE | ASYNC_GROUP_SYNC);
                if (!local_lock && cmd == OBD_BRW_READ) {
                        /*
                         * In OST-side locking case short reads cannot be
                         * detected properly.
                         *
                         * The root of the problem is that
                         *
                         * kms = lov_merge_size(lsm, 1);
                         * if (end >= kms)
                         *         glimpse_size(inode);
                         * else
                         *         st->st_size = kms;
                         *
                         * logic in the read code (both llite and liblustre)
                         * only works correctly when client holds DLM lock on
                         * [start, end]. Without DLM lock KMS can be
                         * completely out of date, and client can either make
                         * spurious short-read (missing concurrent write), or
                         * return stale data (missing concurrent
                         * truncate). For llite client this is fatal, because
                         * incorrect data are cached and can be later sent
                         * back to the server (vide bug 5047). This is hard to
                         * fix by handling short-reads on the server, as there
                         * is no easy way to communicate file size (or amount
                         * of bytes read/written) back to the client,
                         * _especially_ because OSC pages can be sliced and
                         * dices into multiple RPCs arbitrary. Fortunately,
                         * liblustre doesn't cache data and the worst case is
                         * that we get race with concurrent write or truncate.
                         */
                }
                if (rc) {
                        LASSERT(rc < 0);
                        RETURN(rc);
                }

                llap->llap_queued = 1;
        }

        RETURN(ret_bytes);
}

static
struct llu_io_group * get_io_group(struct inode *inode, int maxpages,
                                   struct lustre_rw_params *params)
{
        struct llu_io_group *group;
        int rc;

        if (!llap_cookie_size)
                llap_cookie_size = obd_prep_async_page(llu_i2obdexp(inode),
                                                       NULL, NULL, NULL, 0,
                                                       NULL, NULL, NULL, 0,
                                                       NULL);

        OBD_ALLOC(group, LLU_IO_GROUP_SIZE(maxpages));
        if (!group)
                return ERR_PTR(-ENOMEM);

        I_REF(inode);
        group->lig_inode = inode;
        group->lig_maxpages = maxpages;
        group->lig_params = params;
        group->lig_llaps = (struct ll_async_page *)(group + 1);
        group->lig_pages = (struct page *)(&group->lig_llaps[maxpages]);
        group->lig_llap_cookies = (void *)(&group->lig_pages[maxpages]);

        rc = oig_init(&group->lig_oig);
        if (rc) {
                OBD_FREE(group, LLU_IO_GROUP_SIZE(maxpages));
                return ERR_PTR(rc);
        }

        return group;
}

static int max_io_pages(ssize_t len, int iovlen)
{
        return (((len + CFS_PAGE_SIZE -1) >> CFS_PAGE_SHIFT) + 2 + iovlen - 1);
}

static
void put_io_group(struct llu_io_group *group)
{
        struct lov_stripe_md *lsm = llu_i2info(group->lig_inode)->lli_smd;
        struct obd_export *exp = llu_i2obdexp(group->lig_inode);
        struct ll_async_page *llap = group->lig_llaps;
        int i;

        for (i = 0; i < group->lig_npages; i++, llap++) {
                if (llap->llap_cookie)
                        obd_teardown_async_page(exp, lsm, NULL,
                                                llap->llap_cookie);
        }

        I_RELE(group->lig_inode);

        oig_release(group->lig_oig);
        OBD_FREE(group, LLU_IO_GROUP_SIZE(group->lig_maxpages));
}

static
ssize_t llu_file_prwv(const struct iovec *iovec, int iovlen,
                        _SYSIO_OFF_T pos, ssize_t len,
                        void *private)
{
        struct llu_io_session *session = (struct llu_io_session *) private;
        struct inode *inode = session->lis_inode;
        struct llu_inode_info *lli = llu_i2info(inode);
        struct intnl_stat *st = llu_i2stat(inode);
        struct ll_file_data *fd = lli->lli_file_data;
        struct lustre_handle lockh = {0};
        struct lov_stripe_md *lsm = lli->lli_smd;
        struct obd_export *exp = NULL;
        struct llu_io_group *iogroup;
        struct lustre_rw_params p;
        struct ost_lvb lvb;
        __u64 kms;
        int err, is_read, iovidx, ret;
        int local_lock;
        ssize_t ret_len = len;
        ENTRY;

        /* in a large iov read/write we'll be repeatedly called.
         * so give a chance to answer cancel ast here
         */
        liblustre_wait_event(0);

        exp = llu_i2obdexp(inode);
        if (exp == NULL)
                RETURN(-EINVAL);

        if (len == 0 || iovlen == 0)
                RETURN(0);

        if (pos + len > lli->lli_maxbytes)
                RETURN(-ERANGE);

        lustre_build_lock_params(session->lis_cmd, lli->lli_open_flags,
                                 lli->lli_sbi->ll_lco.lco_flags,
                                 pos, len, &p);

        iogroup = get_io_group(inode, max_io_pages(len, iovlen), &p);
        if (IS_ERR(iogroup))
                RETURN(PTR_ERR(iogroup));

        local_lock = p.lrp_lock_mode != LCK_NL;

        err = llu_extent_lock(fd, inode, lsm, p.lrp_lock_mode, &p.lrp_policy,
                              &lockh, p.lrp_ast_flags);
        if (err != ELDLM_OK)
                GOTO(err_put, err);

        is_read = (session->lis_cmd == OBD_BRW_READ);
        if (is_read) {
                /*
                 * If OST-side locking is used, KMS can be completely out of
                 * date, and, hence, cannot be used for short-read
                 * detection. Rely in OST to handle short reads in that case.
                 */
                inode_init_lvb(inode, &lvb);
                obd_merge_lvb(exp, lsm, &lvb, 1);
                kms = lvb.lvb_size;
                /* extent.end is last byte of the range */
                if (p.lrp_policy.l_extent.end >= kms) {
                        /* A glimpse is necessary to determine whether
                         * we return a short read or some zeroes at
                         * the end of the buffer
                         *
                         * In the case of OST-side locking KMS can be
                         * completely out of date and short-reads maybe
                         * mishandled. See llu_queue_pio() for more detailed
                         * comment.
                         */
                        if ((err = llu_glimpse_size(inode))) {
                                GOTO(err_unlock, err);
                        } else {
                                /* If objective page index exceed end-of-file
                                 * page index, return directly. --bug 17336 */
                                loff_t size = st->st_size;
                                unsigned long cur_index = pos >> CFS_PAGE_SHIFT;

                                if ((size == 0 && cur_index != 0) ||
                                    (((size - 1) >> CFS_PAGE_SHIFT) < cur_index))
                                        GOTO(err_unlock, err);
                        }
                } else {
                        st->st_size = kms;
                }
        } else if (lli->lli_open_flags & O_APPEND) {
                pos = st->st_size;
        }

        if (local_lock) {
                struct ost_lvb xtimes;

                lov_stripe_lock(lsm);
                /* inode might mtime and ctime set earlier in race with stat
                 * which merged into inode timestamps obtained from mds and
                 * osts */
                st->st_atime = st->st_mtime = st->st_ctime = CURRENT_TIME;
                xtimes.lvb_atime = st->st_atime;
                xtimes.lvb_mtime = st->st_mtime;
                xtimes.lvb_ctime = st->st_ctime;
                obd_update_lvb(exp, lsm, &xtimes,
                               is_read ? OBD_MD_FLATIME :
                               (OBD_MD_FLMTIME | OBD_MD_FLCTIME));
                lov_stripe_unlock(lsm);
        }

        for (iovidx = 0; iovidx < iovlen; iovidx++) {
                char *buf = (char *) iovec[iovidx].iov_base;
                size_t count = iovec[iovidx].iov_len;

                if (!count)
                        continue;
                if (len < count)
                        count = len;
                if (IS_BAD_PTR(buf) || IS_BAD_PTR(buf + count)) {
                        GOTO(err_unlock, err = -EFAULT);
                }

                if (is_read) {
                        if (/* local_lock && */ pos >= st->st_size)
                                break;
                } else {
                        if (pos >= lli->lli_maxbytes) {
                                GOTO(err_unlock, err = -EFBIG);
                        }
                        if (pos + count >= lli->lli_maxbytes)
                                count = lli->lli_maxbytes - pos;
                }

                ret = llu_queue_pio(session->lis_cmd, iogroup, buf, count, pos);
                if (ret < 0) {
                        GOTO(err_unlock, err = ret);
                } else {
                        pos += ret;
                        if (!is_read) {
                                LASSERT(ret == count);
                                obd_adjust_kms(exp, lsm, pos, 0);
                                /* file size grow immediately */
                                if (pos > st->st_size)
                                        st->st_size = pos;
                        }
                        len -= ret;
                        if (!len)
                                break;
                }
        }
        LASSERT(len == 0 || is_read); /* libsysio should guarantee this */

        err = obd_trigger_group_io(exp, lsm, NULL, iogroup->lig_oig);
        if (err)
                GOTO(err_unlock, err);

        err = oig_wait(iogroup->lig_oig);
        if (err) {
                CERROR("%s error: %s\n", is_read ? "read" : "write", strerror(-err));
                GOTO(err_unlock, err);
        }

        ret = llu_extent_unlock(fd, inode, lsm, p.lrp_lock_mode, &lockh);
        if (ret)
                CERROR("extent unlock error %d\n", ret);

        session->lis_groups[session->lis_ngroups++] = iogroup;
        RETURN(ret_len);

err_unlock:
        llu_extent_unlock(fd, inode, lsm, p.lrp_lock_mode, &lockh);
err_put:
        put_io_group(iogroup);
        RETURN((ssize_t)err);
}

static
struct llu_io_session *get_io_session(struct inode *ino, int ngroups, int cmd)
{
        struct llu_io_session *session;

        OBD_ALLOC(session, LLU_IO_SESSION_SIZE(ngroups));
        if (!session)
                return NULL;

        I_REF(ino);
        session->lis_inode = ino;
        session->lis_max_groups = ngroups;
        session->lis_cmd = cmd;
        return session;
}

static void put_io_session(struct llu_io_session *session)
{
        int i;

        for (i = 0; i < session->lis_ngroups; i++) {
                if (session->lis_groups[i]) {
                        put_io_group(session->lis_groups[i]);
                        session->lis_groups[i] = NULL;
                }
        }

        I_RELE(session->lis_inode);
        OBD_FREE(session, LLU_IO_SESSION_SIZE(session->lis_max_groups));
}

static int llu_file_rwx(struct inode *ino,
                        struct ioctx *ioctx,
                        int read)
{
        struct llu_io_session *session;
        ssize_t cc;
        int cmd = read ? OBD_BRW_READ : OBD_BRW_WRITE;
        ENTRY;

        LASSERT(ioctx->ioctx_xtvlen >= 0);
        LASSERT(ioctx->ioctx_iovlen >= 0);

        liblustre_wait_event(0);

        if (!ioctx->ioctx_xtvlen)
                RETURN(0);

        /* XXX consider other types later */
        if (S_ISDIR(llu_i2stat(ino)->st_mode))
                RETURN(-EISDIR);
        if (!S_ISREG(llu_i2stat(ino)->st_mode))
                RETURN(-EOPNOTSUPP);

        session = get_io_session(ino, ioctx->ioctx_xtvlen * 2, cmd);
        if (!session)
                RETURN(-ENOMEM);

        cc = _sysio_enumerate_extents(ioctx->ioctx_xtv, ioctx->ioctx_xtvlen,
                                      ioctx->ioctx_iov, ioctx->ioctx_iovlen,
                                      llu_file_prwv, session);

        if (cc >= 0) {
                LASSERT(!ioctx->ioctx_cc);
                ioctx->ioctx_private = session;
                cc = 0;
        } else {
                put_io_session(session);
        }

        liblustre_wait_event(0);
        RETURN(cc);
}

int llu_iop_read(struct inode *ino,
                 struct ioctx *ioctx)
{
        /* BUG: 5972 */
        struct intnl_stat *st = llu_i2stat(ino);
        st->st_atime = CURRENT_TIME;

        return llu_file_rwx(ino, ioctx, 1);
}

int llu_iop_write(struct inode *ino,
                  struct ioctx *ioctx)
{
        struct intnl_stat *st = llu_i2stat(ino);
        st->st_mtime = st->st_ctime = CURRENT_TIME;

        return llu_file_rwx(ino, ioctx, 0);
}

int llu_iop_iodone(struct ioctx *ioctx)
{
        struct llu_io_session *session;
        struct llu_io_group *group;
        int i, err = 0, rc = 0;
        ENTRY;

        liblustre_wait_event(0);

        session = (struct llu_io_session *) ioctx->ioctx_private;
        LASSERT(session);
        LASSERT(!IS_ERR(session));

        for (i = 0; i < session->lis_ngroups; i++) {
                group = session->lis_groups[i];
                if (group) {
                        if (!rc) {
                                err = oig_wait(group->lig_oig);
                                if (err)
                                        rc = err;
                        }
                        if (!rc)
                                ioctx->ioctx_cc += group->lig_rwcount;
                        put_io_group(group);
                        session->lis_groups[i] = NULL;
                }
        }

        if (rc) {
                LASSERT(rc < 0);
                ioctx->ioctx_cc = -1;
                ioctx->ioctx_errno = -rc;
        }

        put_io_session(session);
        ioctx->ioctx_private = NULL;
        liblustre_wait_event(0);

        RETURN(1);
}

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
 * lustre/liblustre/namei.c
 *
 * Lustre Light name resolution
 */

#define DEBUG_SUBSYSTEM S_LLITE

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/queue.h>

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

void ll_intent_drop_lock(struct lookup_intent *it)
{
        struct lustre_handle *handle;

        if (it->it_op && it->d.lustre.it_lock_mode) {
                handle = (struct lustre_handle *)&it->d.lustre.it_lock_handle;
                CDEBUG(D_DLMTRACE, "releasing lock with cookie "LPX64
                       " from it %p\n", handle->cookie, it);
                ldlm_lock_decref(handle, it->d.lustre.it_lock_mode);

                /* bug 494: intent_release may be called multiple times, from
                 * this thread and we don't want to double-decref this lock */
                it->d.lustre.it_lock_mode = 0;
        }
}

void ll_intent_release(struct lookup_intent *it)
{
        ENTRY;

        ll_intent_drop_lock(it);
        it->it_magic = 0;
        it->it_op_release = 0;
        it->d.lustre.it_disposition = 0;
        it->d.lustre.it_data = NULL;
        EXIT;
}

#if 0
/*
 * remove the stale inode from pnode
 */
void unhook_stale_inode(struct pnode *pno)
{
        struct inode *inode = pno->p_base->pb_ino;
        ENTRY;

        LASSERT(inode);
        LASSERT(llu_i2info(inode)->lli_stale_flag);

        pno->p_base->pb_ino = NULL;
        I_RELE(inode);

        if (!llu_i2info(inode)->lli_open_count) {
                CDEBUG(D_INODE, "unhook inode %p (ino %lu) from pno %p\n",
                                inode, llu_i2info(inode)->lli_st_ino, pno);
                if (!inode->i_ref)
                        _sysio_i_gone(inode);
        }

        EXIT;
        return;
}
#endif

void llu_lookup_finish_locks(struct lookup_intent *it, struct pnode *pnode)
{
        struct inode *inode;
        LASSERT(it);
        LASSERT(pnode);

        inode = pnode->p_base->pb_ino;
        if (it->d.lustre.it_lock_mode && inode != NULL) {
                CDEBUG(D_DLMTRACE, "setting l_data to inode %p (%llu/%lu)\n",
                       inode, (long long)llu_i2stat(inode)->st_ino,
                       llu_i2info(inode)->lli_st_generation);
                mdc_set_lock_data(&it->d.lustre.it_lock_handle, inode, NULL);
        }

        /* drop lookup/getattr locks */
        if (it->it_op == IT_LOOKUP || it->it_op == IT_GETATTR)
                ll_intent_release(it);

}

static inline void llu_invalidate_inode_pages(struct inode * inode)
{
        /* do nothing */
}

int llu_mdc_blocking_ast(struct ldlm_lock *lock,
                         struct ldlm_lock_desc *desc,
                         void *data, int flag)
{
        int rc;
        struct lustre_handle lockh;
        ENTRY;


        switch (flag) {
        case LDLM_CB_BLOCKING:
                ldlm_lock2handle(lock, &lockh);
                rc = ldlm_cli_cancel(&lockh);
                if (rc < 0) {
                        CDEBUG(D_INODE, "ldlm_cli_cancel: %d\n", rc);
                        RETURN(rc);
                }
                break;
        case LDLM_CB_CANCELING: {
                struct inode *inode = llu_inode_from_resource(lock);
                struct llu_inode_info *lli;
                struct intnl_stat *st;
                __u64 bits = lock->l_policy_data.l_inodebits.bits;

                 /* Inode is set to lock->l_resource->lr_lvb_inode
                  * for mdc - bug 24555 */
                LASSERT(lock->l_ast_data == NULL);

                /* Invalidate all dentries associated with this inode */
                if (inode == NULL)
                        break;

                lli =  llu_i2info(inode);
                st = llu_i2stat(inode);

                if (bits & MDS_INODELOCK_UPDATE)
                        clear_bit(LLI_F_HAVE_MDS_SIZE_LOCK, &lli->lli_flags);

                if (lock->l_resource->lr_name.name[0] != st->st_ino ||
                    lock->l_resource->lr_name.name[1] !=lli->lli_st_generation){
                        LDLM_ERROR(lock, "data mismatch with ino %llu/%lu",
                                  (long long)st->st_ino,lli->lli_st_generation);
                }
                if (S_ISDIR(st->st_mode) &&
                    (bits & MDS_INODELOCK_UPDATE)) {
                        CDEBUG(D_INODE, "invalidating inode %llu\n",
                               (long long)st->st_ino);

                        llu_invalidate_inode_pages(inode);
                }

/*
                if (inode->i_sb->s_root &&
                    inode != inode->i_sb->s_root->d_inode)
                        ll_unhash_aliases(inode);
*/
                I_RELE(inode);
                break;
        }
        default:
                LBUG();
        }

        RETURN(0);
}

static int pnode_revalidate_finish(struct ptlrpc_request *req,
                                   int offset,
                                   struct lookup_intent *it,
                                   struct pnode *pnode)
{
        struct inode *inode = pnode->p_base->pb_ino;
        struct lustre_md md;
        int rc = 0;
        ENTRY;

        LASSERT(inode);

        if (!req)
                RETURN(0);

        if (it_disposition(it, DISP_LOOKUP_NEG))
                RETURN(-ENOENT);

        rc = mdc_req2lustre_md(req, offset, llu_i2sbi(inode)->ll_osc_exp, &md);
        if (rc)
                RETURN(rc);

        llu_update_inode(inode, md.body, md.lsm);

        RETURN(rc);
}

static int llu_pb_revalidate(struct pnode *pnode, int flags,
                             struct lookup_intent *it)
{
        struct pnode_base *pb = pnode->p_base;
        struct it_cb_data icbd;
        struct mdc_op_data op_data;
        struct ptlrpc_request *req = NULL;
        struct lookup_intent lookup_it = { .it_op = IT_LOOKUP };
        struct obd_export *exp;
        int rc;
        ENTRY;

        CDEBUG(D_VFSTRACE, "VFS Op:name=%.*s,intent=%x\n",
               (int)pb->pb_name.len, pb->pb_name.name, it ? it->it_op : 0);

        /* We don't want to cache negative dentries, so return 0 immediately.
         * We believe that this is safe, that negative dentries cannot be
         * pinned by someone else */
        if (pb->pb_ino == NULL) {
                CDEBUG(D_INODE, "negative pb\n");
                RETURN(0);
        }

        /* This is due to bad interaction with libsysio. remove this when we
         * switched to libbsdio XXX
         */
        {
                struct llu_inode_info *lli = llu_i2info(pb->pb_ino);
                struct intnl_stat *st = llu_i2stat(pb->pb_ino);
                if (lli->lli_it) {
                        CDEBUG(D_INODE, "inode %llu still have intent "
                                        "%p(opc 0x%x), release it\n",
                                        (long long) st->st_ino, lli->lli_it,
                                        lli->lli_it->it_op);
                        ll_intent_release(lli->lli_it);
                        OBD_FREE(lli->lli_it, sizeof(*lli->lli_it));
                        lli->lli_it = NULL;
                }
        }

        exp = llu_i2mdcexp(pb->pb_ino);
        icbd.icbd_parent = pnode->p_parent->p_base->pb_ino;
        icbd.icbd_child = pnode;

        if (!it) {
                it = &lookup_it;
                it->it_op_release = ll_intent_release;
        }

        llu_prepare_mdc_op_data(&op_data, pnode->p_parent->p_base->pb_ino,
                                pb->pb_ino, pb->pb_name.name,pb->pb_name.len,0);

        rc = mdc_intent_lock(exp, &op_data, NULL, 0, it, flags,
                             &req, llu_mdc_blocking_ast,
                             LDLM_FL_CANCEL_ON_BLOCK);
        /* If req is NULL, then mdc_intent_lock only tried to do a lock match;
         * if all was well, it will return 1 if it found locks, 0 otherwise. */
        if (req == NULL && rc >= 0)
                GOTO(out, rc);

        if (rc < 0)
                GOTO(out, rc = 0);

        rc = pnode_revalidate_finish(req, DLM_REPLY_REC_OFF, it, pnode);
        if (rc != 0) {
                ll_intent_release(it);
                GOTO(out, rc = 0);
        }
        rc = 1;

        /* Note: ll_intent_lock may cause a callback, check this! */

        if (it->it_op & IT_OPEN)
                LL_SAVE_INTENT(pb->pb_ino, it);

 out:
        if (req && rc == 1)
                ptlrpc_req_finished(req);
        if (rc == 0) {
                LASSERT(pb->pb_ino);
                I_RELE(pb->pb_ino);
                pb->pb_ino = NULL;
        } else {
                llu_lookup_finish_locks(it, pnode);
        }
        RETURN(rc);
}

static int lookup_it_finish(struct ptlrpc_request *request, int offset,
                            struct lookup_intent *it, void *data)
{
        struct it_cb_data *icbd = data;
        struct pnode *child = icbd->icbd_child;
        struct inode *parent = icbd->icbd_parent;
        struct llu_sb_info *sbi = llu_i2sbi(parent);
        struct inode *inode = NULL;
        int rc;

        /* libsysio require us generate inode right away if success.
         * so if mds created new inode for us we need make sure it
         * succeeded. thus for any error we can't delay to the
         * llu_file_open() time. */
        if (it_disposition(it, DISP_OPEN_CREATE) &&
            it_open_error(DISP_OPEN_CREATE, it)) {
                CDEBUG(D_INODE, "detect mds create error\n");
                return it_open_error(DISP_OPEN_CREATE, it);
        }
        if (it_disposition(it, DISP_OPEN_OPEN) &&
            it_open_error(DISP_OPEN_OPEN, it)) {
                CDEBUG(D_INODE, "detect mds open error\n");
                /* undo which did by mdc_intent_lock */
                if (it_disposition(it, DISP_OPEN_CREATE) &&
                    !it_open_error(DISP_OPEN_CREATE, it)) {
                        LASSERT(request);
                        LASSERT(atomic_read(&request->rq_refcount) > 1);
                        CDEBUG(D_INODE, "dec a ref of req %p\n", request);
                        ptlrpc_req_finished(request);
                }
                return it_open_error(DISP_OPEN_OPEN, it);
        }

        /* NB 1 request reference will be taken away by ll_intent_lock()
         * when I return
         */
        if (!it_disposition(it, DISP_LOOKUP_NEG) || (it->it_op & IT_CREAT)) {
                struct lustre_md md;
                struct llu_inode_info *lli;
                struct intnl_stat *st;
                ENTRY;

                if (it_disposition(it, DISP_OPEN_CREATE))
                        ptlrpc_req_finished(request);

                rc = mdc_req2lustre_md(request, offset, sbi->ll_osc_exp, &md);
                if (rc)
                        RETURN(rc);

                inode = llu_iget(parent->i_fs, &md);
                if (!inode || IS_ERR(inode)) {
                        /* free the lsm if we allocated one above */
                        if (md.lsm != NULL)
                                obd_free_memmd(sbi->ll_osc_exp, &md.lsm);
                        RETURN(inode ? PTR_ERR(inode) : -ENOMEM);
                } else if (md.lsm != NULL &&
                           llu_i2info(inode)->lli_smd != md.lsm) {
                        obd_free_memmd(sbi->ll_osc_exp, &md.lsm);
                }

                lli = llu_i2info(inode);
                st = llu_i2stat(inode);

                /* If this is a stat, get the authoritative file size */
                if (it->it_op == IT_GETATTR && S_ISREG(st->st_mode) &&
                    lli->lli_smd != NULL) {
                        struct lov_stripe_md *lsm = lli->lli_smd;
                        ldlm_error_t rc;

                        LASSERT(lsm->lsm_object_id != 0);

                        /* bug 2334: drop MDS lock before acquiring OST lock */
                        ll_intent_drop_lock(it);

                        rc = llu_glimpse_size(inode);
                        if (rc) {
                                I_RELE(inode);
                                RETURN(rc);
                        }
                }
        } else {
                ENTRY;
        }

        /* intent will be further used in cases of open()/getattr() */
        if (inode && (it->it_op & IT_OPEN))
                LL_SAVE_INTENT(inode, it);

        child->p_base->pb_ino = inode;

        RETURN(0);
}

struct inode *llu_inode_from_resource(struct ldlm_lock *lock)
{
        struct inode *inode;
        lock_res_and_lock(lock);

        if (lock->l_resource->lr_lvb_inode) {
                inode = (struct inode *)lock->l_resource->lr_lvb_inode;
                I_REF(inode);
        } else
                inode = NULL;

        unlock_res_and_lock(lock);
        return inode;
}

struct inode *llu_inode_from_lock(struct ldlm_lock *lock)
{
        struct inode *inode;
        lock_res_and_lock(lock);

        if (lock->l_ast_data) {
                inode = (struct inode *)lock->l_ast_data;
                I_REF(inode);
        } else
                inode = NULL;

        unlock_res_and_lock(lock);
        return inode;
}

static int llu_lookup_it(struct inode *parent, struct pnode *pnode,
                         struct lookup_intent *it, int flags)
{
        struct mdc_op_data op_data;
        struct it_cb_data icbd;
        struct ptlrpc_request *req = NULL;
        struct lookup_intent lookup_it = { .it_op = IT_LOOKUP };
        int rc;
        ENTRY;

        if (pnode->p_base->pb_name.len > EXT2_NAME_LEN)
                RETURN(-ENAMETOOLONG);

        if (!it) {
                it = &lookup_it;
                it->it_op_release = ll_intent_release;
        }

        icbd.icbd_child = pnode;
        icbd.icbd_parent = parent;

        llu_prepare_mdc_op_data(&op_data, parent, NULL,
                                pnode->p_base->pb_name.name,
                                pnode->p_base->pb_name.len, flags);

        rc = mdc_intent_lock(llu_i2mdcexp(parent), &op_data, NULL, 0, it,
                             flags, &req, llu_mdc_blocking_ast,
                             LDLM_FL_CANCEL_ON_BLOCK);
        if (rc < 0)
                GOTO(out, rc);

        rc = lookup_it_finish(req, DLM_REPLY_REC_OFF, it, &icbd);
        if (rc != 0) {
                ll_intent_release(it);
                GOTO(out, rc);
        }

        llu_lookup_finish_locks(it, pnode);

 out:
        if (req)
                ptlrpc_req_finished(req);
        return rc;
}

static struct lookup_intent*
translate_lookup_intent(struct intent *intent, const char *path)
{
        struct lookup_intent *it;
        int fmode;

        /* libsysio trick */
        if (!intent || path) {
                CDEBUG(D_VFSTRACE, "not intent needed\n");
                return NULL;
        }

        OBD_ALLOC(it, sizeof(*it));
        LASSERT(it);

        memset(it, 0, sizeof(*it));

        /* libsysio will assign intent like following:
         * NOTE: INT_CREAT has include INT_UPDPARENT
         *
         * open: INT_OPEN [| INT_CREAT]
         * mkdir: INT_CREAT
         * symlink: INT_CREAT
         * unlink: INT_UPDPARENT
         * rmdir: INT_UPDPARENT
         * mknod: INT_CREAT
         * stat: INT_GETATTR
         * setattr: NULL
         *
         * following logic is adjusted for libsysio
         */

        it->it_flags = intent->int_arg2 ? *((int*)intent->int_arg2) : 0;

        if (intent->int_opmask & INT_OPEN) {
                it->it_op |= IT_OPEN;

                /* convert access mode from O_ to FMODE_ */
                if (it->it_flags & O_WRONLY)
                        fmode = FMODE_WRITE;
                else if (it->it_flags & O_RDWR)
                        fmode = FMODE_READ | FMODE_WRITE;
                else
                        fmode = FMODE_READ;
                it->it_flags &= ~O_ACCMODE;
                it->it_flags |= fmode;
        }

        /* XXX libsysio has strange code on intent handling,
         * more check later */
        if (it->it_flags & O_CREAT) {
                it->it_op |= IT_CREAT;
                it->it_create_mode = *((int*)intent->int_arg1);
                /* bug 7278: libsysio hack. For O_EXCL, libsysio depends on
                   this lookup to return negative result, but then there is no
                   way to find out original intent in ll_iop_open(). So we just
                   clear O_EXCL from libsysio flags here to avoid checking
                   for negative result. O_EXCL will be enforced by MDS. */
                *((int*)intent->int_arg2) &= ~O_EXCL;
        }

        if (intent->int_opmask & INT_GETATTR)
                it->it_op |= IT_GETATTR;

        LASSERT(!(intent->int_opmask & INT_SETATTR));

        /* libsysio is different to linux vfs when doing unlink/rmdir,
         * INT_UPDPARENT was passed down during name resolution. Here
         * we treat it as normal lookup, later unlink()/rmdir() will
         * do the actual work */

        /* conform to kernel code, if only IT_LOOKUP was set, don't
         * pass down it */
        if (!it->it_op || it->it_op == IT_LOOKUP) {
                OBD_FREE(it, sizeof(*it));
                it = NULL;
        }
        if (it)
                it->it_op_release = ll_intent_release;

        CDEBUG(D_VFSTRACE, "final intent 0x%x\n", it ? it->it_op : 0);
        return it;
}

int llu_iop_lookup(struct pnode *pnode,
                   struct inode **inop,
                   struct intent *intnt,
                   const char *path)
{
        struct lookup_intent *it;
        int rc;
        ENTRY;

        liblustre_wait_event(0);

        *inop = NULL;

        /* the mount root inode have no name, so don't call
         * remote in this case. but probably we need revalidate
         * it here? FIXME */
        if (pnode->p_mount->mnt_root == pnode) {
                struct inode *i = pnode->p_base->pb_ino;
                *inop = i;
                RETURN(0);
        }

        if (!pnode->p_base->pb_name.len)
                RETURN(-EINVAL);

        it = translate_lookup_intent(intnt, path);

        /* param flags is not used, let it be 0 */
        if (llu_pb_revalidate(pnode, 0, it)) {
                LASSERT(pnode->p_base->pb_ino);
                *inop = pnode->p_base->pb_ino;
                GOTO(out, rc = 0);
        }

        rc = llu_lookup_it(pnode->p_parent->p_base->pb_ino, pnode, it, 0);
        if (!rc) {
                if (!pnode->p_base->pb_ino)
                        rc = -ENOENT;
                else
                        *inop = pnode->p_base->pb_ino;
        }

out:
        if (it)
                OBD_FREE(it, sizeof(*it));
        liblustre_wait_event(0);
        RETURN(rc);
}

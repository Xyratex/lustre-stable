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
 */

#define DEBUG_SUBSYSTEM S_RPC
#ifndef __KERNEL__
#include <errno.h>
#include <signal.h>
#include <liblustre.h>
#endif

#include <obd_support.h>
#include <obd_class.h>
#include <lustre_lib.h>
#include <lustre_ha.h>
#include <lustre_import.h>

#include "ptlrpc_internal.h"

#ifdef __KERNEL__

void ptlrpc_fill_bulk_md (lnet_md_t *md, struct ptlrpc_bulk_desc *desc)
{
        LASSERT (desc->bd_iov_count <= PTLRPC_MAX_BRW_PAGES);
        LASSERT (!(md->options & (LNET_MD_IOVEC | LNET_MD_KIOV | LNET_MD_PHYS)));

        md->options |= LNET_MD_KIOV;
        md->start = &desc->bd_iov[0];
        md->length = desc->bd_iov_count;
}

void ptlrpc_add_bulk_page(struct ptlrpc_bulk_desc *desc, cfs_page_t *page,
                          int pageoffset, int len)
{
        lnet_kiov_t *kiov = &desc->bd_iov[desc->bd_iov_count];

        kiov->kiov_page = page;
        kiov->kiov_offset = pageoffset;
        kiov->kiov_len = len;

        desc->bd_iov_count++;
}

#else /* !__KERNEL__ */

void ptlrpc_fill_bulk_md(lnet_md_t *md, struct ptlrpc_bulk_desc *desc)
{
        LASSERT (!(md->options & (LNET_MD_IOVEC | LNET_MD_KIOV | LNET_MD_PHYS)));
        if (desc->bd_iov_count == 1) {
                md->start = desc->bd_iov[0].iov_base;
                md->length = desc->bd_iov[0].iov_len;
                return;
        }

        md->options |= LNET_MD_IOVEC;
        md->start = &desc->bd_iov[0];
        md->length = desc->bd_iov_count;
}

static int can_merge_iovs(lnet_md_iovec_t *existing, lnet_md_iovec_t *candidate)
{
        if (existing->iov_base + existing->iov_len == candidate->iov_base)
                return 1;
#if 0
        /* Enable this section to provide earlier evidence of fragmented bulk */
        CERROR("Can't merge iovs %p for %x, %p for %x\n",
               existing->iov_base, existing->iov_len,
               candidate->iov_base, candidate->iov_len);
#endif
        return 0;
}

void ptlrpc_add_bulk_page(struct ptlrpc_bulk_desc *desc, cfs_page_t *page,
                          int pageoffset, int len)
{
        lnet_md_iovec_t *iov = &desc->bd_iov[desc->bd_iov_count];

        iov->iov_base = page->addr + pageoffset;
        iov->iov_len = len;

        if (desc->bd_iov_count > 0 && can_merge_iovs(iov - 1, iov)) {
                (iov - 1)->iov_len += len;
        } else {
                desc->bd_iov_count++;
        }
}

#endif /* !__KERNEL__ */

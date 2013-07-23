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

#ifndef _LUSTRE_DEBUG_H
#define _LUSTRE_DEBUG_H

#include <lustre_net.h>

#if defined(__linux__)
#include <linux/lustre_debug.h>
#elif defined(__APPLE__)
#include <darwin/lustre_debug.h>
#elif defined(__WINNT__)
#include <winnt/lustre_debug.h>
#else
#error Unsupported operating system.
#endif

#define ASSERT_MAX_SIZE_MB 60000ULL
#define ASSERT_PAGE_INDEX(index, OP)                                    \
do { if (index > ASSERT_MAX_SIZE_MB << (20 - CFS_PAGE_SHIFT)) {         \
        CERROR("bad page index %lu > %Lu\n", index,                     \
               ASSERT_MAX_SIZE_MB << (20 - CFS_PAGE_SHIFT));            \
        libcfs_debug = ~0UL;                                            \
        OP;                                                             \
}} while(0)

#define ASSERT_FILE_OFFSET(offset, OP)                                  \
do { if (offset > ASSERT_MAX_SIZE_MB << 20) {                           \
        CERROR("bad file offset %Lu > %Lu\n", offset,                   \
               ASSERT_MAX_SIZE_MB << 20);                               \
        libcfs_debug = ~0UL;                                            \
        OP;                                                             \
}} while(0)

/* lib/debug.c */
int dump_lniobuf(struct niobuf_local *lnb);
int dump_rniobuf(struct niobuf_remote *rnb);
int dump_ioo(struct obd_ioobj *nb);
int dump_req(struct ptlrpc_request *req);
int dump_obdo(struct obdo *oa);
void dump_lsm(int level, struct lov_stripe_md *lsm);
int block_debug_setup(void *addr, int len, __u64 off, __u64 id);
int block_debug_check(char *who, void *addr, int len, __u64 off, __u64 id);
#endif

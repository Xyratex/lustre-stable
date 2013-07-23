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
 * lustre/include/darwin/lprocfs_status.h
 *
 * Author: Hariharan Thantry thantry@users.sourceforge.net
 */
#ifndef _DARWIN_LPROCFS_SNMP_H
#define _DARWIN_LPROCFS_SNMP_H

#ifndef _LPROCFS_SNMP_H
#error Do not #include this file directly. #include <lprocfs_status.h> instead
#endif

#ifdef LPROCFS
#undef LPROCFS
#endif

#include <libcfs/libcfs.h>
#define kstatfs statfs

/*
 * XXX nikita: temporary! Stubs for naked procfs calls made by Lustre
 * code. Should be replaced with our own procfs-like API.
 */

static inline cfs_proc_dir_entry_t *proc_symlink(const char *name,
                                                 cfs_proc_dir_entry_t *parent,
                                                 const char *dest)
{
        return NULL;
}

static inline cfs_proc_dir_entry_t *create_proc_entry(const char *name,
                                                      mode_t mode,
                                                      cfs_proc_dir_entry_t *p)
{
        return NULL;
}

#endif /* XNU_LPROCFS_SNMP_H */

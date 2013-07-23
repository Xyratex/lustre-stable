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
 * lustre/include/darwin/lustre_lite.h
 *
 * lustre lite cluster file system
 */

#ifndef _DARWIN_LL_H
#define _DARWIN_LL_H

#ifndef _LL_H
#error Do not #include this file directly. #include <lustre_lite.h> instead
#endif

#include <libcfs/libcfs.h>

#ifdef __KERNEL__

struct iattr {
        unsigned int    ia_valid;
        umode_t         ia_mode;
        uid_t           ia_uid;
        gid_t           ia_gid;
        loff_t          ia_size;
        time_t          ia_atime;
        time_t          ia_mtime;
        time_t          ia_ctime;
        unsigned int    ia_attr_flags;
};

/*
 * intent data-structured. For Linux they are defined in
 * linux/include/linux/dcache.h
 */
#define IT_OPEN     0x0001
#define IT_CREAT    0x0002
#define IT_READDIR  0x0004
#define IT_GETATTR  0x0008
#define IT_LOOKUP   0x0010
#define IT_UNLINK   0x0020
#define IT_GETXATTR 0x0040
#define IT_EXEC     0x0080
#define IT_PIN      0x0100

#define IT_FL_LOCKED   0x0001
#define IT_FL_FOLLOWED 0x0002 /* set by vfs_follow_link */

#define INTENT_MAGIC 0x19620323 /* Happy birthday! */

struct lustre_intent_data {
        int     it_disposition;
        int     it_status;
        __u64   it_lock_handle;
        void    *it_data;
        int     it_lock_mode;
};

/*
 * Liang: We keep the old lookup_intent struct in XNU 
 * to avoid unnecessary allocate/free. 
 */
#define LUSTRE_IT(it) ((struct lustre_intent_data *)(&(it)->d.lustre))

struct lookup_intent {
	int     it_magic;
	void    (*it_op_release)(struct lookup_intent *);
	int     it_op;
	int     it_flags;
	int     it_create_mode;
	union {
                struct lustre_intent_data lustre;
		void *fs_data;
	} d;
};

struct super_operations{
};
#endif

#endif

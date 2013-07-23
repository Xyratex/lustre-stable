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

#ifndef LUSTRE_INTENT_H
#define LUSTRE_INTENT_H

#include <linux/lustre_version.h>

#ifndef HAVE_VFS_INTENT_PATCHES
#define IT_OPEN     (1)
#define IT_CREAT    (1<<1)
#define IT_READDIR  (1<<2)
#define IT_GETATTR  (1<<3)
#define IT_LOOKUP   (1<<4)
#define IT_UNLINK   (1<<5)
#define IT_TRUNC    (1<<6)
#define IT_GETXATTR (1<<7)

struct lustre_intent_data {
        int       it_disposition;
        int       it_status;
        __u64     it_lock_handle;
        void     *it_data;
        int       it_lock_mode;
};

struct lookup_intent {
        int     it_op;
        int     it_flags;
	int     it_create_mode;
        union {
                struct lustre_intent_data lustre;
        } d;
};


#endif
#endif

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

#ifndef _OBD_ECHO_H
#define _OBD_ECHO_H

/* The persistent object (i.e. actually stores stuff!) */
#define ECHO_PERSISTENT_OBJID    1ULL
#define ECHO_PERSISTENT_SIZE     ((__u64)(1<<20))

/* block size to use for data verification */
#define OBD_ECHO_BLOCK_SIZE	(4<<10)

struct ec_object {
        struct list_head       eco_obj_chain;
        struct obd_device     *eco_device;
        int                    eco_refcount;
        int                    eco_deleted;
        obd_id                 eco_id;
        struct lov_stripe_md  *eco_lsm;
};

struct ec_lock {
        struct list_head       ecl_exp_chain;
        struct ec_object      *ecl_object;
        __u64                  ecl_cookie;
        struct lustre_handle   ecl_lock_handle;
        ldlm_policy_data_t     ecl_policy;
        __u32                  ecl_mode;
};

#endif

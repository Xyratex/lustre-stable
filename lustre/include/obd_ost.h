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
 * lustre/include/obd_ost.h
 *
 * Data structures for object storage targets and client: OST & OSC's
 * 
 * See also lustre_idl.h for wire formats of requests.
 */

#ifndef _LUSTRE_OST_H
#define _LUSTRE_OST_H

#include <obd_class.h>

struct osc_brw_async_args {
        struct obdo     *aa_oa;
        int              aa_requested_nob;
        int              aa_nio_count;
        obd_count        aa_page_count;
        int              aa_resends;
        int              aa_pshift;
        struct brw_page **aa_ppga;
        struct client_obd *aa_cli;
        struct list_head aa_oaps;
};

#define osc_grant_args osc_brw_async_args
struct osc_async_args {
        struct obd_info   *aa_oi;
};

struct osc_enqueue_args {
        struct obd_export       *oa_exp;
        struct obd_info         *oa_oi;
        struct ldlm_enqueue_info*oa_ei;
};

int osc_extent_blocking_cb(struct ldlm_lock *lock,
                           struct ldlm_lock_desc *new, void *data,
                           int flag);

/** 
 * Build DLM resource name from object id & group for osc-ost extent lock.
 */
static inline struct ldlm_res_id *osc_build_res_name(__u64 id, __u64 gr,
                                                     struct ldlm_res_id *name)
{
        memset(name, 0, sizeof *name);
        name->name[0] = id;
        name->name[1] = gr;
        return name;
}

/**
 * Return true if the resource is for the object identified by this id & group.
 */
static inline int osc_res_name_eq(__u64 id, __u64 gr, struct ldlm_res_id *name)
{
        return name->name[0] == id && name->name[1] == gr;
}

#endif

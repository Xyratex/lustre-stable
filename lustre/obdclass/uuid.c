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
 * lustre/obdclass/uuid.c
 *
 * Public include file for the UUID library
 */

#define DEBUG_SUBSYSTEM S_CLASS

#ifndef __KERNEL__
# include <liblustre.h>
#else
# include <libcfs/kp30.h>
#endif

#include <obd_support.h>
#include <obd_class.h>


static inline __u32 consume(int nob, __u8 **ptr)
{
	__u32 value;

	LASSERT(nob <= sizeof value);

	for (value = 0; nob > 0; --nob)
		value = (value << 8) | *((*ptr)++);
	return value;
}

#define CONSUME(val, ptr) (val) = consume(sizeof(val), (ptr))

static void uuid_unpack(class_uuid_t in, __u16 *uu, int nr)
{
        __u8 *ptr = in;

	LASSERT(nr * sizeof *uu == sizeof(class_uuid_t));

	while (nr-- > 0)
		CONSUME(uu[nr], &ptr);
}

void class_uuid_unparse(class_uuid_t uu, struct obd_uuid *out)
{
	/* uu as an array of __u16's */
        __u16 uuid[sizeof(class_uuid_t) / sizeof(__u16)];

	CLASSERT(ARRAY_SIZE(uuid) == 8);

        uuid_unpack(uu, uuid, ARRAY_SIZE(uuid));
        sprintf(out->uuid, "%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
		uuid[0], uuid[1], uuid[2], uuid[3],
		uuid[4], uuid[5], uuid[6], uuid[7]);
}

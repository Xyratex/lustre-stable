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

#ifndef __LNET_SYSCTL_H__
#define __LNET_SYSCTL_H__

#if defined(CONFIG_SYSCTL)

#ifndef HAVE_SYSCTL_UNNUMBERED

#define CTL_KRANAL      201
#define CTL_GMLND       202
#define CTL_KIBNAL      203
#define CTL_IIBBLND     204
#define CTL_O2IBLND     205
#define CTL_PTLLND      206
#define CTL_QSWNAL      207
#define CTL_SOCKLND     208
#define CTL_VIBLND      209
#define CTL_GNILND      210

#else

#define CTL_KRANAL      CTL_UNNUMBERED
#define CTL_GMLND       CTL_UNNUMBERED
#define CTL_KIBNAL      CTL_UNNUMBERED
#define CTL_IIBLND      CTL_UNNUMBERED
#define CTL_O2IBLND     CTL_UNNUMBERED
#define CTL_PTLLND      CTL_UNNUMBERED
#define CTL_QSWNAL	CTL_UNNUMBERED
#define CTL_SOCKLND     CTL_UNNUMBERED
#define CTL_VIBLND      CTL_UNNUMBERED
#define CTL_GNILND      CTL_UNNUMBERED

#endif /* sysctl id */

#endif

#endif

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
 * lnet/include/lnet/ptllnd.h
 *
 * Author: PJ Kirner <pjkirner@clusterfs.com>
 */
 
/*
 * The PTLLND was designed to support Portals with
 * Lustre and non-lustre UNLINK semantics.
 * However for now the two targets are Cray Portals
 * on the XT3 and Lustre Portals (for testing) both
 * have Lustre UNLINK semantics, so this is defined
 * by default.
 */
#define LUSTRE_PORTALS_UNLINK_SEMANTICS
 
 
#ifdef _USING_LUSTRE_PORTALS_

/* NIDs are 64-bits on Lustre Portals */
#define FMT_NID LPU64
#define FMT_PID "%d"

/* When using Lustre Portals Lustre completion semantics are imlicit*/
#define PTL_MD_LUSTRE_COMPLETION_SEMANTICS      0

#else /* _USING_CRAY_PORTALS_ */

/* NIDs are integers on Cray Portals */
#define FMT_NID "%u"
#define FMT_PID "%d"

/* When using Cray Portals this is defined in the Cray Portals Header*/
/*#define PTL_MD_LUSTRE_COMPLETION_SEMANTICS */

/* Can compare handles directly on Cray Portals */
#define PtlHandleIsEqual(a,b) ((a) == (b))

/* Diffrent error types on Cray Portals*/
#define ptl_err_t ptl_ni_fail_t

/*
 * The Cray Portals has no maximum number of IOVs.  The
 * maximum is limited only by memory and size of the
 * int parameters (2^31-1).
 * Lustre only really require that the underyling
 * implemenation to support at least LNET_MAX_IOV,
 * so for Cray portals we can safely just use that
 * value here.
 *
 */
#define PTL_MD_MAX_IOV          LNET_MAX_IOV

#endif

#define FMT_PTLID "ptlid:"FMT_PID"-"FMT_NID

/* Align incoming small request messages to an 8 byte boundary if this is
 * supported to avoid alignment issues on some architectures */
#ifndef PTL_MD_LOCAL_ALIGN8
# define PTL_MD_LOCAL_ALIGN8 0
#endif

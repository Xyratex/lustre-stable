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

#ifndef _LUSTRE_LINUX_TYPES_H
#define _LUSTRE_LINUX_TYPES_H

#ifdef HAVE_ASM_TYPES_H
#include <asm/types.h>
#endif

#ifdef __KERNEL__
# include <linux/types.h>
# include <linux/fs.h>    /* to check for FMODE_EXEC, dev_t, lest we redefine */
#else
#ifdef __CYGWIN__
# include <sys/types.h>
#elif defined(_AIX)
# include <inttypes.h>
#else
# include <stdint.h>
#endif
#endif

#if !defined(_LINUX_TYPES_H) && !defined(_BLKID_TYPES_H) && \
        !defined(_EXT2_TYPES_H) && !defined(_I386_TYPES_H) && \
        !defined(_ASM_IA64_TYPES_H) && !defined(_X86_64_TYPES_H) && \
        !defined(_PPC_TYPES_H) && !defined(_PPC64_TYPES_H) && \
        !defined(_ASM_POWERPC_TYPES_H) && !defined(__mips64__) && \
	!defined(_CRAYNV_TYPES_H)
        /* yuck, would be nicer with _ASM_TYPES_H */

typedef unsigned short umode_t;
/*
 * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
 * header files exported to user space
 */

typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

typedef __signed__ long long __s64;
typedef unsigned long long __u64;
#endif

#endif

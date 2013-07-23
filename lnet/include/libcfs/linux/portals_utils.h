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

#ifndef __LIBCFS_LINUX_PORTALS_UTILS_H__
#define __LIBCFS_LINUX_PORTALS_UTILS_H__

#ifndef __LIBCFS_PORTALS_UTILS_H__
#error Do not #include this file directly. #include <libcfs/portals_utils.h> instead
#endif

#ifdef __KERNEL__
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/random.h>

#include <asm/unistd.h>
#include <asm/semaphore.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
# include <linux/tqueue.h>
#else /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)) */
# include <linux/workqueue.h>
#endif
#include <libcfs/linux/linux-mem.h>
#include <libcfs/linux/linux-prim.h>
#else /* !__KERNEL__ */

#include <endian.h>
#include <libcfs/list.h>

#ifdef HAVE_LINUX_VERSION_H
# include <linux/version.h>

# if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#  define BUG()                            /* workaround for module.h includes */
#  include <linux/module.h>
# endif
#endif /* !HAVE_LINUX_VERSION_H */

#ifndef __CYGWIN__
# include <sys/syscall.h>
#else /* __CYGWIN__ */
# include <windows.h>
# include <windef.h>
# include <netinet/in.h>
#endif /* __CYGWIN__ */

#endif /* !__KERNEL__ */
#endif

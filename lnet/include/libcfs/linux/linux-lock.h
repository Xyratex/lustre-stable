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
 * lnet/include/libcfs/linux/linux-lock.h
 *
 * Basic library routines.
 */

#ifndef __LIBCFS_LINUX_CFS_LOCK_H__
#define __LIBCFS_LINUX_CFS_LOCK_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

#ifdef __KERNEL__
#include <linux/smp_lock.h>

/*
 * IMPORTANT !!!!!!!!
 *
 * All locks' declaration are not guaranteed to be initialized,
 * Althought some of they are initialized in Linux. All locks
 * declared by CFS_DECL_* should be initialized explicitly.
 */


/*
 * spin_lock (use Linux kernel's primitives)
 *
 * - spin_lock_init(x)
 * - spin_lock(x)
 * - spin_unlock(x)
 * - spin_trylock(x)
 *
 * - spin_lock_irqsave(x, f)
 * - spin_unlock_irqrestore(x, f)
 */

/*
 * rw_semaphore (use Linux kernel's primitives)
 *
 * - init_rwsem(x)
 * - down_read(x)
 * - up_read(x)
 * - down_write(x)
 * - up_write(x)
 */

/*
 * rwlock_t (use Linux kernel's primitives)
 *
 * - rwlock_init(x)
 * - read_lock(x)
 * - read_unlock(x)
 * - write_lock(x)
 * - write_unlock(x)
 */

/*
 * mutex:
 *
 * - init_mutex(x)
 * - init_mutex_locked(x)
 * - mutex_up(x)
 * - mutex_down(x)
 */
#define init_mutex(x)                   init_MUTEX(x)
#define init_mutex_locked(x)            init_MUTEX_LOCKED(x)
#define mutex_up(x)                     up(x)
#define mutex_down(x)                   down(x)
#define mutex_down_trylock(x)           down_trylock(x)

/*
 * completion (use Linux kernel's primitives)
 *
 * - init_complition(c)
 * - complete(c)
 * - wait_for_completion(c)
 */

/*
 * spinlock "implementation"
 */

typedef spinlock_t cfs_spinlock_t;

#define cfs_spin_lock_init(lock) spin_lock_init(lock)
#define cfs_spin_lock(lock)      spin_lock(lock)
#define cfs_spin_lock_bh(lock)   spin_lock_bh(lock)
#define cfs_spin_unlock(lock)    spin_unlock(lock)
#define cfs_spin_unlock_bh(lock) spin_unlock_bh(lock)

/*
 * rwlock "implementation"
 */

typedef rwlock_t cfs_rwlock_t;

#define cfs_rwlock_init(lock)      rwlock_init(lock)
#define cfs_read_lock(lock)        read_lock(lock)
#define cfs_read_unlock(lock)      read_unlock(lock)
#define cfs_write_lock_bh(lock)    write_lock_bh(lock)
#define cfs_write_unlock_bh(lock)  write_unlock_bh(lock)

/* __KERNEL__ */
#else

#include "../user-lock.h"

/* __KERNEL__ */
#endif
#endif

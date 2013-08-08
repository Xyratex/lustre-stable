/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/watchdog.c
 *
 * Author: Jacob Berkman <jacob@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/libcfs.h>
#include "tracefile.h"

struct lc_watchdog {
        cfs_spinlock_t  lcw_lock;     /* check or change lcw_list */
        int             lcw_refcount; /* must hold lcw_pending_timers_lock */
        cfs_timer_t     lcw_timer;    /* kernel timer */
        cfs_list_t      lcw_list;     /* chain on pending list */
        cfs_time_t      lcw_last_touched; /* last touched stamp */
        cfs_task_t     *lcw_task;     /* owner task */
        void          (*lcw_callback)(pid_t, void *);
        void           *lcw_data;

        pid_t           lcw_pid;

        enum {
                LC_WATCHDOG_DISABLED,
                LC_WATCHDOG_ENABLED,
                LC_WATCHDOG_EXPIRED
        } lcw_state;
};

#ifdef WITH_WATCHDOG
/*
 * The dispatcher will complete lcw_start_completion when it starts,
 * and lcw_stop_completion when it exits.
 * Wake lcw_event_waitq to signal timer callback dispatches.
 */
static cfs_completion_t lcw_start_completion;
static cfs_completion_t  lcw_stop_completion;
static cfs_waitq_t lcw_event_waitq;

/*
 * Set this and wake lcw_event_waitq to stop the dispatcher.
 */
enum {
        LCW_FLAG_STOP = 0
};
static unsigned long lcw_flags = 0;

/*
 * Number of outstanding watchdogs.
 * When it hits 1, we start the dispatcher.
 * When it hits 0, we stop the dispatcher.
 */
static __u32         lcw_refcount = 0;
static CFS_DEFINE_MUTEX(lcw_refcount_mutex);

/*
 * List of timers that have fired that need their callbacks run by the
 * dispatcher.
 */
/* BH lock! */
static cfs_spinlock_t lcw_pending_timers_lock = CFS_SPIN_LOCK_UNLOCKED;
static cfs_list_t lcw_pending_timers = \
        CFS_LIST_HEAD_INIT(lcw_pending_timers);

/* Last time a watchdog expired */
static cfs_time_t lcw_last_watchdog_time;
static int lcw_recent_watchdog_count;

static void
lcw_dump(struct lc_watchdog *lcw)
{
        ENTRY;
#if defined(HAVE_TASKLIST_LOCK)
        cfs_read_lock(&tasklist_lock);
#else
        rcu_read_lock();
#endif
       if (lcw->lcw_task == NULL) {
                LCONSOLE_WARN("Process " LPPID " was not found in the task "
                              "list; watchdog callback may be incomplete\n",
                              (int)lcw->lcw_pid);
        } else {
                libcfs_debug_dumpstack(lcw->lcw_task);
        }

#if defined(HAVE_TASKLIST_LOCK)
        cfs_read_unlock(&tasklist_lock);
#else
        rcu_read_unlock();
#endif
        EXIT;
}

static void lcw_cb(ulong_ptr_t data)
{
        struct lc_watchdog *lcw = (struct lc_watchdog *)data;
        ENTRY;

        if (lcw->lcw_state != LC_WATCHDOG_ENABLED) {
                EXIT;
                return;
        }

        lcw->lcw_state = LC_WATCHDOG_EXPIRED;

        cfs_spin_lock_bh(&lcw->lcw_lock);
        LASSERT(cfs_list_empty(&lcw->lcw_list));

        cfs_spin_lock_bh(&lcw_pending_timers_lock);
        lcw->lcw_refcount++; /* +1 for pending list */
        cfs_list_add(&lcw->lcw_list, &lcw_pending_timers);
        cfs_waitq_signal(&lcw_event_waitq);

        cfs_spin_unlock_bh(&lcw_pending_timers_lock);
        cfs_spin_unlock_bh(&lcw->lcw_lock);
        EXIT;
}

static int is_watchdog_fired(void)
{
        int rc;

        if (cfs_test_bit(LCW_FLAG_STOP, &lcw_flags))
                return 1;

        cfs_spin_lock_bh(&lcw_pending_timers_lock);
        rc = !cfs_list_empty(&lcw_pending_timers);
        cfs_spin_unlock_bh(&lcw_pending_timers_lock);
        return rc;
}

static void lcw_dump_stack(struct lc_watchdog *lcw)
{
        cfs_time_t      current_time;
        cfs_duration_t  delta_time;
        struct timeval  timediff;

        current_time = cfs_time_current();
        delta_time = cfs_time_sub(current_time, lcw->lcw_last_touched);
        cfs_duration_usec(delta_time, &timediff);

        /*
         * Check to see if we should throttle the watchdog timer to avoid
         * too many dumps going to the console thus triggering an NMI.
         */
        delta_time = cfs_duration_sec(cfs_time_sub(current_time,
                                                   lcw_last_watchdog_time));

        if (delta_time < libcfs_watchdog_ratelimit &&
            lcw_recent_watchdog_count > 3) {
                LCONSOLE_WARN("Service thread pid %u was inactive for "
                              "%lu.%.02lus. Watchdog stack traces are limited "
                              "to 3 per %d seconds, skipping this one.\n",
                              (int)lcw->lcw_pid,
                              timediff.tv_sec,
                              timediff.tv_usec / 10000,
                              libcfs_watchdog_ratelimit);
        } else {
                if (delta_time < libcfs_watchdog_ratelimit) {
                        lcw_recent_watchdog_count++;
                } else {
                        memcpy(&lcw_last_watchdog_time, &current_time,
                               sizeof(current_time));
                        lcw_recent_watchdog_count = 0;
                }

                LCONSOLE_WARN("Service thread pid %u was inactive for "
                              "%lu.%.02lus. The thread might be hung, or it "
                              "might only be slow and will resume later. "
                              "Dumping the stack trace for debugging purposes:"
                              "\n",
                              (int)lcw->lcw_pid,
                              timediff.tv_sec,
                              timediff.tv_usec / 10000);
                lcw_dump(lcw);
        }
}

static int lcw_dispatch_main(void *data)
{
        int                 rc = 0;
        unsigned long       flags;
        struct lc_watchdog *lcw;
        CFS_LIST_HEAD      (zombies);

        ENTRY;

        cfs_daemonize("lc_watchdogd");

        SIGNAL_MASK_LOCK(current, flags);
        sigfillset(&current->blocked);
        RECALC_SIGPENDING;
        SIGNAL_MASK_UNLOCK(current, flags);

        cfs_complete(&lcw_start_completion);

        while (1) {
                int dumplog = 1;

                cfs_wait_event_interruptible(lcw_event_waitq,
                                             is_watchdog_fired(), rc);
                CDEBUG(D_INFO, "Watchdog got woken up...\n");
                if (cfs_test_bit(LCW_FLAG_STOP, &lcw_flags)) {
                        CDEBUG(D_INFO, "LCW_FLAG_STOP was set, shutting down...\n");

                        cfs_spin_lock_bh(&lcw_pending_timers_lock);
                        rc = !cfs_list_empty(&lcw_pending_timers);
                        cfs_spin_unlock_bh(&lcw_pending_timers_lock);
                        if (rc) {
                                CERROR("pending timers list was not empty at "
                                       "time of watchdog dispatch shutdown\n");
                        }
                        break;
                }

                cfs_spin_lock_bh(&lcw_pending_timers_lock);
                while (!cfs_list_empty(&lcw_pending_timers)) {
                        int is_dumplog;

                        lcw = cfs_list_entry(lcw_pending_timers.next,
                                             struct lc_watchdog, lcw_list);
                        /* +1 ref for callback to make sure lwc wouldn't be
                         * deleted after releasing lcw_pending_timers_lock */
                        lcw->lcw_refcount++;
                        cfs_spin_unlock_bh(&lcw_pending_timers_lock);

                        /* lock ordering */
                        cfs_spin_lock_bh(&lcw->lcw_lock);
                        cfs_spin_lock_bh(&lcw_pending_timers_lock);

                        if (cfs_list_empty(&lcw->lcw_list)) {
                                /* already removed from pending list */
                                lcw->lcw_refcount--; /* -1 ref for callback */
                                if (lcw->lcw_refcount == 0)
                                        cfs_list_add(&lcw->lcw_list, &zombies);
                                cfs_spin_unlock_bh(&lcw->lcw_lock);
                                /* still hold lcw_pending_timers_lock */
                                continue;
                        }

                        cfs_list_del_init(&lcw->lcw_list);
                        lcw->lcw_refcount--; /* -1 ref for pending list */

                        cfs_spin_unlock_bh(&lcw_pending_timers_lock);
                        cfs_spin_unlock_bh(&lcw->lcw_lock);

                        CDEBUG(D_INFO, "found lcw for pid " LPPID "\n",
                               lcw->lcw_pid);
                        lcw_dump_stack(lcw);

                        is_dumplog = lcw->lcw_callback == lc_watchdog_dumplog;
                        if (lcw->lcw_state != LC_WATCHDOG_DISABLED &&
                            (dumplog || !is_dumplog)) {
                                lcw->lcw_callback(lcw->lcw_pid, lcw->lcw_data);
                                if (dumplog && is_dumplog)
                                        dumplog = 0;
                        }

                        cfs_spin_lock_bh(&lcw_pending_timers_lock);
                        lcw->lcw_refcount--; /* -1 ref for callback */
                        if (lcw->lcw_refcount == 0)
                                cfs_list_add(&lcw->lcw_list, &zombies);
                }
                cfs_spin_unlock_bh(&lcw_pending_timers_lock);

                while (!cfs_list_empty(&zombies)) {
                        lcw = cfs_list_entry(zombies.next,
                                         struct lc_watchdog, lcw_list);
                        cfs_list_del_init(&lcw->lcw_list);
                        LIBCFS_FREE(lcw, sizeof(*lcw));
                }
        }

        cfs_complete(&lcw_stop_completion);

        RETURN(rc);
}

static void lcw_dispatch_start(void)
{
        int rc;

        ENTRY;
        LASSERT(lcw_refcount == 1);

        cfs_init_completion(&lcw_stop_completion);
        cfs_init_completion(&lcw_start_completion);
        cfs_waitq_init(&lcw_event_waitq);

        CDEBUG(D_INFO, "starting dispatch thread\n");
        rc = cfs_create_thread(lcw_dispatch_main, NULL, 0);
        if (rc < 0) {
                CERROR("error spawning watchdog dispatch thread: %d\n", rc);
                EXIT;
                return;
        }
        cfs_wait_for_completion(&lcw_start_completion);
        CDEBUG(D_INFO, "watchdog dispatcher initialization complete.\n");

        EXIT;
}

static void lcw_dispatch_stop(void)
{
        ENTRY;
        LASSERT(lcw_refcount == 0);

        CDEBUG(D_INFO, "trying to stop watchdog dispatcher.\n");

        cfs_set_bit(LCW_FLAG_STOP, &lcw_flags);
        cfs_waitq_signal(&lcw_event_waitq);

        cfs_wait_for_completion(&lcw_stop_completion);

        CDEBUG(D_INFO, "watchdog dispatcher has shut down.\n");

        EXIT;
}

struct lc_watchdog *lc_watchdog_add(int timeout,
                                    void (*callback)(pid_t, void *),
                                    void *data)
{
        struct lc_watchdog *lcw = NULL;
        ENTRY;

        LIBCFS_ALLOC(lcw, sizeof(*lcw));
        if (lcw == NULL) {
                CDEBUG(D_INFO, "Could not allocate new lc_watchdog\n");
                RETURN(ERR_PTR(-ENOMEM));
        }

        cfs_spin_lock_init(&lcw->lcw_lock);
        lcw->lcw_refcount = 1; /* refcount for owner */
        lcw->lcw_task     = cfs_current();
        lcw->lcw_pid      = cfs_curproc_pid();
        lcw->lcw_callback = (callback != NULL) ? callback : lc_watchdog_dumplog;
        lcw->lcw_data     = data;
        lcw->lcw_state    = LC_WATCHDOG_DISABLED;

        CFS_INIT_LIST_HEAD(&lcw->lcw_list);
        cfs_timer_init(&lcw->lcw_timer, lcw_cb, lcw);

        cfs_mutex_lock(&lcw_refcount_mutex);
        if (++lcw_refcount == 1)
                lcw_dispatch_start();
        cfs_mutex_unlock(&lcw_refcount_mutex);

        /* Keep this working in case we enable them by default */
        if (lcw->lcw_state == LC_WATCHDOG_ENABLED) {
                lcw->lcw_last_touched = cfs_time_current();
                cfs_timer_arm(&lcw->lcw_timer, cfs_time_seconds(timeout) +
                              cfs_time_current());
        }

        RETURN(lcw);
}
EXPORT_SYMBOL(lc_watchdog_add);

static void lcw_update_time(struct lc_watchdog *lcw, const char *message)
{
        cfs_time_t newtime = cfs_time_current();;

        if (lcw->lcw_state == LC_WATCHDOG_EXPIRED) {
                struct timeval timediff;
                cfs_time_t delta_time = cfs_time_sub(newtime,
                                                     lcw->lcw_last_touched);
                cfs_duration_usec(delta_time, &timediff);

                LCONSOLE_WARN("Service thread pid %u %s after %lu.%.02lus. "
                              "This indicates the system was overloaded (too "
                              "many service threads, or there were not enough "
                              "hardware resources).\n",
                              lcw->lcw_pid,
                              message,
                              timediff.tv_sec,
                              timediff.tv_usec / 10000);
        }
        lcw->lcw_last_touched = newtime;
}

static void lc_watchdog_del_pending(struct lc_watchdog *lcw)
{
        cfs_spin_lock_bh(&lcw->lcw_lock);
        if (unlikely(!cfs_list_empty(&lcw->lcw_list))) {
                cfs_spin_lock_bh(&lcw_pending_timers_lock);
                cfs_list_del_init(&lcw->lcw_list);
                lcw->lcw_refcount--; /* -1 ref for pending list */
                cfs_spin_unlock_bh(&lcw_pending_timers_lock);
        }

        cfs_spin_unlock_bh(&lcw->lcw_lock);
}

void lc_watchdog_touch(struct lc_watchdog *lcw, int timeout)
{
        ENTRY;
        LASSERT(lcw != NULL);

        lc_watchdog_del_pending(lcw);

        lcw_update_time(lcw, "resumed");
        lcw->lcw_state = LC_WATCHDOG_ENABLED;

        cfs_timer_arm(&lcw->lcw_timer, cfs_time_current() +
                      cfs_time_seconds(timeout));

        EXIT;
}
EXPORT_SYMBOL(lc_watchdog_touch);

void lc_watchdog_disable(struct lc_watchdog *lcw)
{
        ENTRY;
        LASSERT(lcw != NULL);

        lc_watchdog_del_pending(lcw);

        lcw_update_time(lcw, "completed");
        lcw->lcw_state = LC_WATCHDOG_DISABLED;

        EXIT;
}
EXPORT_SYMBOL(lc_watchdog_disable);

void lc_watchdog_delete(struct lc_watchdog *lcw)
{
        int dead;

        ENTRY;
        LASSERT(lcw != NULL);

        cfs_timer_disarm(&lcw->lcw_timer);

        lcw_update_time(lcw, "stopped");

        cfs_spin_lock_bh(&lcw->lcw_lock);
        cfs_spin_lock_bh(&lcw_pending_timers_lock);
        if (unlikely(!cfs_list_empty(&lcw->lcw_list))) {
                cfs_list_del_init(&lcw->lcw_list);
                lcw->lcw_refcount--; /* -1 ref for pending list */
        }

        lcw->lcw_refcount--; /* -1 ref for owner */
        dead = lcw->lcw_refcount == 0;
        cfs_spin_unlock_bh(&lcw_pending_timers_lock);
        cfs_spin_unlock_bh(&lcw->lcw_lock);

        if (dead)
                LIBCFS_FREE(lcw, sizeof(*lcw));

        cfs_mutex_lock(&lcw_refcount_mutex);
        if (--lcw_refcount == 0)
                lcw_dispatch_stop();
        cfs_mutex_unlock(&lcw_refcount_mutex);

        EXIT;
}
EXPORT_SYMBOL(lc_watchdog_delete);

/*
 * Provided watchdog handlers
 */

void lc_watchdog_dumplog(pid_t pid, void *data)
{
        libcfs_debug_dumplog_internal((void *)((long_ptr_t)pid));
}
EXPORT_SYMBOL(lc_watchdog_dumplog);

#else   /* !defined(WITH_WATCHDOG) */

struct lc_watchdog *lc_watchdog_add(int timeout,
                                    void (*callback)(pid_t pid, void *),
                                    void *data)
{
        static struct lc_watchdog      watchdog;
        return &watchdog;
}
EXPORT_SYMBOL(lc_watchdog_add);

void lc_watchdog_touch(struct lc_watchdog *lcw, int timeout)
{
}
EXPORT_SYMBOL(lc_watchdog_touch);

void lc_watchdog_disable(struct lc_watchdog *lcw)
{
}
EXPORT_SYMBOL(lc_watchdog_disable);

void lc_watchdog_delete(struct lc_watchdog *lcw)
{
}
EXPORT_SYMBOL(lc_watchdog_delete);

#endif

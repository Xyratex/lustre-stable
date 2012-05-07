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
 * libcfs/libcfs/winnt/winnt-curproc.c
 *
 * Impletion of winnt curproc routines.
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/libcfs.h>

/*
 * Implementation of cfs_curproc API (see portals/include/libcfs/curproc.h)
 * for Linux kernel.
 */

cfs_task_t this_task =
    { /* umask */ 0,/* blocked*/0, /* pid */ 0, /* pgrp */ 0,
      /* uid,euid,suid,fsuid */  0, 0, 0, 0, 
      /* gid_t gid,egid,sgid,fsgid */ 0, 0, 0, 0,
      /* ngroups*/ 1, /*cgroups*/ 0, /*groups*/ 0,
      /* group_info */ NULL,
      /* cap_effective, cap_inheritable, cap_permitted */  0, 0, 0,
      /* comm */"sysetm\0",
      /* journal_info */ NULL
    };


uid_t  cfs_curproc_uid(void)
{
    return this_task.uid;
}

gid_t  cfs_curproc_gid(void)
{
    return this_task.gid;
}

uid_t  cfs_curproc_fsuid(void)
{
    return this_task.fsuid;
}

gid_t cfs_curproc_fsgid(void)
{
    return this_task.fsgid;
}

pid_t cfs_curproc_pid(void)
{
    return cfs_current()->pid;
}

int cfs_curproc_groups_nr(void)
{
    return this_task.ngroups;
}

void cfs_curproc_groups_dump(gid_t *array, int size)
{
    LASSERT(size <= NGROUPS);
    size = min_t(int, size, this_task.ngroups);
    memcpy(array, this_task.groups, size * sizeof(__u32));
}

int cfs_curproc_is_in_groups(gid_t gid)
{
    return in_group_p(gid);
}

mode_t cfs_curproc_umask(void)
{
    return this_task.umask;
}

char  *cfs_curproc_comm(void)
{
    return this_task.comm;
}

void cfs_cap_raise(cfs_cap_t cap)
{
        this_task.cap_effective |= (1 << cap);
}

void cfs_cap_lower(cfs_cap_t cap)
{
        this_task.cap_effective &= ~(1 << cap);
}

int cfs_cap_raised(cfs_cap_t cap)
{
        return this_task.cap_effective & (1 << cap);
}

cfs_cap_t cfs_curproc_cap_pack(void) {
        return this_task.cap_effective;
}

void cfs_curproc_cap_unpack(cfs_cap_t cap) {
        this_task.cap_effective = cap;
}

int cfs_capable(cfs_cap_t cap)
{
        return TRUE;
}

/*
 * Implementation of linux task management routines
 */


/* global of the task manager structure */

TASK_MAN cfs_win_task_manger;

/* global idr context */
struct idr_context * cfs_win_task_slot_idp = NULL;

/*
 *  task slot routiens
 */

PTASK_SLOT
alloc_task_slot()
{
    PTASK_SLOT task = NULL;

    if (cfs_win_task_manger.slab) {
        task = cfs_mem_cache_alloc(cfs_win_task_manger.slab, 0);
    } else {
        task = cfs_alloc(sizeof(TASK_SLOT), 0);
    }

    return task;
}

void
init_task_slot(PTASK_SLOT task)
{
    memset(task, 0, sizeof(TASK_SLOT));
    task->Magic = TASKSLT_MAGIC;
    task->task  = this_task;
    cfs_init_event(&task->Event, TRUE, FALSE);
}

void
cleanup_task_slot(PTASK_SLOT task)
{
    if (task->task.pid) {
        cfs_idr_remove(cfs_win_task_slot_idp, task->task.pid);
    }

    if (cfs_win_task_manger.slab) {
        cfs_mem_cache_free(cfs_win_task_manger.slab, task);
    } else {
        cfs_free(task);
    }
}

/*
 *  task manager related routines
 */

VOID
task_manager_notify(
    IN HANDLE   ProcessId,
    IN HANDLE   ThreadId,
    IN BOOLEAN  Create
    )
{
    PLIST_ENTRY ListEntry = NULL; 
    PTASK_SLOT  TaskSlot  = NULL;

    cfs_spin_lock(&(cfs_win_task_manger.Lock));

    ListEntry = cfs_win_task_manger.TaskList.Flink;
    while (ListEntry != (&(cfs_win_task_manger.TaskList))) {

        TaskSlot = CONTAINING_RECORD(ListEntry, TASK_SLOT, Link);

        if (TaskSlot->Pid == ProcessId && TaskSlot->Tid == ThreadId) {

            if (!Create) {
                /* remove the taskslot */
                RemoveEntryList(&(TaskSlot->Link));
                cfs_win_task_manger.NumOfTasks--;

                /* now free the task slot */
                cleanup_task_slot(TaskSlot);
            }
        }

        ListEntry = ListEntry->Flink;
    }

    cfs_spin_unlock(&(cfs_win_task_manger.Lock));
}

int
init_task_manager()
{
    NTSTATUS    status;

    /* initialize the content and magic */
    memset(&cfs_win_task_manger, 0, sizeof(TASK_MAN));
    cfs_win_task_manger.Magic = TASKMAN_MAGIC;

    /* initialize the spinlock protection */
    cfs_spin_lock_init(&cfs_win_task_manger.Lock);

    /* create slab memory cache */
    cfs_win_task_manger.slab = cfs_mem_cache_create(
        "TSLT", sizeof(TASK_SLOT), 0, 0);

    /* intialize the list header */
    InitializeListHead(&(cfs_win_task_manger.TaskList));

    cfs_win_task_slot_idp = cfs_idr_init();
    if (!cfs_win_task_slot_idp) {
        return -ENOMEM;
    }

    /* set the thread creation/destruction notify routine */
    status = PsSetCreateThreadNotifyRoutine(task_manager_notify);

    if (!NT_SUCCESS(status)) {
        cfs_enter_debugger();
        /* remove idr context */
        if (cfs_win_task_slot_idp) {
            cfs_idr_exit(cfs_win_task_slot_idp);
            cfs_win_task_slot_idp = NULL;
        }
        return cfs_error_code(status);
    }

    return 0;
}

void
cleanup_task_manager()
{
    PLIST_ENTRY ListEntry = NULL; 
    PTASK_SLOT  TaskSlot  = NULL;

    /* remove ThreadNotifyRoutine: task_manager_notify */
    PsRemoveCreateThreadNotifyRoutine(task_manager_notify);

    /* remove idr context */
    if (cfs_win_task_slot_idp) {
        cfs_idr_exit(cfs_win_task_slot_idp);
        cfs_win_task_slot_idp = NULL;
    }

    /* cleanup all the taskslots attached to the list */
    cfs_spin_lock(&(cfs_win_task_manger.Lock));

    while (!IsListEmpty(&(cfs_win_task_manger.TaskList))) {

        ListEntry = cfs_win_task_manger.TaskList.Flink;
        TaskSlot = CONTAINING_RECORD(ListEntry, TASK_SLOT, Link);

        RemoveEntryList(ListEntry);
        cleanup_task_slot(TaskSlot);
    }

    cfs_spin_unlock(&cfs_win_task_manger.Lock);

    /* destroy the taskslot cache slab */
    cfs_mem_cache_destroy(cfs_win_task_manger.slab);
    memset(&cfs_win_task_manger, 0, sizeof(TASK_MAN));
}


/*
 * schedule routines (task slot list)
 */


cfs_task_t *
cfs_current()
{
    HANDLE      Pid = PsGetCurrentProcessId();
    HANDLE      Tid = PsGetCurrentThreadId();
    PETHREAD    Tet = PsGetCurrentThread();

    PLIST_ENTRY ListEntry = NULL; 
    PTASK_SLOT  TaskSlot  = NULL;

    cfs_spin_lock(&(cfs_win_task_manger.Lock));

    ListEntry = cfs_win_task_manger.TaskList.Flink;
    while (ListEntry != (&(cfs_win_task_manger.TaskList))) {

        TaskSlot = CONTAINING_RECORD(ListEntry, TASK_SLOT, Link);
        if (TaskSlot->Pid == Pid && TaskSlot->Tid == Tid) {
            if (TaskSlot->Tet != Tet) {

                //
                // The old thread was already exit. This must be a
                // new thread which get the same Tid to the previous.
                //

                TaskSlot->Tet = Tet;
            }
            break;

        } else {

            if (TaskSlot->Pid > Pid) {
                TaskSlot = NULL;
                break;
            } else if (TaskSlot->Pid == Pid) {
                if (TaskSlot->Tid > Tid) {
                    TaskSlot = NULL;
                    break;
                }
            }
            TaskSlot =  NULL;
        }

        ListEntry = ListEntry->Flink;
    }

    if (!TaskSlot) {

        /* allocate new task slot */
        TaskSlot = alloc_task_slot();
        if (!TaskSlot) {
            cfs_enter_debugger();
            goto errorout;
        }

        /* set task slot IDs */
        init_task_slot(TaskSlot);
        TaskSlot->Pid = Pid;
        TaskSlot->Tid = Tid;
        TaskSlot->Tet = Tet;
        TaskSlot->task.pid = (pid_t)cfs_idr_get_new(cfs_win_task_slot_idp, Tet);

        if (ListEntry == (&(cfs_win_task_manger.TaskList))) {
            //
            // Empty case or the biggest case, put it to the tail.
            //
            InsertTailList(&(cfs_win_task_manger.TaskList), &(TaskSlot->Link));
        } else {
            //
            // Get a slot and smaller than it's tid, put it just before.
            //
            InsertHeadList(ListEntry->Blink, &(TaskSlot->Link));
        }

        cfs_win_task_manger.NumOfTasks++;
    }

    //
    // To Check whether he task structures are arranged in the expected order ?
    //

    {
        PTASK_SLOT  Prev = NULL, Curr = NULL;
        
        ListEntry = cfs_win_task_manger.TaskList.Flink;

        while (ListEntry != (&(cfs_win_task_manger.TaskList))) {

            Curr = CONTAINING_RECORD(ListEntry, TASK_SLOT, Link);
            ListEntry = ListEntry->Flink;

            if (Prev) {
                if (Prev->Pid > Curr->Pid) {
                    cfs_enter_debugger();
                } else if (Prev->Pid == Curr->Pid) {
                    if (Prev->Tid > Curr->Tid) {
                        cfs_enter_debugger();
                    }
                }
            }

            Prev = Curr;
        }
    }

errorout:

    cfs_spin_unlock(&(cfs_win_task_manger.Lock));

    if (!TaskSlot) {
        cfs_enter_debugger();
        return NULL;
    }

    return (&(TaskSlot->task));
}

/* deschedule for a bit... */
void
cfs_pause(cfs_duration_t ticks)
{
    cfs_schedule_timeout_and_set_state(CFS_TASK_UNINTERRUPTIBLE, ticks);
}

void
cfs_schedule_timeout_and_set_state(cfs_task_state_t state, int64_t time)
{
    cfs_task_t * task = cfs_current();
    PTASK_SLOT   slot = NULL;

    if (!task) {
        cfs_enter_debugger();
        return;
    }

    slot = CONTAINING_RECORD(task, TASK_SLOT, task);
    cfs_assert(slot->Magic == TASKSLT_MAGIC);

    if (time == CFS_MAX_SCHEDULE_TIMEOUT) {
        time = 0;
    }

    cfs_wait_event_internal(&(slot->Event), time);
}

void
cfs_schedule()
{
    cfs_schedule_timeout_and_set_state(CFS_TASK_UNINTERRUPTIBLE, 0);
}

int
wake_up_process(
    cfs_task_t * task
    )
{
    PTASK_SLOT   slot = NULL;

    if (!task) {
        cfs_enter_debugger();
        return 0;
    }

    slot = CONTAINING_RECORD(task, TASK_SLOT, task);
    cfs_assert(slot->Magic == TASKSLT_MAGIC);

    cfs_wake_event(&(slot->Event));

    return TRUE;
}

void
sleep_on(cfs_waitq_t *waitq)
{
	cfs_waitlink_t link;
	
	cfs_waitlink_init(&link);
	cfs_waitq_add(waitq, &link);
	cfs_waitq_wait(&link, CFS_TASK_INTERRUPTIBLE);
	cfs_waitq_del(waitq, &link);
}

EXPORT_SYMBOL(cfs_curproc_uid);
EXPORT_SYMBOL(cfs_curproc_pid);
EXPORT_SYMBOL(cfs_curproc_gid);
EXPORT_SYMBOL(cfs_curproc_fsuid);
EXPORT_SYMBOL(cfs_curproc_fsgid);
EXPORT_SYMBOL(cfs_curproc_umask);
EXPORT_SYMBOL(cfs_curproc_comm);
EXPORT_SYMBOL(cfs_curproc_groups_nr);
EXPORT_SYMBOL(cfs_curproc_groups_dump);
EXPORT_SYMBOL(cfs_curproc_is_in_groups);
EXPORT_SYMBOL(cfs_cap_raise);
EXPORT_SYMBOL(cfs_cap_lower);
EXPORT_SYMBOL(cfs_cap_raised);
EXPORT_SYMBOL(cfs_curproc_cap_pack);
EXPORT_SYMBOL(cfs_curproc_cap_unpack);
EXPORT_SYMBOL(cfs_capable);

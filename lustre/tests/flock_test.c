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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>

void chd_lock_unlock(int);
char fname[1024];

int main(int argc, char **argv)
{
    pid_t pid;
    int cfd, fd, rc;

    if (argc != 2) {
        fprintf(stderr, "\nUSAGE: flock_test filepath\n");
        exit(2);
    }
    strncpy(fname, argv[1], 1023);
    fname[1023] ='\0';
    fd = open(fname, O_RDWR|O_CREAT, (mode_t)0666);
    if (fd == -1) {
        fprintf(stderr, "flock_test: failed to open %s : ", fname);
        perror("");
        exit(1);
    }
    if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
        fprintf(stderr, "flock_test: parent attempt to lock %s failed : ", \
            fname);
        perror("");
        exit(1);
    }

    pid = fork();
    if (pid == -1) {
        fprintf(stderr, "flock_test: fork failed : ");
        perror("");
        exit(1);
    }

    if (pid == 0) {
        pid = getpid();
        sleep(2);
        if ((cfd = open(fname, O_RDWR)) == -1) {
            fprintf(stderr, "flock_test child (%d) cannot open %s: ", \
                pid, fname);
            perror("");
            exit(1);
        }
        if(flock(cfd, LOCK_EX | LOCK_NB) != -1) {
            fprintf(stderr, "flock_test child (%d): %s not yet locked  : ", \
                pid, fname);
            exit(1);
        }
        if(flock(fd, LOCK_UN) == -1) {
            fprintf(stderr, "flock_test child (%d): cannot unlock %s: ", \
                pid, fname);
            perror("");
            exit(1);
        }
        if(flock(cfd, LOCK_EX | LOCK_NB) == -1 ) {
            fprintf(stderr, \
                "flock_test: child (%d) cannot re-lock %s after unlocking : ", \
                pid, fname);
            perror("");
            exit(1);
        }
        close(cfd);
        exit(0);
    }

    waitpid(pid, &rc, 0);
    close(fd);
    unlink(fname);
    if (WIFEXITED(rc) && WEXITSTATUS(rc) != 0) {
        fprintf(stderr, "flock_test: child (%d) exit code = %d\n", \
            pid, WEXITSTATUS(rc));
        exit(1);
    }
    exit(0);
}

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

/* for O_DIRECTORY */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>

char fname[1024];


int main(int argc, char **argv)
{
        char *dname1;
        int fddir1, rc;
        int fd;

        if (argc != 2) {
                fprintf(stderr, "usage: %s dirname1\n", argv[0]);
                exit(1);
        }

        dname1 = argv[1];

        //create the directory
        fprintf(stderr, "creating directory %s\n", dname1);
        rc = mkdir(dname1, 0744);
        if (rc == -1) {
                fprintf(stderr, "creating %s fails: %s\n",
                        dname1, strerror(errno));
                exit(1);
        }

        sprintf(fname, "%s/0", dname1);
        fprintf(stderr, "creating file %s\n", fname);
        fd = creat(fname, 0666);
        if (fd < 0) {
                fprintf(stderr, "creation %s fails: %s\n",
                        fname, strerror(errno));
                exit(1);
        }
        close(fd);

        // open the dir again
        fprintf(stderr, "opening directory\n");
        fddir1 = open(dname1, O_RDONLY | O_DIRECTORY);
        if (fddir1 == -1) {
                fprintf(stderr, "open %s fails: %s\n",
                        dname1, strerror(errno));
                exit(1);
        }

        // delete the dir
        fprintf(stderr, "unlinking %s\n", dname1);
        rc = rmdir(dname1);
        if (rc == 0) {
                fprintf(stderr, "unlinked non-empty %s successfully\n",
                        dname1);
                exit(1);
        }

        if (access(dname1, F_OK) != 0){
                fprintf(stderr, "can't access %s: %s\n",
                        dname1, strerror(errno));
                exit(1);
        }

        fprintf(stderr, "Ok, everything goes well.\n");
        return 0;
}

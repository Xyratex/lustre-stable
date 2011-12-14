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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/tests/write_disjoint.c
 *
 * Each loop does 3 things:
 *   - rank 0 truncates to 0
 *   - all ranks agree on a random chunk size
 *   - all ranks race to write their pattern to their chunk of the file
 *   - rank 0 makes sure that the resulting file size is ranks * chunk size
 *   - rank 0 makes sure that everyone's patterns went to the right place
 *
 * compile: mpicc -g -Wall -o write_disjoint write_disjoint.c
 * run:     mpirun -np N -machlist <hostlist file> write_disjoint
 *  or:     pdsh -w <N hosts> write_disjoint
 *  or:     prun -n N [-N M] write_disjoint
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include "mpi.h"

#define CHUNK_MAX_SIZE 123456

void usage()
{
        printf("usage: write_disjoint [-c chunk_max_size] [-i] [-l]\n"
               "\t\t[-n numloops] [-s] [-t] [-f <filename>] \n");
        printf("\t-c chunk_max_size: maximum size of data chunk, default %u bytes\n",
               CHUNK_MAX_SIZE);
        printf("\t-i do not ignore write() EINTR error\n");
        printf("\t-l reopen file between write/read\n");
        printf("\t-n numloops: count of loops to run, default %u\n", 1000);
        printf("\t-s skip stat()\n");
        printf("\t-t do not randomize the chunk size, use default %u, or chunk_max_size if set by -c\n",
               CHUNK_MAX_SIZE);
        printf("\t-f filename\n");

        MPI_Finalize();
        exit(1);
}

void rprintf_abort(int rank, int loop, const char *fmt, ...)
{
        va_list       ap;

        printf("rank %d, loop %d: ", rank, loop);

        va_start(ap, fmt);

        vprintf(fmt, ap);

        MPI_Abort(MPI_COMM_WORLD, -1); /* This will exit() according to man */
}

#define CHUNK_SIZE(n) chunk_size[(n) % 2]

int main (int argc, char *argv[]) {
        int i, n, c, fd = 0;
        unsigned long chunk_size[2];
        int rank, noProcessors, done;
        int error;
        off_t offset;
        char **chunk_buf;
        char *read_buf;
        struct stat stat_buf;
        ssize_t ret;
        char *filename = "/mnt/lustre/write_disjoint";
        int numloops = 1000;
        int randomval = 0;
        int chunk_max_size = CHUNK_MAX_SIZE;
        int norandomize = 0;
        int reopen = 0;
        int nostat = 0;
        int dont_ignore_eintr = 0;

        error = MPI_Init(&argc, &argv);
        if (error != MPI_SUCCESS)
                rprintf_abort(-1, -1, "MPI_Init failed: %d\n", error);

        /* Parse command line options */
        while ((c = getopt(argc, argv, "c:f:iln:st")) != EOF) {
                switch (c) {
                case 'c':
                        chunk_max_size = strtoul(optarg, NULL, 0);
                        break;
                case 'f':
                        filename = optarg;
                        break;
                case 'i':
                        dont_ignore_eintr++;
                        break;
                case 'l':
                        reopen++;
                        break;
                case 'n':
                        numloops = strtoul(optarg, NULL, 0);
                        break;
                case 's':
                        nostat++;
                        break;
                case 't':
                        norandomize++;
                        break;
                default:
                        fprintf(stderr, "unknown option '%c'\n", c);
                        usage();

                }
        }

        MPI_Comm_size(MPI_COMM_WORLD, &noProcessors);
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);

        chunk_buf = malloc(noProcessors * sizeof(chunk_buf[0]));
        if (chunk_buf == NULL)
                rprintf_abort(rank, -1, "malloc() chunk_buf returned %s\n",
                              strerror(errno));

        for (i=0; i < noProcessors; i++) {
                chunk_buf[i] = malloc(chunk_max_size);
                if (chunk_buf[i] == NULL)
                        rprintf_abort(rank, -1,
                                      "malloc() chunk_buf[%d] returned %s\n",
                                      i, strerror(errno));

                memset(chunk_buf[i], 'A'+ i, chunk_max_size);
        }

        read_buf = malloc(noProcessors * chunk_max_size);
        if (read_buf == NULL)
                rprintf_abort(rank, -1, "malloc() read_buf returned %s\n",
                              strerror(errno));

        if (rank == 0) {
                fd = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0666);
                if (fd < 0)
                        rprintf_abort(rank, -1, "open() returned %s\n",
                                      strerror(errno));
        }
        MPI_Barrier(MPI_COMM_WORLD);
        if (rank != 0) {
                fd = open(filename, O_RDWR);
                if (fd < 0)
                        rprintf_abort(rank, -1, "open() returned %s\n",
                                      strerror(errno));
        }

        for (n = 0; n < numloops; n++) {
                /* reset the environment */
                if (rank == 0) {
                        ret = truncate(filename, 0);
                        if (ret != 0)
                                rprintf_abort(rank, n,
                                              "truncate() returned %s\n",
                                              strerror(errno));
                        randomval = rand();
                }
                MPI_Bcast(&randomval, 1, MPI_INT, 0, MPI_COMM_WORLD);
                CHUNK_SIZE(n) = norandomize ? chunk_max_size :
                                (randomval % chunk_max_size);

                if (n % 1000 == 0 && rank == 0)
                        printf("loop %d: chunk_size %lu\n", n, CHUNK_SIZE(n));

                if (!nostat) {
                        if (stat(filename, &stat_buf) < 0)
                                rprintf_abort(rank, n, "error stating %s: %s\n",
                                              filename, strerror(errno));

                        if (stat_buf.st_size != 0)
                                rprintf_abort(rank, n, "filesize = %lu. "
                                              "Should be zero after truncate\n",
                                              stat_buf.st_size);
                }

                MPI_Barrier(MPI_COMM_WORLD);

                /* Do the race */
                offset = rank * CHUNK_SIZE(n);

                if (lseek(fd, offset, SEEK_SET) < 0)
                        rprintf_abort(rank, n, "error seeking to 0: %s\n",
                                      strerror(errno));

                done = 0;
                do {
                        ret = write(fd, chunk_buf[rank] + done,
                                    CHUNK_SIZE(n) - done);
                        if (ret < 0 && (dont_ignore_eintr || errno != EINTR))
                                rprintf_abort(rank, n, "write() returned %s\n",
                                              strerror(errno));
                        if (ret > 0)
                                done += ret;
                } while (done != CHUNK_SIZE(n));

                if (reopen) {
                         close(fd);
                         MPI_Barrier(MPI_COMM_WORLD);
                         fd = open(filename, O_RDWR);
                         if (fd < 0)
                                rprintf_abort(rank, n, "open() returned %s\n",
                                              strerror(errno));
                } else
                         MPI_Barrier(MPI_COMM_WORLD);

                /* Check the result */
                 if (!nostat) {
                        if (stat(filename, &stat_buf) < 0)
                                rprintf_abort(rank, n, "error stating %s: %s\n",
                                              filename, strerror(errno));

                        if (stat_buf.st_size != CHUNK_SIZE(n) * noProcessors) {
                                if (n > 0)
                                        printf("loop %d: chunk_size %lu, "
                                               "file size was %lu\n",
                                               n - 1, CHUNK_SIZE(n - 1),
                                               CHUNK_SIZE(n - 1) *noProcessors);
                                rprintf_abort(rank, n, "invalid file size %lu"
                                              " instead of %lu = %lu * %u\n",
                                              (unsigned long)stat_buf.st_size,
                                              CHUNK_SIZE(n) * noProcessors,
                                              CHUNK_SIZE(n), noProcessors);
                        }
                }

                if (rank == 0) {
                        if (lseek(fd, 0, SEEK_SET) < 0)
                                rprintf_abort(rank, n,
                                              "error seeking to 0: %s\n",
                                              strerror(errno));

                        done = 0;
                        do {
                                ret = read(fd, read_buf + done,
                                           CHUNK_SIZE(n) * noProcessors - done);
                                if (ret < 0)
                                        rprintf_abort(rank, n,
                                                      "read returned %s\n",
                                                      strerror(errno));

                                done += ret;
                        } while (done != CHUNK_SIZE(n) * noProcessors);

                        for (i = 0; i < noProcessors; i++) {
                                char command[4096];
                                int j, rc;
                                if (!memcmp(read_buf + (i * CHUNK_SIZE(n)),
                                            chunk_buf[i], CHUNK_SIZE(n)))
                                        continue;

                                /* print out previous chunk sizes */
                                if (n > 0)
                                        printf("loop %d: chunk_size %lu\n",
                                               n - 1, CHUNK_SIZE(n - 1));

                                printf("loop %d: chunk %d corrupted "
                                       "with chunk_size %lu, page_size %d\n",
                                       n, i, CHUNK_SIZE(n), getpagesize());
                                printf("ranks:\tpage boundry\tchunk boundry\t"
                                       "page boundry\n");
                                for (j = 1 ; j < noProcessors; j++) {
                                        int b = j * CHUNK_SIZE(n);
                                        printf("%c -> %c:\t%d\t%d\t%d\n",
                                               'A' + j - 1, 'A' + j,
                                               b & ~(getpagesize()-1), b,
                                               (b + getpagesize()) &
                                               ~(getpagesize()-1));
                                }

                                sprintf(command, "od -Ad -a %s", filename);
                                rc = system(command);
                                rprintf_abort(0, n,
                                              "data check error - exiting\n");
                        }
                }
                MPI_Barrier(MPI_COMM_WORLD);
        }

        close(fd);

        printf("Finished after %d loops\n", n);
        MPI_Finalize();
        return 0;
}

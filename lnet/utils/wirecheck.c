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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE     /* for strnlen() in string.h */
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <lnet/lib-lnet.h>

#include <string.h>

#ifndef HAVE_STRNLEN
#define strnlen(s, i) strlen(s)
#endif

#define BLANK_LINE()                            \
do {                                            \
        printf ("\n");                          \
} while (0)

#define COMMENT(c)                              \
do {                                            \
        printf ("        /* "c" */\n");         \
} while (0)

#define STRINGIFY(a) #a

#define CHECK_DEFINE(a)                                                 \
do {                                                                    \
        printf ("        CLASSERT ("#a" == "STRINGIFY(a)");\n");        \
} while (0)

#define CHECK_VALUE(a)                                  \
do {                                                    \
        printf ("        CLASSERT ("#a" == %d);\n", a); \
} while (0)

#define CHECK_MEMBER_OFFSET(s,m)                \
do {                                            \
        CHECK_VALUE((int)offsetof(s, m));       \
} while (0)

#define CHECK_MEMBER_SIZEOF(s,m)                \
do {                                            \
        CHECK_VALUE((int)sizeof(((s *)0)->m));  \
} while (0)

#define CHECK_MEMBER(s,m)                       \
do {                                            \
        CHECK_MEMBER_OFFSET(s, m);              \
        CHECK_MEMBER_SIZEOF(s, m);              \
} while (0)

#define CHECK_STRUCT(s)                         \
do {                                            \
        BLANK_LINE ();                          \
        COMMENT ("Checks for struct "#s);       \
        CHECK_VALUE((int)sizeof(s));            \
} while (0)

void
check_lnet_handle_wire (void)
{
        CHECK_STRUCT (lnet_handle_wire_t);
        CHECK_MEMBER (lnet_handle_wire_t, wh_interface_cookie);
        CHECK_MEMBER (lnet_handle_wire_t, wh_object_cookie);
}

void
check_lnet_magicversion (void)
{
        CHECK_STRUCT (lnet_magicversion_t);
        CHECK_MEMBER (lnet_magicversion_t, magic);
        CHECK_MEMBER (lnet_magicversion_t, version_major);
        CHECK_MEMBER (lnet_magicversion_t, version_minor);
}

void
check_lnet_hdr (void)
{
        CHECK_STRUCT (lnet_hdr_t);
        CHECK_MEMBER (lnet_hdr_t, dest_nid);
        CHECK_MEMBER (lnet_hdr_t, src_nid);
        CHECK_MEMBER (lnet_hdr_t, dest_pid);
        CHECK_MEMBER (lnet_hdr_t, src_pid);
        CHECK_MEMBER (lnet_hdr_t, type);
        CHECK_MEMBER (lnet_hdr_t, payload_length);
        CHECK_MEMBER (lnet_hdr_t, msg);

        BLANK_LINE ();
        COMMENT ("Ack");
        CHECK_MEMBER (lnet_hdr_t, msg.ack.dst_wmd);
        CHECK_MEMBER (lnet_hdr_t, msg.ack.match_bits);
        CHECK_MEMBER (lnet_hdr_t, msg.ack.mlength);

        BLANK_LINE ();
        COMMENT ("Put");
        CHECK_MEMBER (lnet_hdr_t, msg.put.ack_wmd);
        CHECK_MEMBER (lnet_hdr_t, msg.put.match_bits);
        CHECK_MEMBER (lnet_hdr_t, msg.put.hdr_data);
        CHECK_MEMBER (lnet_hdr_t, msg.put.ptl_index);
        CHECK_MEMBER (lnet_hdr_t, msg.put.offset);

        BLANK_LINE ();
        COMMENT ("Get");
        CHECK_MEMBER (lnet_hdr_t, msg.get.return_wmd);
        CHECK_MEMBER (lnet_hdr_t, msg.get.match_bits);
        CHECK_MEMBER (lnet_hdr_t, msg.get.ptl_index);
        CHECK_MEMBER (lnet_hdr_t, msg.get.src_offset);
        CHECK_MEMBER (lnet_hdr_t, msg.get.sink_length);

        BLANK_LINE ();
        COMMENT ("Reply");
        CHECK_MEMBER (lnet_hdr_t, msg.reply.dst_wmd);

        BLANK_LINE ();
        COMMENT ("Hello");
        CHECK_MEMBER (lnet_hdr_t, msg.hello.incarnation);
        CHECK_MEMBER (lnet_hdr_t, msg.hello.type);
}

void
system_string (char *cmdline, char *str, int len)
{
        int   fds[2];
        int   rc;
        pid_t pid;

        rc = pipe (fds);
        if (rc != 0)
                abort ();

        pid = fork ();
        if (pid == 0) {
                /* child */
                int   fd = fileno(stdout);

                rc = dup2(fds[1], fd);
                if (rc != fd)
                        abort();

                exit(system(cmdline));
                /* notreached */
        } else if ((int)pid < 0) {
                abort();
        } else {
                FILE *f = fdopen (fds[0], "r");

                if (f == NULL)
                        abort();

                close(fds[1]);

                if (fgets(str, len, f) == NULL)
                        abort();

                if (waitpid(pid, &rc, 0) != pid)
                        abort();

                if (!WIFEXITED(rc) ||
                    WEXITSTATUS(rc) != 0)
                        abort();

                if (strnlen(str, len) == len)
                        str[len - 1] = 0;

                if (str[strlen(str) - 1] == '\n')
                        str[strlen(str) - 1] = 0;

                fclose(f);
        }
}

static void check_lnet_cap()
{
        CLASSERT(CFS_CAP_CHOWN                  == 0);
        CLASSERT(CFS_CAP_DAC_OVERRIDE           == 1);
        CLASSERT(CFS_CAP_DAC_READ_SEARCH        == 2);
        CLASSERT(CFS_CAP_FOWNER                 == 3);
        CLASSERT(CFS_CAP_FSETID                 == 4);
        CLASSERT(CFS_CAP_LINUX_IMMUTABLE        == 9);
        CLASSERT(CFS_CAP_SYS_ADMIN              == 21);
        CLASSERT(CFS_CAP_SYS_BOOT               == 23);
        CLASSERT(CFS_CAP_SYS_RESOURCE           == 24);
}

int
main (int argc, char **argv)
{
        char unameinfo[256];
        char gccinfo[256];

        system_string("uname -a", unameinfo, sizeof(unameinfo));
        system_string("gcc -v 2>&1 | tail -1", gccinfo, sizeof(gccinfo));

        printf ("void lnet_assert_wire_constants (void)\n"
                "{\n"
                "        /* Wire protocol assertions generated by 'wirecheck'\n"
                "         * running on %s\n"
                "         * with %s */\n"
                "\n", unameinfo, gccinfo);

        BLANK_LINE ();

        COMMENT ("Constants...");

        CHECK_DEFINE (LNET_PROTO_OPENIB_MAGIC);
        CHECK_DEFINE (LNET_PROTO_RA_MAGIC);

        CHECK_DEFINE (LNET_PROTO_TCP_MAGIC);
        CHECK_DEFINE (LNET_PROTO_TCP_VERSION_MAJOR);
        CHECK_DEFINE (LNET_PROTO_TCP_VERSION_MINOR);

        CHECK_VALUE (LNET_MSG_ACK);
        CHECK_VALUE (LNET_MSG_PUT);
        CHECK_VALUE (LNET_MSG_GET);
        CHECK_VALUE (LNET_MSG_REPLY);
        CHECK_VALUE (LNET_MSG_HELLO);

        check_lnet_handle_wire ();
        check_lnet_magicversion ();
        check_lnet_hdr ();
        check_lnet_cap();

        printf ("}\n\n");

        return (0);
}

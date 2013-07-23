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
 * lustre/utils/obdctl.c
 *
 * Author: Peter J. Braam <braam@clusterfs.com>
 * Author: Phil Schwan <phil@clusterfs.com>
 */

#include <stdlib.h>
#include <stdio.h>

#include "obdctl.h"
#include "parser.h"

/* the functions that were in here are now in obd.c */

static int jt_quit(int argc, char **argv)
{
        int rc = 0;
        Parser_quit(argc, argv);

        return rc;
}

command_t cmdlist[] = {
        /* Metacommands */
        {"--device", jt_opt_device, 0, "--device <devno> <command [args ...]>"},
        {"--threads", jt_opt_threads, 0,
         "--threads <threads> <devno> <command [args ...]>"},

        /* Device configuration commands */
        {"lov_setconfig", jt_obd_lov_setconfig, 0, "configure lov data on MDS "
         "[usage: lovconfig lov-uuid stripecount, stripesize, pattern, UUID1, [UUID2, ...]"},
        {"list", jt_obd_list, 0, "list the devices (no args)"},
        {"newdev", jt_obd_newdev, 0, "set device to a new unused obd (no args)"},
        {"device", jt_obd_device, 0, "set current device (args device_no name)"},
        {"name2dev", jt_obd_name2dev, 0,
         "set device by name [usage: name2dev devname]"},
        {"attach", jt_obd_attach, 0, "name the type of device (args: type data"},
        {"setup", jt_obd_setup, 0, "setup device (args: <blkdev> [data]"},
        {"detach", jt_obd_detach, 0, "detach the current device (arg: )"},
        {"cleanup", jt_obd_cleanup, 0, "cleanup the current device (arg: )"},

        /* Session commands */
        {"connect", jt_obd_connect, 0, "connect - get a connection to device"},
        {"disconnect", jt_obd_disconnect, 0,
         "disconnect - break connection to device"},

        /* Session operations */
        {"create", jt_obd_create, 0, "create <count> [mode [verbose]]"},
        {"destroy", jt_obd_destroy, 0, "destroy <id> [count [verbose]]"},
        {"getattr", jt_obd_getattr, 0, "getattr <id>"},
        {"setattr", jt_obd_setattr, 0, "setattr <id> <mode>"},
        {"newconn", jt_obd_newconn, 0, "newconn <olduuid> [newuuid]"},
        {"test_getattr", jt_obd_test_getattr, 0, "test_getattr <count> [verbose [[t]objid]]"},
        {"test_setattr", jt_obd_test_setattr, 0, "test_setattr <count> [verbose [[t]objid]]"},
        {"test_brw", jt_obd_test_brw, 0, "test_brw [t]<count> [write [verbose [pages [[t]objid]]]]"},
        {"dump_ldlm", jt_obd_dump_ldlm, 0, "dump all lock manager state (no args)"},

        /* User interface commands */
        {"help", Parser_help, 0, "help"},
        {"exit", jt_quit, 0, "quit"},
        {"quit", jt_quit, 0, "quit"},
        {0, 0, 0, NULL}
};


int main(int argc, char **argv)
{
        int rc;

        setlinebuf(stdout);

        if (obd_initialize(argc, argv) < 0)
                exit(1);

        Parser_init("obdctl > ", cmdlist);

        if (argc > 1) {
                rc = Parser_execarg(argc - 1, argv + 1, cmdlist);
        } else {
                rc = Parser_commands();
        }

        obd_finalize(argc, argv);
        return rc;
}

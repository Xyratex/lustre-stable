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
 * lustre/include/lustre_param.h
 *
 * User-settable parameter keys
 *
 * Author: Nathan Rutman <nathan@clusterfs.com>
 */

#ifndef _LUSTRE_PARAM_H
#define _LUSTRE_PARAM_H

/* obd_config.c */
int class_find_param(char *buf, char *key, char **valp);
int class_match_param(char *buf, char *key, char **valp);
int class_parse_nid(char *buf, lnet_nid_t *nid, char **endh);
/* obd_mount.c */
int do_lcfg(char *cfgname, lnet_nid_t nid, int cmd,
            char *s1, char *s2, char *s3, char *s4);



/****************** User-settable parameter keys *********************/
/* e.g.
        tunefs.lustre --param="failover.node=192.168.0.13@tcp0" /dev/sda
        lctl conf_param testfs-OST0000 failover.node=3@elan,192.168.0.3@tcp0
                    ... testfs-MDT0000.lov.stripesize=4M
                    ... testfs-OST0000.ost.client_cache_seconds=15
                    ... testfs.sys.timeout=<secs>
                    ... testfs.llite.max_read_ahead_mb=16
*/

/* System global or special params not handled in obd's proc
 * See mgs_write_log_sys()
 */
#define PARAM_TIMEOUT              "timeout="          /* global */
#define PARAM_LDLM_TIMEOUT         "ldlm_timeout="     /* global */
#define PARAM_AT_MIN               "at_min="           /* global */
#define PARAM_AT_MAX               "at_max="           /* global */
#define PARAM_AT_EXTRA             "at_extra="         /* global */
#define PARAM_AT_EARLY_MARGIN      "at_early_margin="  /* global */
#define PARAM_AT_HISTORY           "at_history="       /* global */
#define PARAM_MGSNODE              "mgsnode="          /* only at mounttime */
#define PARAM_FAILNODE             "failover.node="    /* add failover nid */
#define PARAM_FAILMODE             "failover.mode="    /* initial mount only */
#define PARAM_ACTIVE               "active="           /* activate/deactivate */
#define PARAM_MDT_UPCALL           "mdt.group_upcall=" /* mds group upcall */

/* Prefixes for parameters handled by obd's proc methods (XXX_process_config) */
#define PARAM_OST                  "ost."
#define PARAM_OSC                  "osc."
#define PARAM_MDT                  "mdt."
#define PARAM_MDC                  "mdc."
#define PARAM_LLITE                "llite."
#define PARAM_LOV                  "lov."
#define PARAM_SYS                  "sys."              /* global */

#endif /* _LUSTRE_PARAM_H */

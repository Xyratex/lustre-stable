/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright (C) 2002 Cluster File Systems, Inc.
 *
 *   This file is part of Lustre, http://www.lustre.org.
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Lustre; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _LUSTRE_NET_H
#define _LUSTRE_NET_H

#include <linux/kp30.h>
#include <linux/obd_support.h>
#include <linux/obd_class.h>
#include <portals/p30.h>
#include <linux/lustre_idl.h>

/* FOO_REQUEST_PORTAL is for incoming requests on the FOO
 * FOO_REPLY_PORTAL   is for incoming replies on the FOO
 * FOO_BULK_PORTAL    is for incoming bulk on the FOO
 */

#define CONNMGR_REQUEST_PORTAL    1
#define CONNMGR_REPLY_PORTAL      2
#define OSC_REQUEST_PORTAL      3
#define OSC_REPLY_PORTAL        4
#define OSC_BULK_PORTAL         5
#define OST_REQUEST_PORTAL      6
#define OST_REPLY_PORTAL        7
#define OST_BULK_PORTAL         8
#define MDC_REQUEST_PORTAL      9
#define MDC_REPLY_PORTAL        10
#define MDC_BULK_PORTAL         11
#define MDS_REQUEST_PORTAL      12
#define MDS_REPLY_PORTAL        13
#define MDS_BULK_PORTAL         14
#define LDLM_REQUEST_PORTAL     15
#define LDLM_REPLY_PORTAL       16
#define LDLM_CLI_REQUEST_PORTAL 17
#define LDLM_CLI_REPLY_PORTAL   18

/* default rpc ring length */
#define RPC_RING_LENGTH    2

#define SVC_STOPPING 1
#define SVC_RUNNING  2
#define SVC_STOPPED  4
#define SVC_KILLED   8
#define SVC_EVENT    16
#define SVC_HA_EVENT 32
#define SVC_SIGNAL   64

struct ptlrpc_connection {
        struct list_head c_link;
        struct lustre_peer c_peer;

        __u32 c_generation;  /* changes upon new connection */
        __u32 c_epoch;       /* changes when peer changes */
        __u32 c_bootcount;   /* peer's boot count */ 

        spinlock_t c_lock;
        __u32 c_xid_in;
        __u32 c_xid_out;

        atomic_t c_refcount;
        __u64 c_token;
        __u64 c_remote_conn;
        __u64 c_remote_token;
};

struct ptlrpc_client {
        struct obd_device *cli_obd;
        __u32 cli_request_portal;
        __u32 cli_reply_portal;

        struct semaphore cli_rpc_sem; /* limits outstanding requests */

        struct list_head cli_sending_head; /* lists protected by ha_mgr lock */
        struct list_head cli_sent_head;
        struct list_head cli_ha_item; 

        struct connmgr_obd *cli_ha_mgr;
};

/* These do double-duty in rq_type and rq_flags */
#define PTL_RPC_INTR    1
#define PTL_RPC_REQUEST 2
#define PTL_RPC_REPLY   3
#define PTL_RPC_BULK    4
#define PTL_RPC_SENT    5
#define PTL_BULK_SENT   6
#define PTL_BULK_RCVD   7
#define PTL_RPC_ERR     8

struct ptlrpc_request { 
        int rq_type; /* one of PTL_RPC_REQUEST, PTL_RPC_REPLY, PTL_RPC_BULK */
        spinlock_t rq_lock;
        struct list_head rq_list;
        struct obd_device *rq_obd;
        int rq_status;
        int rq_flags; 
        __u32 rq_connid;

        int rq_reqlen;
        struct lustre_msg *rq_reqmsg;

        int rq_replen;
        struct lustre_msg *rq_repmsg;

        char *rq_bulkbuf;
        int rq_bulklen;

        time_t rq_time;
        //        void * rq_reply_handle;
        wait_queue_head_t rq_wait_for_rep;

        /* incoming reply */
        ptl_md_t rq_reply_md;
        ptl_handle_md_t rq_reply_md_h;
        ptl_handle_me_t rq_reply_me_h;

        /* outgoing req/rep */
        ptl_md_t rq_req_md;
        ptl_handle_md_t rq_req_md_h;

        struct ptlrpc_connection *rq_connection;
        struct ptlrpc_client *rq_client;
};

struct ptlrpc_bulk_desc {
        int b_flags;
        struct ptlrpc_connection *b_connection;
        __u32 b_portal;
        char *b_buf;
        int b_buflen;
        int (*b_cb)(struct ptlrpc_bulk_desc *, void *);
        struct page *b_page;
        struct obd_conn b_conn;
        __u32 b_xid;

        wait_queue_head_t b_waitq;

        ptl_md_t b_md;
        ptl_handle_md_t b_md_h;
        ptl_handle_me_t b_me_h;
};

struct ptlrpc_service {
        time_t srv_time;
        time_t srv_timeout;

        /* incoming request buffers */
        /* FIXME: perhaps a list of EQs, if multiple NIs are used? */
        char *srv_buf[RPC_RING_LENGTH];
        __u32 srv_ref_count[RPC_RING_LENGTH];
        ptl_handle_me_t srv_me_h[RPC_RING_LENGTH];
        __u32 srv_buf_size;
        __u32 srv_ring_length;
        __u32 srv_req_portal;
        __u32 srv_rep_portal;

        __u32 srv_xid;

        /* event queue */
        ptl_handle_eq_t srv_eq_h;

        __u32 srv_flags; 
        struct lustre_peer srv_self;
        ptl_process_id_t srv_id;

        struct task_struct *srv_thread;
        wait_queue_head_t srv_waitq;
        wait_queue_head_t srv_ctl_waitq;

        spinlock_t srv_lock;
        struct list_head srv_reqs;
        ptl_event_t  srv_ev;
        int (*srv_handler)(struct obd_device *obddev, 
                           struct ptlrpc_service *svc,
                           struct ptlrpc_request *req);
};

typedef int (*svc_handler_t)(struct obd_device *obddev,
                             struct ptlrpc_service *svc,
                             struct ptlrpc_request *req);

/* rpc/connection.c */
struct ptlrpc_connection *ptlrpc_get_connection(struct lustre_peer *peer);
int ptlrpc_put_connection(struct ptlrpc_connection *c);
struct ptlrpc_connection *ptlrpc_connection_addref(struct ptlrpc_connection *);
void ptlrpc_init_connection(void);
void ptlrpc_cleanup_connection(void);

/* rpc/niobuf.c */
int ptlrpc_check_bulk_sent(struct ptlrpc_bulk_desc *);
int ptlrpc_send_bulk(struct ptlrpc_bulk_desc *, int portal);
int ptlrpc_register_bulk(struct ptlrpc_bulk_desc *);
int ptlrpc_abort_bulk(struct ptlrpc_bulk_desc *bulk);
int ptlrpc_reply(struct ptlrpc_service *svc, struct ptlrpc_request *req);
int ptlrpc_error(struct ptlrpc_service *svc, struct ptlrpc_request *req);
int ptl_send_rpc(struct ptlrpc_request *request);
void ptlrpc_link_svc_me(struct ptlrpc_service *service, int i);

/* rpc/client.c */
void ptlrpc_init_client(struct connmgr_obd *, int req_portal, int rep_portal,
                        struct ptlrpc_client *);
struct ptlrpc_connection *ptlrpc_uuid_to_connection(char *uuid);
int ptlrpc_queue_wait(struct ptlrpc_request *req);
struct ptlrpc_request *ptlrpc_prep_req(struct ptlrpc_client *cl,
                                       struct ptlrpc_connection *u, int opcode,
                                       int count, int *lengths, char **bufs);
void ptlrpc_free_bulk(struct ptlrpc_bulk_desc *bulk);
void ptlrpc_free_req(struct ptlrpc_request *request);
struct ptlrpc_bulk_desc *ptlrpc_prep_bulk(struct ptlrpc_connection *);
int ptlrpc_check_status(struct ptlrpc_request *req, int err);

/* rpc/service.c */
struct ptlrpc_service *
ptlrpc_init_svc(__u32 bufsize, int req_portal, int rep_portal, char *uuid,
                svc_handler_t);
void ptlrpc_stop_thread(struct ptlrpc_service *svc);
int ptlrpc_start_thread(struct obd_device *dev, struct ptlrpc_service *svc,
                        char *name);
int rpc_unregister_service(struct ptlrpc_service *service);

struct ptlrpc_svc_data { 
        char *name;
        struct ptlrpc_service *svc; 
        struct obd_device *dev;
}; 

/* rpc/pack_generic.c */
int lustre_pack_msg(int count, int *lens, char **bufs, int *len,
                    struct lustre_msg **msg);
int lustre_msg_size(int count, int *lengths);
int lustre_unpack_msg(struct lustre_msg *m, int len);
void *lustre_msg_buf(struct lustre_msg *m, int n);

#endif

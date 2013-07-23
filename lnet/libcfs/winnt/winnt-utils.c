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


/*
 * miscellaneous libcfs stuff
 */
#define DEBUG_SUBSYSTEM S_LNET
#include <lnet/types.h>

/*
 * Convert server error code to client format. Error codes are from
 * Linux errno.h, so for Linux client---identity.
 */
int convert_server_error(__u64 ecode)
{
	return cfs_error_code((NTSTATUS)ecode);
}

/*
 * convert <fcntl.h> flag from client to server.
 * 
 * nt kernel uses several members to describe the open flags
 * such as DesiredAccess/ShareAccess/CreateDisposition/CreateOptions
 * so it's better to convert when using, not here.
 */

int convert_client_oflag(int cflag, int *result)
{
    *result = 0;
	return 0;
}


int cfs_error_code(NTSTATUS Status)
{
    switch (Status) {

        case STATUS_ACCESS_DENIED:
            return (-EACCES);

        case STATUS_ACCESS_VIOLATION:
            return (-EFAULT);
    
        case STATUS_BUFFER_TOO_SMALL:
            return (-ETOOSMALL);

        case STATUS_INVALID_PARAMETER:
            return (-EINVAL);

        case STATUS_NOT_IMPLEMENTED:
        case STATUS_NOT_SUPPORTED:
            return (-EOPNOTSUPP);

        case STATUS_INVALID_ADDRESS:
        case STATUS_INVALID_ADDRESS_COMPONENT:
            return (-EADDRNOTAVAIL);

        case STATUS_NO_SUCH_DEVICE:
        case STATUS_NO_SUCH_FILE:
        case STATUS_OBJECT_NAME_NOT_FOUND:
        case STATUS_OBJECT_PATH_NOT_FOUND:  
        case STATUS_NETWORK_BUSY:
        case STATUS_INVALID_NETWORK_RESPONSE:
        case STATUS_UNEXPECTED_NETWORK_ERROR:
            return (-ENETDOWN);

        case STATUS_BAD_NETWORK_PATH:
        case STATUS_NETWORK_UNREACHABLE:
        case STATUS_PROTOCOL_UNREACHABLE:     
            return (-ENETUNREACH);

        case STATUS_LOCAL_DISCONNECT:
        case STATUS_TRANSACTION_ABORTED:
        case STATUS_CONNECTION_ABORTED:
            return (-ECONNABORTED);

        case STATUS_REMOTE_DISCONNECT:
        case STATUS_LINK_FAILED:
        case STATUS_CONNECTION_DISCONNECTED:
        case STATUS_CONNECTION_RESET:
        case STATUS_PORT_UNREACHABLE:
            return (-ECONNRESET);

        case STATUS_PAGEFILE_QUOTA:
        case STATUS_NO_MEMORY:
        case STATUS_CONFLICTING_ADDRESSES:
        case STATUS_QUOTA_EXCEEDED:
        case STATUS_TOO_MANY_PAGING_FILES:
        case STATUS_INSUFFICIENT_RESOURCES:
        case STATUS_WORKING_SET_QUOTA:
        case STATUS_COMMITMENT_LIMIT:
        case STATUS_TOO_MANY_ADDRESSES:
        case STATUS_REMOTE_RESOURCES:
            return (-ENOBUFS);

        case STATUS_INVALID_CONNECTION:
            return (-ENOTCONN);

        case STATUS_PIPE_DISCONNECTED:
            return (-ESHUTDOWN);

        case STATUS_TIMEOUT:
        case STATUS_IO_TIMEOUT:
        case STATUS_LINK_TIMEOUT:
            return (-ETIMEDOUT);

        case STATUS_REMOTE_NOT_LISTENING:
        case STATUS_CONNECTION_REFUSED:
            return (-ECONNREFUSED);

        case STATUS_HOST_UNREACHABLE:
            return (-EHOSTUNREACH);

        case STATUS_PENDING:
        case STATUS_DEVICE_NOT_READY:
            return (-EAGAIN);

        case STATUS_CANCELLED:
        case STATUS_REQUEST_ABORTED:
            return (-EINTR);

        case STATUS_BUFFER_OVERFLOW:
        case STATUS_INVALID_BUFFER_SIZE:
            return (-EMSGSIZE);

    }

    if (NT_SUCCESS(Status)) 
        return 0;

    return (-EINVAL);
}


void cfs_stack_trace_fill(struct cfs_stack_trace *trace)
{
}

void *cfs_stack_trace_frame(struct cfs_stack_trace *trace, int frame_no)
{
    return NULL;
}

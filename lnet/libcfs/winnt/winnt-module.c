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


#define DEBUG_SUBSYSTEM S_LIBCFS

#include <libcfs/libcfs.h>
#include <libcfs/kp30.h>

#define LIBCFS_MINOR 240

int libcfs_ioctl_getdata(char *buf, char *end, void *arg)
{
        struct libcfs_ioctl_hdr *hdr;
        struct libcfs_ioctl_data *data;
        int err;
        ENTRY;

        hdr = (struct libcfs_ioctl_hdr *)buf;
        data = (struct libcfs_ioctl_data *)buf;

        err = copy_from_user(buf, (void *)arg, sizeof(*hdr));
        if (err)
                RETURN(err);

        if (hdr->ioc_version != LIBCFS_IOCTL_VERSION) {
                CERROR(("LIBCFS: version mismatch kernel vs application\n"));
                RETURN(-EINVAL);
        }

        if (hdr->ioc_len + buf >= end) {
                CERROR(("LIBCFS: user buffer exceeds kernel buffer\n"));
                RETURN(-EINVAL);
        }

        if (hdr->ioc_len < sizeof(struct libcfs_ioctl_data)) {
                CERROR(("LIBCFS: user buffer too small for ioctl\n"));
                RETURN(-EINVAL);
        }

        err = copy_from_user(buf, (void *)arg, hdr->ioc_len);
        if (err)
                RETURN(err);

        if (libcfs_ioctl_is_invalid(data)) {
                CERROR(("LIBCFS: ioctl not correctly formatted\n"));
                RETURN(-EINVAL);
        }

        if (data->ioc_inllen1)
                data->ioc_inlbuf1 = &data->ioc_bulk[0];

        if (data->ioc_inllen2)
                data->ioc_inlbuf2 = &data->ioc_bulk[0] +
                        size_round(data->ioc_inllen1);

        RETURN(0);
}
                                                                                                                                                                        
extern struct cfs_psdev_ops          libcfs_psdev_ops;

static int 
libcfs_psdev_open(cfs_file_t * file)
{ 
	struct libcfs_device_userstate **pdu = NULL;
	int    rc = 0;

	pdu = (struct libcfs_device_userstate **)&file->private_data;
	if (libcfs_psdev_ops.p_open != NULL)
		rc = libcfs_psdev_ops.p_open(0, (void *)pdu);
	else
		return (-EPERM);
	return rc;
}

/* called when closing /dev/device */
static int 
libcfs_psdev_release(cfs_file_t * file)
{
	struct libcfss_device_userstate *pdu;
	int    rc = 0;

	pdu = file->private_data;
	if (libcfs_psdev_ops.p_close != NULL)
		rc = libcfs_psdev_ops.p_close(0, (void *)pdu);
	else
		rc = -EPERM;
	return rc;
}

static int 
libcfs_ioctl(cfs_file_t * file, unsigned int cmd, ulong_ptr arg)
{ 
	struct cfs_psdev_file	 pfile;
	int    rc = 0;

	if ( _IOC_TYPE(cmd) != IOC_LIBCFS_TYPE || 
	     _IOC_NR(cmd) < IOC_LIBCFS_MIN_NR  || 
	     _IOC_NR(cmd) > IOC_LIBCFS_MAX_NR ) { 
		CDEBUG(D_IOCTL, ("invalid ioctl ( type %d, nr %d, size %d )\n", 
		       _IOC_TYPE(cmd), _IOC_NR(cmd), _IOC_SIZE(cmd))); 
		return (-EINVAL); 
	} 
	
	/* Handle platform-dependent IOC requests */
	switch (cmd) { 
	case IOC_LIBCFS_PANIC: 
		if (!cfs_capable(CFS_CAP_SYS_BOOT)) 
			return (-EPERM); 
		CERROR(("debugctl-invoked panic"));
        KeBugCheckEx('LUFS', (ULONG_PTR)libcfs_ioctl, (ULONG_PTR)NULL, (ULONG_PTR)NULL, (ULONG_PTR)NULL);

		return (0);
	case IOC_LIBCFS_MEMHOG:

		if (!cfs_capable(CFS_CAP_SYS_ADMIN)) 
			return -EPERM;
        break;
	}

	pfile.off = 0;
	pfile.private_data = file->private_data;
	if (libcfs_psdev_ops.p_ioctl != NULL) 
		rc = libcfs_psdev_ops.p_ioctl(&pfile, cmd, (void *)arg); 
	else
		rc = -EPERM;
	return (rc);
}

static struct file_operations libcfs_fops = {
    /* lseek: */  NULL,
    /* read: */   NULL,
    /* write: */  NULL,
    /* ioctl: */  libcfs_ioctl,
    /* open: */   libcfs_psdev_open,
    /* release:*/ libcfs_psdev_release
};

cfs_psdev_t libcfs_dev = { 
	LIBCFS_MINOR, 
	"lnet", 
	&libcfs_fops
};

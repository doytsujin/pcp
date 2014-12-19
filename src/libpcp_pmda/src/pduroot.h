/*
 * PDUs for elevated privilege service (pmdaroot) communication.
 *
 * Copyright (c) 2014 Red Hat.
 * 
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 */
#ifndef _PDUROOT_H
#define _PDUROOT_H

typedef struct {
    __pmdaRootPDUHdr	hdr;
    int			features;
    int			zeroed;
} __pmdaRootPDUInfo;

typedef struct {
    __pmdaRootPDUHdr	hdr;
    int			flags;
    int			namelen;
    char		name[0];
} __pmdaRootPDUNameSpaceFdsReq;

typedef struct {
    __pmdaRootPDUHdr	hdr;
    int			pid;
    /* file descriptors arrive via SCM_RIGHTS */
} __pmdaRootPDUNameSpaceFds;

#endif	/* _PDUROOT_H */

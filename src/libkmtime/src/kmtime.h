/*
 * Copyright (C) 2006-2007 Aconex.  All Rights Reserved.
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
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 */
#ifndef KMTIME_H
#define KMTIME_H

#include <sys/time.h>

typedef enum {
    KM_TCTL_SET		= (1<<0),	// client -> server
    KM_TCTL_STEP	= (1<<1),	// server -> clients
    KM_TCTL_TZ		= (1<<2),	// server -> clients
    KM_TCTL_VCRMODE	= (1<<3),	// server -> clients
    KM_TCTL_VCRMODE_DRAG= (1<<4),	// server -> clients
    KM_TCTL_GUISHOW	= (1<<5),	// client -> server
    KM_TCTL_GUIHIDE	= (1<<6),	// client -> server
    KM_TCTL_BOUNDS	= (1<<7),	// client -> server
    KM_TCTL_ACK		= (1<<8),	// client -> server (except handshake)
    KM_TCTL_GUISTYLE	= (1<<9),	// client <-> server (both ways)
} km_tctl_command;

typedef enum {
    KM_STATE_STOP	= 0,
    KM_STATE_FORWARD	= 1,
    KM_STATE_BACKWARD	= 2,
} km_tctl_state;

typedef enum {
    KM_MODE_STEP	= 0,
    KM_MODE_NORMAL	= 1,
    KM_MODE_FAST	= 2,
} km_tctl_mode;

typedef enum {
    KM_SOURCE_NONE	= -1,
    KM_SOURCE_HOST	= 0,
    KM_SOURCE_ARCHIVE	= 1,
} km_tctl_source;

#define KMTIME_MAGIC	0x54494D45	/* "TIME" */

typedef struct {
    unsigned int	magic;
    unsigned int	length;
    km_tctl_command	command;
    km_tctl_source	source;
    km_tctl_state	state;
    km_tctl_mode	mode;
    struct timeval	delta;
    struct timeval	position;
    struct timeval	start;		/* archive only */
    struct timeval	end;		/* archive only */
    char		data[0];	/* arbitrary length info ($TZ/style) */
} kmTime;

extern int kmTimeSendAck(int, struct timeval *);
extern int kmTimeConnect(int, kmTime *);
extern int kmTimeShowDialog(int, int);
extern int kmTimeRecv(int, kmTime **);

#endif	/* KMTIME_H */

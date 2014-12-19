/*
 * Copyright (c) 2014 Red Hat.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#ifndef _ROOT_H
#define _ROOT_H

enum {
    CONTAINERS_INDOM,
    NUM_INDOMS
};

enum {
    CONTAINERS_DRIVER,
    CONTAINERS_NAME,
    CONTAINERS_PID,
    CONTAINERS_RUNNING,
    CONTAINERS_PAUSED,
    CONTAINERS_RESTARTING,
    NUM_METRICS
};

/*
 * General container services, abstracting individual implementations into
 * "drivers" which are then instantiated one-per-container-technology.
 */

struct stat;
struct container;
struct container_driver;

typedef void (*container_setup_t)(struct container_driver *);
typedef int (*container_changed_t)(struct container_driver *);
typedef void (*container_insts_t)(struct container_driver *, pmInDom);
typedef int (*container_values_t)(struct container_driver *,
		const char *, struct container *);
typedef int (*container_match_t)(struct container_driver *,
		const char *, const char *, const char *);

typedef struct container_driver {
    char		*name;
    int			state;
    char		path[60];
    container_setup_t	setup;
    container_changed_t	indom_changed;
    container_insts_t	insts_refresh;
    container_values_t	value_refresh;
    container_match_t	name_matching;
} container_driver_t;

typedef struct container {
    int			pid;
    int			status;
    char		*name;
    struct stat		stat;
    container_driver_t	*driver;
} container_t;

enum {
    CONTAINER_FLAG_RUNNING	= (1<<0),
    CONTAINER_FLAG_PAUSED	= (1<<1),
    CONTAINER_FLAG_RESTARTING	= (1<<2),
};

extern int root_stat_time_differs(struct stat *, struct stat *);

#endif

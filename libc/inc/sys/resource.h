/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Ghost, a micro-kernel based operating system for the x86 architecture    *
 *  Copyright (C) 2015, Max Schlüssel <lokoxe@gmail.com>                     *
 *                                                                           *
 *  This program is free software: you can redistribute it and/or modify     *
 *  it under the terms of the GNU General Public License as published by     *
 *  the Free Software Foundation, either version 3 of the License, or        *
 *  (at your option) any later version.                                      *
 *                                                                           *
 *  This program is distributed in the hope that it will be useful,          *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU General Public License for more details.                             *
 *                                                                           *
 *  You should have received a copy of the GNU General Public License        *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.    *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef __GHOST_LIBC_SYS_RESOURCE__
#define __GHOST_LIBC_SYS_RESOURCE__

#include "ghost/common.h"
#include "sys/time.h"
#include "sys/types.h"

__BEGIN_C

struct rusage {
	struct timeval ru_utime;
	struct timeval ru_stime;
};

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN -1

#define PRIO_PROCESS 0
#define PRIO_PGRP    1
#define PRIO_USER    2

#define PRIO_MIN (-20)
#define PRIO_MAX 20

int getpriority(int which, id_t who);
int setpriority(int which, id_t who, int prio);

__END_C

#endif

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

#ifndef __GHOST_LIBC_SYS_TIME__
#define __GHOST_LIBC_SYS_TIME__

#include "ghost/common.h"
#include <time.h>

__BEGIN_C

struct timeval {
	time_t tv_sec;
	suseconds_t tv_usec;
};

#define timerclear(tvp) do { \
	(tvp)->tv_sec = 0; \
	(tvp)->tv_usec = 0; \
} while(0)

#define timerisset(tvp) ((tvp)->tv_sec || (tvp)->tv_usec)

#define timercmp(a, b, cmp) (((a)->tv_sec == (b)->tv_sec) ? \
	((a)->tv_usec cmp (b)->tv_usec) : ((a)->tv_sec cmp (b)->tv_sec))

#define timeradd(a, b, result) do { \
	(result)->tv_sec = (a)->tv_sec + (b)->tv_sec; \
	(result)->tv_usec = (a)->tv_usec + (b)->tv_usec; \
	if((result)->tv_usec >= 1000000) { \
		++(result)->tv_sec; \
		(result)->tv_usec -= 1000000; \
	} \
} while(0)

#define timersub(a, b, result) do { \
	long long __usec = (long long)(a)->tv_usec - (long long)(b)->tv_usec; \
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
	if(__usec < 0) { \
		--(result)->tv_sec; \
		__usec += 1000000; \
	} \
	(result)->tv_usec = (suseconds_t)__usec; \
} while(0)

int gettimeofday(struct timeval* tp, void* tzp);

__END_C

#endif

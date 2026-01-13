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

#include "stdio.h"
#include "stdlib.h"
#include "errno.h"

int vasprintf(char** strp, const char* format, va_list arg) {
	if(!strp || !format) {
		errno = EINVAL;
		return -1;
	}

	va_list ap_copy;
	va_copy(ap_copy, arg);
	int needed = vsnprintf(NULL, 0, format, ap_copy);
	va_end(ap_copy);
	if(needed < 0) {
		*strp = NULL;
		return -1;
	}

	char* buf = (char*)malloc((size_t)needed + 1);
	if(!buf) {
		errno = ENOMEM;
		*strp = NULL;
		return -1;
	}

	int written = vsnprintf(buf, (size_t)needed + 1, format, arg);
	if(written < 0) {
		free(buf);
		*strp = NULL;
		return -1;
	}

	*strp = buf;
	return written;
}

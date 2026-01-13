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

#include "unistd.h"
#include "stdarg.h"
#include "stdlib.h"
#include "errno.h"

int execlp(const char* file, const char* arg, ...) {
	if(file == 0) {
		errno = ENOENT;
		return -1;
	}

	va_list ap;
	size_t argc = 0;

	va_start(ap, arg);
	const char* cur = arg;
	while(cur) {
		++argc;
		cur = va_arg(ap, const char*);
	}
	va_end(ap);

	char** argv = (char**)malloc(sizeof(char*) * (argc + 1));
	if(!argv) {
		errno = ENOMEM;
		return -1;
	}

	va_start(ap, arg);
	cur = arg;
	for(size_t i = 0; i < argc; ++i) {
		argv[i] = (char*)cur;
		cur = va_arg(ap, const char*);
	}
	va_end(ap);
	argv[argc] = 0;

	int ret = execvp(file, argv);
	free(argv);
	return ret;
}

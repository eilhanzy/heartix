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
#include "stdlib.h"
#include "string.h"
#include "errno.h"

static int execv_try(const char* path, char* const argv[]) {
	int ret = execv(path, argv);
	if(ret == -1 && (errno == ENOENT || errno == ENOTDIR)) {
		return -1;
	}
	return ret;
}

int execvp(const char* file, char* const argv[]) {
	if(file == 0 || *file == '\0') {
		errno = ENOENT;
		return -1;
	}

	if(strchr(file, '/')) {
		return execv(file, argv);
	}

	const char* path = getenv("PATH");
	if(path == 0 || *path == '\0') {
		path = "/bin:/usr/bin";
	}

	const char* segment = path;
	while(*segment) {
		const char* colon = strchr(segment, ':');
		size_t seg_len = colon ? (size_t)(colon - segment) : strlen(segment);

		const char* dir = segment;
		size_t dir_len = seg_len;
		if(dir_len == 0) {
			dir = ".";
			dir_len = 1;
		}

		size_t file_len = strlen(file);
		size_t full_len = dir_len + 1 + file_len + 1;
		char* full = (char*)malloc(full_len);
		if(!full) {
			errno = ENOMEM;
			return -1;
		}

		memcpy(full, dir, dir_len);
		full[dir_len] = '/';
		memcpy(full + dir_len + 1, file, file_len);
		full[dir_len + 1 + file_len] = '\0';

		int ret = execv_try(full, argv);
		free(full);
		if(ret != -1 || errno != ENOENT) {
			return ret;
		}

		if(!colon) {
			break;
		}
		segment = colon + 1;
	}

	errno = ENOENT;
	return -1;
}

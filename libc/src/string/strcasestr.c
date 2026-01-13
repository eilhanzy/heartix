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

#include "string.h"
#include "ctype.h"

char* strcasestr(const char* haystack, const char* needle) {
	if(!haystack || !needle) {
		return 0;
	}
	if(*needle == '\0') {
		return (char*)haystack;
	}

	for(const char* h = haystack; *h; ++h) {
		const char* h_it = h;
		const char* n_it = needle;
		while(*h_it && *n_it) {
			int hc = tolower((unsigned char)*h_it);
			int nc = tolower((unsigned char)*n_it);
			if(hc != nc) {
				break;
			}
			++h_it;
			++n_it;
		}
		if(*n_it == '\0') {
			return (char*)h;
		}
	}
	return 0;
}

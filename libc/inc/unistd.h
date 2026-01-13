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

#ifndef __GHOST_LIBC_UNISTD__
#define __GHOST_LIBC_UNISTD__

#include "ghost/common.h"
#include "stdio.h"
#include "sys/types.h"
#include "sys/stat.h"

__BEGIN_C

extern char **environ;
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

// constants for use with <access>
#define	F_OK		0	// file existence
#define	X_OK		1	// execute or search permission
#define	W_OK		2	// write permission
#define	R_OK		3	// read permission

/**
 * POSIX wrapper for <g_read>
 */
ssize_t read(int fd, void* buf, size_t count);

/**
 * POSIX wrapper for <g_write>
 */
ssize_t write(int fd, const void* buf, size_t count);

/**
 * POSIX wrapper for <g_seek>
 */
off_t lseek(int fd, off_t offset, int whence);

/**
 * POSIX wrapper for <g_tell>
 */
long int tell(int filedes);

/**
 * POSIX wrapper for <g_close>
 */
int close(int filedes);

/**
 * POSIX wrapper for <g_sbrk>
 */
void* sbrk(intptr_t increment);

/**
 * POSIX wrapper for <g_get_tid>
 */
pid_t getpid();

/**
 * POSIX wrapper for <g_get_pid>
 */
pid_t getppid();

/**
 * POSIX wrapper for <g_sleep>, but with seconds instead of milliseconds
 */
unsigned sleep(unsigned seconds);
int usleep(useconds_t usec);
long sysconf(int name);

#define _SC_PAGESIZE 1
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_CLK_TCK 2
#define _SC_NPROCESSORS_ONLN 3
#define _SC_NPROCESSORS_CONF 4

/**
 *
 */
char* getcwd(char* buf, size_t size);

/**
 * TODO
 */
int isatty(int fd);
int access(const char* pathname, int mode);
int fcntl(int fildes, int cmd, ...);
int rmdir(const char* path);
int symlink(const char* path1, const char* path2);
int chdir(const char *path);
int fchdir(int fd);
int execve(const char* path, char* const argv[], char* const envp[]);
int execv(const char* path, char* const argv[]);
int execvp(const char* file, char* const argv[]);
int execlp(const char* file, const char* arg, ...);
int pipe(int fds[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
pid_t fork(void);
int getopt(int argc, char * const argv[], const char *optstring);
char* getlogin(void);
void _exit(int status);
uid_t getuid(void);
gid_t getgid(void);
uid_t geteuid(void);
gid_t getegid(void);
ssize_t readlink(const char* path, char* buf, size_t bufsize);
ssize_t readlinkat(int dirfd, const char* path, char* buf, size_t bufsize);


__END_C

#endif

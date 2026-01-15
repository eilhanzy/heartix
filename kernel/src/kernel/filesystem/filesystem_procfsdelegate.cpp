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

#include "kernel/filesystem/filesystem_procfsdelegate.hpp"

#include "kernel/build_config.hpp"
#include "kernel/memory/heap.hpp"
#include "kernel/memory/memory.hpp"
#include "kernel/memory/constants.hpp"
#include "kernel/system/processor/processor.hpp"
#include "kernel/tasking/clock.hpp"
#include "kernel/tasking/tasking.hpp"
#include "kernel/tasking/tasking_directory.hpp"
#include "kernel/utils/string.hpp"

#include <ghost/tasks/types.h>

enum procfs_node_type
{
	PROCFS_NODE_ROOT = 1,
	PROCFS_NODE_STAT,
	PROCFS_NODE_MEMINFO,
	PROCFS_NODE_UPTIME,
	PROCFS_NODE_LOADAVG,
	PROCFS_NODE_CPUINFO,
	PROCFS_NODE_VERSION,
	PROCFS_NODE_PID_DIR,
	PROCFS_NODE_PID_STAT,
	PROCFS_NODE_PID_STATUS,
	PROCFS_NODE_PID_CMDLINE,
	PROCFS_NODE_PID_STATM,
};

#define PROCFS_TYPE_SHIFT 24
#define PROCFS_TYPE_MASK 0xFF
#define PROCFS_PID_MASK 0x00FFFFFF

static inline g_fs_phys_id procfsMakeId(procfs_node_type type, g_pid pid)
{
	return (g_fs_phys_id) (((type & PROCFS_TYPE_MASK) << PROCFS_TYPE_SHIFT) | (pid & PROCFS_PID_MASK));
}

g_fs_phys_id filesystemProcfsRootId()
{
	return procfsMakeId(PROCFS_NODE_ROOT, 0);
}

static inline procfs_node_type procfsNodeType(const g_fs_node* node)
{
	return (procfs_node_type) ((node->physicalId >> PROCFS_TYPE_SHIFT) & PROCFS_TYPE_MASK);
}

static inline g_pid procfsNodePid(const g_fs_node* node)
{
	return (g_pid) (node->physicalId & PROCFS_PID_MASK);
}

struct procfs_buffer
{
	char* data;
	size_t len;
	size_t cap;
};

static procfs_buffer procfsBufferCreate(size_t cap)
{
	procfs_buffer buf{};
	buf.cap = cap ? cap : 128;
	buf.data = (char*) heapAllocate(buf.cap);
	buf.len = 0;
	return buf;
}

static void procfsBufferEnsure(procfs_buffer* buf, size_t extra)
{
	if(buf->len + extra <= buf->cap)
		return;

	size_t newCap = buf->cap + extra + 64;
	char* next = (char*) heapAllocate(newCap);
	memoryCopy(next, buf->data, buf->len);
	heapFree(buf->data);
	buf->data = next;
	buf->cap = newCap;
}

static void procfsBufferAppendChar(procfs_buffer* buf, char c)
{
	procfsBufferEnsure(buf, 1);
	buf->data[buf->len++] = c;
}

static void procfsBufferAppendStr(procfs_buffer* buf, const char* str)
{
	if(!str)
		return;
	while(*str)
	{
		procfsBufferAppendChar(buf, *str++);
	}
}

static void procfsBufferAppendU64(procfs_buffer* buf, uint64_t value)
{
	char tmp[32];
	int len = 0;
	do
	{
		tmp[len++] = (char) ('0' + (value % 10));
		value /= 10;
	} while(value && len < (int) sizeof(tmp));

	for(int i = len - 1; i >= 0; --i)
		procfsBufferAppendChar(buf, tmp[i]);
}

static void procfsBufferAppendI64(procfs_buffer* buf, int64_t value)
{
	if(value < 0)
	{
		procfsBufferAppendChar(buf, '-');
		value = -value;
	}
	procfsBufferAppendU64(buf, (uint64_t) value);
}

static bool procfsParsePid(const char* name, g_pid* outPid)
{
	if(!name || !*name)
		return false;

	g_pid pid = 0;
	const char* c = name;
	while(*c)
	{
		if(*c < '0' || *c > '9')
			return false;
		pid = pid * 10 + (*c - '0');
		++c;
	}

	*outPid = pid;
	return true;
}

static bool procfsEnsureChild(g_fs_node* parent, const char* name, procfs_node_type type, g_pid pid,
                              g_fs_node_type fsType)
{
	g_fs_node* existing = nullptr;
	if(filesystemFindExistingChild(parent, name, &existing))
		return true;

	g_fs_node* node = filesystemCreateNode(fsType, name);
	node->physicalId = procfsMakeId(type, pid);
	filesystemAddChild(parent, node);
	return true;
}

static const char* procfsTaskName(g_task* task, char* buffer, size_t cap)
{
	const char* identifier = taskingDirectoryGetIdentifier(task->id);
	if(identifier && identifier[0])
		return identifier;

	stringCopy(buffer, "ghost");
	return buffer;
}

static char procfsTaskState(g_task* task)
{
	switch(task->status)
	{
		case G_TASK_STATUS_RUNNING:
			return 'R';
		case G_TASK_STATUS_WAITING:
			return 'S';
		case G_TASK_STATUS_DEAD:
			return 'Z';
		default:
			return '?';
	}
}

static uint32_t procfsProcessThreadCount(g_pid pid)
{
	(void) pid;
	return 1;
}

static bool procfsBuildRootFile(procfs_node_type type, procfs_buffer* buf)
{
	uint64_t totalTicks = 0;
	uint64_t idleTicks = 0;

	auto iter = hashmapIteratorStart(taskGlobalMap);
	while(hashmapIteratorHasNext(&iter))
	{
		auto task = hashmapIteratorNext(&iter)->value;
		if(task)
			totalTicks += task->statistics.timesScheduled;
	}
	hashmapIteratorEnd(&iter);

	uint64_t userTicks = (totalTicks >= idleTicks) ? (totalTicks - idleTicks) : 0;

	if(type == PROCFS_NODE_STAT)
	{
		procfsBufferAppendStr(buf, "cpu ");
		procfsBufferAppendU64(buf, userTicks);
		procfsBufferAppendStr(buf, " 0 0 ");
		procfsBufferAppendU64(buf, idleTicks);
		procfsBufferAppendStr(buf, " 0 0 0 0 0 0\n");

		for(int i = 0; i < processorGetNumberOfProcessors(); ++i)
		{
			procfsBufferAppendStr(buf, "cpu");
			procfsBufferAppendU64(buf, (uint64_t) i);
			procfsBufferAppendChar(buf, ' ');
			procfsBufferAppendU64(buf, userTicks);
			procfsBufferAppendStr(buf, " 0 0 ");
			procfsBufferAppendU64(buf, idleTicks);
			procfsBufferAppendStr(buf, " 0 0 0 0 0 0\n");
		}

		procfsBufferAppendStr(buf, "intr 0\n");
		procfsBufferAppendStr(buf, "ctxt 0\n");
		procfsBufferAppendStr(buf, "btime 0\n");

		uint32_t totalTasks = hashmapSize(taskGlobalMap);
		procfsBufferAppendStr(buf, "processes ");
		procfsBufferAppendU64(buf, totalTasks);
		procfsBufferAppendChar(buf, '\n');

		uint32_t running = 0;
		iter = hashmapIteratorStart(taskGlobalMap);
		while(hashmapIteratorHasNext(&iter))
		{
			auto task = hashmapIteratorNext(&iter)->value;
			if(task && task->status == G_TASK_STATUS_RUNNING)
				++running;
		}
		hashmapIteratorEnd(&iter);

		procfsBufferAppendStr(buf, "procs_running ");
		procfsBufferAppendU64(buf, running);
		procfsBufferAppendChar(buf, '\n');

		procfsBufferAppendStr(buf, "procs_blocked 0\n");
		return true;
	}

	if(type == PROCFS_NODE_MEMINFO)
	{
		uint64_t totalKb = (uint64_t) memoryPhysicalAllocator.totalPageCount * G_PAGE_SIZE / 1024;
		uint64_t freeKb = (uint64_t) memoryPhysicalAllocator.freePageCount * G_PAGE_SIZE / 1024;

		procfsBufferAppendStr(buf, "MemTotal: ");
		procfsBufferAppendU64(buf, totalKb);
		procfsBufferAppendStr(buf, " kB\n");

		procfsBufferAppendStr(buf, "MemFree: ");
		procfsBufferAppendU64(buf, freeKb);
		procfsBufferAppendStr(buf, " kB\n");

		procfsBufferAppendStr(buf, "MemAvailable: ");
		procfsBufferAppendU64(buf, freeKb);
		procfsBufferAppendStr(buf, " kB\n");

		procfsBufferAppendStr(buf, "Buffers: 0 kB\n");
		procfsBufferAppendStr(buf, "Cached: 0 kB\n");
		procfsBufferAppendStr(buf, "SwapTotal: 0 kB\n");
		procfsBufferAppendStr(buf, "SwapFree: 0 kB\n");
		return true;
	}

	if(type == PROCFS_NODE_UPTIME)
	{
		uint64_t millis = clockGetLocal()->time;
		uint64_t secs = millis / 1000;
		uint64_t cent = (millis % 1000) / 10;

		procfsBufferAppendU64(buf, secs);
		procfsBufferAppendChar(buf, '.');
		if(cent < 10)
			procfsBufferAppendChar(buf, '0');
		procfsBufferAppendU64(buf, cent);
		procfsBufferAppendStr(buf, " 0.00\n");
		return true;
	}

	if(type == PROCFS_NODE_LOADAVG)
	{
		uint32_t running = 0;
		uint32_t total = 0;
		uint32_t lastPid = 0;
		iter = hashmapIteratorStart(taskGlobalMap);
		while(hashmapIteratorHasNext(&iter))
		{
			auto task = hashmapIteratorNext(&iter)->value;
			if(task)
			{
				++total;
				if(task->status == G_TASK_STATUS_RUNNING)
					++running;
				if((uint32_t) task->id > lastPid)
					lastPid = task->id;
			}
		}
		hashmapIteratorEnd(&iter);

		procfsBufferAppendStr(buf, "0.00 0.00 0.00 ");
		procfsBufferAppendU64(buf, running);
		procfsBufferAppendChar(buf, '/');
		procfsBufferAppendU64(buf, total);
		procfsBufferAppendChar(buf, ' ');
		procfsBufferAppendU64(buf, lastPid);
		procfsBufferAppendChar(buf, '\n');
		return true;
	}

	if(type == PROCFS_NODE_CPUINFO)
	{
		char vendor[13];
		processorGetVendor(vendor);
		vendor[12] = 0;

		char brand[49];
		processorGetBrand(brand, sizeof(brand));
		const char* modelName = (brand[0] != 0) ? brand : "Unknown CPU";

		int cpus = processorGetNumberOfProcessors();
		for(int i = 0; i < cpus; ++i)
		{
			procfsBufferAppendStr(buf, "processor\t: ");
			procfsBufferAppendU64(buf, (uint64_t) i);
			procfsBufferAppendChar(buf, '\n');
			procfsBufferAppendStr(buf, "vendor_id\t: ");
			procfsBufferAppendStr(buf, vendor);
			procfsBufferAppendChar(buf, '\n');
			procfsBufferAppendStr(buf, "model name\t: ");
			procfsBufferAppendStr(buf, modelName);
			procfsBufferAppendChar(buf, '\n');
			uint32_t mhz = processorGetFrequencyMHz();
			procfsBufferAppendStr(buf, "cpu MHz\t\t: ");
			procfsBufferAppendU64(buf, mhz);
			procfsBufferAppendStr(buf, ".000\n");
			procfsBufferAppendStr(buf, "bogomips\t: 0\n\n");
		}
		return true;
	}

	if(type == PROCFS_NODE_VERSION)
	{
		procfsBufferAppendStr(buf, "Ghost ");
		procfsBufferAppendU64(buf, G_VERSION_MAJOR);
		procfsBufferAppendChar(buf, '.');
		procfsBufferAppendU64(buf, G_VERSION_MINOR);
		procfsBufferAppendChar(buf, '.');
		procfsBufferAppendU64(buf, G_VERSION_PATCH);
		procfsBufferAppendChar(buf, '\n');
		return true;
	}

	return false;
}

static bool procfsBuildPidFile(procfs_node_type type, g_pid pid, procfs_buffer* buf)
{
	auto iter = hashmapIteratorStart(taskGlobalMap);
	g_task* task = hashmapGet(taskGlobalMap, pid, (g_task*) nullptr);
	if(!task || task->status == G_TASK_STATUS_DEAD)
	{
		hashmapIteratorEnd(&iter);
		return false;
	}

	char nameBuf[64];
	const char* name = procfsTaskName(task, nameBuf, sizeof(nameBuf));
	char state = procfsTaskState(task);
	g_pid ppid = 0;

	uint64_t vsize = 0;
	uint64_t rssPages = 0;

	uint64_t utime = task->statistics.timesScheduled;
	uint32_t threads = procfsProcessThreadCount(pid);

	if(type == PROCFS_NODE_PID_STAT)
	{
		procfsBufferAppendU64(buf, pid);
		procfsBufferAppendChar(buf, ' ');
		procfsBufferAppendChar(buf, '(');
		procfsBufferAppendStr(buf, name);
		procfsBufferAppendChar(buf, ')');
		procfsBufferAppendChar(buf, ' ');
		procfsBufferAppendChar(buf, state);
		procfsBufferAppendChar(buf, ' ');
		procfsBufferAppendU64(buf, ppid);
		procfsBufferAppendStr(buf, " 0 0 0 0 0 0 0 0 ");
		procfsBufferAppendU64(buf, utime);
		procfsBufferAppendStr(buf, " 0 0 0 0 0 ");
		procfsBufferAppendU64(buf, threads);
		procfsBufferAppendStr(buf, " 0 0 ");
		procfsBufferAppendU64(buf, vsize);
		procfsBufferAppendChar(buf, ' ');
		procfsBufferAppendU64(buf, rssPages);
		procfsBufferAppendChar(buf, '\n');
		hashmapIteratorEnd(&iter);
		return true;
	}

	if(type == PROCFS_NODE_PID_STATUS)
	{
		procfsBufferAppendStr(buf, "Name:\t");
		procfsBufferAppendStr(buf, name);
		procfsBufferAppendChar(buf, '\n');

		procfsBufferAppendStr(buf, "State:\t");
		procfsBufferAppendChar(buf, state);
		procfsBufferAppendChar(buf, '\n');

		procfsBufferAppendStr(buf, "Tgid:\t");
		procfsBufferAppendU64(buf, pid);
		procfsBufferAppendChar(buf, '\n');

		procfsBufferAppendStr(buf, "Pid:\t");
		procfsBufferAppendU64(buf, pid);
		procfsBufferAppendChar(buf, '\n');

		procfsBufferAppendStr(buf, "PPid:\t");
		procfsBufferAppendU64(buf, ppid);
		procfsBufferAppendChar(buf, '\n');

		procfsBufferAppendStr(buf, "Threads:\t");
		procfsBufferAppendU64(buf, threads);
		procfsBufferAppendChar(buf, '\n');

		procfsBufferAppendStr(buf, "VmSize:\t");
		procfsBufferAppendU64(buf, vsize / 1024);
		procfsBufferAppendStr(buf, " kB\n");

		procfsBufferAppendStr(buf, "VmRSS:\t");
		procfsBufferAppendU64(buf, (rssPages * G_PAGE_SIZE) / 1024);
		procfsBufferAppendStr(buf, " kB\n");

		procfsBufferAppendStr(buf, "Uid:\t0 0 0 0\n");
		hashmapIteratorEnd(&iter);
		return true;
	}

	if(type == PROCFS_NODE_PID_CMDLINE)
	{
		procfsBufferAppendChar(buf, '\0');
		hashmapIteratorEnd(&iter);
		return true;
	}

	if(type == PROCFS_NODE_PID_STATM)
	{
		procfsBufferAppendU64(buf, rssPages);
		procfsBufferAppendChar(buf, ' ');
		procfsBufferAppendU64(buf, rssPages);
		procfsBufferAppendStr(buf, " 0 0 0 0 0\n");
		hashmapIteratorEnd(&iter);
		return true;
	}

	hashmapIteratorEnd(&iter);
	return false;
}

static bool procfsBuildContent(g_fs_node* node, procfs_buffer* buf)
{
	procfs_node_type type = procfsNodeType(node);
	if(type == PROCFS_NODE_PID_STAT || type == PROCFS_NODE_PID_STATUS ||
	   type == PROCFS_NODE_PID_CMDLINE || type == PROCFS_NODE_PID_STATM)
	{
		return procfsBuildPidFile(type, procfsNodePid(node), buf);
	}

	return procfsBuildRootFile(type, buf);
}

g_fs_open_status filesystemProcfsDelegateOpen(g_fs_node* node, g_file_flag_mode flags)
{
	(void) node;
	(void) flags;
	return G_FS_OPEN_SUCCESSFUL;
}

g_fs_close_status filesystemProcfsDelegateClose(g_fs_node* node, g_file_flag_mode openFlags)
{
	(void) node;
	(void) openFlags;
	return G_FS_CLOSE_SUCCESSFUL;
}

g_fs_open_status filesystemProcfsDelegateDiscover(g_fs_node* parent, const char* name, g_fs_node** outNode)
{
	if(!parent || !name)
		return G_FS_OPEN_NOT_FOUND;

	g_fs_node* existing = nullptr;
	if(filesystemFindExistingChild(parent, name, &existing))
	{
		*outNode = existing;
		return G_FS_OPEN_SUCCESSFUL;
	}

	procfs_node_type parentType = procfsNodeType(parent);
	if(parentType == PROCFS_NODE_ROOT)
	{
		if(stringEquals(name, "stat"))
			procfsEnsureChild(parent, name, PROCFS_NODE_STAT, 0, G_FS_NODE_TYPE_FILE);
		else if(stringEquals(name, "meminfo"))
			procfsEnsureChild(parent, name, PROCFS_NODE_MEMINFO, 0, G_FS_NODE_TYPE_FILE);
		else if(stringEquals(name, "uptime"))
			procfsEnsureChild(parent, name, PROCFS_NODE_UPTIME, 0, G_FS_NODE_TYPE_FILE);
		else if(stringEquals(name, "loadavg"))
			procfsEnsureChild(parent, name, PROCFS_NODE_LOADAVG, 0, G_FS_NODE_TYPE_FILE);
		else if(stringEquals(name, "cpuinfo"))
			procfsEnsureChild(parent, name, PROCFS_NODE_CPUINFO, 0, G_FS_NODE_TYPE_FILE);
		else if(stringEquals(name, "version"))
			procfsEnsureChild(parent, name, PROCFS_NODE_VERSION, 0, G_FS_NODE_TYPE_FILE);
		else
		{
			g_pid pid = 0;
			if(procfsParsePid(name, &pid))
				procfsEnsureChild(parent, name, PROCFS_NODE_PID_DIR, pid, G_FS_NODE_TYPE_FOLDER);
			else
				return G_FS_OPEN_NOT_FOUND;
		}
	}
	else if(parentType == PROCFS_NODE_PID_DIR)
	{
		g_pid pid = procfsNodePid(parent);
		if(stringEquals(name, "stat"))
			procfsEnsureChild(parent, name, PROCFS_NODE_PID_STAT, pid, G_FS_NODE_TYPE_FILE);
		else if(stringEquals(name, "status"))
			procfsEnsureChild(parent, name, PROCFS_NODE_PID_STATUS, pid, G_FS_NODE_TYPE_FILE);
		else if(stringEquals(name, "cmdline"))
			procfsEnsureChild(parent, name, PROCFS_NODE_PID_CMDLINE, pid, G_FS_NODE_TYPE_FILE);
		else if(stringEquals(name, "statm"))
			procfsEnsureChild(parent, name, PROCFS_NODE_PID_STATM, pid, G_FS_NODE_TYPE_FILE);
		else
			return G_FS_OPEN_NOT_FOUND;
	}
	else
	{
		return G_FS_OPEN_NOT_FOUND;
	}

	if(filesystemFindExistingChild(parent, name, outNode))
		return G_FS_OPEN_SUCCESSFUL;

	return G_FS_OPEN_NOT_FOUND;
}

g_fs_directory_refresh_status filesystemProcfsDelegateRefreshDir(g_fs_node* node)
{
	if(!node)
		return G_FS_DIRECTORY_REFRESH_ERROR;

	procfs_node_type type = procfsNodeType(node);
	if(type == PROCFS_NODE_ROOT)
	{
		procfsEnsureChild(node, "stat", PROCFS_NODE_STAT, 0, G_FS_NODE_TYPE_FILE);
		procfsEnsureChild(node, "meminfo", PROCFS_NODE_MEMINFO, 0, G_FS_NODE_TYPE_FILE);
		procfsEnsureChild(node, "uptime", PROCFS_NODE_UPTIME, 0, G_FS_NODE_TYPE_FILE);
		procfsEnsureChild(node, "loadavg", PROCFS_NODE_LOADAVG, 0, G_FS_NODE_TYPE_FILE);
		procfsEnsureChild(node, "cpuinfo", PROCFS_NODE_CPUINFO, 0, G_FS_NODE_TYPE_FILE);
		procfsEnsureChild(node, "version", PROCFS_NODE_VERSION, 0, G_FS_NODE_TYPE_FILE);

		auto iter = hashmapIteratorStart(taskGlobalMap);
		while(hashmapIteratorHasNext(&iter))
		{
			auto task = hashmapIteratorNext(&iter)->value;
			if(!task)
				continue;
			char pidName[16];
			char* end = stringWriteNumber(pidName, task->id);
			*end = 0;
			procfsEnsureChild(node, pidName, PROCFS_NODE_PID_DIR, task->id, G_FS_NODE_TYPE_FOLDER);
		}
		hashmapIteratorEnd(&iter);

		// Keep stale pid entries to avoid races with readers traversing children.
		return G_FS_DIRECTORY_REFRESH_SUCCESSFUL;
	}

	if(type == PROCFS_NODE_PID_DIR)
	{
		g_pid pid = procfsNodePid(node);
		procfsEnsureChild(node, "stat", PROCFS_NODE_PID_STAT, pid, G_FS_NODE_TYPE_FILE);
		procfsEnsureChild(node, "status", PROCFS_NODE_PID_STATUS, pid, G_FS_NODE_TYPE_FILE);
		procfsEnsureChild(node, "cmdline", PROCFS_NODE_PID_CMDLINE, pid, G_FS_NODE_TYPE_FILE);
		procfsEnsureChild(node, "statm", PROCFS_NODE_PID_STATM, pid, G_FS_NODE_TYPE_FILE);
		return G_FS_DIRECTORY_REFRESH_SUCCESSFUL;
	}

	return G_FS_DIRECTORY_REFRESH_SUCCESSFUL;
}

g_fs_read_status filesystemProcfsDelegateRead(g_fs_node* node, uint8_t* buffer, uint64_t offset, uint64_t length,
                                              int64_t* outRead)
{
	if(!node || !buffer || !outRead)
		return G_FS_READ_ERROR;

	procfs_buffer content = procfsBufferCreate(256);
	if(!procfsBuildContent(node, &content))
	{
		heapFree(content.data);
		return G_FS_READ_ERROR;
	}

	if(offset >= content.len)
	{
		*outRead = 0;
		heapFree(content.data);
		return G_FS_READ_SUCCESSFUL;
	}

	uint64_t toCopy = length;
	if(offset + toCopy > content.len)
		toCopy = content.len - offset;

	memoryCopy(buffer, content.data + offset, (int32_t) toCopy);
	*outRead = toCopy;
	heapFree(content.data);
	return G_FS_READ_SUCCESSFUL;
}

g_fs_length_status filesystemProcfsDelegateGetLength(g_fs_node* node, uint64_t* outLength)
{
	if(!node || !outLength)
		return G_FS_LENGTH_ERROR;

	procfs_buffer content = procfsBufferCreate(128);
	if(!procfsBuildContent(node, &content))
	{
		heapFree(content.data);
		return G_FS_LENGTH_ERROR;
	}

	*outLength = content.len;
	heapFree(content.data);
	return G_FS_LENGTH_SUCCESSFUL;
}

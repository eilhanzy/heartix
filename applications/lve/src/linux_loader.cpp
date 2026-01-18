/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  LVE Linux bzImage loader helpers                                         *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "linux_loader.hpp"

#include <ghost/filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{

constexpr uint32_t kBootFlag = 0xAA55;
constexpr uint32_t kHdrsSignature = 0x53726448; // "HdrS"

constexpr uint64_t kDefaultKernelLoad = 0x100000;
constexpr uint64_t kBootParamsAddr = 0x10000;
constexpr uint64_t kBootParamsSize = 0x1000;
constexpr uint64_t kCmdlineAddr = 0x20000;
constexpr uint64_t kSetupHeaderOffset = 0x1F1;
constexpr uint64_t kE820EntriesOffset = 0x1E8;
constexpr uint64_t kE820TableOffset = 0x2D0;

constexpr uint8_t kLoadFlagLoadedHigh = 1u << 0;
constexpr uint8_t kLoadFlagCanUseHeap = 1u << 7;

constexpr uint16_t kHeapEndPtr = 0xE000;
constexpr uint64_t kE820Ram = 1;
constexpr uint64_t kE820Reserved = 2;

struct lve_setup_header
{
	uint8_t setup_sects;
	uint16_t root_flags;
	uint32_t syssize;
	uint16_t ram_size;
	uint16_t vid_mode;
	uint16_t root_dev;
	uint16_t boot_flag;
	uint16_t jump;
	uint32_t header;
	uint16_t version;
	uint32_t realmode_swtch;
	uint16_t start_sys_seg;
	uint16_t kernel_version;
	uint8_t type_of_loader;
	uint8_t loadflags;
	uint16_t setup_move_size;
	uint32_t code32_start;
	uint32_t ramdisk_image;
	uint32_t ramdisk_size;
	uint32_t bootsect_kludge;
	uint16_t heap_end_ptr;
	uint8_t ext_loader_ver;
	uint8_t ext_loader_type;
	uint32_t cmd_line_ptr;
	uint32_t initrd_addr_max;
	uint32_t kernel_alignment;
	uint8_t relocatable_kernel;
	uint8_t min_alignment;
	uint16_t xloadflags;
	uint32_t cmdline_size;
	uint32_t hardware_subarch;
	uint64_t hardware_subarch_data;
	uint32_t payload_offset;
	uint32_t payload_length;
	uint64_t setup_data;
	uint64_t pref_address;
	uint32_t init_size;
	uint32_t handover_offset;
} __attribute__((packed));

struct lve_e820_entry
{
	uint64_t addr;
	uint64_t size;
	uint32_t type;
} __attribute__((packed));

uint64_t alignUp(uint64_t value, uint64_t align)
{
	if(align == 0)
		return value;
	return (value + (align - 1)) & ~(align - 1);
}

uint64_t alignDown(uint64_t value, uint64_t align)
{
	if(align == 0)
		return value;
	return value & ~(align - 1);
}

bool readFile(const char* path, uint8_t** outBuffer, size_t* outSize)
{
	if(!path || !outBuffer || !outSize)
		return false;

	g_fs_open_status openStatus = G_FS_OPEN_SUCCESSFUL;
	g_fd fd = g_open_fs(path, G_FILE_FLAG_MODE_READ, &openStatus);
	if(openStatus != G_FS_OPEN_SUCCESSFUL)
		return false;

	g_fs_length_status lengthStatus = G_FS_LENGTH_SUCCESSFUL;
	int64_t size = g_length_s(fd, &lengthStatus);
	if(lengthStatus != G_FS_LENGTH_SUCCESSFUL || size <= 0)
	{
		g_close(fd);
		return false;
	}

	auto* buffer = static_cast<uint8_t*>(malloc(static_cast<size_t>(size)));
	if(!buffer)
	{
		g_close(fd);
		return false;
	}

	size_t totalRead = 0;
	while(totalRead < static_cast<size_t>(size))
	{
		g_fs_read_status readStatus = G_FS_READ_SUCCESSFUL;
		int32_t chunk = g_read_s(fd, buffer + totalRead,
		                         static_cast<uint64_t>(size - totalRead), &readStatus);
		if(readStatus != G_FS_READ_SUCCESSFUL || chunk <= 0)
		{
			free(buffer);
			g_close(fd);
			return false;
		}
		totalRead += static_cast<size_t>(chunk);
	}
	g_close(fd);

	*outBuffer = buffer;
	*outSize = static_cast<size_t>(size);
	return true;
}

} // namespace

bool lveLoadBzImageToGuest(const char* path, lve_guest_memory* guest, const char* cmdline,
                           const char* initrdPath, lve_linux_load_result* out)
{
	if(!guest)
	{
		printf("lve: invalid guest memory handle\n");
		return false;
	}
	if(!guest->base)
	{
		printf("lve: guest memory base not allocated\n");
		return false;
	}
	if(!path)
	{
		printf("lve: missing bzImage path\n");
		return false;
	}

	uint8_t* image = nullptr;
	size_t imageSize = 0;
	if(!readFile(path, &image, &imageSize))
	{
		printf("lve: failed to read bzImage '%s'\n", path);
		return false;
	}

	if(imageSize < kSetupHeaderOffset + sizeof(lve_setup_header))
	{
		printf("lve: bzImage too small (%zu bytes)\n", imageSize);
		free(image);
		return false;
	}

	lve_setup_header header{};
	memcpy(&header, image + kSetupHeaderOffset, sizeof(header));
	if(header.boot_flag != kBootFlag || header.header != kHdrsSignature)
	{
		printf("lve: bzImage header invalid (boot_flag=0x%04x hdr=0x%08x)\n",
		       header.boot_flag, header.header);
		free(image);
		return false;
	}

	uint8_t setupSects = header.setup_sects == 0 ? 4 : header.setup_sects;
	uint64_t setupBytes = static_cast<uint64_t>(setupSects + 1) * 512ull;
	if(setupBytes >= imageSize)
	{
		printf("lve: bzImage setup exceeds image size (setup=%llu image=%llu sects=%u)\n",
		       static_cast<unsigned long long>(setupBytes),
		       static_cast<unsigned long long>(imageSize),
		       static_cast<unsigned int>(setupSects));
		free(image);
		return false;
	}

	uint64_t payloadOffset = setupBytes;
	if(header.payload_offset != 0 && header.payload_offset < imageSize)
		payloadOffset = header.payload_offset;
	if(payloadOffset >= imageSize)
	{
		printf("lve: bzImage payload offset out of range (0x%llx)\n",
		       static_cast<unsigned long long>(payloadOffset));
		free(image);
		return false;
	}
	uint64_t payloadSize = imageSize - payloadOffset;
	if(header.payload_length != 0 && header.payload_length < payloadSize)
		payloadSize = header.payload_length;

	uint64_t loadAddr = kDefaultKernelLoad;
	if(header.relocatable_kernel && header.kernel_alignment)
		loadAddr = alignUp(loadAddr, header.kernel_alignment);

	if(loadAddr + payloadSize > guest->size)
	{
		printf("lve: kernel payload exceeds guest ram (payload=%llu guest=%llu)\n",
		       static_cast<unsigned long long>(payloadSize),
		       static_cast<unsigned long long>(guest->size));
		free(image);
		return false;
	}

	auto* guestBytes = static_cast<uint8_t*>(guest->base);
	memcpy(guestBytes + loadAddr, image + payloadOffset, payloadSize);

	if(kBootParamsAddr + kBootParamsSize > guest->size)
	{
		printf("lve: guest ram too small for boot params\n");
		free(image);
		return false;
	}

	memset(guestBytes + kBootParamsAddr, 0, kBootParamsSize);
	memcpy(guestBytes + kBootParamsAddr + kSetupHeaderOffset, &header, sizeof(header));

	auto* outHeader = reinterpret_cast<lve_setup_header*>(guestBytes + kBootParamsAddr + kSetupHeaderOffset);
	outHeader->type_of_loader = 0xFF;
	outHeader->loadflags |= kLoadFlagLoadedHigh;
	outHeader->loadflags |= kLoadFlagCanUseHeap;
	outHeader->heap_end_ptr = kHeapEndPtr;
	outHeader->code32_start = static_cast<uint32_t>(loadAddr);

	uint64_t cmdlineAddr = 0;
	if(cmdline && cmdline[0])
	{
		size_t cmdlineLen = strlen(cmdline);
		if(kCmdlineAddr + cmdlineLen + 1 > guest->size)
		{
			printf("lve: guest ram too small for cmdline\n");
			free(image);
			return false;
		}
		memcpy(guestBytes + kCmdlineAddr, cmdline, cmdlineLen);
		guestBytes[kCmdlineAddr + cmdlineLen] = '\0';

		outHeader->cmd_line_ptr = static_cast<uint32_t>(kCmdlineAddr);
		outHeader->cmdline_size = static_cast<uint32_t>(cmdlineLen + 1);
		cmdlineAddr = kCmdlineAddr;
	}
	else
	{
		outHeader->cmd_line_ptr = 0;
		outHeader->cmdline_size = 0;
	}

	uint64_t initrdAddr = 0;
	uint64_t initrdSize = 0;
	if(initrdPath)
	{
		uint8_t* initrd = nullptr;
		size_t initrdBytes = 0;
		if(!readFile(initrdPath, &initrd, &initrdBytes))
		{
			printf("lve: failed to read initrd '%s'\n", initrdPath);
			free(image);
			return false;
		}

		uint64_t initrdMax = header.initrd_addr_max ? header.initrd_addr_max : 0x37FFFFFF;
		if(initrdMax >= guest->size)
			initrdMax = guest->size - 1;

		uint64_t initrdEnd = initrdMax + 1;
		if(initrdBytes > initrdEnd)
		{
			printf("lve: initrd too large for guest ram\n");
			free(initrd);
			free(image);
			return false;
		}
		uint64_t initrdStart = alignDown(initrdEnd - initrdBytes, G_PAGE_SIZE);
		if(initrdStart < 0x100000)
			initrdStart = alignUp(0x100000, G_PAGE_SIZE);

		if(initrdStart + initrdBytes > initrdEnd)
		{
			printf("lve: initrd placement exceeds bounds\n");
			free(initrd);
			free(image);
			return false;
		}

		if(initrdStart + initrdBytes > guest->size)
		{
			printf("lve: initrd placement exceeds guest ram\n");
			free(initrd);
			free(image);
			return false;
		}
		if(initrdStart < loadAddr + payloadSize && initrdStart + initrdBytes > loadAddr)
		{
			printf("lve: initrd overlaps kernel payload\n");
			free(initrd);
			free(image);
			return false;
		}

		memcpy(guestBytes + initrdStart, initrd, initrdBytes);
		initrdAddr = initrdStart;
		initrdSize = initrdBytes;
		free(initrd);
	}

	outHeader->ramdisk_image = static_cast<uint32_t>(initrdAddr);
	outHeader->ramdisk_size = static_cast<uint32_t>(initrdSize);

	uint8_t* e820Entries = guestBytes + kBootParamsAddr + kE820EntriesOffset;
	lve_e820_entry* e820 = reinterpret_cast<lve_e820_entry*>(guestBytes + kBootParamsAddr + kE820TableOffset);
	uint8_t entries = 0;

	const uint64_t ramTop = guest->size;
	auto addE820 = [&](uint64_t addr, uint64_t size, uint32_t type)
	{
		if(size == 0)
			return;
		e820[entries++] = {addr, size, type};
	};

	if(ramTop > 0)
	{
		const uint64_t lowRam = ramTop < 0x0009F000 ? ramTop : 0x0009F000;
		addE820(0x00000000, lowRam, kE820Ram);

		if(ramTop > 0x0009F000)
		{
			const uint64_t holeEnd = ramTop < 0x00100000 ? ramTop : 0x00100000;
			addE820(0x0009F000, holeEnd - 0x0009F000, kE820Reserved);
		}

		if(ramTop > 0x00100000)
			addE820(0x00100000, ramTop - 0x00100000, kE820Ram);
	}
	*e820Entries = entries;

	if(out)
	{
		out->kernel_load_addr = loadAddr;
		out->kernel_size = payloadSize;
		out->entry_point = header.code32_start ? header.code32_start : loadAddr;
		out->boot_params_addr = kBootParamsAddr;
		out->cmdline_addr = cmdlineAddr;
		out->initrd_addr = initrdAddr;
		out->initrd_size = initrdSize;
		out->version = header.version;
	}

	free(image);
	return true;
}

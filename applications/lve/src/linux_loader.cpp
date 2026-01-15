/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  LVE Linux bzImage loader helpers                                         *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "linux_loader.hpp"

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

	FILE* file = fopen(path, "rb");
	if(!file)
		return false;

	if(fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return false;
	}
	long size = ftell(file);
	if(size <= 0)
	{
		fclose(file);
		return false;
	}
	if(fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return false;
	}

	auto* buffer = static_cast<uint8_t*>(malloc(static_cast<size_t>(size)));
	if(!buffer)
	{
		fclose(file);
		return false;
	}

	size_t readBytes = fread(buffer, 1, static_cast<size_t>(size), file);
	fclose(file);

	if(readBytes != static_cast<size_t>(size))
	{
		free(buffer);
		return false;
	}

	*outBuffer = buffer;
	*outSize = static_cast<size_t>(size);
	return true;
}

} // namespace

bool lveLoadBzImageToGuest(const char* path, lve_guest_memory* guest, const char* cmdline,
                           const char* initrdPath, lve_linux_load_result* out)
{
	if(!guest || !guest->base || !path)
		return false;

	uint8_t* image = nullptr;
	size_t imageSize = 0;
	if(!readFile(path, &image, &imageSize))
		return false;

	if(imageSize < kSetupHeaderOffset + sizeof(lve_setup_header))
	{
		free(image);
		return false;
	}

	lve_setup_header header{};
	memcpy(&header, image + kSetupHeaderOffset, sizeof(header));
	if(header.boot_flag != kBootFlag || header.header != kHdrsSignature)
	{
		free(image);
		return false;
	}

	uint8_t setupSects = header.setup_sects == 0 ? 4 : header.setup_sects;
	uint64_t setupBytes = static_cast<uint64_t>(setupSects + 1) * 512ull;
	if(setupBytes >= imageSize)
	{
		free(image);
		return false;
	}

	uint64_t payloadOffset = setupBytes;
	uint64_t payloadSize = imageSize - payloadOffset;
	if(header.payload_length != 0 && header.payload_length < payloadSize)
		payloadSize = header.payload_length;

	uint64_t loadAddr = kDefaultKernelLoad;
	if(header.relocatable_kernel && header.kernel_alignment)
		loadAddr = alignUp(loadAddr, header.kernel_alignment);

	if(loadAddr + payloadSize > guest->size)
	{
		free(image);
		return false;
	}

	auto* guestBytes = static_cast<uint8_t*>(guest->base);
	memcpy(guestBytes + loadAddr, image + payloadOffset, payloadSize);

	if(kBootParamsAddr + kBootParamsSize > guest->size)
	{
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
			free(image);
			return false;
		}

		uint64_t initrdMax = header.initrd_addr_max ? header.initrd_addr_max : 0x37FFFFFF;
		if(initrdMax >= guest->size)
			initrdMax = guest->size - 1;

		uint64_t initrdEnd = initrdMax + 1;
		if(initrdBytes > initrdEnd)
		{
			free(initrd);
			free(image);
			return false;
		}
		uint64_t initrdStart = alignDown(initrdEnd - initrdBytes, G_PAGE_SIZE);
		if(initrdStart < 0x100000)
			initrdStart = alignUp(0x100000, G_PAGE_SIZE);

		if(initrdStart + initrdBytes > initrdEnd)
		{
			free(initrd);
			free(image);
			return false;
		}

		if(initrdStart + initrdBytes > guest->size)
		{
			free(initrd);
			free(image);
			return false;
		}
		if(initrdStart < loadAddr + payloadSize && initrdStart + initrdBytes > loadAddr)
		{
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
		out->boot_params_addr = kBootParamsAddr;
		out->cmdline_addr = cmdlineAddr;
		out->initrd_addr = initrdAddr;
		out->initrd_size = initrdSize;
		out->version = header.version;
	}

	free(image);
	return true;
}

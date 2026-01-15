/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  LVE probe utility                                                       *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <ghost/system.h>

#include "ept.hpp"
#include "linux_loader.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* vmxStatusToString(g_vmx_status status)
{
	switch(status)
	{
		case G_VMX_STATUS_SUCCESS:
			return "success";
		case G_VMX_STATUS_UNSUPPORTED:
			return "unsupported";
		case G_VMX_STATUS_DISABLED:
			return "disabled";
		case G_VMX_STATUS_NO_MEMORY:
			return "no-memory";
		case G_VMX_STATUS_NOT_PERMITTED:
			return "not-permitted";
		default:
			return "failed";
	}
}

static void printCaps(const g_vmx_caps& caps)
{
	printf("vmx: %s\n", caps.vmx ? "yes" : "no");
	printf("feature-control: locked=%u vmxon=%u\n",
	       caps.featureControlLocked, caps.featureControlVmxon);
	printf("active: %u\n", caps.active);
	printf("ept: %u\n", caps.hasEpt);
	printf("unrestricted-guest: %u\n", caps.hasUnrestrictedGuest);
	printf("revision-id: 0x%08x\n", caps.revisionId);
	printf("vmx-basic: 0x%016llx\n", (unsigned long long) caps.vmxBasic);
	printf("proc-ctls: 0x%016llx\n", (unsigned long long) caps.vmxProcCtls);
	printf("proc-ctls2: 0x%016llx\n", (unsigned long long) caps.vmxProcCtls2);
	printf("ept-vpid: 0x%016llx\n", (unsigned long long) caps.vmxEptVpid);
}

int main(int argc, char** argv)
{
	bool doEnable = false;
	bool doDisable = false;
	uint64_t ramMb = 0;
	const char* bzImagePath = nullptr;
	const char* cmdline = nullptr;
	const char* initrdPath = nullptr;

	for(int i = 1; i < argc; ++i)
	{
		if(strcmp(argv[i], "--enable") == 0)
			doEnable = true;
		else if(strcmp(argv[i], "--disable") == 0)
			doDisable = true;
		else if(strcmp(argv[i], "--ram") == 0)
		{
			if(i + 1 >= argc)
			{
				printf("lve: --ram requires a size in MiB\n");
				return 1;
			}
			ramMb = strtoull(argv[++i], nullptr, 0);
		}
		else if(strcmp(argv[i], "--bzimage") == 0)
		{
			if(i + 1 >= argc)
			{
				printf("lve: --bzimage requires a path\n");
				return 1;
			}
			bzImagePath = argv[++i];
		}
		else if(strcmp(argv[i], "--cmdline") == 0)
		{
			if(i + 1 >= argc)
			{
				printf("lve: --cmdline requires a value\n");
				return 1;
			}
			cmdline = argv[++i];
		}
		else if(strcmp(argv[i], "--initrd") == 0)
		{
			if(i + 1 >= argc)
			{
				printf("lve: --initrd requires a path\n");
				return 1;
			}
			initrdPath = argv[++i];
		}
		else if(strcmp(argv[i], "--help") == 0)
		{
			printf("lve [--enable|--disable] [--ram <MiB>] [--bzimage <path>] [--initrd <path>] [--cmdline <args>]\n");
			return 0;
		}
	}

	if(doEnable)
	{
		g_vmx_status status = g_vmx_enable();
		printf("vmx enable: %s\n", vmxStatusToString(status));
	}
	if(doDisable)
	{
		g_vmx_status status = g_vmx_disable();
		printf("vmx disable: %s\n", vmxStatusToString(status));
	}

	g_vmx_caps caps{};
	g_vmx_status status = g_vmx_get_caps(&caps);
	printf("vmx caps: %s\n", vmxStatusToString(status));
	if(status == G_VMX_STATUS_SUCCESS || status == G_VMX_STATUS_DISABLED)
		printCaps(caps);

	if(bzImagePath && ramMb == 0)
	{
		printf("lve: --bzimage requires --ram <MiB>\n");
		return 1;
	}
	if(initrdPath && !bzImagePath)
	{
		printf("lve: --initrd requires --bzimage\n");
		return 1;
	}

	if(ramMb > 0)
	{
		if(!(status == G_VMX_STATUS_SUCCESS || status == G_VMX_STATUS_DISABLED))
		{
			printf("lve: unable to query VMX capabilities\n");
			return 1;
		}
		if(!caps.hasEpt)
		{
			printf("lve: EPT not supported on this CPU\n");
			return 1;
		}

		const uint64_t ramBytes = ramMb * 1024ull * 1024ull;
		lve_guest_memory guest{};
		if(!lveGuestAllocate(&guest, ramBytes))
		{
			printf("lve: guest memory allocation failed\n");
			return 1;
		}

		lve_linux_load_result linuxInfo{};
		if(bzImagePath)
		{
			if(!lveLoadBzImageToGuest(bzImagePath, &guest, cmdline, initrdPath, &linuxInfo))
			{
				printf("lve: bzImage load failed\n");
				lveGuestFree(&guest);
				return 1;
			}
			printf("linux: kernel addr=0x%llx size=%llu version=0x%04x\n",
			       static_cast<unsigned long long>(linuxInfo.kernel_load_addr),
			       static_cast<unsigned long long>(linuxInfo.kernel_size),
			       linuxInfo.version);
			printf("linux: boot_params=0x%llx cmdline=0x%llx\n",
			       static_cast<unsigned long long>(linuxInfo.boot_params_addr),
			       static_cast<unsigned long long>(linuxInfo.cmdline_addr));
			if(linuxInfo.initrd_addr)
				printf("linux: initrd addr=0x%llx size=%llu\n",
				       static_cast<unsigned long long>(linuxInfo.initrd_addr),
				       static_cast<unsigned long long>(linuxInfo.initrd_size));
		}

		lve_ept ept{};
		if(!lveEptBuild(&ept, &guest))
		{
			printf("lve: EPT build failed\n");
			lveGuestFree(&guest);
			return 1;
		}

		const uint64_t eptp = lveEptMakePointer(&ept);
		printf("guest ram: %llu MiB (%llu pages)\n",
		       static_cast<unsigned long long>(ramMb),
		       static_cast<unsigned long long>(guest.pageCount));
		printf("eptp: 0x%016llx\n", static_cast<unsigned long long>(eptp));

		lveEptFree(&ept);
		lveGuestFree(&guest);
	}

	return 0;
}

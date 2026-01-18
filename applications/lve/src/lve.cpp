/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  LVE probe utility                                                       *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <ghost/system.h>

#include "ept.hpp"
#include "linux_loader.hpp"

#include <stdio.h>
#include <string.h>

namespace
{

bool parseUint64(const char* text, uint64_t* out)
{
	if(!text || !*text || !out)
		return false;

	uint64_t value = 0;
	int base = 10;
	size_t idx = 0;

	if(text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
	{
		base = 16;
		idx = 2;
		if(!text[idx])
			return false;
	}

	for(; text[idx]; ++idx)
	{
		char c = text[idx];
		uint8_t digit = 0;
		if(c >= '0' && c <= '9')
			digit = static_cast<uint8_t>(c - '0');
		else if(base == 16 && c >= 'a' && c <= 'f')
			digit = static_cast<uint8_t>(10 + (c - 'a'));
		else if(base == 16 && c >= 'A' && c <= 'F')
			digit = static_cast<uint8_t>(10 + (c - 'A'));
		else
			return false;

		if(digit >= base)
			return false;
		value = value * static_cast<uint64_t>(base) + digit;
	}

	*out = value;
	return true;
}

constexpr uint32_t VMCS_LINK_POINTER = 0x2800;
constexpr uint32_t VMCS_GUEST_IA32_DEBUGCTL = 0x2802;
constexpr uint32_t VMCS_GUEST_IA32_PAT = 0x2804;
constexpr uint32_t VMCS_GUEST_IA32_EFER = 0x2806;
constexpr uint32_t VMCS_GUEST_IA32_PERF_GLOBAL_CTRL = 0x2808;
constexpr uint32_t VMCS_GUEST_ES_SELECTOR = 0x0800;
constexpr uint32_t VMCS_GUEST_CS_SELECTOR = 0x0802;
constexpr uint32_t VMCS_GUEST_SS_SELECTOR = 0x0804;
constexpr uint32_t VMCS_GUEST_DS_SELECTOR = 0x0806;
constexpr uint32_t VMCS_GUEST_FS_SELECTOR = 0x0808;
constexpr uint32_t VMCS_GUEST_GS_SELECTOR = 0x080A;
constexpr uint32_t VMCS_GUEST_LDTR_SELECTOR = 0x080C;
constexpr uint32_t VMCS_GUEST_TR_SELECTOR = 0x080E;
constexpr uint32_t VMCS_GUEST_ES_LIMIT = 0x4800;
constexpr uint32_t VMCS_GUEST_CS_LIMIT = 0x4802;
constexpr uint32_t VMCS_GUEST_SS_LIMIT = 0x4804;
constexpr uint32_t VMCS_GUEST_DS_LIMIT = 0x4806;
constexpr uint32_t VMCS_GUEST_FS_LIMIT = 0x4808;
constexpr uint32_t VMCS_GUEST_GS_LIMIT = 0x480A;
constexpr uint32_t VMCS_GUEST_LDTR_LIMIT = 0x480C;
constexpr uint32_t VMCS_GUEST_TR_LIMIT = 0x480E;
constexpr uint32_t VMCS_GUEST_GDTR_LIMIT = 0x4810;
constexpr uint32_t VMCS_GUEST_IDTR_LIMIT = 0x4812;
constexpr uint32_t VMCS_GUEST_ES_ACCESS = 0x4814;
constexpr uint32_t VMCS_GUEST_CS_ACCESS = 0x4816;
constexpr uint32_t VMCS_GUEST_SS_ACCESS = 0x4818;
constexpr uint32_t VMCS_GUEST_DS_ACCESS = 0x481A;
constexpr uint32_t VMCS_GUEST_FS_ACCESS = 0x481C;
constexpr uint32_t VMCS_GUEST_GS_ACCESS = 0x481E;
constexpr uint32_t VMCS_GUEST_LDTR_ACCESS = 0x4820;
constexpr uint32_t VMCS_GUEST_TR_ACCESS = 0x4822;
constexpr uint32_t VMCS_GUEST_INTERRUPTIBILITY = 0x4824;
constexpr uint32_t VMCS_GUEST_ACTIVITY_STATE = 0x4826;
constexpr uint32_t VMCS_GUEST_CR0 = 0x6800;
constexpr uint32_t VMCS_GUEST_CR3 = 0x6802;
constexpr uint32_t VMCS_GUEST_CR4 = 0x6804;
constexpr uint32_t VMCS_GUEST_ES_BASE = 0x6806;
constexpr uint32_t VMCS_GUEST_CS_BASE = 0x6808;
constexpr uint32_t VMCS_GUEST_SS_BASE = 0x680A;
constexpr uint32_t VMCS_GUEST_DS_BASE = 0x680C;
constexpr uint32_t VMCS_GUEST_FS_BASE = 0x680E;
constexpr uint32_t VMCS_GUEST_GS_BASE = 0x6810;
constexpr uint32_t VMCS_GUEST_LDTR_BASE = 0x6812;
constexpr uint32_t VMCS_GUEST_TR_BASE = 0x6814;
constexpr uint32_t VMCS_GUEST_GDTR_BASE = 0x6816;
constexpr uint32_t VMCS_GUEST_IDTR_BASE = 0x6818;
constexpr uint32_t VMCS_GUEST_DR7 = 0x681A;
constexpr uint32_t VMCS_GUEST_RSP = 0x681C;
constexpr uint32_t VMCS_GUEST_RIP = 0x681E;
constexpr uint32_t VMCS_GUEST_RFLAGS = 0x6820;
constexpr uint32_t VMCS_GUEST_PENDING_DBG = 0x6822;
constexpr uint32_t VMCS_GUEST_SYSENTER_ESP = 0x6824;
constexpr uint32_t VMCS_GUEST_SYSENTER_EIP = 0x6826;
constexpr uint32_t VMCS_GUEST_SYSENTER_CS = 0x482A;

constexpr uint32_t VMCS_PIN_BASED_CTLS = 0x4000;
constexpr uint32_t VMCS_PROC_BASED_CTLS = 0x4002;
constexpr uint32_t VMCS_EXIT_CTLS = 0x400C;
constexpr uint32_t VMCS_EXIT_MSR_STORE_COUNT = 0x400E;
constexpr uint32_t VMCS_EXIT_MSR_LOAD_COUNT = 0x4010;
constexpr uint32_t VMCS_ENTRY_CTLS = 0x4012;
constexpr uint32_t VMCS_ENTRY_MSR_LOAD_COUNT = 0x4014;
constexpr uint32_t VMCS_ENTRY_INTR_INFO = 0x4016;
constexpr uint32_t VMCS_ENTRY_EXCEPTION_ERROR = 0x4018;
constexpr uint32_t VMCS_ENTRY_INSTRUCTION_LEN = 0x401A;
constexpr uint32_t VMCS_PROC_BASED_CTLS2 = 0x401E;

constexpr uint32_t VMCS_EPT_POINTER = 0x201A;
constexpr uint32_t VMCS_TSC_OFFSET = 0x2010;
constexpr uint32_t VMCS_VMEXIT_MSR_STORE_ADDR = 0x2006;
constexpr uint32_t VMCS_VMEXIT_MSR_LOAD_ADDR = 0x2008;
constexpr uint32_t VMCS_VMENTRY_MSR_LOAD_ADDR = 0x200A;
constexpr uint32_t VMCS_EXCEPTION_BITMAP = 0x4004;
constexpr uint32_t VMCS_PF_ERROR_MASK = 0x4006;
constexpr uint32_t VMCS_PF_ERROR_MATCH = 0x4008;
constexpr uint32_t VMCS_CR3_TARGET_COUNT = 0x400A;
constexpr uint32_t VMCS_CR0_GUEST_HOST_MASK = 0x6000;
constexpr uint32_t VMCS_CR4_GUEST_HOST_MASK = 0x6002;
constexpr uint32_t VMCS_CR0_READ_SHADOW = 0x6004;
constexpr uint32_t VMCS_CR4_READ_SHADOW = 0x6006;
constexpr uint32_t VMCS_VMEXIT_REASON = 0x4402;
constexpr uint32_t VMCS_VMEXIT_QUALIFICATION = 0x6400;

constexpr uint32_t PROC_BASED_CTL_HLT_EXIT = 1u << 7;
constexpr uint32_t PROC_BASED_CTL_SECONDARY = 1u << 31;
constexpr uint32_t PROC_BASED_CTL2_EPT = 1u << 1;
constexpr uint32_t PROC_BASED_CTL2_UNRESTRICTED = 1u << 7;
constexpr uint32_t VM_EXIT_HOST_ADDR_SPACE_SIZE = 1u << 9;

constexpr uint64_t CR0_PE = 1ull << 0;
constexpr uint64_t CR0_NE = 1ull << 5;
constexpr uint64_t CR0_PG = 1ull << 31;
constexpr uint64_t CR4_PSE = 1ull << 4;

constexpr uint32_t kGuestCodeAccess = 0xC09B;
constexpr uint32_t kGuestDataAccess = 0xC093;
constexpr uint32_t kGuestTssAccess = 0x008B;
constexpr uint32_t kGuestUnusableAccess = 0x10000;
constexpr uint32_t kGuestSegmentLimit = 0xFFFFF;

constexpr uint32_t kGuestTssAddr = 0x5000;
constexpr uint32_t kGuestTssLimit = 0x67;
constexpr uint32_t kGuestStubAddr = 0x7000;
constexpr uint32_t kGuestStackAddr = 0x8000;
constexpr uint32_t kGuestPageDirAddr = 0x9000;

uint32_t vmxAdjustControls(uint64_t msrValue, uint32_t desired)
{
	uint32_t allowed0 = static_cast<uint32_t>(msrValue);
	uint32_t allowed1 = static_cast<uint32_t>(msrValue >> 32);
	return (desired | allowed0) & allowed1;
}

uint64_t vmxAdjustCr(uint64_t fixed0, uint64_t fixed1, uint64_t desired)
{
	return (desired | fixed0) & fixed1;
}

bool vmxWriteField(g_vmx_vcpu_id vcpu, uint32_t field, uint64_t value, const char* name)
{
	uint32_t error = 0;
	g_vmx_status st = g_vmx_vcpu_write(vcpu, field, value, &error);
	if(st != G_VMX_STATUS_SUCCESS)
	{
		printf("lve: vmwrite %s (0x%04x) failed status=%u error=0x%x\n",
		       name, field, st, error);
		return false;
	}
	return true;
}

bool vmxReadField(g_vmx_vcpu_id vcpu, uint32_t field, uint64_t* outValue, const char* name)
{
	uint32_t error = 0;
	g_vmx_status st = g_vmx_vcpu_read(vcpu, field, outValue, &error);
	if(st != G_VMX_STATUS_SUCCESS)
	{
		printf("lve: vmread %s (0x%04x) failed status=%u error=0x%x\n",
		       name, field, st, error);
		return false;
	}
	return true;
}

bool writeLinuxStub(lve_guest_memory* guest, const lve_linux_load_result& linuxInfo)
{
	if(!guest || !guest->base)
		return false;

	uint8_t* mem = static_cast<uint8_t*>(guest->base);
	if(kGuestStubAddr + 15 >= guest->size)
		return false;

	const uint32_t entry = static_cast<uint32_t>(linuxInfo.entry_point);

	mem[kGuestStubAddr + 0] = 0xBE; // mov esi, imm32
	*reinterpret_cast<uint32_t*>(mem + kGuestStubAddr + 1) =
		static_cast<uint32_t>(linuxInfo.boot_params_addr);
	mem[kGuestStubAddr + 5] = 0xBC; // mov esp, imm32
	*reinterpret_cast<uint32_t*>(mem + kGuestStubAddr + 6) = kGuestStackAddr;
	mem[kGuestStubAddr + 10] = 0xE9; // jmp rel32
	int32_t rel = static_cast<int32_t>(entry - (kGuestStubAddr + 15));
	*reinterpret_cast<int32_t*>(mem + kGuestStubAddr + 11) = rel;

	return true;
}

bool writeTestStub(lve_guest_memory* guest)
{
	if(!guest || !guest->base)
		return false;
	uint8_t* mem = static_cast<uint8_t*>(guest->base);
	if(kGuestStubAddr + 1 >= guest->size)
		return false;
	mem[kGuestStubAddr] = 0xF4; // hlt
	return true;
}

bool setupGuestPaging(lve_guest_memory* guest)
{
	if(!guest || !guest->base)
		return false;
	if(kGuestPageDirAddr + G_PAGE_SIZE > guest->size)
		return false;

	auto* dir = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(guest->base) + kGuestPageDirAddr);
	for(size_t i = 0; i < 1024; ++i)
		dir[i] = 0;
	dir[0] = 0x83; // present | write | 4MB page
	return true;
}

bool setupGuestTss(lve_guest_memory* guest)
{
	if(!guest || !guest->base)
		return false;
	if(kGuestTssAddr + kGuestTssLimit + 1 >= guest->size)
		return false;

	memset(static_cast<uint8_t*>(guest->base) + kGuestTssAddr, 0, kGuestTssLimit + 1);
	return true;
}

} // namespace

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
	printf("pin-ctls: 0x%016llx\n", (unsigned long long) caps.vmxPinCtls);
	printf("proc-ctls: 0x%016llx\n", (unsigned long long) caps.vmxProcCtls);
	printf("proc-ctls2: 0x%016llx\n", (unsigned long long) caps.vmxProcCtls2);
	printf("exit-ctls: 0x%016llx\n", (unsigned long long) caps.vmxExitCtls);
	printf("entry-ctls: 0x%016llx\n", (unsigned long long) caps.vmxEntryCtls);
	printf("cr0-fixed: 0x%016llx/0x%016llx\n",
	       (unsigned long long) caps.vmxCr0Fixed0, (unsigned long long) caps.vmxCr0Fixed1);
	printf("cr4-fixed: 0x%016llx/0x%016llx\n",
	       (unsigned long long) caps.vmxCr4Fixed0, (unsigned long long) caps.vmxCr4Fixed1);
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
			if(!parseUint64(argv[++i], &ramMb))
			{
				printf("lve: invalid --ram value\n");
				return 1;
			}
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
		const uint64_t requestedMb = ramMb;
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
		if(!caps.active)
		{
			g_vmx_status enableStatus = g_vmx_enable();
			if(enableStatus != G_VMX_STATUS_SUCCESS)
			{
				printf("lve: vmx enable failed (%s)\n", vmxStatusToString(enableStatus));
				return 1;
			}
			status = g_vmx_get_caps(&caps);
			if(status != G_VMX_STATUS_SUCCESS)
			{
				printf("lve: vmx caps failed after enable (%s)\n", vmxStatusToString(status));
				return 1;
			}
		}

		const uint64_t ramBytes = requestedMb * 1024ull * 1024ull;
		lve_guest_memory guest{};
		if(!lveGuestAllocate(&guest, ramBytes))
		{
			printf("lve: guest memory allocation failed\n");
			return 1;
		}
		const uint64_t actualMb = guest.size / (1024ull * 1024ull);
		if(actualMb != requestedMb)
		{
			printf("lve: ram clamped to %llu MiB (requested %llu MiB)\n",
			       static_cast<unsigned long long>(actualMb),
			       static_cast<unsigned long long>(requestedMb));
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
		       static_cast<unsigned long long>(actualMb),
		       static_cast<unsigned long long>(guest.pageCount));
		printf("eptp: 0x%016llx\n", static_cast<unsigned long long>(eptp));

		g_vmx_vcpu_id vcpu = 0;
		g_vmx_status vcpuStatus = g_vmx_vcpu_create(&vcpu);
		if(vcpuStatus != G_VMX_STATUS_SUCCESS)
		{
			printf("lve: vcpu create failed (%s)\n", vmxStatusToString(vcpuStatus));
			lveEptFree(&ept);
			lveGuestFree(&guest);
			return 1;
		}
		uint32_t vmxError = 0;
		vcpuStatus = g_vmx_vcpu_clear(vcpu, &vmxError);
		if(vcpuStatus != G_VMX_STATUS_SUCCESS)
		{
			printf("lve: vcpu clear failed (%s) error=0x%x\n",
			       vmxStatusToString(vcpuStatus), vmxError);
			g_vmx_vcpu_destroy(vcpu);
			lveEptFree(&ept);
			lveGuestFree(&guest);
			return 1;
		}

		uint32_t pinCtls = vmxAdjustControls(caps.vmxPinCtls, 0);
		uint32_t procDesired = PROC_BASED_CTL_HLT_EXIT;
		if(caps.hasEpt || caps.hasUnrestrictedGuest)
			procDesired |= PROC_BASED_CTL_SECONDARY;
		uint32_t procCtls = vmxAdjustControls(caps.vmxProcCtls, procDesired);
		uint32_t proc2Desired = 0;
		if(caps.hasEpt)
			proc2Desired |= PROC_BASED_CTL2_EPT;
		if(caps.hasUnrestrictedGuest)
			proc2Desired |= PROC_BASED_CTL2_UNRESTRICTED;
		uint32_t procCtls2 = vmxAdjustControls(caps.vmxProcCtls2, proc2Desired);
		uint32_t exitCtls = vmxAdjustControls(caps.vmxExitCtls, VM_EXIT_HOST_ADDR_SPACE_SIZE);
		uint32_t entryCtls = vmxAdjustControls(caps.vmxEntryCtls, 0);

		uint64_t guestCr0 = vmxAdjustCr(caps.vmxCr0Fixed0, caps.vmxCr0Fixed1, CR0_PE | CR0_NE);
		uint64_t guestCr4 = vmxAdjustCr(caps.vmxCr4Fixed0, caps.vmxCr4Fixed1, 0);
		uint64_t guestCr3 = 0;
		if(guestCr0 & CR0_PG)
		{
			if(!setupGuestPaging(&guest))
			{
				printf("lve: failed to set up guest paging\n");
				g_vmx_vcpu_destroy(vcpu);
				lveEptFree(&ept);
				lveGuestFree(&guest);
				return 1;
			}
			guestCr4 = vmxAdjustCr(caps.vmxCr4Fixed0, caps.vmxCr4Fixed1, CR4_PSE);
			guestCr3 = kGuestPageDirAddr;
		}

		bool stubOk = false;
		if(bzImagePath)
			stubOk = writeLinuxStub(&guest, linuxInfo);
		else
			stubOk = writeTestStub(&guest);
		if(!stubOk)
		{
			printf("lve: failed to write guest stub\n");
			g_vmx_vcpu_destroy(vcpu);
			lveEptFree(&ept);
			lveGuestFree(&guest);
			return 1;
		}
		if(!setupGuestTss(&guest))
		{
			printf("lve: failed to set up guest TSS\n");
			g_vmx_vcpu_destroy(vcpu);
			lveEptFree(&ept);
			lveGuestFree(&guest);
			return 1;
		}

		bool ok = true;
		ok &= vmxWriteField(vcpu, VMCS_LINK_POINTER, 0xFFFFFFFFFFFFFFFFull, "link-pointer");
		ok &= vmxWriteField(vcpu, VMCS_PIN_BASED_CTLS, pinCtls, "pin-ctls");
		ok &= vmxWriteField(vcpu, VMCS_PROC_BASED_CTLS, procCtls, "proc-ctls");
		ok &= vmxWriteField(vcpu, VMCS_PROC_BASED_CTLS2, procCtls2, "proc-ctls2");
		ok &= vmxWriteField(vcpu, VMCS_EXIT_CTLS, exitCtls, "exit-ctls");
		ok &= vmxWriteField(vcpu, VMCS_ENTRY_CTLS, entryCtls, "entry-ctls");
		ok &= vmxWriteField(vcpu, VMCS_EXIT_MSR_STORE_COUNT, 0, "exit-msr-store");
		ok &= vmxWriteField(vcpu, VMCS_EXIT_MSR_LOAD_COUNT, 0, "exit-msr-load");
		ok &= vmxWriteField(vcpu, VMCS_ENTRY_MSR_LOAD_COUNT, 0, "entry-msr-load");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_IA32_DEBUGCTL, 0, "guest-debugctl");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_IA32_PAT, 0, "guest-pat");
		ok &= vmxWriteField(vcpu, VMCS_ENTRY_INTR_INFO, 0, "entry-intr");
		ok &= vmxWriteField(vcpu, VMCS_ENTRY_EXCEPTION_ERROR, 0, "entry-exc-err");
		ok &= vmxWriteField(vcpu, VMCS_ENTRY_INSTRUCTION_LEN, 0, "entry-ins-len");
		ok &= vmxWriteField(vcpu, VMCS_VMEXIT_MSR_STORE_ADDR, 0, "exit-msr-store-addr");
		ok &= vmxWriteField(vcpu, VMCS_VMEXIT_MSR_LOAD_ADDR, 0, "exit-msr-load-addr");
		ok &= vmxWriteField(vcpu, VMCS_VMENTRY_MSR_LOAD_ADDR, 0, "entry-msr-load-addr");
		ok &= vmxWriteField(vcpu, VMCS_TSC_OFFSET, 0, "tsc-offset");
		ok &= vmxWriteField(vcpu, VMCS_EXCEPTION_BITMAP, 0, "exception-bitmap");
		ok &= vmxWriteField(vcpu, VMCS_PF_ERROR_MASK, 0, "pf-mask");
		ok &= vmxWriteField(vcpu, VMCS_PF_ERROR_MATCH, 0, "pf-match");
		ok &= vmxWriteField(vcpu, VMCS_CR3_TARGET_COUNT, 0, "cr3-target-count");
		ok &= vmxWriteField(vcpu, VMCS_CR0_GUEST_HOST_MASK, 0, "cr0-mask");
		ok &= vmxWriteField(vcpu, VMCS_CR4_GUEST_HOST_MASK, 0, "cr4-mask");
		ok &= vmxWriteField(vcpu, VMCS_CR0_READ_SHADOW, 0, "cr0-shadow");
		ok &= vmxWriteField(vcpu, VMCS_CR4_READ_SHADOW, 0, "cr4-shadow");
		ok &= vmxWriteField(vcpu, VMCS_EPT_POINTER, eptp, "eptp");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_IA32_EFER, 0, "guest-efer");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_IA32_PERF_GLOBAL_CTRL, 0, "guest-perf-ctrl");

		ok &= vmxWriteField(vcpu, VMCS_GUEST_CR0, guestCr0, "guest-cr0");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_CR3, guestCr3, "guest-cr3");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_CR4, guestCr4, "guest-cr4");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_DR7, 0x400, "guest-dr7");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_PENDING_DBG, 0, "guest-pending-dbg");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_RSP, kGuestStackAddr, "guest-rsp");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_RIP, kGuestStubAddr, "guest-rip");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_RFLAGS, 0x2, "guest-rflags");

		ok &= vmxWriteField(vcpu, VMCS_GUEST_CS_SELECTOR, 0x08, "guest-cs");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_DS_SELECTOR, 0x10, "guest-ds");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_ES_SELECTOR, 0x10, "guest-es");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_FS_SELECTOR, 0x10, "guest-fs");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_GS_SELECTOR, 0x10, "guest-gs");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_SS_SELECTOR, 0x10, "guest-ss");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_LDTR_SELECTOR, 0, "guest-ldtr");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_TR_SELECTOR, 0x28, "guest-tr");

		ok &= vmxWriteField(vcpu, VMCS_GUEST_CS_BASE, 0, "guest-cs-base");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_DS_BASE, 0, "guest-ds-base");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_ES_BASE, 0, "guest-es-base");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_FS_BASE, 0, "guest-fs-base");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_GS_BASE, 0, "guest-gs-base");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_SS_BASE, 0, "guest-ss-base");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_LDTR_BASE, 0, "guest-ldtr-base");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_TR_BASE, kGuestTssAddr, "guest-tr-base");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_GDTR_BASE, 0, "guest-gdtr-base");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_IDTR_BASE, 0, "guest-idtr-base");

		ok &= vmxWriteField(vcpu, VMCS_GUEST_CS_LIMIT, kGuestSegmentLimit, "guest-cs-limit");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_DS_LIMIT, kGuestSegmentLimit, "guest-ds-limit");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_ES_LIMIT, kGuestSegmentLimit, "guest-es-limit");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_FS_LIMIT, kGuestSegmentLimit, "guest-fs-limit");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_GS_LIMIT, kGuestSegmentLimit, "guest-gs-limit");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_SS_LIMIT, kGuestSegmentLimit, "guest-ss-limit");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_LDTR_LIMIT, 0, "guest-ldtr-limit");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_TR_LIMIT, kGuestTssLimit, "guest-tr-limit");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_GDTR_LIMIT, 0xFFFF, "guest-gdtr-limit");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_IDTR_LIMIT, 0xFFFF, "guest-idtr-limit");

		ok &= vmxWriteField(vcpu, VMCS_GUEST_CS_ACCESS, kGuestCodeAccess, "guest-cs-access");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_DS_ACCESS, kGuestDataAccess, "guest-ds-access");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_ES_ACCESS, kGuestDataAccess, "guest-es-access");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_FS_ACCESS, kGuestDataAccess, "guest-fs-access");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_GS_ACCESS, kGuestDataAccess, "guest-gs-access");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_SS_ACCESS, kGuestDataAccess, "guest-ss-access");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_LDTR_ACCESS, kGuestUnusableAccess, "guest-ldtr-access");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_TR_ACCESS, kGuestTssAccess, "guest-tr-access");

		ok &= vmxWriteField(vcpu, VMCS_GUEST_INTERRUPTIBILITY, 0, "guest-intr");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_ACTIVITY_STATE, 0, "guest-state");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_SYSENTER_CS, 0, "guest-sysenter-cs");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_SYSENTER_ESP, 0, "guest-sysenter-esp");
		ok &= vmxWriteField(vcpu, VMCS_GUEST_SYSENTER_EIP, 0, "guest-sysenter-eip");

		if(!ok)
		{
			printf("lve: vmcs setup failed\n");
			g_vmx_vcpu_destroy(vcpu);
			lveEptFree(&ept);
			lveGuestFree(&guest);
			return 1;
		}

		vcpuStatus = g_vmx_vcpu_launch(vcpu, &vmxError);
		if(vcpuStatus != G_VMX_STATUS_SUCCESS)
		{
			printf("lve: vmlaunch failed (%s) error=0x%x\n",
			       vmxStatusToString(vcpuStatus), vmxError);
		}
		else
		{
			uint64_t exitReason = 0;
			uint64_t exitQual = 0;
			vmxReadField(vcpu, VMCS_VMEXIT_REASON, &exitReason, "exit-reason");
			vmxReadField(vcpu, VMCS_VMEXIT_QUALIFICATION, &exitQual, "exit-qual");
			printf("lve: vmexit reason=0x%llx qual=0x%llx\n",
			       (unsigned long long) exitReason,
			       (unsigned long long) exitQual);
		}

		g_vmx_vcpu_destroy(vcpu);
		lveEptFree(&ept);
		lveGuestFree(&guest);
	}

	return 0;
}

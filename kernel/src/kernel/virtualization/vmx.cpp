/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Heartix (Ghost-derived) kernel                                           *
 *                                                                           *
 *  Intel VMX helpers (capability query + host enable/disable)               *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "kernel/virtualization/vmx.hpp"

#include "kernel/logger/logger.hpp"
#include "kernel/memory/heap.hpp"
#include "kernel/memory/gdt.hpp"
#include "kernel/memory/memory.hpp"
#include "kernel/memory/paging.hpp"
#include "kernel/system/interrupts/interrupts.hpp"
#include "kernel/system/mutex.hpp"
#include "kernel/system/processor/processor.hpp"

namespace
{

constexpr uint32_t IA32_FEATURE_CONTROL = 0x3A;
constexpr uint32_t IA32_VMX_BASIC = 0x480;
constexpr uint32_t IA32_VMX_PINBASED_CTLS = 0x481;
constexpr uint32_t IA32_VMX_PROCBASED_CTLS = 0x482;
constexpr uint32_t IA32_VMX_EXIT_CTLS = 0x483;
constexpr uint32_t IA32_VMX_ENTRY_CTLS = 0x484;
constexpr uint32_t IA32_VMX_CR0_FIXED0 = 0x486;
constexpr uint32_t IA32_VMX_CR0_FIXED1 = 0x487;
constexpr uint32_t IA32_VMX_CR4_FIXED0 = 0x488;
constexpr uint32_t IA32_VMX_CR4_FIXED1 = 0x489;
constexpr uint32_t IA32_VMX_PROCBASED_CTLS2 = 0x48B;
constexpr uint32_t IA32_VMX_EPT_VPID_CAP = 0x48C;
constexpr uint32_t IA32_VMX_TRUE_PINBASED_CTLS = 0x48D;
constexpr uint32_t IA32_VMX_TRUE_PROCBASED_CTLS = 0x48E;
constexpr uint32_t IA32_VMX_TRUE_EXIT_CTLS = 0x48F;
constexpr uint32_t IA32_VMX_TRUE_ENTRY_CTLS = 0x490;

constexpr uint64_t FEATURE_CONTROL_LOCK = (1ULL << 0);
constexpr uint64_t FEATURE_CONTROL_VMXON = (1ULL << 2);

constexpr uint32_t PROCBASED_CTL_SECONDARY = (1u << 31);
constexpr uint32_t PROCBASED_CTL2_EPT = (1u << 1);
constexpr uint32_t PROCBASED_CTL2_UNRESTRICTED = (1u << 7);

constexpr uint64_t CR4_VMXE = (1ULL << 13);

constexpr uint32_t VMCS_PIN_BASED_CTLS = 0x4000;
constexpr uint32_t VMCS_PROC_BASED_CTLS = 0x4002;
constexpr uint32_t VMCS_EXIT_CTLS = 0x400C;
constexpr uint32_t VMCS_ENTRY_CTLS = 0x4012;
constexpr uint32_t VMCS_PROC_BASED_CTLS2 = 0x401E;

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

constexpr uint32_t VMCS_HOST_ES_SELECTOR = 0x0C00;
constexpr uint32_t VMCS_HOST_CS_SELECTOR = 0x0C02;
constexpr uint32_t VMCS_HOST_SS_SELECTOR = 0x0C04;
constexpr uint32_t VMCS_HOST_DS_SELECTOR = 0x0C06;
constexpr uint32_t VMCS_HOST_FS_SELECTOR = 0x0C08;
constexpr uint32_t VMCS_HOST_GS_SELECTOR = 0x0C0A;
constexpr uint32_t VMCS_HOST_TR_SELECTOR = 0x0C0C;
constexpr uint32_t VMCS_HOST_SYSENTER_CS = 0x4C00;
constexpr uint32_t VMCS_HOST_CR0 = 0x6C00;
constexpr uint32_t VMCS_HOST_CR3 = 0x6C02;
constexpr uint32_t VMCS_HOST_CR4 = 0x6C04;
constexpr uint32_t VMCS_HOST_FS_BASE = 0x6C06;
constexpr uint32_t VMCS_HOST_GS_BASE = 0x6C08;
constexpr uint32_t VMCS_HOST_TR_BASE = 0x6C0A;
constexpr uint32_t VMCS_HOST_GDTR_BASE = 0x6C0C;
constexpr uint32_t VMCS_HOST_IDTR_BASE = 0x6C0E;
constexpr uint32_t VMCS_HOST_SYSENTER_ESP = 0x6C10;
constexpr uint32_t VMCS_HOST_SYSENTER_EIP = 0x6C12;
constexpr uint32_t VMCS_HOST_RSP = 0x6C14;
constexpr uint32_t VMCS_HOST_RIP = 0x6C16;
constexpr uint32_t VMCS_HOST_IA32_EFER = 0x2C02;
constexpr uint32_t VMCS_HOST_IA32_PAT = 0x2C00;
constexpr uint32_t VMCS_HOST_IA32_PERF_GLOBAL_CTRL = 0x2C04;

constexpr uint32_t VMCS_EPT_POINTER = 0x201A;

constexpr uint64_t IA32_FS_BASE = 0xC0000100;
constexpr uint64_t IA32_GS_BASE = 0xC0000101;
constexpr uint64_t IA32_EFER = 0xC0000080;
constexpr uint64_t IA32_PAT = 0x277;
constexpr uint64_t IA32_PERF_GLOBAL_CTRL = 0x38F;
constexpr uint64_t IA32_SYSENTER_CS = 0x174;
constexpr uint64_t IA32_SYSENTER_ESP = 0x175;
constexpr uint64_t IA32_SYSENTER_EIP = 0x176;

struct vmx_cpu_state
{
	bool active;
	g_virtual_address vmxonRegion;
	g_physical_address vmxonPhys;
	uint32_t revisionId;
};

g_mutex vmxStateLock;
bool vmxStateLockReady = false;
vmx_cpu_state* vmxStates = nullptr;
uint32_t vmxStateCount = 0;

struct vmx_vcpu
{
	g_vmx_vcpu_id id;
	g_pid owner;
	g_virtual_address vmcsRegion;
	g_physical_address vmcsPhys;
	uint32_t revisionId;
	g_virtual_address hostStack;
	g_virtual_address hostStackTop;
	g_virtual_address hostSavedRsp;
	g_virtual_address hostSavedRip;
	uint32_t lastExitReason;
	uint64_t lastExitQualification;
	vmx_vcpu* next;
};

g_mutex vmxVcpuLock;
bool vmxVcpuLockReady = false;
vmx_vcpu* vmxVcpuList = nullptr;
g_vmx_vcpu_id vmxNextVcpuId = 1;
vmx_vcpu** vmxActiveVcpu = nullptr;
bool vmxActiveVcpuReady = false;

uint64_t vmxReadMsr(uint32_t msr)
{
	uint32_t lo = 0;
	uint32_t hi = 0;
	processorReadMsr(msr, &lo, &hi);
	return (static_cast<uint64_t>(hi) << 32) | lo;
}

void vmxWriteMsr(uint32_t msr, uint64_t value)
{
	processorWriteMsr(msr, static_cast<uint32_t>(value), static_cast<uint32_t>(value >> 32));
}

uint64_t vmxReadCr0()
{
	uint64_t value = 0;
	asm volatile("mov %%cr0, %0" : "=r"(value));
	return value;
}

void vmxWriteCr0(uint64_t value)
{
	asm volatile("mov %0, %%cr0" : : "r"(value) : "memory");
}

uint64_t vmxReadCr4()
{
	uint64_t value = 0;
	asm volatile("mov %%cr4, %0" : "=r"(value));
	return value;
}

uint64_t vmxReadCr3()
{
	uint64_t value = 0;
	asm volatile("mov %%cr3, %0" : "=r"(value));
	return value;
}

void vmxWriteCr4(uint64_t value)
{
	asm volatile("mov %0, %%cr4" : : "r"(value) : "memory");
}

void vmxAdjustCr0Cr4()
{
	uint64_t cr0Fixed0 = vmxReadMsr(IA32_VMX_CR0_FIXED0);
	uint64_t cr0Fixed1 = vmxReadMsr(IA32_VMX_CR0_FIXED1);
	uint64_t cr0 = vmxReadCr0();
	cr0 |= cr0Fixed0;
	cr0 &= cr0Fixed1;
	vmxWriteCr0(cr0);

	uint64_t cr4Fixed0 = vmxReadMsr(IA32_VMX_CR4_FIXED0);
	uint64_t cr4Fixed1 = vmxReadMsr(IA32_VMX_CR4_FIXED1);
	uint64_t cr4 = vmxReadCr4();
	cr4 |= cr4Fixed0;
	cr4 &= cr4Fixed1;
	cr4 |= CR4_VMXE;
	vmxWriteCr4(cr4);
}

bool vmxInstructionOk(uint8_t status)
{
	return status == 0;
}

bool vmxOn(g_physical_address phys)
{
	uint8_t status = 0;
	uint64_t operand = phys;
	asm volatile("vmxon %1; setna %0" : "=rm"(status) : "m"(operand) : "cc", "memory");
	return vmxInstructionOk(status);
}

bool vmxOff()
{
	uint8_t status = 0;
	asm volatile("vmxoff; setna %0" : "=rm"(status) : : "cc", "memory");
	return vmxInstructionOk(status);
}

bool vmxClear(g_physical_address phys)
{
	uint8_t status = 0;
	uint64_t operand = phys;
	asm volatile("vmclear %1; setna %0" : "=rm"(status) : "m"(operand) : "cc", "memory");
	return vmxInstructionOk(status);
}

bool vmxLoad(g_physical_address phys)
{
	uint8_t status = 0;
	uint64_t operand = phys;
	asm volatile("vmptrld %1; setna %0" : "=rm"(status) : "m"(operand) : "cc", "memory");
	return vmxInstructionOk(status);
}

bool vmxVmread(uint64_t field, uint64_t* value)
{
	uint8_t status = 0;
	uint64_t out = 0;
	asm volatile("vmread %2, %1; setna %0" : "=rm"(status), "=r"(out) : "r"(field) : "cc");
	if(vmxInstructionOk(status) && value)
		*value = out;
	return vmxInstructionOk(status);
}

bool vmxVmwrite(uint64_t field, uint64_t value)
{
	uint8_t status = 0;
	asm volatile("vmwrite %2, %1; setna %0" : "=rm"(status) : "r"(field), "r"(value) : "cc");
	return vmxInstructionOk(status);
}

bool vmxLaunch()
{
	uint8_t status = 0;
	asm volatile("vmlaunch; setna %0" : "=rm"(status) : : "cc", "memory");
	return vmxInstructionOk(status);
}

bool vmxResume()
{
	uint8_t status = 0;
	asm volatile("vmresume; setna %0" : "=rm"(status) : : "cc", "memory");
	return vmxInstructionOk(status);
}

uint32_t vmxReadInstructionError()
{
	uint64_t error = 0;
	if(!vmxVmread(0x4400, &error))
		return 0xFFFFFFFFu;
	return static_cast<uint32_t>(error);
}

struct vmx_desc_table
{
	uint16_t limit;
	uint64_t base;
} __attribute__((packed));

vmx_desc_table vmxReadGdtr()
{
	vmx_desc_table table{};
	asm volatile("sgdt %0" : "=m"(table));
	return table;
}

vmx_desc_table vmxReadIdtr()
{
	vmx_desc_table table{};
	asm volatile("sidt %0" : "=m"(table));
	return table;
}

uint16_t vmxReadCs()
{
	uint16_t sel = 0;
	asm volatile("mov %%cs, %0" : "=r"(sel));
	return sel;
}

uint16_t vmxReadSs()
{
	uint16_t sel = 0;
	asm volatile("mov %%ss, %0" : "=r"(sel));
	return sel;
}

uint16_t vmxReadDs()
{
	uint16_t sel = 0;
	asm volatile("mov %%ds, %0" : "=r"(sel));
	return sel;
}

uint16_t vmxReadEs()
{
	uint16_t sel = 0;
	asm volatile("mov %%es, %0" : "=r"(sel));
	return sel;
}

uint16_t vmxReadFs()
{
	uint16_t sel = 0;
	asm volatile("mov %%fs, %0" : "=r"(sel));
	return sel;
}

uint16_t vmxReadGs()
{
	uint16_t sel = 0;
	asm volatile("mov %%gs, %0" : "=r"(sel));
	return sel;
}

uint16_t vmxReadTr()
{
	uint16_t sel = 0;
	asm volatile("str %0" : "=r"(sel));
	return sel;
}

uint64_t vmxSegmentBaseFromDescriptor(const g_gdt_descriptor& desc)
{
	uint64_t base = desc.baseLow;
	base |= static_cast<uint64_t>(desc.baseMiddle) << 16;
	base |= static_cast<uint64_t>(desc.baseHigh) << 24;
	return base;
}

uint64_t vmxReadTssBase(const vmx_desc_table& gdtr, uint16_t selector)
{
	if(selector == 0)
		return 0;

	auto* tssDesc = reinterpret_cast<const g_gdt_tss_descriptor*>(
			gdtr.base + (selector & ~0x7u));
	uint64_t base = vmxSegmentBaseFromDescriptor(tssDesc->main);
	base |= static_cast<uint64_t>(tssDesc->baseUpper) << 32;
	return base;
}

bool vmxEnsureActiveVcpuArray()
{
	if(vmxActiveVcpuReady && vmxActiveVcpu)
		return true;

	const uint32_t cores = processorGetNumberOfProcessors();
	if(cores == 0)
		return false;

	vmxActiveVcpu = static_cast<vmx_vcpu**>(heapAllocateClear(sizeof(vmx_vcpu*) * cores));
	vmxActiveVcpuReady = vmxActiveVcpu != nullptr;
	return vmxActiveVcpuReady;
}

extern "C" void vmxExitHandler();

void vmxWriteHostState(vmx_vcpu* vcpu)
{
	vmx_desc_table gdtr = vmxReadGdtr();
	vmx_desc_table idtr = vmxReadIdtr();
	uint16_t cs = vmxReadCs();
	uint16_t ss = vmxReadSs();
	uint16_t ds = vmxReadDs();
	uint16_t es = vmxReadEs();
	uint16_t fs = vmxReadFs();
	uint16_t gs = vmxReadGs();
	uint16_t tr = vmxReadTr();

	uint64_t fsBase = vmxReadMsr(IA32_FS_BASE);
	uint64_t gsBase = vmxReadMsr(IA32_GS_BASE);
	uint64_t efer = vmxReadMsr(IA32_EFER);
	uint64_t pat = vmxReadMsr(IA32_PAT);
	uint64_t perf = vmxReadMsr(IA32_PERF_GLOBAL_CTRL);
	uint64_t sysenterCs = vmxReadMsr(IA32_SYSENTER_CS);
	uint64_t sysenterEsp = vmxReadMsr(IA32_SYSENTER_ESP);
	uint64_t sysenterEip = vmxReadMsr(IA32_SYSENTER_EIP);

	uint64_t hostRsp = vcpu->hostStackTop & ~0xFull;
	uint64_t hostRip = reinterpret_cast<uint64_t>(&vmxExitHandler);
	uint64_t trBase = vmxReadTssBase(gdtr, tr);

	vmxVmwrite(VMCS_HOST_CR0, vmxReadCr0());
	vmxVmwrite(VMCS_HOST_CR3, vmxReadCr3());
	vmxVmwrite(VMCS_HOST_CR4, vmxReadCr4());

	vmxVmwrite(VMCS_HOST_ES_SELECTOR, es);
	vmxVmwrite(VMCS_HOST_CS_SELECTOR, cs);
	vmxVmwrite(VMCS_HOST_SS_SELECTOR, ss);
	vmxVmwrite(VMCS_HOST_DS_SELECTOR, ds);
	vmxVmwrite(VMCS_HOST_FS_SELECTOR, fs);
	vmxVmwrite(VMCS_HOST_GS_SELECTOR, gs);
	vmxVmwrite(VMCS_HOST_TR_SELECTOR, tr);

	vmxVmwrite(VMCS_HOST_FS_BASE, fsBase);
	vmxVmwrite(VMCS_HOST_GS_BASE, gsBase);
	vmxVmwrite(VMCS_HOST_TR_BASE, trBase);
	vmxVmwrite(VMCS_HOST_GDTR_BASE, gdtr.base);
	vmxVmwrite(VMCS_HOST_IDTR_BASE, idtr.base);

	vmxVmwrite(VMCS_HOST_SYSENTER_CS, sysenterCs);
	vmxVmwrite(VMCS_HOST_SYSENTER_ESP, sysenterEsp);
	vmxVmwrite(VMCS_HOST_SYSENTER_EIP, sysenterEip);
	vmxVmwrite(VMCS_HOST_IA32_EFER, efer);
	vmxVmwrite(VMCS_HOST_IA32_PAT, pat);
	vmxVmwrite(VMCS_HOST_IA32_PERF_GLOBAL_CTRL, perf);

	vmxVmwrite(VMCS_HOST_RSP, hostRsp);
	vmxVmwrite(VMCS_HOST_RIP, hostRip);
}

__attribute__((noreturn))
void vmxExitHandler()
{
	uint32_t cpu = processorGetCurrentId();
	vmx_vcpu* vcpu = (vmxActiveVcpu && cpu < vmxStateCount) ? vmxActiveVcpu[cpu] : nullptr;
	if(vcpu)
	{
		uint64_t reason = 0;
		uint64_t qualification = 0;
		vmxVmread(0x4402, &reason);
		vmxVmread(0x6400, &qualification);
		vcpu->lastExitReason = static_cast<uint32_t>(reason);
		vcpu->lastExitQualification = qualification;
	}

	if(!vcpu || !vcpu->hostSavedRip || !vcpu->hostSavedRsp)
	{
		for(;;)
			asm volatile("hlt");
	}

	asm volatile(
		"mov %0, %%rsp\n"
		"jmp *%1\n"
		:
		: "r"(vcpu->hostSavedRsp), "r"(vcpu->hostSavedRip)
		: "memory");
	__builtin_unreachable();
}

bool vmxEnsureStateArray()
{
	if(!vmxStateLockReady)
	{
		mutexInitializeGlobal(&vmxStateLock, __func__);
		vmxStateLockReady = true;
	}

	if(vmxStates)
		return true;

	vmxStateCount = processorGetNumberOfProcessors();
	if(vmxStateCount == 0)
		vmxStateCount = 1;

	vmxStates = static_cast<vmx_cpu_state*>(heapAllocateClear(sizeof(vmx_cpu_state) * vmxStateCount));
	return vmxStates != nullptr;
}

bool vmxEnsureVcpuList()
{
	if(!vmxVcpuLockReady)
	{
		mutexInitializeGlobal(&vmxVcpuLock, __func__);
		vmxVcpuLockReady = true;
	}
	return true;
}

vmx_vcpu* vmxFindVcpuLocked(g_vmx_vcpu_id id, g_pid owner)
{
	vmx_vcpu* entry = vmxVcpuList;
	while(entry)
	{
		if(entry->id == id && entry->owner == owner)
			return entry;
		entry = entry->next;
	}
	return nullptr;
}

} // namespace

bool vmxIsEnabled(uint32_t cpu)
{
	if(!vmxStates || cpu >= vmxStateCount)
		return false;
	return vmxStates[cpu].active;
}

g_vmx_status vmxGetCaps(g_vmx_caps* caps)
{
	if(!caps)
		return G_VMX_STATUS_FAILED;

	memorySetBytes(caps, 0, sizeof(*caps));

	const bool hasVmx = processorHasFeature(g_cpuid_extended_ecx_feature::VMX);
	const bool hasMsr = processorHasFeature(g_cpuid_standard_edx_feature::MSR);
	caps->vmx = hasVmx ? 1 : 0;

	if(!hasVmx || !hasMsr)
		return G_VMX_STATUS_UNSUPPORTED;

	const uint64_t featureControl = vmxReadMsr(IA32_FEATURE_CONTROL);
	caps->featureControlLocked = (featureControl & FEATURE_CONTROL_LOCK) ? 1 : 0;
	caps->featureControlVmxon = (featureControl & FEATURE_CONTROL_VMXON) ? 1 : 0;

	caps->vmxBasic = vmxReadMsr(IA32_VMX_BASIC);
	const bool useTrueControls = (caps->vmxBasic & (1ULL << 55)) != 0;
	if(useTrueControls)
	{
		caps->vmxPinCtls = vmxReadMsr(IA32_VMX_TRUE_PINBASED_CTLS);
		caps->vmxProcCtls = vmxReadMsr(IA32_VMX_TRUE_PROCBASED_CTLS);
		caps->vmxExitCtls = vmxReadMsr(IA32_VMX_TRUE_EXIT_CTLS);
		caps->vmxEntryCtls = vmxReadMsr(IA32_VMX_TRUE_ENTRY_CTLS);
	}
	else
	{
		caps->vmxPinCtls = vmxReadMsr(IA32_VMX_PINBASED_CTLS);
		caps->vmxProcCtls = vmxReadMsr(IA32_VMX_PROCBASED_CTLS);
		caps->vmxExitCtls = vmxReadMsr(IA32_VMX_EXIT_CTLS);
		caps->vmxEntryCtls = vmxReadMsr(IA32_VMX_ENTRY_CTLS);
	}
	caps->vmxProcCtls2 = vmxReadMsr(IA32_VMX_PROCBASED_CTLS2);
	caps->vmxCr0Fixed0 = vmxReadMsr(IA32_VMX_CR0_FIXED0);
	caps->vmxCr0Fixed1 = vmxReadMsr(IA32_VMX_CR0_FIXED1);
	caps->vmxCr4Fixed0 = vmxReadMsr(IA32_VMX_CR4_FIXED0);
	caps->vmxCr4Fixed1 = vmxReadMsr(IA32_VMX_CR4_FIXED1);
	caps->vmxEptVpid = vmxReadMsr(IA32_VMX_EPT_VPID_CAP);
	caps->revisionId = static_cast<uint32_t>(caps->vmxBasic & 0x7FFFFFFF);

	const uint32_t procAllowed1 = static_cast<uint32_t>(caps->vmxProcCtls >> 32);
	const bool secondaryAllowed = (procAllowed1 & PROCBASED_CTL_SECONDARY);
	const uint32_t proc2Allowed1 = static_cast<uint32_t>(caps->vmxProcCtls2 >> 32);

	caps->hasEpt = (secondaryAllowed && (proc2Allowed1 & PROCBASED_CTL2_EPT)) ? 1 : 0;
	caps->hasUnrestrictedGuest = (secondaryAllowed && (proc2Allowed1 & PROCBASED_CTL2_UNRESTRICTED)) ? 1 : 0;

	caps->active = vmxIsEnabled(processorGetCurrentId()) ? 1 : 0;

	if((featureControl & FEATURE_CONTROL_LOCK) && !(featureControl & FEATURE_CONTROL_VMXON))
		return G_VMX_STATUS_DISABLED;

	return G_VMX_STATUS_SUCCESS;
}

g_vmx_status vmxEnable()
{
	if(!processorHasFeature(g_cpuid_extended_ecx_feature::VMX))
		return G_VMX_STATUS_UNSUPPORTED;

	if(!processorHasFeature(g_cpuid_standard_edx_feature::MSR))
		return G_VMX_STATUS_UNSUPPORTED;

	if(!vmxEnsureStateArray())
		return G_VMX_STATUS_NO_MEMORY;

	mutexAcquire(&vmxStateLock);
	const uint32_t cpu = processorGetCurrentId();
	if(cpu >= vmxStateCount)
	{
		mutexRelease(&vmxStateLock);
		return G_VMX_STATUS_FAILED;
	}

	if(vmxStates[cpu].active)
	{
		mutexRelease(&vmxStateLock);
		return G_VMX_STATUS_SUCCESS;
	}

	uint64_t featureControl = vmxReadMsr(IA32_FEATURE_CONTROL);
	if(!(featureControl & FEATURE_CONTROL_LOCK))
	{
		featureControl |= FEATURE_CONTROL_LOCK | FEATURE_CONTROL_VMXON;
		vmxWriteMsr(IA32_FEATURE_CONTROL, featureControl);
	}
	else if(!(featureControl & FEATURE_CONTROL_VMXON))
	{
		mutexRelease(&vmxStateLock);
		return G_VMX_STATUS_DISABLED;
	}

	vmxAdjustCr0Cr4();

	const uint64_t vmxBasic = vmxReadMsr(IA32_VMX_BASIC);
	const uint32_t revisionId = static_cast<uint32_t>(vmxBasic & 0x7FFFFFFF);

	g_virtual_address vmxonRegion = memoryAllocateKernel(1);
	if(!vmxonRegion)
	{
		mutexRelease(&vmxStateLock);
		return G_VMX_STATUS_NO_MEMORY;
	}

	g_physical_address vmxonPhys = pagingVirtualToPhysical(vmxonRegion);
	memorySetBytes(reinterpret_cast<void*>(vmxonRegion), 0, G_PAGE_SIZE);
	*reinterpret_cast<uint32_t*>(vmxonRegion) = revisionId;

	bool hadIF = interruptsAreEnabled();
	interruptsDisable();
	bool ok = vmxOn(vmxonPhys);
	if(hadIF)
		interruptsEnable();

	if(!ok)
	{
		memoryFreeKernelRange(vmxonRegion);
		mutexRelease(&vmxStateLock);
		logWarn("%! VMXON failed on core %i (phys=%h)", "vmx", cpu, vmxonPhys);
		return G_VMX_STATUS_FAILED;
	}

	vmxStates[cpu].active = true;
	vmxStates[cpu].vmxonRegion = vmxonRegion;
	vmxStates[cpu].vmxonPhys = vmxonPhys;
	vmxStates[cpu].revisionId = revisionId;

	mutexRelease(&vmxStateLock);
	return G_VMX_STATUS_SUCCESS;
}

g_vmx_status vmxDisable()
{
	if(!processorHasFeature(g_cpuid_extended_ecx_feature::VMX))
		return G_VMX_STATUS_UNSUPPORTED;

	if(!vmxEnsureStateArray())
		return G_VMX_STATUS_NO_MEMORY;

	mutexAcquire(&vmxStateLock);
	const uint32_t cpu = processorGetCurrentId();
	if(cpu >= vmxStateCount)
	{
		mutexRelease(&vmxStateLock);
		return G_VMX_STATUS_FAILED;
	}

	if(!vmxStates[cpu].active)
	{
		mutexRelease(&vmxStateLock);
		return G_VMX_STATUS_SUCCESS;
	}

	bool hadIF = interruptsAreEnabled();
	interruptsDisable();
	bool ok = vmxOff();
	if(hadIF)
		interruptsEnable();

	if(!ok)
	{
		mutexRelease(&vmxStateLock);
		logWarn("%! VMXOFF failed on core %i", "vmx", cpu);
		return G_VMX_STATUS_FAILED;
	}

	if(vmxStates[cpu].vmxonRegion)
		memoryFreeKernelRange(vmxStates[cpu].vmxonRegion);

	vmxStates[cpu].active = false;
	vmxStates[cpu].vmxonRegion = 0;
	vmxStates[cpu].vmxonPhys = 0;
	vmxStates[cpu].revisionId = 0;

	mutexRelease(&vmxStateLock);
	return G_VMX_STATUS_SUCCESS;
}

g_vmx_status vmxVcpuCreate(g_pid owner, g_vmx_vcpu_id* outId)
{
	if(!outId)
		return G_VMX_STATUS_FAILED;

	if(!processorHasFeature(g_cpuid_extended_ecx_feature::VMX))
		return G_VMX_STATUS_UNSUPPORTED;

	if(!vmxIsEnabled(processorGetCurrentId()))
		return G_VMX_STATUS_DISABLED;

	if(!vmxEnsureVcpuList())
		return G_VMX_STATUS_FAILED;
	if(!vmxEnsureActiveVcpuArray())
		return G_VMX_STATUS_FAILED;

	mutexAcquire(&vmxVcpuLock);

	vmx_vcpu* vcpu = static_cast<vmx_vcpu*>(heapAllocateClear(sizeof(vmx_vcpu)));
	if(!vcpu)
	{
		mutexRelease(&vmxVcpuLock);
		return G_VMX_STATUS_NO_MEMORY;
	}

	const uint32_t revisionId = static_cast<uint32_t>(vmxReadMsr(IA32_VMX_BASIC) & 0x7FFFFFFF);
	g_virtual_address vmcsRegion = memoryAllocateKernel(1);
	if(!vmcsRegion)
	{
		heapFree(vcpu);
		mutexRelease(&vmxVcpuLock);
		return G_VMX_STATUS_NO_MEMORY;
	}

	g_virtual_address hostStack = memoryAllocateKernel(4);
	if(!hostStack)
	{
		memoryFreeKernelRange(vmcsRegion);
		heapFree(vcpu);
		mutexRelease(&vmxVcpuLock);
		return G_VMX_STATUS_NO_MEMORY;
	}

	g_physical_address vmcsPhys = pagingVirtualToPhysical(vmcsRegion);
	memorySetBytes(reinterpret_cast<void*>(vmcsRegion), 0, G_PAGE_SIZE);
	*reinterpret_cast<uint32_t*>(vmcsRegion) = revisionId;

	vcpu->id = vmxNextVcpuId++;
	vcpu->owner = owner;
	vcpu->vmcsRegion = vmcsRegion;
	vcpu->vmcsPhys = vmcsPhys;
	vcpu->revisionId = revisionId;
	vcpu->hostStack = hostStack;
	vcpu->hostStackTop = hostStack + (4 * G_PAGE_SIZE);
	vcpu->next = vmxVcpuList;
	vmxVcpuList = vcpu;

	mutexRelease(&vmxVcpuLock);

	*outId = vcpu->id;
	return G_VMX_STATUS_SUCCESS;
}

g_vmx_status vmxVcpuDestroy(g_pid owner, g_vmx_vcpu_id id)
{
	if(!vmxEnsureVcpuList())
		return G_VMX_STATUS_FAILED;

	mutexAcquire(&vmxVcpuLock);
	vmx_vcpu* prev = nullptr;
	vmx_vcpu* entry = vmxVcpuList;
	while(entry)
	{
		if(entry->id == id && entry->owner == owner)
		{
			if(prev)
				prev->next = entry->next;
			else
				vmxVcpuList = entry->next;

			if(vmxIsEnabled(processorGetCurrentId()))
				vmxClear(entry->vmcsPhys);
			if(entry->vmcsRegion)
				memoryFreeKernelRange(entry->vmcsRegion);
			if(entry->hostStack)
				memoryFreeKernelRange(entry->hostStack);
			heapFree(entry);
			mutexRelease(&vmxVcpuLock);
			return G_VMX_STATUS_SUCCESS;
		}
		prev = entry;
		entry = entry->next;
	}
	mutexRelease(&vmxVcpuLock);
	return G_VMX_STATUS_FAILED;
}

g_vmx_status vmxVcpuClear(g_pid owner, g_vmx_vcpu_id id, uint32_t* outError)
{
	if(!vmxIsEnabled(processorGetCurrentId()))
		return G_VMX_STATUS_DISABLED;

	if(!vmxEnsureVcpuList())
		return G_VMX_STATUS_FAILED;

	mutexAcquire(&vmxVcpuLock);
	vmx_vcpu* vcpu = vmxFindVcpuLocked(id, owner);
	if(!vcpu)
	{
		mutexRelease(&vmxVcpuLock);
		return G_VMX_STATUS_FAILED;
	}
	bool ok = vmxClear(vcpu->vmcsPhys);
	uint32_t error = ok ? 0 : vmxReadInstructionError();
	mutexRelease(&vmxVcpuLock);

	if(outError)
		*outError = error;
	return ok ? G_VMX_STATUS_SUCCESS : G_VMX_STATUS_FAILED;
}

g_vmx_status vmxVcpuLoad(g_pid owner, g_vmx_vcpu_id id, uint32_t* outError)
{
	if(!vmxIsEnabled(processorGetCurrentId()))
		return G_VMX_STATUS_DISABLED;

	if(!vmxEnsureVcpuList())
		return G_VMX_STATUS_FAILED;

	mutexAcquire(&vmxVcpuLock);
	vmx_vcpu* vcpu = vmxFindVcpuLocked(id, owner);
	if(!vcpu)
	{
		mutexRelease(&vmxVcpuLock);
		return G_VMX_STATUS_FAILED;
	}
	bool ok = vmxLoad(vcpu->vmcsPhys);
	uint32_t error = ok ? 0 : vmxReadInstructionError();
	mutexRelease(&vmxVcpuLock);

	if(outError)
		*outError = error;
	return ok ? G_VMX_STATUS_SUCCESS : G_VMX_STATUS_FAILED;
}

g_vmx_status vmxVcpuRead(g_pid owner, g_vmx_vcpu_id id, uint32_t field, uint64_t* outValue, uint32_t* outError)
{
	if(!outValue)
		return G_VMX_STATUS_FAILED;

	if(!vmxIsEnabled(processorGetCurrentId()))
		return G_VMX_STATUS_DISABLED;

	if(!vmxEnsureVcpuList())
		return G_VMX_STATUS_FAILED;

	mutexAcquire(&vmxVcpuLock);
	vmx_vcpu* vcpu = vmxFindVcpuLocked(id, owner);
	if(!vcpu)
	{
		mutexRelease(&vmxVcpuLock);
		return G_VMX_STATUS_FAILED;
	}
	bool ok = vmxLoad(vcpu->vmcsPhys);
	if(ok)
		ok = vmxVmread(field, outValue);
	uint32_t error = ok ? 0 : vmxReadInstructionError();
	mutexRelease(&vmxVcpuLock);

	if(outError)
		*outError = error;
	return ok ? G_VMX_STATUS_SUCCESS : G_VMX_STATUS_FAILED;
}

g_vmx_status vmxVcpuWrite(g_pid owner, g_vmx_vcpu_id id, uint32_t field, uint64_t value, uint32_t* outError)
{
	if(!vmxIsEnabled(processorGetCurrentId()))
		return G_VMX_STATUS_DISABLED;

	if(!vmxEnsureVcpuList())
		return G_VMX_STATUS_FAILED;

	mutexAcquire(&vmxVcpuLock);
	vmx_vcpu* vcpu = vmxFindVcpuLocked(id, owner);
	if(!vcpu)
	{
		mutexRelease(&vmxVcpuLock);
		return G_VMX_STATUS_FAILED;
	}
	bool ok = vmxLoad(vcpu->vmcsPhys);
	if(ok)
		ok = vmxVmwrite(field, value);
	uint32_t error = ok ? 0 : vmxReadInstructionError();
	mutexRelease(&vmxVcpuLock);

	if(outError)
		*outError = error;
	return ok ? G_VMX_STATUS_SUCCESS : G_VMX_STATUS_FAILED;
}

g_vmx_status vmxVcpuLaunch(g_pid owner, g_vmx_vcpu_id id, uint32_t* outError)
{
	if(!vmxIsEnabled(processorGetCurrentId()))
		return G_VMX_STATUS_DISABLED;

	if(!vmxEnsureVcpuList())
		return G_VMX_STATUS_FAILED;

	mutexAcquire(&vmxVcpuLock);
	vmx_vcpu* vcpu = vmxFindVcpuLocked(id, owner);
	if(!vcpu)
	{
		mutexRelease(&vmxVcpuLock);
		return G_VMX_STATUS_FAILED;
	}
	bool ok = vmxLoad(vcpu->vmcsPhys);
	if(ok)
		vmxWriteHostState(vcpu);

	if(ok && vmxActiveVcpu && processorGetCurrentId() < vmxStateCount)
		vmxActiveVcpu[processorGetCurrentId()] = vcpu;

	uint8_t status = 0;
	if(ok)
	{
		uint64_t* ripSlot = &vcpu->hostSavedRip;
		uint64_t* rspSlot = &vcpu->hostSavedRsp;
		asm volatile(
			"leaq 1f(%%rip), %%r10\n"
			"mov %%r10, (%1)\n"
			"pushfq\n"
			"push %%rax\n"
			"push %%rcx\n"
			"push %%rdx\n"
			"push %%rbx\n"
			"push %%rbp\n"
			"push %%rsi\n"
			"push %%rdi\n"
			"push %%r8\n"
			"push %%r9\n"
			"push %%r10\n"
			"push %%r11\n"
			"push %%r12\n"
			"push %%r13\n"
			"push %%r14\n"
			"push %%r15\n"
			"mov %%rsp, (%2)\n"
			"vmlaunch\n"
			"setna %0\n"
			"jmp 2f\n"
			"1:\n"
			"movb $0, %0\n"
			"2:\n"
			"pop %%r15\n"
			"pop %%r14\n"
			"pop %%r13\n"
			"pop %%r12\n"
			"pop %%r11\n"
			"pop %%r10\n"
			"pop %%r9\n"
			"pop %%r8\n"
			"pop %%rdi\n"
			"pop %%rsi\n"
			"pop %%rbp\n"
			"pop %%rbx\n"
			"pop %%rdx\n"
			"pop %%rcx\n"
			"pop %%rax\n"
			"popfq\n"
			: "=rm"(status)
			: "r"(ripSlot), "r"(rspSlot)
			: "cc", "memory");
	}

	uint32_t error = (status == 0) ? 0 : vmxReadInstructionError();
	mutexRelease(&vmxVcpuLock);

	if(outError)
		*outError = error;
	return (ok && status == 0) ? G_VMX_STATUS_SUCCESS : G_VMX_STATUS_FAILED;
}

g_vmx_status vmxVcpuResume(g_pid owner, g_vmx_vcpu_id id, uint32_t* outError)
{
	if(!vmxIsEnabled(processorGetCurrentId()))
		return G_VMX_STATUS_DISABLED;

	if(!vmxEnsureVcpuList())
		return G_VMX_STATUS_FAILED;

	mutexAcquire(&vmxVcpuLock);
	vmx_vcpu* vcpu = vmxFindVcpuLocked(id, owner);
	if(!vcpu)
	{
		mutexRelease(&vmxVcpuLock);
		return G_VMX_STATUS_FAILED;
	}
	bool ok = vmxLoad(vcpu->vmcsPhys);
	if(ok)
		vmxWriteHostState(vcpu);

	if(ok && vmxActiveVcpu && processorGetCurrentId() < vmxStateCount)
		vmxActiveVcpu[processorGetCurrentId()] = vcpu;

	uint8_t status = 0;
	if(ok)
	{
		uint64_t* ripSlot = &vcpu->hostSavedRip;
		uint64_t* rspSlot = &vcpu->hostSavedRsp;
		asm volatile(
			"leaq 1f(%%rip), %%r10\n"
			"mov %%r10, (%1)\n"
			"pushfq\n"
			"push %%rax\n"
			"push %%rcx\n"
			"push %%rdx\n"
			"push %%rbx\n"
			"push %%rbp\n"
			"push %%rsi\n"
			"push %%rdi\n"
			"push %%r8\n"
			"push %%r9\n"
			"push %%r10\n"
			"push %%r11\n"
			"push %%r12\n"
			"push %%r13\n"
			"push %%r14\n"
			"push %%r15\n"
			"mov %%rsp, (%2)\n"
			"vmresume\n"
			"setna %0\n"
			"jmp 2f\n"
			"1:\n"
			"movb $0, %0\n"
			"2:\n"
			"pop %%r15\n"
			"pop %%r14\n"
			"pop %%r13\n"
			"pop %%r12\n"
			"pop %%r11\n"
			"pop %%r10\n"
			"pop %%r9\n"
			"pop %%r8\n"
			"pop %%rdi\n"
			"pop %%rsi\n"
			"pop %%rbp\n"
			"pop %%rbx\n"
			"pop %%rdx\n"
			"pop %%rcx\n"
			"pop %%rax\n"
			"popfq\n"
			: "=rm"(status)
			: "r"(ripSlot), "r"(rspSlot)
			: "cc", "memory");
	}

	uint32_t error = (status == 0) ? 0 : vmxReadInstructionError();
	mutexRelease(&vmxVcpuLock);

	if(outError)
		*outError = error;
	return (ok && status == 0) ? G_VMX_STATUS_SUCCESS : G_VMX_STATUS_FAILED;
}

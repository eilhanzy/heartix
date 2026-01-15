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
#include "kernel/memory/memory.hpp"
#include "kernel/memory/paging.hpp"
#include "kernel/system/interrupts/interrupts.hpp"
#include "kernel/system/mutex.hpp"
#include "kernel/system/processor/processor.hpp"

namespace
{

constexpr uint32_t IA32_FEATURE_CONTROL = 0x3A;
constexpr uint32_t IA32_VMX_BASIC = 0x480;
constexpr uint32_t IA32_VMX_PROCBASED_CTLS = 0x482;
constexpr uint32_t IA32_VMX_CR0_FIXED0 = 0x486;
constexpr uint32_t IA32_VMX_CR0_FIXED1 = 0x487;
constexpr uint32_t IA32_VMX_CR4_FIXED0 = 0x488;
constexpr uint32_t IA32_VMX_CR4_FIXED1 = 0x489;
constexpr uint32_t IA32_VMX_PROCBASED_CTLS2 = 0x48B;
constexpr uint32_t IA32_VMX_EPT_VPID_CAP = 0x48C;

constexpr uint64_t FEATURE_CONTROL_LOCK = (1ULL << 0);
constexpr uint64_t FEATURE_CONTROL_VMXON = (1ULL << 2);

constexpr uint32_t PROCBASED_CTL_SECONDARY = (1u << 31);
constexpr uint32_t PROCBASED_CTL2_EPT = (1u << 1);
constexpr uint32_t PROCBASED_CTL2_UNRESTRICTED = (1u << 7);

constexpr uint64_t CR4_VMXE = (1ULL << 13);

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
	caps->vmxProcCtls = vmxReadMsr(IA32_VMX_PROCBASED_CTLS);
	caps->vmxProcCtls2 = vmxReadMsr(IA32_VMX_PROCBASED_CTLS2);
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

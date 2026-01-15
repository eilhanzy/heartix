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

struct vmx_vcpu
{
	g_vmx_vcpu_id id;
	g_pid owner;
	g_virtual_address vmcsRegion;
	g_physical_address vmcsPhys;
	uint32_t revisionId;
	vmx_vcpu* next;
};

g_mutex vmxVcpuLock;
bool vmxVcpuLockReady = false;
vmx_vcpu* vmxVcpuList = nullptr;
g_vmx_vcpu_id vmxNextVcpuId = 1;

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

	g_physical_address vmcsPhys = pagingVirtualToPhysical(vmcsRegion);
	memorySetBytes(reinterpret_cast<void*>(vmcsRegion), 0, G_PAGE_SIZE);
	*reinterpret_cast<uint32_t*>(vmcsRegion) = revisionId;

	vcpu->id = vmxNextVcpuId++;
	vcpu->owner = owner;
	vcpu->vmcsRegion = vmcsRegion;
	vcpu->vmcsPhys = vmcsPhys;
	vcpu->revisionId = revisionId;
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
		ok = vmxLaunch();
	uint32_t error = ok ? 0 : vmxReadInstructionError();
	mutexRelease(&vmxVcpuLock);

	if(outError)
		*outError = error;
	return ok ? G_VMX_STATUS_SUCCESS : G_VMX_STATUS_FAILED;
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
		ok = vmxResume();
	uint32_t error = ok ? 0 : vmxReadInstructionError();
	mutexRelease(&vmxVcpuLock);

	if(outError)
		*outError = error;
	return ok ? G_VMX_STATUS_SUCCESS : G_VMX_STATUS_FAILED;
}

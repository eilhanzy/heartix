/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Heartix (Ghost-derived) kernel                                           *
 *                                                                           *
 *  Intel VMX helpers (capability query + host enable/disable)               *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef __KERNEL_VIRTUALIZATION_VMX__
#define __KERNEL_VIRTUALIZATION_VMX__

#include <ghost/stdint.h>
#include <ghost/system/types.h>
#include <ghost/tasks/types.h>

g_vmx_status vmxGetCaps(g_vmx_caps* caps);
g_vmx_status vmxEnable();
g_vmx_status vmxDisable();
bool vmxIsEnabled(uint32_t cpu);

g_vmx_status vmxVcpuCreate(g_pid owner, g_vmx_vcpu_id* outId);
g_vmx_status vmxVcpuDestroy(g_pid owner, g_vmx_vcpu_id id);
g_vmx_status vmxVcpuClear(g_pid owner, g_vmx_vcpu_id id, uint32_t* outError);
g_vmx_status vmxVcpuLoad(g_pid owner, g_vmx_vcpu_id id, uint32_t* outError);
g_vmx_status vmxVcpuRead(g_pid owner, g_vmx_vcpu_id id, uint32_t field, uint64_t* outValue, uint32_t* outError);
g_vmx_status vmxVcpuWrite(g_pid owner, g_vmx_vcpu_id id, uint32_t field, uint64_t value, uint32_t* outError);
g_vmx_status vmxVcpuLaunch(g_pid owner, g_vmx_vcpu_id id, uint32_t* outError);
g_vmx_status vmxVcpuResume(g_pid owner, g_vmx_vcpu_id id, uint32_t* outError);

#endif

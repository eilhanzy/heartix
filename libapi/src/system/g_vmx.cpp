/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Ghost, a micro-kernel based operating system for the x86 architecture    *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "ghost/syscall.h"
#include "ghost/system.h"
#include "ghost/system/callstructs.h"

g_vmx_status g_vmx_get_caps(g_vmx_caps* outCaps)
{
	if(!outCaps)
		return G_VMX_STATUS_FAILED;

	g_syscall_vmx data{};
	data.command = G_VMX_CMD_GET_CAPS;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);

	if(data.status == G_VMX_STATUS_SUCCESS)
		*outCaps = data.caps;

	return data.status;
}

g_vmx_status g_vmx_enable()
{
	g_syscall_vmx data{};
	data.command = G_VMX_CMD_ENABLE;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);
	return data.status;
}

g_vmx_status g_vmx_disable()
{
	g_syscall_vmx data{};
	data.command = G_VMX_CMD_DISABLE;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);
	return data.status;
}

g_vmx_status g_vmx_vcpu_create(g_vmx_vcpu_id* outId)
{
	if(!outId)
		return G_VMX_STATUS_FAILED;

	g_syscall_vmx data{};
	data.command = G_VMX_CMD_VCPU_CREATE;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);
	if(data.status == G_VMX_STATUS_SUCCESS)
		*outId = data.vcpu;

	return data.status;
}

g_vmx_status g_vmx_vcpu_destroy(g_vmx_vcpu_id id)
{
	g_syscall_vmx data{};
	data.command = G_VMX_CMD_VCPU_DESTROY;
	data.vcpu = id;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);
	return data.status;
}

g_vmx_status g_vmx_vcpu_clear(g_vmx_vcpu_id id, uint32_t* outError)
{
	g_syscall_vmx data{};
	data.command = G_VMX_CMD_VCPU_CLEAR;
	data.vcpu = id;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);
	if(outError)
		*outError = data.error;
	return data.status;
}

g_vmx_status g_vmx_vcpu_load(g_vmx_vcpu_id id, uint32_t* outError)
{
	g_syscall_vmx data{};
	data.command = G_VMX_CMD_VCPU_LOAD;
	data.vcpu = id;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);
	if(outError)
		*outError = data.error;
	return data.status;
}

g_vmx_status g_vmx_vcpu_read(g_vmx_vcpu_id id, uint32_t field, uint64_t* outValue, uint32_t* outError)
{
	if(!outValue)
		return G_VMX_STATUS_FAILED;

	g_syscall_vmx data{};
	data.command = G_VMX_CMD_VCPU_READ;
	data.vcpu = id;
	data.field = field;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);
	if(data.status == G_VMX_STATUS_SUCCESS)
		*outValue = data.value;
	if(outError)
		*outError = data.error;
	return data.status;
}

g_vmx_status g_vmx_vcpu_write(g_vmx_vcpu_id id, uint32_t field, uint64_t value, uint32_t* outError)
{
	g_syscall_vmx data{};
	data.command = G_VMX_CMD_VCPU_WRITE;
	data.vcpu = id;
	data.field = field;
	data.value = value;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);
	if(outError)
		*outError = data.error;
	return data.status;
}

g_vmx_status g_vmx_vcpu_launch(g_vmx_vcpu_id id, uint32_t* outError)
{
	g_syscall_vmx data{};
	data.command = G_VMX_CMD_VCPU_LAUNCH;
	data.vcpu = id;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);
	if(outError)
		*outError = data.error;
	return data.status;
}

g_vmx_status g_vmx_vcpu_resume(g_vmx_vcpu_id id, uint32_t* outError)
{
	g_syscall_vmx data{};
	data.command = G_VMX_CMD_VCPU_RESUME;
	data.vcpu = id;

	g_syscall(G_SYSCALL_VMX, (g_address) &data);
	if(outError)
		*outError = data.error;
	return data.status;
}

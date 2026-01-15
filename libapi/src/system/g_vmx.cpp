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

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

g_vmx_status vmxGetCaps(g_vmx_caps* caps);
g_vmx_status vmxEnable();
g_vmx_status vmxDisable();
bool vmxIsEnabled(uint32_t cpu);

#endif

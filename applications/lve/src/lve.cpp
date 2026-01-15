/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  LVE probe utility                                                       *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <ghost/system.h>

#include <stdio.h>
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

	for(int i = 1; i < argc; ++i)
	{
		if(strcmp(argv[i], "--enable") == 0)
			doEnable = true;
		else if(strcmp(argv[i], "--disable") == 0)
			doDisable = true;
		else if(strcmp(argv[i], "--help") == 0)
		{
			printf("lve [--enable|--disable]\n");
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

	return 0;
}

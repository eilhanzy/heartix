/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Ghost, a micro-kernel based operating system for the x86 architecture    *
 *  Copyright (C) 2015, Max Schlüssel <lokoxe@gmail.com>                     *
 *                                                                           *
 *  This program is free software: you can redistribute it and/or modify     *
 *  it under the terms of the GNU General Public License as published by     *
 *  the Free Software Foundation, either version 3 of the License, or        *
 *  (at your option) any later version.                                      *
 *                                                                           *
 *  This program is distributed in the hope that it will be useful,          *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU General Public License for more details.                             *
 *                                                                           *
 *  You should have received a copy of the GNU General Public License        *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.    *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef GHOST_API_SYSTEM_TYPES
#define GHOST_API_SYSTEM_TYPES

#include "../common.h"
#include "../stdint.h"

__BEGIN_C

/**
 * VM86 related
 */
typedef uint8_t g_vm86_call_status;

#define G_VM86_CALL_STATUS_SUCCESSFUL 0
#define G_VM86_CALL_STATUS_FAILED_NOT_PERMITTED 1

typedef struct
{
	uint16_t ax;
	uint16_t bx;
	uint16_t cx;
	uint16_t dx;
	uint16_t si;
	uint16_t di;
	uint16_t ds;
	uint16_t es;
} __attribute__((packed)) g_vm86_registers;

/**
 * VMX (Intel VT-x) related
 */
typedef uint8_t g_vmx_status;

#define G_VMX_STATUS_SUCCESS 0
#define G_VMX_STATUS_UNSUPPORTED 1
#define G_VMX_STATUS_DISABLED 2
#define G_VMX_STATUS_NO_MEMORY 3
#define G_VMX_STATUS_FAILED 4
#define G_VMX_STATUS_NOT_PERMITTED 5

typedef uint32_t g_vmx_command;

#define G_VMX_CMD_GET_CAPS 1
#define G_VMX_CMD_ENABLE 2
#define G_VMX_CMD_DISABLE 3

typedef struct
{
	uint8_t vmx;
	uint8_t featureControlLocked;
	uint8_t featureControlVmxon;
	uint8_t hasEpt;
	uint8_t hasUnrestrictedGuest;
	uint8_t active;
	uint8_t reserved[2];
	uint32_t revisionId;
	uint64_t vmxBasic;
	uint64_t vmxProcCtls;
	uint64_t vmxProcCtls2;
	uint64_t vmxEptVpid;
} __attribute__((packed)) g_vmx_caps;

__END_C

#endif

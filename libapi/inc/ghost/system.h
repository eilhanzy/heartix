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

#ifndef GHOST_API_SYSTEM
#define GHOST_API_SYSTEM

#include "ghost/common.h"
#include "ghost/stdint.h"
#include "ghost/filesystem/types.h"
#include "ghost/system/types.h"
#include "ghost/memory/types.h"
#include "ghost/tasks.h" // for g_exit declaration used by __G_NOT_IMPLEMENTED


__BEGIN_C
// not implemented warning
#define __G_NOT_IMPLEMENTED_WARN(name)		g_log("'" #name "' is not implemented");
#define __G_NOT_IMPLEMENTED(name)		    __G_NOT_IMPLEMENTED_WARN(name) g_exit(0);


/**
 * Performs a Virtual 8086 BIOS interrupt call.
 *
 * @param interrupt
 * 		number of the interrupt to fire
 * @param in
 * 		input register values
 * @param out
 * 		output register values
 *
 * @return one of the G_VM86_CALL_STATUS_* status codes
 *
 * @security-level DRIVER
 */
g_vm86_call_status g_call_vm86(uint32_t interrupt, g_vm86_registers* in, g_vm86_registers* out);

/**
 * Prints a message to the log.
 *
 * @param message
 * 		the message to log
 *
 * @security-level APPLICATION
 */
void g_log(const char* message);

/**
 * Opens the kernel log pipe for reading.
 * 
 * @return file descriptor to the kernel log pipe
 */
g_fd g_open_log_pipe();

/**
 * Copies the buffered kernel log history into the provided buffer.
 *
 * @param buffer
 * 		target buffer
 * @param length
 * 		number of bytes available in buffer
 * @return number of bytes copied
 *
 * @security-level APPLICATION
 */
size_t g_read_log_history(char* buffer, size_t length);

/**
 * Enables or disables logging to the video output.
 *
 * @param enabled
 * 		whether to enable/disable video log
 *
 * @security-level APPLICATION
 */
void g_set_video_log(uint8_t enabled);

/**
 * Test-call for kernel debugging.
 *
 * @security-level VARIOUS
 */
uint32_t g_test(uint32_t test);

/**
 * Creates an IOAPIC redirection entry for an IRQ.
 *
 * @security-level DRIVER
 */
void g_irq_create_redirect(uint32_t source, uint32_t irq);

/**
 * Awaits a specific IRQ. This may only be used on core 0.
 *
 * @param irq
 *     the IRQ
 *
 * @param-opt timeout
 *     timeout in milliseconds
 *
 * @security-level DRIVER
 */
void g_await_irq(uint8_t irq);
void g_await_irq_t(uint8_t irq, uint32_t timeout);

uint8_t g_io_port_read_byte(uint16_t port);
uint16_t g_io_port_read_word(uint16_t port);
uint32_t g_io_port_read_dword(uint16_t port);

void g_io_port_write_byte(uint16_t port, uint8_t data);
void g_io_port_write_word(uint16_t port, uint16_t data);
void g_io_port_write_dword(uint16_t port, uint32_t data);

/**
 * Ask the kernel for the EFI framebuffer data.
 *
 * @security-level DRIVER
 */
void g_get_efi_framebuffer(g_address* outFramebuffer, uint16_t* outWidth, uint16_t* outHeight, uint16_t* outBpp, uint32_t* outPitch);

/**
 * Retrieves VMX capability information from the kernel.
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_get_caps(g_vmx_caps* outCaps);

/**
 * Enables VMX operation on the current CPU.
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_enable();

/**
 * Disables VMX operation on the current CPU.
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_disable();

/**
 * Creates a VMCS-backed vCPU handle.
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_vcpu_create(g_vmx_vcpu_id* outId);

/**
 * Destroys a previously created vCPU handle.
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_vcpu_destroy(g_vmx_vcpu_id id);

/**
 * Clears the VMCS state for a vCPU.
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_vcpu_clear(g_vmx_vcpu_id id, uint32_t* outError);

/**
 * Loads the VMCS for a vCPU on the current CPU.
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_vcpu_load(g_vmx_vcpu_id id, uint32_t* outError);

/**
 * Reads a VMCS field.
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_vcpu_read(g_vmx_vcpu_id id, uint32_t field, uint64_t* outValue, uint32_t* outError);

/**
 * Writes a VMCS field.
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_vcpu_write(g_vmx_vcpu_id id, uint32_t field, uint64_t value, uint32_t* outError);

/**
 * Attempts VM-entry (VMLAUNCH).
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_vcpu_launch(g_vmx_vcpu_id id, uint32_t* outError);

/**
 * Attempts VM-entry (VMRESUME).
 *
 * @security-level DRIVER
 */
g_vmx_status g_vmx_vcpu_resume(g_vmx_vcpu_id id, uint32_t* outError);

__END_C

#endif

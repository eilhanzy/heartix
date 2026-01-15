/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  LVE Linux bzImage loader helpers                                         *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef LVE_LINUX_LOADER_HPP
#define LVE_LINUX_LOADER_HPP

#include "ept.hpp"

#include <stdint.h>

struct lve_linux_load_result
{
	uint64_t kernel_load_addr;
	uint64_t kernel_size;
	uint64_t boot_params_addr;
	uint64_t cmdline_addr;
	uint64_t initrd_addr;
	uint64_t initrd_size;
	uint32_t version;
};

bool lveLoadBzImageToGuest(const char* path, lve_guest_memory* guest, const char* cmdline,
                           const char* initrdPath, lve_linux_load_result* out);

#endif

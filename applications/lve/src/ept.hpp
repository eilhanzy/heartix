/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  LVE guest memory + EPT helpers                                           *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef LVE_EPT_HPP
#define LVE_EPT_HPP

#include <ghost/memory/types.h>
#include <stdint.h>

struct lve_guest_memory
{
	void* base;
	uint64_t size;
	uint64_t pageCount;
	g_physical_address* hostPages;
};

struct lve_ept
{
	uint64_t* pml4;
	g_physical_address pml4Phys;
	void* pages;
};

bool lveGuestAllocate(lve_guest_memory* mem, uint64_t sizeBytes);
void lveGuestFree(lve_guest_memory* mem);

bool lveEptBuild(lve_ept* ept, const lve_guest_memory* mem);
uint64_t lveEptMakePointer(const lve_ept* ept);
void lveEptFree(lve_ept* ept);

#endif

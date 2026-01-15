/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  LVE guest memory + EPT helpers                                           *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "ept.hpp"

#include <ghost/memory.h>

#include <stdlib.h>
#include <string.h>

namespace
{

constexpr uint64_t kEptRead = 1ull << 0;
constexpr uint64_t kEptWrite = 1ull << 1;
constexpr uint64_t kEptExec = 1ull << 2;
constexpr uint64_t kEptPerms = kEptRead | kEptWrite | kEptExec;
constexpr uint64_t kEptMemTypeWb = 6ull << 3;
constexpr uint64_t kEptLeafFlags = kEptPerms | kEptMemTypeWb;
constexpr uint64_t kEptAddrMask = ~0xFFFULL;

constexpr uint64_t kEptpMemTypeWb = 6ull;
constexpr uint64_t kEptpPageWalk4 = 3ull << 3;

struct lve_ept_page
{
	g_physical_address phys;
	uint64_t* virt;
	lve_ept_page* next;
};

uint16_t eptPml4Index(uint64_t gpa)
{
	return static_cast<uint16_t>((gpa >> 39) & 0x1FF);
}

uint16_t eptPdptIndex(uint64_t gpa)
{
	return static_cast<uint16_t>((gpa >> 30) & 0x1FF);
}

uint16_t eptPdIndex(uint64_t gpa)
{
	return static_cast<uint16_t>((gpa >> 21) & 0x1FF);
}

uint16_t eptPtIndex(uint64_t gpa)
{
	return static_cast<uint16_t>((gpa >> 12) & 0x1FF);
}

lve_ept_page* eptAllocPage(lve_ept* ept)
{
	void* page = g_alloc_mem(G_PAGE_SIZE);
	if(!page)
		return nullptr;

	memset(page, 0, G_PAGE_SIZE);
	g_physical_address phys = g_virt_to_phys(page);
	if(!phys)
	{
		g_unmap(page);
		return nullptr;
	}

	auto* node = static_cast<lve_ept_page*>(malloc(sizeof(lve_ept_page)));
	if(!node)
	{
		g_unmap(page);
		return nullptr;
	}

	node->phys = phys;
	node->virt = static_cast<uint64_t*>(page);
	node->next = static_cast<lve_ept_page*>(ept->pages);
	ept->pages = node;
	return node;
}

uint64_t* eptLookupPage(const lve_ept* ept, g_physical_address phys)
{
	auto* node = static_cast<lve_ept_page*>(ept->pages);
	while(node)
	{
		if(node->phys == phys)
			return node->virt;
		node = node->next;
	}
	return nullptr;
}

uint64_t* eptGetOrAllocTable(lve_ept* ept, uint64_t* entry)
{
	if(*entry)
	{
		g_physical_address phys = (*entry) & kEptAddrMask;
		return eptLookupPage(ept, phys);
	}

	lve_ept_page* page = eptAllocPage(ept);
	if(!page)
		return nullptr;

	*entry = (page->phys & kEptAddrMask) | kEptPerms;
	return page->virt;
}

} // namespace

bool lveGuestAllocate(lve_guest_memory* mem, uint64_t sizeBytes)
{
	if(!mem || sizeBytes == 0)
		return false;

	memset(mem, 0, sizeof(*mem));

	const uint64_t sizeAligned = G_PAGE_ALIGN_UP(sizeBytes);
	void* base = g_alloc_mem(sizeAligned);
	if(!base)
		return false;

	const uint64_t pageCount = sizeAligned / G_PAGE_SIZE;
	auto* hostPages = static_cast<g_physical_address*>(malloc(sizeof(g_physical_address) * pageCount));
	if(!hostPages)
	{
		g_unmap(base);
		return false;
	}

	memset(base, 0, sizeAligned);

	for(uint64_t i = 0; i < pageCount; ++i)
	{
		void* page = static_cast<uint8_t*>(base) + (i * G_PAGE_SIZE);
		g_physical_address phys = g_virt_to_phys(page);
		if(!phys)
		{
			free(hostPages);
			g_unmap(base);
			return false;
		}
		hostPages[i] = phys;
	}

	mem->base = base;
	mem->size = sizeAligned;
	mem->pageCount = pageCount;
	mem->hostPages = hostPages;
	return true;
}

void lveGuestFree(lve_guest_memory* mem)
{
	if(!mem)
		return;
	if(mem->base)
		g_unmap(mem->base);
	if(mem->hostPages)
		free(mem->hostPages);
	memset(mem, 0, sizeof(*mem));
}

bool lveEptBuild(lve_ept* ept, const lve_guest_memory* mem)
{
	if(!ept || !mem || !mem->base || !mem->hostPages || mem->pageCount == 0)
		return false;

	memset(ept, 0, sizeof(*ept));

	lve_ept_page* root = eptAllocPage(ept);
	if(!root)
		return false;

	ept->pml4 = root->virt;
	ept->pml4Phys = root->phys;

	for(uint64_t i = 0; i < mem->pageCount; ++i)
	{
		const uint64_t gpa = i * G_PAGE_SIZE;
		const g_physical_address hpa = mem->hostPages[i];

		uint64_t* pdpt = eptGetOrAllocTable(ept, &ept->pml4[eptPml4Index(gpa)]);
		if(!pdpt)
			return false;
		uint64_t* pd = eptGetOrAllocTable(ept, &pdpt[eptPdptIndex(gpa)]);
		if(!pd)
			return false;
		uint64_t* pt = eptGetOrAllocTable(ept, &pd[eptPdIndex(gpa)]);
		if(!pt)
			return false;

		pt[eptPtIndex(gpa)] = (hpa & kEptAddrMask) | kEptLeafFlags;
	}

	return true;
}

uint64_t lveEptMakePointer(const lve_ept* ept)
{
	if(!ept || !ept->pml4Phys)
		return 0;

	return (ept->pml4Phys & kEptAddrMask) | kEptpMemTypeWb | kEptpPageWalk4;
}

void lveEptFree(lve_ept* ept)
{
	if(!ept)
		return;

	auto* node = static_cast<lve_ept_page*>(ept->pages);
	while(node)
	{
		auto* next = node->next;
		if(node->virt)
			g_unmap(node->virt);
		free(node);
		node = next;
	}

	memset(ept, 0, sizeof(*ept));
}

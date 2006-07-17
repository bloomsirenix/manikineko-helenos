/*
 * Copyright (C) 2001-2004 Jakub Jermar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup xen32mm	
 * @{
 */
/** @file
 */

#ifndef __xen32_PAGE_H__
#define __xen32_PAGE_H__

#include <arch/mm/frame.h>

#define PAGE_WIDTH	FRAME_WIDTH
#define PAGE_SIZE	FRAME_SIZE

#ifdef KERNEL

#ifndef __ASM__
#	define KA2PA(x)	(((uintptr_t) (x)) - 0x80000000)
#	define PA2KA(x)	(((uintptr_t) (x)) + 0x80000000)
#else
#	define KA2PA(x)	((x) - 0x80000000)
#	define PA2KA(x)	((x) + 0x80000000)
#endif

/*
 * Implementation of generic 4-level page table interface.
 * IA-32 has 2-level page tables, so PTL1 and PTL2 are left out.
 */
#define PTL0_ENTRIES_ARCH	1024
#define PTL1_ENTRIES_ARCH	0
#define PTL2_ENTRIES_ARCH	0
#define PTL3_ENTRIES_ARCH	1024

#define PTL0_INDEX_ARCH(vaddr)	(((vaddr)>>22)&0x3ff)
#define PTL1_INDEX_ARCH(vaddr)	0
#define PTL2_INDEX_ARCH(vaddr)	0
#define PTL3_INDEX_ARCH(vaddr)	(((vaddr)>>12)&0x3ff)

#define GET_PTL1_ADDRESS_ARCH(ptl0, i)		((pte_t *)((((pte_t *)(ptl0))[(i)].frame_address)<<12))
#define GET_PTL2_ADDRESS_ARCH(ptl1, i)		(ptl1)
#define GET_PTL3_ADDRESS_ARCH(ptl2, i)		(ptl2)
#define GET_FRAME_ADDRESS_ARCH(ptl3, i)		((uintptr_t)((((pte_t *)(ptl3))[(i)].frame_address)<<12))

#define SET_PTL0_ADDRESS_ARCH(ptl0)		(write_cr3((uintptr_t) (ptl0)))
#define SET_PTL1_ADDRESS_ARCH(ptl0, i, a)	(((pte_t *)(ptl0))[(i)].frame_address = (a)>>12)
#define SET_PTL2_ADDRESS_ARCH(ptl1, i, a)
#define SET_PTL3_ADDRESS_ARCH(ptl2, i, a)
#define SET_FRAME_ADDRESS_ARCH(ptl3, i, a)	(((pte_t *)(ptl3))[(i)].frame_address = (a)>>12)

#define GET_PTL1_FLAGS_ARCH(ptl0, i)		get_pt_flags((pte_t *)(ptl0), (index_t)(i))
#define GET_PTL2_FLAGS_ARCH(ptl1, i)		PAGE_PRESENT
#define GET_PTL3_FLAGS_ARCH(ptl2, i)		PAGE_PRESENT
#define GET_FRAME_FLAGS_ARCH(ptl3, i)		get_pt_flags((pte_t *)(ptl3), (index_t)(i))

#define SET_PTL1_FLAGS_ARCH(ptl0, i, x)		set_pt_flags((pte_t *)(ptl0), (index_t)(i), (x))
#define SET_PTL2_FLAGS_ARCH(ptl1, i, x)
#define SET_PTL3_FLAGS_ARCH(ptl2, i, x)
#define SET_FRAME_FLAGS_ARCH(ptl3, i, x)	set_pt_flags((pte_t *)(ptl3), (index_t)(i), (x))

#define PTE_VALID_ARCH(p)			(*((uint32_t *) (p)) != 0)
#define PTE_PRESENT_ARCH(p)			((p)->present != 0)
#define PTE_GET_FRAME_ARCH(p)			((p)->frame_address<<FRAME_WIDTH)
#define PTE_WRITABLE_ARCH(p)			((p)->writeable != 0)
#define PTE_EXECUTABLE_ARCH(p)			1

#ifndef __ASM__

#include <mm/page.h>
#include <arch/types.h>
#include <arch/mm/frame.h>
#include <typedefs.h>

/* Page fault error codes. */

/** When bit on this position is 0, the page fault was caused by a not-present page. */
#define PFERR_CODE_P		(1<<0)

/** When bit on this position is 1, the page fault was caused by a write. */
#define PFERR_CODE_RW		(1<<1)

/** When bit on this position is 1, the page fault was caused in user mode. */
#define PFERR_CODE_US		(1<<2)

/** When bit on this position is 1, a reserved bit was set in page directory. */ 
#define PFERR_CODE_RSVD		(1<<3)	

/** Page Table Entry. */
struct page_specifier {
	unsigned present : 1;
	unsigned writeable : 1;
	unsigned uaccessible : 1;
	unsigned page_write_through : 1;
	unsigned page_cache_disable : 1;
	unsigned accessed : 1;
	unsigned dirty : 1;
	unsigned pat : 1;
	unsigned global : 1;
	unsigned soft_valid : 1;	/**< Valid content even if the present bit is not set. */
	unsigned avl : 2;
	unsigned frame_address : 20;
} __attribute__ ((packed));

static inline int get_pt_flags(pte_t *pt, index_t i)
{
	pte_t *p = &pt[i];
	
	return (
		(!p->page_cache_disable)<<PAGE_CACHEABLE_SHIFT |
		(!p->present)<<PAGE_PRESENT_SHIFT |
		p->uaccessible<<PAGE_USER_SHIFT |
		1<<PAGE_READ_SHIFT |
		p->writeable<<PAGE_WRITE_SHIFT |
		1<<PAGE_EXEC_SHIFT |
		p->global<<PAGE_GLOBAL_SHIFT
	);
}

static inline void set_pt_flags(pte_t *pt, index_t i, int flags)
{
	pte_t *p = &pt[i];
	
	p->page_cache_disable = !(flags & PAGE_CACHEABLE);
	p->present = !(flags & PAGE_NOT_PRESENT);
	p->uaccessible = (flags & PAGE_USER) != 0;
	p->writeable = (flags & PAGE_WRITE) != 0;
	p->global = (flags & PAGE_GLOBAL) != 0;
	
	/*
	 * Ensure that there is at least one bit set even if the present bit is cleared.
	 */
	p->soft_valid = true;
}

extern void page_arch_init(void);
extern void page_fault(int n, istate_t *istate);

#endif /* __ASM__ */

#endif /* KERNEL */

#endif

/** @}
 */

#include <types.h>
#include <current.h>
#include <spinlock.h>
#include <synch.h>
#include <proc.h>
#include <cpu.h>
#include <addrspace.h>
#include <vm.h>
#include <coremap.h>
#include <pagetable.h>

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages){

	if(coremap_lock == NULL){
		paddr_t paddr = coremap_allocate_early(npages);
		return PADDR_TO_KVADDR(paddr);
	}

	paddr_t paddr = coremap_allocate_page(true, 0, npages, NULL);

	vaddr_t vaddr = PADDR_TO_KVADDR(paddr);
	return vaddr;
}

/* Kfree MUST be able to operate while in interrupt, and therefore can't acquire the coremap lock */
void free_kpages(vaddr_t addr){
	paddr_t paddr = KVADDR_TO_PADDR(addr);
	coremap_free_page(paddr);
}



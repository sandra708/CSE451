#include <types.h>
#include <synch.h>
#include <vm.h>
#include <coremap.h>

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress){
	//TODO
	(void) faulttype;
	(void) faultaddress;
	return 0;
}

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages){
	(void) npages; //TODO: make this work for more than one page

	lock_acquire(coremap_lock);
	paddr_t paddr = coremap_allocate_page(true);
	lock_release(coremap_lock);

	vaddr_t vaddr = PADDR_TO_KVADDR(paddr);
	return vaddr;
}

void free_kpages(vaddr_t addr){
	paddr_t paddr = -1; 
	(void) addr;
	// TODO: convert vaddr to paddr
	
	lock_acquire(coremap_lock);
	coremap_free_page(paddr);
	lock_release(coremap_lock);
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *tlbshootdown){
	(void) tlbshootdown;
	//TODO
}

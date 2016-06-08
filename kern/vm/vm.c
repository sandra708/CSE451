#include <types.h>
#include <synch.h>
#include <vm.h>
#include <coremap.h>
#include <pagetable.h>
#include <current.h>
#include <proc.h>

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress){
  struct proc *cur = curproc; 
  if(cur == NULL)
  {
		return 1;
	}
  if (faulttype < 2)
  {
    paddr_t newentry = pagetable_lookup(cur->pages, faultaddress);
    if (newentry == 0)
    {
      return 1;
    }
    return 0;
  }
	else
  {
    //TODO attempting to write a readonly page?
  }
	return 0;
}

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages){

	if(coremap_lock == NULL){
		paddr_t paddr = coremap_allocate_early(npages);
		return PADDR_TO_KVADDR(paddr);
	}

	paddr_t paddr = coremap_allocate_page(true, 0, npages);

	vaddr_t vaddr = PADDR_TO_KVADDR(paddr);
	return vaddr;
}

/* Kfree MUST be able to operate while in interrupt, and therefore can't acquire the coremap lock */
void free_kpages(vaddr_t addr){
	paddr_t paddr = KVADDR_TO_PADDR(addr);
	coremap_free_page(paddr);
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *tlbshootdown){
	(void) tlbshootdown;
	//TODO
}

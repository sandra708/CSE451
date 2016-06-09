#include <types.h>
#include <current.h>
#include <mips/tlb.h>
#include <spinlock.h>
#include <synch.h>
#include <proc.h>
#include <addrspace.h>
#include <vm.h>
#include <coremap.h>
#include <pagetable.h>

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress){
  struct addrspace *as = proc_getas(); 
  if(as == NULL)
  {
    return 1;
  }
  if (faulttype < 2)
  {
    struct pagetable_entry *newentry = pagetable_lookup(as->pages, faultaddress);
    if (newentry == NULL)
    {
      // no page exists
      pagetable_pull(as->pages, faultaddress);
      spinlock_acquire(&newentry->lock);
      uint32_t tlb_hi = faultaddress;
      uint32_t tlb_lo = (newentry->addr << 12) | TLBLO_VALID;
      tlb_random(tlb_hi, tlb_lo);
      spinlock_release(&newentry->lock);
      return 0;
    } 
    spinlock_acquire(&newentry->lock);
    if(!(newentry->flags & PAGETABLE_INMEM))
    {
      pagetable_swap_in(newentry, faultaddress, as->pid);
    }    
    uint32_t tlb_hi = faultaddress & TLBHI_VPAGE;
    uint32_t tlb_lo = (newentry->addr << 12) | TLBLO_VALID;
    tlb_random(tlb_hi, tlb_lo);
    spinlock_release(&newentry->lock); 
    return 0;
  }
  else
  {
    //Exception READ-ONLY
    struct pagetable_entry *newentry = pagetable_lookup(as->pages, faultaddress);
    bool map = coremap_lock_acquire(newentry->addr << 12);
    if(!map) // the memory will become invalid shortly
      return 1;    
    if(newentry->flags & PAGETABLE_READ_ONLY)
      return 1;
    newentry->flags |= PAGETABLE_DIRTY;

    coremap_mark_page_dirty(newentry->addr << 12);
    coremap_lock_release(newentry->addr << 12);

    uint32_t tlb_hi = faultaddress;
    uint32_t tlb_lo = (newentry->addr << 12) | (TLBLO_VALID | TLBLO_DIRTY);
    int tlb_idx = tlb_probe(tlb_hi, 0);
    if(tlb_idx < 0) // entry has been invalidated
      return 1;

    tlb_write(tlb_hi, tlb_lo, tlb_idx);
    return 0;
  }
}

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

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *tlbshootdown){
	(void) tlbshootdown;
	//TODO
}

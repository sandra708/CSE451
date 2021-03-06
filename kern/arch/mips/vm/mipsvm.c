#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <mips/vm.h>
#include <addrspace.h>
#include <vm.h>
#include <coremap.h>
#include <swap.h>

// base and bound for vm-managed memory
paddr_t base;
paddr_t bound;

// TLB context and lock
struct spinlock tlb_lock;

// depends on the page size being 4096 KB
#define VM_PAGEOFFSET 12

void
vm_bootstrap(){
	coremap_bootstrap();
	swap_bootstrap(PAGE_SIZE);
	spinlock_init(&tlb_lock);
}

void
coremap_bootstrap(){
	bound = ram_getsize();
	base = ram_getfirstfree();

	// now allocate space for the coremap (assuming 4KB pages)
	if(PAGE_SIZE != 4096){
		panic("Page sizes other than 4096 bytes not supported.");
	}
	int npages = (bound - base) >> VM_PAGEOFFSET;
	int nbytes = npages * sizeof(struct coremap_entry);

	// allocate the coremap right at the base address	
	coremap = (struct coremap_entry*) PADDR_TO_KVADDR(base);
	coremap_length = npages;
	

	// set the coremap entries for the memory occupied by the coremap itself
	int mappages = 0;
	while(nbytes > 0){
		coremap[mappages].flags = (COREMAP_INUSE) | (COREMAP_DIRTY);
		coremap[mappages].pid = 0; // the kernel's reserved pid
		coremap[mappages].vaddr = 0; // userspace field
		nbytes = nbytes - PAGE_SIZE;
		mappages++;
	}

	// initialize bitmaps as NULL, so kmalloc can use the coremap to allocate the space for the bitmaps
	coremap_free = NULL;
	coremap_swappable = NULL;

	// now allocate the bitmaps
	coremap_free = bitmap_create(npages);
	coremap_swappable = bitmap_create(npages);
	for(int i = 0; i < mappages; i++){
		bitmap_mark(coremap_free, i);
		bitmap_mark(coremap_swappable, i);
	}

	// now allocate the lock and cv (which uses kmalloc and the coremap as normal)	
	coremap_lock = lock_create("COREMAP");
	coremap_cv = cv_create("COREMAP");
	spinlock_init(&coremap_spinlock);
}

paddr_t
coremap_allocate_early(int npages){
	if(coremap == NULL){
		return ram_stealmem(npages);
	} else{
		int i = 0;
		while(true){
			if(!(coremap[i].flags | COREMAP_INUSE))
				break;
			i++;
		}
		for(int j = 0; j < npages; j++){
			coremap[i + j].flags = COREMAP_INUSE | COREMAP_DIRTY;
			coremap[i + j].pid = 0;
		}
		return coremap_untranslate(i);
	}
}

paddr_t
coremap_untranslate(int idx){
	paddr_t paddr = (idx << VM_PAGEOFFSET);
	paddr += base;
	return paddr;
}

int
coremap_translate(paddr_t paddr){
	int frame = paddr - base;
	int idx = (frame & PAGE_FRAME) >> VM_PAGEOFFSET;
	
	return idx;
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *tlbshootdown){
	spinlock_acquire(&tlb_lock);
	int tlb_idx = tlb_probe(tlbshootdown->badaddr, 0);
	if(tlb_idx >= 0){
		uint32_t tlb_hi = MIPS_KSEG2 | (tlb_idx << 12);
		uint32_t tlb_lo = 0;
		tlb_write(tlb_hi, tlb_lo, tlb_idx);
	}
	spinlock_release(&tlb_lock);
}

void vm_flush_tlb(int pid){
	struct cpu *cur = curthread->t_cpu;

	spinlock_acquire(&tlb_lock);

	/* Check if the pid doesn't match, invalidate the whole tlb */
	if(pid == 0 || pid != cur->c_tlb_pid){
		for(uint32_t i = 0; i < NUM_TLB; i++){
			uint32_t tlb_hi = MIPS_KSEG2 | (i << 12); // out of range
			uint32_t tlb_lo = 0; // shouldn't matter
			tlb_write(tlb_hi, tlb_lo, i);
		}
		
		if(pid != 0)
			cur->c_tlb_pid = pid;
	}
	spinlock_release(&tlb_lock);
}

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress){
  
  /* Address out of bounds */
  if(faultaddress >= USERSTACK)
    return 1;
 
  /* No address space found (kernel process) */
  struct addrspace *as = proc_getas(); 
  if(as == NULL)
  {
    return 1;
  }

  struct pagetable_entry *newentry = pagetable_lookup(as->pages, faultaddress);
  if (faulttype < 2)
  {
    if (newentry == NULL)
    {
      // no page exists
      
      if(!as->loading && faultaddress < as->heap_start)
        return 1;
      
      if(!as->loading && as->stack_base > faultaddress && as->heap_end < faultaddress)
        as->stack_base = (faultaddress & PAGE_SIZE);

      pagetable_pull(as->pages, faultaddress, 0);
      newentry = pagetable_lookup(as->pages, faultaddress);      

      spinlock_acquire(&newentry->lock);
      spinlock_acquire(&tlb_lock);
      uint32_t tlb_hi = faultaddress & TLBHI_VPAGE;
      uint32_t tlb_lo = (newentry->addr << 12) | TLBLO_VALID;
      int tlb_idx = tlb_probe(tlb_hi, 0);
      if(tlb_idx < 0)
        tlb_random(tlb_hi, tlb_lo);
      else
        tlb_write(tlb_hi, tlb_lo, tlb_idx);
      spinlock_release(&tlb_lock);
      spinlock_release(&newentry->lock);
      return 0;
    } 

    spinlock_acquire(&newentry->lock);
    while(!(newentry->flags & PAGETABLE_INMEM))
    {
      // don't hold spinlock across the swap-in process, since it may need to sleep
      spinlock_release(&newentry->lock);
      pagetable_swap_in(newentry, faultaddress, as->pid);
      spinlock_acquire(&newentry->lock);
    }    
    spinlock_acquire(&tlb_lock);
    uint32_t tlb_hi = faultaddress & TLBHI_VPAGE;
    uint32_t tlb_lo = (newentry->addr << 12) | TLBLO_VALID;
    int tlb_idx = tlb_probe(tlb_hi, 0);
    if(tlb_idx < 0)
    {
      tlb_random(tlb_hi, tlb_lo);
    }
    else 
      tlb_write(tlb_hi, tlb_lo, tlb_idx);
    spinlock_release(&tlb_lock);
    spinlock_release(&newentry->lock); 
    return 0;
  }

  else
  {
    //Exception READ-ONLY
    
    // Require that the MODIFY address is within legally allocated space (unless currently loading)
    if(!as->loading && as->stack_base > faultaddress &&  as->heap_end < faultaddress)
      return 1;

    bool map = coremap_lock_acquire(newentry->addr << 12);
    if(!map) 
      // the memory will become invalid shortly 
      // return out of trap handler without fixing anything and hope for better luck next time
      return 0;    
    if(!(newentry->flags & PAGETABLE_WRITEABLE) && !as->loading)
    { // invalid access 
      coremap_lock_release(newentry->addr << 12);
      return 1;
    }

    newentry->flags |= PAGETABLE_DIRTY;

    coremap_mark_page_dirty(newentry->addr << 12);

    spinlock_acquire(&tlb_lock);
    uint32_t tlb_hi = faultaddress & TLBHI_VPAGE;
    uint32_t tlb_lo = (newentry->addr << 12) | (TLBLO_VALID | TLBLO_DIRTY);
    int tlb_idx = tlb_probe(tlb_hi, 0);

    if(tlb_idx < 0) // entry has been invalidated
      tlb_random(tlb_hi, tlb_lo);
    else 
      tlb_write(tlb_hi, tlb_lo, tlb_idx);
    spinlock_release(&tlb_lock);

    coremap_lock_release(newentry->addr << 12);
    return 0;
  }
}

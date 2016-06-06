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

// base and bound for vm-managed memory
paddr_t base;
paddr_t bound;

// depends on the page size being 4096 KB
#define VM_PAGEOFFSET 12

void
vm_bootstrap(){
	coremap_bootstrap();
	//TODO: diskswap_bootstrap?
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
	int bytes = npages * sizeof(struct coremap_entry);

	// allocate the coremap right at the base address	
	coremap = (struct coremap_entry*) PADDR_TO_KVADDR(base);

	// set the coremap entries for the memory occupied by the coremap itself
	int mappages = 0;
	while(bytes > 0){
		coremap[mappages].flags = (COREMAP_INUSE) | (COREMAP_DIRTY);
		coremap[mappages].pid = 0; // the kernel's reserved pid
		bytes = bytes - 4096;
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

	// now allocate the lock (which uses kmalloc and the coremap as normal)	
	coremap_lock = lock_create("COREMAP");
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

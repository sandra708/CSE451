#include <types.h>
#include <lib.h>
#include <bitmap.h>
#include <kern/errno.h>
#include <coremap.h>
#include <vm.h>

paddr_t
coremap_allocate_page(bool iskern, int pid, int npages){
	(void) npages; //TODO
	
	unsigned int idx;
	int err = bitmap_alloc(coremap_free, &idx);
	if(!err){
		coremap[idx].pid = pid;
		coremap[idx].flags = COREMAP_INUSE;
		bitmap_mark(coremap_free, idx);
		if(!iskern){
			coremap[idx].flags |= COREMAP_SWAPPABLE;
		}else{
			bitmap_mark(coremap_swappable, idx);
		}
		return coremap_untranslate(idx);
	}

	err = bitmap_alloc(coremap_swappable, &idx);
	if(!err){
		if(coremap[idx].flags | COREMAP_DIRTY){
			//TODO: push memory out to disk
		}
		//TODO: invalidate former owner's page table
		//TODO: zero physical memory
		coremap[idx].pid = pid;
		coremap[idx].flags = COREMAP_INUSE;
		bitmap_mark(coremap_free, idx);
		if(!iskern){
			coremap[idx].flags |= COREMAP_SWAPPABLE;
		} else{
			bitmap_mark(coremap_swappable, idx);
		}
	}

	panic("Main memory is entirely full with kernel heap: no new memory can be allocated.");
}

paddr_t 
coremap_swap_page(/*param needed*/){
	unsigned int idx;
	int err = bitmap_alloc(coremap_free, &idx);
	if(!err){
		/* We were able to locate an empty page frame */
		paddr_t paddr = coremap_untranslate(idx);
		/* TODO: Swap in from disk */
		return paddr;
	}else{
		err = bitmap_alloc(coremap_swappable, &idx);
		if(err){
			panic("Memory completely full with kernel heap: Unable to swap any page in from disk.\n");
		}
		if(coremap[idx].flags & COREMAP_DIRTY){
			/* TODO: Write out to disk */
		}
		paddr_t paddr = coremap_untranslate(idx);
		/* TODO: swap in from disk */
		return paddr;
	}
}

void 
coremap_free_page(paddr_t paddr){
	/* TODO: Wipe physical memory */
	int idx = coremap_translate(paddr);
	coremap[idx].flags = 0;
	coremap[idx].pid = 0;
	bitmap_unmark(coremap_free, idx);
	bitmap_unmark(coremap_swappable, idx);
}

void 
coremap_mark_page_dirty(paddr_t paddr){
	int idx = coremap_translate(paddr);
	coremap[idx].flags |= COREMAP_DIRTY;
}

void 
coremap_mark_page_clean(paddr_t paddr){
	int idx = coremap_translate(paddr);
	coremap[idx].flags &= ~(COREMAP_DIRTY);
}

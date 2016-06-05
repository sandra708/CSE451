#include <types.h>
#include <lib.h>
#include <bitmap.h>
#include <kern/errno.h>
#include <coremap.h>
#include <vm.h>

paddr_t
coremap_allocate_page(bool iskern){
	//TODO
	(void) iskern;
	return 0;
}

paddr_t 
coremap_swap_page(/*param needed*/){
	unsigned int idx;
	bitmap_alloc(coremap_free, &idx);
	if(idx != ENOSPC){
		/* We were able to locate empty page frame */
		paddr_t paddr = coremap_untranslate(idx);
		/* TODO: Swap in from disk */
		return paddr;
	}else{
		bitmap_alloc(coremap_swappable, &idx);
		if(idx == ENOSPC){
			panic("Memory completely full with kernel data: Unable to swap any page in from disk.\n");
		}
		if(coremap[idx].flags && COREMAP_DIRTY){
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

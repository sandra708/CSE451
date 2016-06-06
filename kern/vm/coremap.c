#include <types.h>
#include <lib.h>
#include <bitmap.h>
#include <kern/errno.h>
#include <coremap.h>
#include <vm.h>

static
int
locate_range(struct bitmap *map, unsigned npages, unsigned int *result){
	unsigned int min = 0;
	while(true){
		unsigned int idx = 0;
		int err  = bitmap_alloc_after(map, min, &idx);
		if(err){
			return err;
		}

		if(idx + npages > coremap_length){
			bitmap_unmark(map, idx);
			return ENOSPC;
		}

		bool success = true;
		for(unsigned int i = 0; i < npages; i++){
			if(coremap[idx + i].flags & COREMAP_INUSE){
				bitmap_unmark(map, idx);
				min = idx + i;
				success = false;
				break;
			}
		}
	
		if(success){
			*result = idx;
			bitmap_unmark(map, idx);
			return 0;
		}
	}
}

paddr_t
coremap_allocate_page(bool iskern, int pid, int npages){
	
	unsigned int idx;
	int err = locate_range(coremap_free, npages, &idx);
	if(!err){
		for(int i = 0; i < npages; i++){
			coremap[idx + i].pid = pid;
			coremap[idx + i].flags = COREMAP_INUSE;
			if(i > 0){
				coremap[idx + i].flags |= COREMAP_MULTI;
			}
			bitmap_mark(coremap_free, idx + i);
			if(!iskern){
				coremap[idx + i].flags |= COREMAP_SWAPPABLE;
			}else{
				bitmap_mark(coremap_swappable, idx + i);
			}
		}
		return coremap_untranslate(idx);
	}

	err = locate_range(coremap_swappable, npages, &idx);
	if(!err){
		for(int i = 0; i < npages; i++){
			if(coremap[idx + i].flags | COREMAP_DIRTY){
				//TODO: push memory out to disk
			}
			//TODO: invalidate former owner's page table
			//TODO: zero physical memory
			coremap[idx + i].pid = pid;
			coremap[idx + i].flags = COREMAP_INUSE;
			bitmap_mark(coremap_free, idx + i);
			if(!iskern){
				coremap[idx + i].flags |= COREMAP_SWAPPABLE;
			} else{
				bitmap_mark(coremap_swappable, idx + i);
			}
		}
		return coremap_untranslate(idx);
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
	int idx = coremap_translate(paddr);
	coremap[idx].flags = 0;
	coremap[idx].pid = 0;
	/* We know that the page was in kernelspace, and thus both bitmaps were set */
	bitmap_unmark(coremap_free, idx);
	bitmap_unmark(coremap_swappable, idx);
	/* Zero this page */
	while(coremap[idx + 1].flags & COREMAP_MULTI){
		idx++;
		coremap[idx].flags = 0;
		coremap[idx].pid = 0;
		bitmap_unmark(coremap_free, idx);
		bitmap_unmark(coremap_swappable, idx);
		/* Zero this page */
	}
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

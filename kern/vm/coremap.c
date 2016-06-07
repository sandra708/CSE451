#include <types.h>
#include <lib.h>
#include <bitmap.h>
#include <kern/errno.h>
#include <wchan.h>
#include <spinlock.h>
#include <synch.h>
#include <coremap.h>
#include <vm.h>

static
int
locate_range(struct bitmap *map, unsigned npages, unsigned int *result){
	spinlock_acquire(&coremap_spinlock);

	unsigned int min = 0;
	while(min < coremap_length){
		unsigned int idx = 0;
		int err  = bitmap_alloc_after(map, min, &idx);
		if(err){
			// no space
			spinlock_release(&coremap_spinlock);
			return err;
		}

		if(idx + npages > coremap_length){
			// too close to the end - no space
			bitmap_unmark(map, idx);
			spinlock_release(&coremap_spinlock);
			return ENOSPC;
		}

		bool success = true;
		for(unsigned int i = 1; i < npages; i++){
			if(bitmap_isset(map, idx + i)){
				for(int j = i - 1; j >= 0; j--){
					bitmap_unmark(map, idx + j);
				}
				min = idx + i;
				success = false;
				break;
			} else{
				bitmap_mark(map, idx + i);
			}
		}
	
		if(success){
			for(unsigned int i = 0; i < npages; i++){
				if(!bitmap_isset(coremap_free, idx + i)){
					bitmap_mark(coremap_free, idx + i);
				}
				if(!bitmap_isset(coremap_swappable, idx + i)){
					bitmap_mark(coremap_swappable, idx + i);
				}
			}
			*result = idx;
			spinlock_release(&coremap_spinlock);
			return 0;
		}
	}
	
	spinlock_release(&coremap_spinlock);
	return ENOSPC;
}

// locates a random page to swap out
static
int
locate_random(unsigned int *idx){
	spinlock_acquire(&coremap_spinlock);
	// first, see if there is a free page
	if(!bitmap_alloc(coremap_free, idx)){
		spinlock_release(&coremap_spinlock);
		return 0;
	}

	struct bitmap *map = coremap_swappable;

	// check that there is some space in the swappable map
	int err = bitmap_alloc(map, idx);
	if(err){
		spinlock_release(&coremap_spinlock);
		return err;
	}
	bitmap_unmark(map, *idx);

	// try 16 random values 
	const unsigned int num_rand = 16;
	unsigned int rand;
	for(unsigned int i = 0; i < num_rand; i++){
		rand = random() % coremap_length;
		if(!bitmap_isset(map, rand)){
			*idx = rand;
			spinlock_release(&coremap_spinlock);
			return 0;
		}
	}

	// try 16 randoms - this time return the next available space after the random
	for(unsigned int i = 0; i < num_rand; i++){
		rand = random() % coremap_length;
		err = bitmap_alloc_after(map, rand, idx);
		if(!err){
			spinlock_release(&coremap_spinlock);
			return err;
		}
	}

	// give up and return the first available space
	err = bitmap_alloc(map, idx);

	spinlock_release(&coremap_spinlock);

	return err;
}

// abstraction for the cache eviction policy
static int 
locate_swap(unsigned int *idx){
	return locate_random(idx);
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
			if(!iskern){
				coremap[idx + i].flags |= COREMAP_SWAPPABLE;
			}

			void* kvaddr = (void*) PADDR_TO_KVADDR(coremap_untranslate(idx));
			memset(kvaddr, 0, PAGE_SIZE);
		}

		return coremap_untranslate(idx);
	}

	while(locate_range(coremap_swappable, npages, &idx)){
		cv_wait(coremap_cv, coremap_lock);
	}

	for(int i = 0; i < npages; i++){
		if(coremap[idx + i].flags | COREMAP_DIRTY){
			//TODO: push memory out to disk
		}
		//TODO: invalidate former owner's page table

		//zero physical memory
		void* kvaddr = (void*) PADDR_TO_KVADDR(coremap_untranslate(idx));
		memset(kvaddr, 0, PAGE_SIZE);

		// set new coremap values
		coremap[idx + i].pid = pid;
		coremap[idx + i].flags = COREMAP_INUSE;
		if(!iskern){
			coremap[idx + i].flags |= COREMAP_SWAPPABLE;
		} 
		if(i > 0){
			coremap[idx + i].flags |= COREMAP_MULTI;
		}
	}

	return coremap_untranslate(idx);
}

paddr_t 
coremap_swap_page(/*param needed*/){
	unsigned int idx;
	while(locate_swap(&idx)){
		cv_wait(coremap_cv, coremap_lock);
	}
	
	/* TODO: reset the other page table */
	if(coremap[idx].flags & COREMAP_DIRTY){
		/* TODO: Write out to disk */
	}
	paddr_t paddr = coremap_untranslate(idx);
	/* TODO: swap in from disk */
	return paddr;
}

void 
coremap_free_page(paddr_t paddr){
	// determine length of original allocation
	int idx = coremap_translate(paddr);
	int num = 1;
	while(coremap[idx + num].flags & COREMAP_MULTI){
		num++;
	}

	// now un-set the coremap use flags
	for(int i = 0; i < num; i++){
		coremap[idx + i].flags = 0;
		coremap[idx + i].pid = 0;
		/* TODO: Zero memory*/
	}

	//TODO: zero / de-allocate disk sector

	/* now, un-set the bitmaps */
	spinlock_acquire(&coremap_spinlock);
	for(int i = 0; i < num; i++){
		bitmap_unmark(coremap_swappable, idx + i);
		bitmap_unmark(coremap_free, idx + i);
	}
	spinlock_release(&coremap_spinlock);

	/* wake threads waiting on more memory */
	if(lock_do_i_hold(coremap_lock)){
		// alert waiting threads to newly freed memory
		cv_broadcast(coremap_cv, coremap_lock);
	} else {
		// we might be in interrupt, so CAN'T hold the lock, but we want to broadcast the cv anyhow
		// code taken from synch.c
		spinlock_acquire(&coremap_cv->cv_spinlock);
		wchan_wakeall(coremap_cv->cv_wchan, &coremap_cv->cv_spinlock);
		spinlock_release(&coremap_cv->cv_spinlock);	
	}
}

void 
coremap_lock_acquire(paddr_t paddr){
	// now lay claim to the bitmap - keep a swap from sneaking up behind us
	spinlock_acquire(&coremap_spinlock);
	bitmap_mark(coremap_swappable, coremap_translate(paddr));
	spinlock_release(&coremap_spinlock);
}

void
coremap_lock_release(paddr_t paddr){ 
	// this shouldn't be done until there's a valid entry in the pagetable
	spinlock_acquire(&coremap_spinlock);
	bitmap_unmark(coremap_swappable, coremap_translate(paddr));
	spinlock_release(&coremap_spinlock);
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

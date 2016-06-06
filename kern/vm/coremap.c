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
	unsigned int min = 0;
	while(min < coremap_length){
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
	
	return ENOSPC;
}

// locates a random page to swap out
static
int
locate_random(struct bitmap *map, unsigned int *idx){
	// check that there is some space in the map
	int err = bitmap_alloc(map, idx);
	if(err){
		return err;
	}
	bitmap_unmark(map, *idx);

	// try 16 random values 
	const int num_rand = 16;
	unsigned int rand;
	for(int i = 0; i < num_rand; i++){
		rand = random() % coremap_length;
		if(!bitmap_isset(map, rand)){
			*idx = rand;
			return 0;
		}
	}

	// try 16 randoms - this time return the next available space after the random
	for(int i = 0; i < num_rand; i++){
		rand = random() % coremap_length;
		int err = bitmap_alloc_after(map, rand, idx);
		if(!err){
			return err;
		}
	}

	// give up and return the first available space
	return bitmap_alloc(map, idx);
}

// abstraction for the cache eviction policy
static int 
locate_swap(struct bitmap *map, unsigned int *idx){
	return locate_random(map, idx);
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
			//TODO: Zero physical memory
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
		while(locate_swap(coremap_swappable, &idx)){
			// wait for the kernel to free more memory
			cv_wait(coremap_cv, coremap_lock);
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
	int num = 1;
	while(coremap[idx + num].flags & COREMAP_MULTI){
		num++;
	}

	// now un-set the coremap use flags

	for(int i = 0; i < num; i++){
		coremap[idx + i].flags = 0;
		coremap[idx + i].pid = 0;
	}

	/* now, in reverse order, un-set the bitmaps
	 any thread interleaving is fine as long as 
	these operations are done in the order specified on each thread 
	even (if in kernelspace) without holding the coremap lock */
	if(lock_do_i_hold(coremap_lock)){
		for(int i = 0; i < num; i++){
			bitmap_unmark(coremap_free, idx + i);
			bitmap_unmark(coremap_swappable, idx + i);
		}
		// alert waiting threads to newly freed memory
		cv_broadcast(coremap_cv, coremap_lock);
	} else {
		struct spinlock lock;
		spinlock_init(&lock);
		spinlock_acquire(&lock);
		for(int i = 0; i < num; i++){
			bitmap_unmark(coremap_free, idx + i);
			bitmap_unmark(coremap_swappable, idx + i);
		}
		spinlock_release(&lock);
		spinlock_cleanup(&lock);

		// we might be in interrupt, so CAN'T hold the lock, but we want to wake the cv anyhow
		// code taken from synch.c
		spinlock_acquire(&coremap_cv->cv_spinlock);
		wchan_wakeall(coremap_cv->cv_wchan, &coremap_cv->cv_spinlock);
		spinlock_release(&coremap_cv->cv_spinlock);	
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

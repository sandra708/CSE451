#include <types.h>
#include <lib.h>
#include <bitmap.h>
#include <kern/errno.h>
#include <wchan.h>
#include <spinlock.h>
#include <synch.h>
#include <pid.h>
#include <proc.h>
#include <addrspace.h>
#include <coremap.h>
#include <swap.h>
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

static 
void
coremap_swap_page_out(unsigned int core_idx){	
	userptr_t vaddr = coremap[core_idx].vaddr;

	struct proc *proc = pid_get_proc(pids, coremap[core_idx].pid);
	spinlock_acquire(&proc->p_lock);
	struct addrspace *as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	struct pagetable_entry *entry = pagetable_lookup(as->pages, (vaddr_t) vaddr);
	if(entry == NULL)
		return;

	if(coremap[core_idx].flags & COREMAP_DIRTY){
		spinlock_acquire(&entry->lock);
		unsigned int disk_idx = entry->swap;
		spinlock_release(&entry->lock);

		paddr_t paddr = coremap_untranslate(core_idx);
		void* kvaddr = (void*) PADDR_TO_KVADDR(paddr);
		swap_page_out(kvaddr, disk_idx);
	}

	spinlock_acquire(&entry->lock);
	entry->flags &= ~(PAGETABLE_DIRTY);
	//TODO: tlb shootdown (wait for completion)
	entry->flags &= ~(PAGETABLE_INMEM);

	// free disk and invalidate entirely if requested
	if(entry->flags & PAGETABLE_REQUEST_FREE){
		swap_free(entry->swap);
		entry->flags &= ~PAGETABLE_VALID;
	}

	spinlock_release(&entry->lock);

	// notify address space if its waiting to destroy safely
	lock_acquire(as->destroy_lock);
	if(as->destroying){
		as->destroy_count--;
		cv_broadcast(as->destroy_cv, as->destroy_lock);
	}
	lock_release(as->destroy_lock);
}

paddr_t
coremap_allocate_page(bool iskern, int pid, int npages, userptr_t vaddr){
	
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
				coremap[idx + i].vaddr = vaddr;
			} else {
				coremap[idx + i].vaddr = 0;
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
		if(coremap[idx + i].flags | COREMAP_INUSE){
			coremap_swap_page_out(idx + i);
		}

		//zero physical memory
		void* kvaddr = (void*) PADDR_TO_KVADDR(coremap_untranslate(idx));
		memset(kvaddr, 0, PAGE_SIZE);

		// set new coremap values
		coremap[idx + i].pid = pid;
		coremap[idx + i].flags = COREMAP_INUSE;
		if(!iskern){
			coremap[idx + i].flags |= COREMAP_SWAPPABLE;
			coremap[idx + i].vaddr = vaddr;
		} else{
			coremap[idx + i].vaddr = 0;
		}
		if(i > 0){
			coremap[idx + i].flags |= COREMAP_MULTI;
		}
	}

	return coremap_untranslate(idx);
}

paddr_t 
coremap_swap_page(unsigned int diskblock, userptr_t vaddr, int pid){
	unsigned int idx;
	// wait for a free page ? return error ?
	while(locate_swap(&idx)){
		cv_wait(coremap_cv, coremap_lock);
	}
	
	coremap_swap_page_out(idx);

	paddr_t paddr = coremap_untranslate(idx);
	swap_page_in((void*) PADDR_TO_KVADDR(paddr), diskblock);
	
	coremap[idx].pid = pid;
	coremap[idx].vaddr = vaddr;
	coremap[idx].flags = COREMAP_INUSE | COREMAP_SWAPPABLE;

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
		coremap[idx + i].vaddr = 0;
	}

	/* now, un-set the bitmaps */
	spinlock_acquire(&coremap_spinlock);
	for(int i = 0; i < num; i++){
		bitmap_unmark(coremap_swappable, idx + i);
		bitmap_unmark(coremap_free, idx + i);
	}
	spinlock_release(&coremap_spinlock);

	/* TODO: consider cv usage in detail */

	/* wake threads waiting on more memory */
	if(lock_do_i_hold(coremap_lock)){
		cv_broadcast(coremap_cv, coremap_lock);
	} else {
		// we might be in interrupt, so CAN'T hold the lock, but we want to broadcast the cv anyhow
		// code taken from synch.c
		spinlock_acquire(&coremap_cv->cv_spinlock);
		wchan_wakeall(coremap_cv->cv_wchan, &coremap_cv->cv_spinlock);
		spinlock_release(&coremap_cv->cv_spinlock);	
	}
}

bool 
coremap_lock_acquire(paddr_t paddr){
	// now lay claim to the bitmap - keep a swap from sneaking up behind us
	spinlock_acquire(&coremap_spinlock);
	if(bitmap_isset(coremap_swappable, coremap_translate(paddr))){
		spinlock_release(&coremap_spinlock);
		return false;
	}
	bitmap_mark(coremap_swappable, coremap_translate(paddr));
	spinlock_release(&coremap_spinlock);
	return true;
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

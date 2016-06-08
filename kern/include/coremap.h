/* Functions relating to the core-map used to track page frames */

#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <kern/limits.h>
#include <bitmap.h>
#include <synch.h>

struct coremap_entry *coremap;
unsigned int coremap_length;
struct spinlock coremap_spinlock;

struct bitmap *coremap_free;
struct bitmap *coremap_swappable;

struct lock *coremap_lock;
struct cv *coremap_cv;

#define COREMAP_INUSE 8
#define COREMAP_SWAPPABLE 4
#define COREMAP_MULTI 2
#define COREMAP_DIRTY 1

struct coremap_entry{
	uint8_t flags;
	uint16_t pid; // limits.h restricts the PID to this size; if that changes, this must too
	userptr_t vaddr;
};

void
coremap_bootstrap(void);

paddr_t
coremap_allocate_page(bool iskern, int pid, int npages, userptr_t vaddr);

// only use before VM system is fully running
paddr_t
coremap_allocate_early(int npages);

paddr_t
coremap_swap_page(unsigned int diskblock, userptr_t vaddr, int pid);

void 
coremap_free_page(paddr_t paddr);

/* These are unusual locking methods as the reciprocal lock/unlock is done within allocate/free.
 * Call lock_acquire before freeing a page, and before releasing the lock on its page-table. 
 * Call lock_release after placing a returned paddr (from swap or allocate) into a user-space page table. 
 * The locks are implemented in a low-memory way through the bitmap allocator, and protect against vaddrs
 * being swapped in or out before allocate/free is complete. 
 *
 * DO NOT call on kernelspace memory. These are methods for paddrs allocated for userspace use. */

bool 
coremap_lock_acquire(paddr_t paddr);

void
coremap_lock_release(paddr_t paddr);

void 
coremap_mark_page_dirty(paddr_t paddr);

void 
coremap_mark_page_clean(paddr_t paddr);

/* An architecture-specific translator between a paddr and the index into that frame of the coremap 
 * These do not check that the values given are valid for memory contained in the coremap */
int
coremap_translate(paddr_t paddr);

paddr_t 
coremap_untranslate(int idx);

#endif

/* Functions relating to the page table used to track page frames */

#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <bitmap.h>
#include <kern/limits.h>
#include <synch.h>
#include <hashtable.h>
#include <coremap.h>
#include <swap.h>

struct pagetable_entry{
	struct spinlock lock;
	paddr_t addr;
	unsigned int swap;
	uint8_t flags;
};

struct pagetable_subptr {
  struct pagetable_entry *entries[1024];
};

struct pagetable_subtable {
  struct bitmap *valids;
  struct pagetable_subptr *ptr;
};

struct pagetable_ptr {
  struct pagetable_subtable *entries[1024];
};

struct pagetable {
  struct bitmap *valids;
  struct pagetable_ptr *ptr;
  struct lock *pagetable_lock;
};

#define PAGETABLE_VALID 1
#define PAGETABLE_INMEM 2
#define PAGETABLE_DIRTY 4
#define PAGETABLE_REQUEST_FREE 8
#define PAGETABLE_READABLE 16
#define PAGETABLE_WRITEABLE 32
#define PAGETABLE_EXECUTABLE 64
#define PAGETABLE_REQUEST_DESTROY 128

/* Swaps the given entry from disk into memory, adjusting the entry as needed */
void pagetable_swap_in(struct pagetable_entry *entry, vaddr_t vaddr, int pid);

/* Allocates a new page in physical memory, adjusting the table entry as needed */
paddr_t pagetable_pull(struct pagetable* table, vaddr_t vaddr, uint8_t flags);

/* Creates a pagetable tree structure */
struct pagetable *pagetable_create(void);

/* Returns the entry for a given vaddr, or NULL if none exists */
struct pagetable_entry
*pagetable_lookup(struct pagetable* table, vaddr_t vaddr);

/* Helper for pagetable_pull */
bool pagetable_add(struct pagetable* table, vaddr_t vaddr, paddr_t paddr, uint8_t flags);

/* Removes the page of the given vaddr, freeing space in memory and on disk */
bool pagetable_remove(struct pagetable* table, vaddr_t vaddr);

/* Copies the entire tree structure of a page table */
bool pagetable_copy(struct pagetable *old, int oldpid, struct pagetable *copy, int copypid);

/* Frees all of the pages (which aren't currently being swapped), and returns the number of failures */
int pagetable_free_all(struct pagetable* table);

/* Destroyes the tree structure, orphaning any extant physical page frames or swap pages */
int pagetable_destroy(struct pagetable* table);

#endif

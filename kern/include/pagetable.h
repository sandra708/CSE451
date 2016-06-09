/* Functions relating to the page table used to track page frames */

#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <kern/limits.h>
#include <synch.h>
#include <hashtable.h>
#include <coremap.h>
#include <swap.h>

struct pagetable {
  struct hashtable *maintable;
  struct lock *pagetable_lock;
};

#define PAGETABLE_VALID 1
#define PAGETABLE_INMEM 2
#define PAGETABLE_DIRTY 4
#define PAGETABLE_REQUEST_FREE 8
#define PAGETABLE_READ_ONLY 16

struct pagetable_entry{
	struct spinlock lock;
	paddr_t addr;
	unsigned int swap;
	uint8_t flags;
};

paddr_t pagetable_pull(struct pagetable* table, vaddr_t vaddr);

struct pagetable *pagetable_create(void);

struct pagetable_entry
*pagetable_lookup(struct pagetable* table, vaddr_t vaddr);

bool pagetable_add(struct pagetable* table, vaddr_t vaddr, paddr_t paddr);

bool pagetable_remove(struct pagetable* table, vaddr_t vaddr);

int pagetable_free_all(struct pagetable* table);

int pagetable_destroy(struct pagetable* table);

#endif

/* Functions relating to the page table used to track page frames */

#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <kern/limits.h>
#include <coremap.h>
#include <synch.h>
#include <hashtable.h>

struct pagetable{
  struct hashtable *maintable;
  struct lock *pagetable_lock;
};

#define PAGETABLE_VALID 1
#define PAGETABLE_INMEM 2
#define PAGETABLE_DIRTY 4

struct pagetable_entry{
	struct spinlock lock;
	paddr_t addr;
	unsigned int swap;
	uint8_t flags;
};

paddr_t pagetable_pull(struct pagetable* table, vaddr_t vaddr);

struct pagetable* pagetable_create(void);

struct pagetable_entry*
pagetable_lookup(struct pagetable* table, vaddr_t vaddr);

bool pagetable_add(struct pagetable* table, vaddr_t vaddr, paddr_t paddr);

bool pagetable_remove(struct pagetable* table, vaddr_t vaddr);

int pagetable_destroy(struct pagetable* table);

#endif

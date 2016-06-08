/* This file holds the interface for swap operations in the VM system. */

#ifndef SWAP_H_
#define SWAP_H_

struct vnode *swap_space;
struct uio *swap_uio; // we want this pre-allocated instead of kmalloc'ing each operation
size_t page_size; 
struct bitmap *swap_free;
struct lock *swap_lock;

/* For the interface with pagetables, disk pointers are represented as unsigned indices 
 * into a logical array in the swap file. */

void
swap_bootstrap(void);

unsigned int
swap_allocate(void);

void
swap_free(unsigned int block);

void 
swap_page_out(void* kvaddr, unsigned int block);

void 
swap_page_in(void* kvaddr, unsigned int block);

#endif

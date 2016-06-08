/* This file holds the interface for swap operations in the VM system. */

#ifndef SWAP_H_
#define SWAP_H_

#include <bitmap.h>
#include <spinlock.h>
#include <vnode.h>
#include <uio.h>

/* For the interface with pagetables, disk pointers are represented as unsigned indices 
 * into a logical array in the swap file. */

void
swap_bootstrap(size_t page_size);

/* returns 0 or ENOSPC */
int
swap_allocate(unsigned int *block);

void
swap_free(unsigned int block);

/* Write page to disk */
void 
swap_page_out(void* kvaddr, unsigned int block);

/* Read page from disk */
void 
swap_page_in(void* kvaddr, unsigned int block);

#endif

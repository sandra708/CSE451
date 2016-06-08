/* This is the file which handles the swap space for the VM system */

#include <types.h>
#include <bitmap.h>
#include <kern/errno.h>
#include <uio.h>
#include <vnode.h>
#include <swap.h>

// needs to be machine-specific code
void
swap_bootstrap(){

}

unsigned int
swap_allocate(){
	lock_acquire(swap_lock);

	unsigned int idx;
	int err = bitmap_alloc(swap_free, &idx);
	if(err){
		return ENOMEM;
	}

	//TODO: zero memory

	lock_release(swap_lock);
	
	return idx;
}

void
swap_free(unsigned int index){
	lock_acquire(swap_lock);

	// verify that the index is within the bounds of the space
	
	bitmap_unmark(swap_free, &index);

	lock_release(swap_lock);
}

void
swap_page_in(void* kvaddr, unsigned int block){
	lock_acquire(swap_lock);

	//TODO: set uio with the block index and pointer

	vop_read(swap_space, swap_uio);

	lock_release(swap_lock);
}

void
swap_page_out(void* kvaddr, unsigned int block){
	lock_acquire(swap_lock);

	//TODO: set uio with the block index and pointer
	
	vop_write(swap_space, swap_uio);

	lock_release(swap_lock);
}

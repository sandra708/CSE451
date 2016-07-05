/* This is the file which handles the swap space for the VM system */

#include <types.h>
#include <bitmap.h>
#include <kern/errno.h>
#include <kern/stat.h>
#include <uio.h>
#include <vnode.h>
#include <vfs.h>
#include <swap.h>

// lock to protect the bitmap
struct spinlock swap_spinlock;
struct bitmap *swap_freelist;

struct vnode *swap_space;
size_t page_size; 

void
swap_bootstrap(size_t page_size_param){
	// TODO: ERROR: this is a hard-coded device name, as an assignment hack
	int err = vfs_swapon("lhd0:", &swap_space);
	if(err){
		panic("Unable to locate swap space.\n");
	}

	page_size = page_size_param;

	// lock for the bitmap
	spinlock_init(&swap_spinlock);

	// figure out how many swap pages we have
	struct stat swap_stats;
	VOP_STAT(swap_space, &swap_stats);
	unsigned int num_pgs = swap_stats.st_size / page_size; 
	swap_freelist = bitmap_create(num_pgs);
}

int
swap_allocate(unsigned int *block){
	spinlock_acquire(&swap_spinlock);
	int err = bitmap_alloc(swap_freelist, block);
	spinlock_release(&swap_spinlock);

	if(err){
		return ENOSPC;
	}

	//TODO: zero memory

	return 0;
}

void
swap_free(unsigned int block){
	// TODO: verify that the index is within the bounds of the space

	spinlock_acquire(&swap_spinlock);
	
	bitmap_unmark(swap_freelist, block);

	spinlock_release(&swap_spinlock);
}

void
swap_page_in(void* kvaddr, unsigned int block){

	// place data  on the stack - not safe to kmalloc
	struct iovec iov_page;
	iov_page.iov_kbase = kvaddr;
	iov_page.iov_len = page_size;

	struct uio swap_uio;
	// set uio with the block index and pointer
	swap_uio.uio_iov = &iov_page;
	swap_uio.uio_iovcnt = 1;
	swap_uio.uio_offset = block * page_size;
	swap_uio.uio_resid = page_size;
	// pretending that everything is kernelspace means not dealing w/ userspace vaddrs
	swap_uio.uio_segflg = UIO_SYSSPACE; 
	swap_uio.uio_rw = UIO_READ;
	swap_uio.uio_space = NULL;

	VOP_READ(swap_space, &swap_uio);
}

void
swap_page_out(void* kvaddr, unsigned int block){
	// leave data on the stack, not safe to kmalloc
	struct iovec iov_page;
	iov_page.iov_kbase = kvaddr;
	iov_page.iov_len = page_size;

	struct uio swap_uio;
	// set uio with the block index and pointer
	swap_uio.uio_iov = &iov_page;
	swap_uio.uio_iovcnt = 1;
	swap_uio.uio_offset = block * page_size;
	swap_uio.uio_resid = page_size;
	// pretending that everything is kernelspace means not dealing w/ userspace vaddrs
	swap_uio.uio_segflg = UIO_SYSSPACE; 
	swap_uio.uio_rw = UIO_WRITE;
	swap_uio.uio_space = NULL;
	
	VOP_WRITE(swap_space, &swap_uio);
}

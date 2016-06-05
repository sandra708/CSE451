#include <types.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <kern/errno.h>
#include <thread.h>
#include <addrspace.h>


int sys_sbrk(intptr_t amount, int *error) {
	struct addrspace *as;
	
	as = proc_getas();
	vaddr_t heap_start = as->heap_start;
	vaddr_t heap_end = as->heap_end;
	
	(void) heap_start;//supress warning	
	*error = 0;
	
	if(amount == 0) {
		return heap_end;
	}
	return -1;
}

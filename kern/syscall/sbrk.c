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
	
	*error = 0;
	int prev;
	
	if (amount == 0) {
		return as->heap_end;
	} else if (amount < 0) {
		if (as->heap_end + amount >= as->heap_start) {
			prev = as->heap_end;
			as->heap_end += amount;
			return prev;
		} else {
			*error = EINVAL;
			return -1;
		}
	} else { //amount > 0
		if(true) { //TODO see what conditions make for an acceptable positive amount
			prev = as->heap_end;
			as->heap_end += amount;
			return prev;
			
		} else {
			*error = ENOMEM;
			return -1;
		}
	}
	return -1;
}

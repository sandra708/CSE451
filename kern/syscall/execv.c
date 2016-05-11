
#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <thread.h>

/*
execv system call
*/
int sys_execv(const char* program_name, char** args) {
	(void) program_name;
	(void) args;
	
	if(program_name == NULL || args == NULL) {
		return EFAULT;
	}
	lock_acquire(execv_lock);

	lock_release(execv_lock);
	return ENOTDIR;
}

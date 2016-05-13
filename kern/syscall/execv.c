
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
#include <copyinout.h>

/*
execv system call
*/
int sys_execv(const char* program_name, char** args) {
	
	vaddr_t entrypoint, stackptr;
	int retval, i, j, total_arg_len;
	int  argc = 0;
	struct vnode *v;
	struct addrspace *as;
	char* kprogram_name;
	char** kargs;
	size_t actual;

	kprintf("begin execv\n");

	lock_acquire(execv_lock);

	if(program_name == NULL || args == NULL) {
		lock_release(execv_lock);
		return EFAULT;
	}
	
	/*allocate space in the kernel for for the program name*/
	kprogram_name = (char*)kmalloc(PATH_MAX); 

	if(kprogram_name == NULL) {
		lock_release(execv_lock);
		return ENOMEM;
	}

	/*copy program name into the kernel*/
	retval = copyinstr((const_userptr_t) program_name, kprogram_name,
			PATH_MAX, &actual);
	
	if(retval) {
		kfree(kprogram_name);
		lock_release(execv_lock);
		return retval;
	}

	while(args[argc] != NULL) {
		argc++;
	}

	kprintf("Num of args = %d\n", argc);

	/* allocate space for argv in kernel */
	kargs = (char**) kmalloc(sizeof(char**));
	if(kargs == NULL) {
		kfree(kprogram_name);
		lock_release(execv_lock);
		return ENOMEM;
	}

	/*Copy in arguments into the kernel  one at a time, it's safest way I
          can think to do it at the time */
	total_arg_len = 0;
	for(i = 0; i < argc; i++) {
		int arglen = 0;
		char* curr_arg = args[i];
		while(curr_arg[arglen]) {
			arglen++;
		}
		total_arg_len += arglen;
		kargs[i] = (char*) kmalloc(arglen + 1); // +1 for null character
		if(kargs[i] == NULL || total_arg_len > ARG_MAX) {
			/* If this spot is reached, out of memory or the max
  			arg length has been exceeded
 			and now have to release all allocated elements of 
			argv, as well as regular other stuff
			*/
			for(j = 0; j < i; j++) {
				kfree(kargs[j]);
			}
			kfree(kargs);
			kfree(kprogram_name);
			lock_release(execv_lock);
			if(total_arg_len > ARG_MAX) {
				return E2BIG;
			}
			return ENOMEM;	
		}
		//Have memory allocated, copy in the argument
		copyinstr((userptr_t)args[i], kargs[i], arglen, &actual);
		kprintf("Args copied into kernel: arg%d=%c\n", i, kargs[i][0]);
	}
	kargs[argc] = NULL; //null terminate kernel args

	/* RunProgram code here for opening file, and creating address space */
	retval = vfs_open(program_name, O_RDONLY, 0, &v);
	if(retval) {
		for(j = 0; j < argc; j++)
			kfree(kargs[j]);
		kfree(kargs);
		kfree(kprogram_name);
		lock_release(execv_lock);
		return retval;
	}

	as = as_create();
	if(as == NULL) {
		for(j = 0; j < argc; j++)
			kfree(kargs[j]);
		kfree(kargs);
		kfree(kprogram_name);
		vfs_close(v);
		lock_release(execv_lock);
		return ENOMEM;
	}

	KASSERT(proc_getas() == null);
	proc_setas(as);
	as_activate();

	retval = load_elf(v, &entrypoint);
	if(retval) {
		for(j = 0; j < argc; j++)
			kfree(kargs[j]);
		kfree(kargs);
		kfree(kprogram_name);
		vfs_close(v);
		lock_release(execv_lock);
		return ENOMEM;
	} 
	
	vfs_close(v);

	retval = as_define_stack(as, &stackptr);
	if(retval) {
		for(j = 0; j < argc; j++)
			kfree(kargs[j]);
		kfree(kargs);
		kfree(kprogram_name);
		lock_release(execv_lock);
		return ENOMEM;
	}	

	lock_release(execv_lock);

	/* Warp to user mode. */
	enter_new_process(argc, NULL /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

}

/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <current.h>
#include <kern/errno.h>
#include <proc.h>
#include <copyinout.h>
#include <addrspace.h>
#include <syscall.h>

int sys_getpid(void){
	struct proc *cur = curproc;

	if(cur == NULL){
		panic("User processes must always have a process control block.");
	}

	pid_acquire_lock(pids);
	int pid = curproc->pid;
	pid_release_lock(pids);

	return pid;
}

static 
int wrap_forked_process(void *data, unsigned long num){
	(void) num;
	as_activate();
	enter_forked_process((struct trapframe *) data);
	/* should not return */
	return EINVAL;
}

int sys_fork(struct trapframe *tf, int *error){
	struct thread *cur = curthread;

	pid_acquire_lock(pids);

	int err = 0;

	// fork the process control block
	struct proc *newproc = proc_create_fork("PCB FORK", cur->t_proc, &err);
	if(err){
		*error = err;
		return -1;
	}

	// copy the trapframe to the kernel heap
	struct trapframe* onheap;
	err = trapframe_copy(tf, &onheap);
	if(err){
		*error = ENOMEM;
		proc_exit(newproc, 0);
		return -1;
	}

	// fork the new thread for the new process
	thread_fork("TCB FORK", newproc, wrap_forked_process, onheap, 0);

	pid_release_lock(pids);

	*error = 0;
	return newproc->pid;
}

int sys_waitpid(int pid, userptr_t status, int options){
	struct proc *cur = curproc;
	int err = 0;

	pid_acquire_lock(pids);

	// check that the options are valid
	if(options != 0){
		pid_release_lock(pids);
		return EINVAL;
	}

	// check that the status is a valid pointer
	int test = 0;
	if(status != NULL){
		err = copyin(status, &test, sizeof(int));
		if(err){
			pid_release_lock(pids);
			return err;
		}
	}

	struct proc *child = pid_get_proc(pids, pid);
	if(child == NULL){
		pid_release_lock(pids);
		return ESRCH;
	}

	if(child->parent != cur->pid){
		pid_release_lock(pids);
		return ECHILD;
	}

	/* indicate which child we are waiting on */
	cur->waitpid = pid;

	while(!child->exited){
		cv_wait(cur->wait, pids->lock);
	}

	// no longer waiting
	cur->waitpid = -1;

	// copy out the exit val
	if(status != NULL){
		int exit_val = child->exit_val;
		err = copyout(&exit_val, status, sizeof(int));
	}

	// clean up the child process
	proc_destroy(child);

	// remove reference from our children list
	proc_remchild(cur, pid);

	pid_release_lock(pids);

	return err;
}

void sys__exit(int exitcode){
	struct thread *cur = curthread;

	pid_acquire_lock(pids);
	// TODO: lock file-table

	struct proc *proc = cur->t_proc;
	
	proc_remthread(cur);

	proc_exit(proc, exitcode);

	pid_release_lock(pids);

	thread_exit();

	panic("SYS__exit should not return!!! braaaiiiinnnsss");
}

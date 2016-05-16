/*
 * Copyright (c) 2013
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

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <syscall.h>

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/* Original destructor code; detatches and cleans up OS161-provided fields*/
void proc_detatch(struct proc *proc);

/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name, int *error)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		*error = ENOMEM;
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		*error = ENOMEM;
		kfree(proc);
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	proc->children = list_create();
	if(proc->children == NULL){
		*error = ENOMEM;
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	proc->files = hashtable_create();
	if(proc->files == NULL){
		*error = ENOMEM;
		list_destroy(proc->children);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	proc->wait = cv_create("PROC WAITPID CV");
	if(proc->wait == NULL){
		*error = ENOMEM;
		hashtable_destroy(proc->files);
		list_destroy(proc->children);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}
	
	
	if(pids == NULL){
		// Should be only the bootstrapping kernel process
		pids = pid_create_tree(proc);
		proc->pid = 0;
	} else {
		// Assign a pid and add to directory tree
		proc->pid = pid_allocate(pids, proc);
		if(proc->pid < 0){
			*error = ENPROC;
			cv_destroy(proc->wait);
			list_destroy(proc->children);
			hashtable_destroy(proc->files);
			kfree(proc->p_name);
			kfree(proc);
			return NULL;
		}
	}

	proc->parent = -1;
	proc->waitpid = -1;
	proc->exited = false;
	proc->exit_val = 0;
  proc->next_fd = 3;
	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_detatch(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);
}

/* 
	Exits the process, destroying all non-essentials, but leaves the 
	structure behind if waitpid() might need to access it.

	Also detatches from open files and orphans (destroying if necessary)
	any child processes. 
*/
void 
proc_exit(struct proc *proc, int exitcode)
{
	/* First, detatch */
	proc_detatch(proc);
	
	/* orphan children */
	int *childpid = list_front(proc->children);
	while(childpid != NULL){
		struct proc *child = pid_get_proc(pids, *childpid);
		if(child != NULL){
			KASSERT(child->pid == *childpid);

			if(child->exited){
				// child is exited, will now never be joined
				proc_destroy(child);
			} else{
				// orphan a running child
				child->parent = -1;
			}
		}

		// advance through list, freeing memory as we go
		list_pop_front(proc->children);
		kfree(childpid);
		childpid = list_front(proc->children);
	}

	// actually destroy the list
	list_destroy(proc->children);
	proc->children = NULL;
  
  // detach from files
  int error;
	for(int i = 3; i < proc->next_fd; i++)
  {
    sys_close(i, &error);
  }
	hashtable_destroy(proc->files);
	proc->files = NULL;

	/* destroy own cv */
	cv_destroy(proc->wait);
	proc->wait = NULL;

	if(proc->parent == -1){
		// process is already orphaned
		proc_destroy(proc);
		return;
	} 

	struct proc *parent = pid_get_proc(pids, proc->parent);
	if(parent == NULL){
		// parent process could not be found
		proc_destroy(proc);
		return;
	}

	// now we have an existing parent, so we can't simply destroy everything ...

	/* Set internal exit data */
	proc->exited = true;
	proc->exit_val = exitcode;

	if(parent->waitpid == proc->pid){
		cv_broadcast(parent->wait, pids->lock);
	}

	return;
}

/* Final destroyer; internal pointers should be freed/detatched already */
void 
proc_destroy(struct proc *proc)
{
	pid_remove_proc(pids, proc->pid);
	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	int err = 0;
	kproc = proc_create("[kernel]", &err);
	if (kproc == NULL || pids == NULL || err != 0) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name, int *error)
{
	struct proc *newproc;

	newproc = proc_create(name, error);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

struct proc *
proc_create_fork(const char *name, struct proc *parent, int *error){
	struct proc *proc;

	proc = proc_create(name, error);
	if(proc == NULL){
		return NULL;
	}

	// VM - copy the address space from the parent

	spinlock_acquire(&parent->p_lock);
	struct addrspace *space = parent->p_addrspace;
	spinlock_release(&parent->p_lock);

	if(space != NULL){
		as_copy(space, &proc->p_addrspace);
	}

	// copy the current working directory
	spinlock_acquire(&parent->p_lock);
	if(parent->p_cwd != NULL){
		VOP_INCREF(parent->p_cwd);
		proc->p_cwd = parent->p_cwd;
	}
  if(parent->files != NULL)
  {
    proc->files = hashtable_create();
	  if(proc->files == NULL) {
		  *error = ENOMEM;
      proc_exit(proc, 0);
    }
    for(int i = 3; i < parent->next_fd; i++)
    {
      char* fdkey = int_to_byte_string(i);
      fcblock *controlblock = (fcblock*) hashtable_find(parent->files, fdkey, strlen(fdkey));
      if (controlblock != NULL)
      {
        fcblock *newcontrolblock = kmalloc(sizeof(fcblock));
        if (newcontrolblock == NULL)
        {
          *error = ENOMEM;
          proc_exit(proc, 0);
        }
        newcontrolblock->node = controlblock->node;
        newcontrolblock->offset = controlblock->offset;
        newcontrolblock->permissions = controlblock->permissions;
        hashtable_add(proc->files, fdkey, strlen(fdkey), newcontrolblock);
      }
    }
  }
	spinlock_release(&parent->p_lock);

	// set parent and child relationship
	int *pid = kmalloc(sizeof(pid));
	if(pid == NULL){
		*error = ENOMEM;
		proc_exit(proc, 0);
		return NULL;
	}

	*pid = proc->pid;
	list_push_back(parent->children, pid);
	proc->parent = parent->pid;

	return proc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/* Adds a file descriptor to the table of open files. 
 * Returns 0 on success or an error.
 */
int 
proc_addfile(struct proc *proc, char* fdkey, void* controlblock)
{
	if(hashtable_getsize(proc->files) >= OPEN_MAX){
		return EMFILE;
	}
	int err = hashtable_add(proc->files, fdkey, strlen(fdkey), controlblock);
	return err;
}

static
void 
proc_remlist(struct list *list, int val)
{
	if(list_isempty(list)){
		return;
	}

	int *first = list_front(list);
	list_pop_front(list);
	if(*first == val || list_isempty(list)){
		return;
	}

	// rotate through the list until we find the head again, or find the fd
	list_push_back(list, first);
	int *cur = list_front(list);
	while(*cur != *first){
		list_pop_front(list);
		if(*cur == val){
			return;
		}
		list_push_back(list, cur);
		cur = list_front(list);
	}
}

void
proc_remfile(struct proc *proc, int fd){
  hashtable_remove(proc->files, (char *) fd, 1);
	//proc_remlist(proc->files, fd);
}

void
proc_remchild(struct proc *proc, int child){
	proc_remlist(proc->children, child);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}

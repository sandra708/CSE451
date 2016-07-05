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
#include <kern/errno.h>
#include <lib.h>
#include <current.h>
#include <machine/tlb.h>
#include <machine/vm.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(int pid)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */
	as->pid = pid;
	as->pages = pagetable_create();

	as->destroying = false;
	as->destroy_count = 0;
	as->destroy_lock = lock_create("ADDRESS SPACE DESTROY");
	as->destroy_cv = cv_create("ADDRESS SPACE DESTROY");

	as->heap_end = 0;
	as->heap_start = 0;
	as->stack_base = (vaddr_t) -1;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret, int newpid)
{
	struct addrspace *newas;

	newas = as_create(newpid);
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	// don't call copy AFTER calling destroy
	KASSERT(old->destroying == false);

	// Insert the new address space into its process block, so it can be found
	struct proc *proc = pid_get_proc(pids, newpid);
	spinlock_acquire(&proc->p_lock);
	// shouldn't happen, but just in case, fix the memory leak
	while(proc->p_addrspace != NULL){
		struct addrspace *oldas = proc->p_addrspace;
		spinlock_release(&proc->p_lock);
		as_destroy(oldas);
		spinlock_acquire(&proc->p_lock);
	}
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);

	bool success = pagetable_copy(old->pages, old->pid, newas->pages, newas->pid);

	if(!success){
		as_destroy(newas);
		return ENOMEM;
	}

	newas->heap_start = old->heap_start;
	newas->heap_end = old->heap_end;
	newas->stack_base = old->stack_base;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */

	/* We produce a reference count of pages which are being destroyed 
	 * by other processes (due to swap conflicts) */
	as->destroy_count = 0;
	as->destroying = true;
	int count = pagetable_free_all(as->pages);

	/* We wait until we're notified that all the other threads have finished destroying the extra pages */
	lock_acquire(as->destroy_lock);
	as->destroy_count += count;
	while(as->destroy_count > 0){
		cv_wait(as->destroy_cv, as->destroy_lock);
	}
	lock_release(as->destroy_lock);

	pagetable_destroy(as->pages);

}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */
	/* Invalidate all of the tlb entries */
	vm_flush_tlb(as->pid);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */
  uint8_t flags = 0;
  if (executable > 0)
  {
    flags += 64;
  }
  if (writeable > 0)
  {
    flags += 32;
  }
  if (readable > 0)
  {
    flags += 16;
  }
  vaddr_t page = vaddr >> 12 << 12;
  for(vaddr_t v = page; v < page + memsize; v += 4096)
  {
    struct pagetable_entry* resident = pagetable_lookup(as->pages, v);
    while (resident == NULL)
    {
      paddr_t newaddr = pagetable_pull(as->pages, v, flags);
      if (newaddr == 0)
      {
        return ENOMEM;
      }
      resident = pagetable_lookup(as->pages, v);
    }
    resident->flags = resident->flags % 16 + flags;
  }

    as->heap_start = (page + memsize);
    if(as->heap_start & (4096 - 1))
    {
      as->heap_start = (as->heap_start + 4096) & (~(4096 - 1));
    }
    as->heap_end = as->heap_start;
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	as->loading = true;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	// revoke special writing privileges; then flush the TLB
	as->loading = false;
	vm_flush_tlb(0);
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	/* Initial user-level stack pointer */
	as->stack_base = USERSTACK - 4096 * 3;
	*stackptr = USERSTACK - 1;

	/* Save heap information */
	int heap_end = as->heap_end;
	int heap_start = as->heap_start;

	/* Initialize three pages for the stack */
	int err = as_define_region(as, as->stack_base, 4096*3, 1, 1, 0);

	as->heap_end = heap_end;
	as->heap_start = heap_start;

	return err;
}


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
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>


/*
 * System call dispatcher.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception-*.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. 64-bit arguments are passed in *aligned*
 * pairs of registers, that is, either a0/a1 or a2/a3. This means that
 * if the first argument is 32-bit and the second is 64-bit, a1 is
 * unused.
 *
 * This much is the same as the calling conventions for ordinary
 * function calls. In addition, the system call number is passed in
 * the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, or v0 and v1 if 64-bit. This is also like an ordinary
 * function call, and additionally the a3 register is also set to 0 to
 * indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/user/lib/libc/arch/mips/syscalls-mips.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * If you run out of registers (which happens quickly with 64-bit
 * values) further arguments must be fetched from the user-level
 * stack, starting at sp+16 to skip over the slots for the
 * registerized values, with copyin().
 */
void
syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	int err;

	KASSERT(curthread != NULL);
	KASSERT(curthread->t_curspl == 0);
	KASSERT(curthread->t_iplhigh_count == 0);

	callno = tf->tf_v0;

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values,
	 * like write.
	 */

	retval = 0;

	switch (callno) {
	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;

	    case SYS___time:
		err = sys___time((userptr_t)tf->tf_a0,
				 (userptr_t)tf->tf_a1);
		break;

	    /* Add stuff here */
	    case SYS_getpid:
	    /* limit.h limits possible pids to within a 32-bit address space */
		retval = (int32_t) sys_getpid();
		err = 0;
		break;

	    case SYS__exit:
		/* TODO: the exitcode? 32 bit? 64 bit? */
		sys__exit(tf->tf_a0);
		/* if we are here, something terrible happened */
		err = EINVAL;
		break;

	    case SYS_fork:
		retval = (int32_t) sys_fork(tf, &err);
		break;

	    case SYS_waitpid:
		err = sys_waitpid(tf->tf_a0, (userptr_t) tf->tf_a1, tf->tf_a2);
		retval = tf->tf_a0; /* we return the user-supplied pid*/
	    break;
	    case SYS_execv:
		err = sys_execv((const char*)tf->tf_a0, (char**)tf->tf_a1);
		break;

      case SYS_open:
        retval = sys_open((const char*)tf->tf_a0, tf->tf_a1, &err);
        break;

      case SYS_read:
        retval = sys_read(tf->tf_a0, (void*)tf->tf_a1, tf->tf_a2, &err);
        break;
      
      case SYS_write:
        retval = sys_write(tf->tf_a0, (const void*)tf->tf_a1, tf->tf_a2, &err);
        break;
      
      case SYS_close:
        retval = sys_close(tf->tf_a0, &err);
        break;

      case SYS_sbrk:
        retval = sys_sbrk(tf->tf_a0, &err);
	break;
	
	    default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}

	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */

	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	KASSERT(curthread->t_curspl == 0);
	/* ...or leak any spinlocks */
	KASSERT(curthread->t_iplhigh_count == 0);
}

/*
 * Enter user mode for a newly forked process.
 *
 * This function is provided as a reminder. You need to write
 * both it and the code that calls it.
 *
 * Thus, you can trash it and do things another way if you prefer.
 */
void
enter_forked_process(struct trapframe *data)
{
	struct trapframe tf;
	trapframe_copyinto(data, &tf);
	kfree(data);

	/* set values for the teleport which mimic returning from a syscall */
	tf.tf_v0 = 0;
	tf.tf_a3 = 0;
	tf.tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	KASSERT(curthread->t_curspl == 0);
	/* ...or leak any spinlocks */
	KASSERT(curthread->t_iplhigh_count == 0);

	mips_usermode(&tf);
}

int
trapframe_copy(struct trapframe *orig, struct trapframe **copy)
{
	*copy = kmalloc(sizeof(**copy));
	if(*copy == NULL){
		return 1;
	}
	trapframe_copyinto(orig, *copy);
	return 0;
}

void 
trapframe_copyinto(struct trapframe *orig, struct trapframe *copy)
{
	copy->tf_vaddr = orig->tf_vaddr;	/* coprocessor 0 vaddr register */
	copy->tf_status = orig->tf_status;	/* coprocessor 0 status register */
	copy->tf_cause = orig->tf_cause;	/* coprocessor 0 cause register */

	copy->tf_lo = orig->tf_lo;
	copy->tf_hi = orig->tf_hi;
	copy->tf_ra = orig->tf_ra;		/* Saved register 31 */
	copy->tf_at = orig->tf_at;		/* Saved register 1 (AT) */

	copy->tf_v0 = orig->tf_v0;		/* Saved register 2 (v0) */
	copy->tf_v1 = orig->tf_v1;		/* etc. */

	copy->tf_a0 = orig->tf_a0;
	copy->tf_a1 = orig->tf_a1;
	copy->tf_a2 = orig->tf_a2;
	copy->tf_a3 = orig->tf_a3;

	copy->tf_t0 = orig->tf_t0;
	copy->tf_t1 = orig->tf_t1;
	copy->tf_t2 = orig->tf_t2;
	copy->tf_t3 = orig->tf_t3;
	copy->tf_t4 = orig->tf_t4;
	copy->tf_t5 = orig->tf_t5;
	copy->tf_t6 = orig->tf_t6;
	copy->tf_t7 = orig->tf_t7;

	copy->tf_s0 = orig->tf_s0;
	copy->tf_s1 = orig->tf_s1;
	copy->tf_s2 = orig->tf_s2;
	copy->tf_s3 = orig->tf_s3;
	copy->tf_s4 = orig->tf_s4;
	copy->tf_s5 = orig->tf_s5;
	copy->tf_s6 = orig->tf_s6;
	copy->tf_s7 = orig->tf_s7;

	copy->tf_t8 = orig->tf_t8;
	copy->tf_t9 = orig->tf_t9;

	copy->tf_k0 = orig->tf_k0;		/* dummy (see exception-mips1.S comments) */
	copy->tf_k1 = orig->tf_k1;		/* dummy */
	
	copy->tf_gp = orig->tf_gp;
	copy->tf_sp = orig->tf_sp;
	copy->tf_s8 = orig->tf_s8;
	copy->tf_epc = orig->tf_epc;	/* coprocessor 0 epc register */
}

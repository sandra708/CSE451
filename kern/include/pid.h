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

#ifndef _PID_H_
#define _PID_H_

#include <synch.h>
#include <limits.h>


/*
	A data structure for maintaining a directory for the pids - process ids, currently in use

	Synchronization is the responsibility of the caller, with a lock provided on every tree-node for the caller's use
	This is because the caller may need to transact multiple operations, or hold the lock longer than is obvious
*/

// size (number of subtree branches) of each tree node
#define PID_DIR_SIZE 8

// from proc.h
struct proc;

struct pid_tree{
	// lock for altering this level of the tree
	struct lock *lock;

	// pointer to parent of the tree
	struct pid_tree *parent;

	// pid/proc pairs kept at this level of the tree
	int local_pids[PID_DIR_SIZE];
	struct proc *local_procs[PID_DIR_SIZE];

	// pointers to subtrees, and counts of pids actively allocated per subtree
	// subtree i is responsible for the address space between local_pid[i] 
	// to local_pid[i+1] or the end of the space
	int subtree_sizes[PID_DIR_SIZE];
	struct pid_tree* subtrees[PID_DIR_SIZE];
};

/*
	Initializes the first tree block and returns. 

	KProc, the kernel process, is designated with reserved pid = 0
*/
struct pid_tree *pid_create_tree(struct proc *kproc);

/*
	Allocates a new pid for the given process, attaches the process to the tree, and returns the pid
*/
int pid_allocate(struct pid_tree *tree, struct proc *next_proc);

/*
	Locates the given pid and returns the process associated with it, if one exists.
	Returns -1 if the process does not exist. 
*/
struct proc *pid_get_proc(struct pid_tree *tree, int pid);

/*
	Locates the given pid and removes its process from the tree, returning it
*/
struct proc *pid_remove_proc(struct pid_tree *tree, int pid);

/*
	Destroyes the given tree-block and all of its children, recursively, detatching from the parent if one is present.
	Returns 1 on success
	Will fail with 0 if any active processes exist in the tree
*/
int pid_destroy_tree(struct pid_tree *tree);

void pid_acquire_lock(struct pid_tree *tree);

void pid_release_lock(struct pid_tree *tree);

 #endif /* _PID_H_ */

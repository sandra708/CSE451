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

int pid_allocate_helper(struct pid_tree *tree, struct proc *proc, int pid_min, int pid_max);

struct pid_tree *pid_create_tree(struct proc *kproc){
	struct pid_tree *tree;
	tree = kmalloc(sizeof(*tree));

	if(tree == NULL){
		return NULL;
	}

	tree->lock = lock_create("PID_DIRECTORY_TREE");
	if(tree->lock == NULL){
		kfree(tree);
		return NULL;
	}

	for(int i = 0; i < PID_DIR_SIZE; i++){
		tree->local_pids[i] = -1;
		tree->local_procs[i] = NULL;
		tree->subtree_sizes[i] = 0;
		tree->subtrees[i] = NULL;
	}

	// Assign the kernel process to pid = 0
	if(kproc != NULL){
		tree->local_pids[0] = 0;
		tree->local_procs[0] = kproc;
	}

	return tree;
}

int pid_allocate(struct pid_tree *tree, struct proc *proc){
	
	if(tree == NULL){
		return -1;
	}

	int pid_min = PID_MIN;
	int pid_max = PID_MAX;

	return pid_allocate_helper(tree, proc, pid_min, pid_max);
}

int pid_allocate_helper(struct pid_tree *tree, struct proc *proc, int pid_min, int pid_max){
	
	// see if there are open spots in this block
	for(int i = 0; i < PID_DIR_SIZE; i++){
		// the given pid-slot is unassigned
		if(tree->local_procs[i] == NULL){

			// pick a pid that divides the address space into similar-sized components
			int pid = pid_min + (i) * (pid_max - pid_min) / PID_DIR_SIZE;

			// if we have a pid hanging around, its correct relative to nearby subtrees; use it
			if(tree->local_pids[i] != -1){
				pid = tree->local_pids[i];
			}

			tree->local_pids[i] = pid;
			tree->local_procs[i] = proc;
			return pid;
		}
	}

	// find the least-full subtree
	int max = tree->subtree_size[0];
	int idx = 0;
	for(int i = 1; i < PID_DIR_SIZE; i++){
		if(tree->subtree_size[i] < max){
			max = tree->subtree_size[i];
			idx = i;
		}
	}

	// if the subtree doesn't exist, create it
	if(tree->subtrees[idx] == NULL{
		tree->subtrees[idx] = pid_create_subtree(NULL);
	}

	// update subtree count  
	tree->subtree_counts[idx] += 1;

	//search recursively
	pid_min = tree->local_pids[idx] + 1;
	if(idx < PID_DIR_SIZE - 1){
		pid_max = tree->local_pids[idx + 1] - 1;
	}
	return pid_allocate_helper(tree->subtrees[i], proc, pid_min, pid_max);
}

struct proc *pid_get_proc(struct pid_tree *tree, int pid){
	// this is a non-binary search tree

	if(tree == NULL){
		return NULL;
	}

	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(pid == tree->local_pids[i]){
			return tree->local_procs[i];
		} else if(pid < tree->local_pids[i]){
			// we need to search between pid[i - 1] < pid < pid[i]
			if(i = 0){
				// falling off the front, there are no pids small enough to match
				return NULL;
			}
			return pid_get_proc(tree->subtrees[i], pid);
		}
	}

	// check the last subtree
	return pid_get_proc(tree->subtrees[PID_DIR_SIZE - 1], pid);
}

struct proc *pid_remove_proc(struct pid_tree *tree, int pid){
	// first we have to FIND the pid
	if(tree == NULL){
		return NULL;
	}

	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(pid == tree->local_pids[i]){
			// we found it!
			struct proc *proc = tree->local_procs[i];
			tree->local_procs[i] = NULL;

			// we want to keep the pid paired with a NULL block if there are subtrees to either side
			if(tree->subtree_sizes[i] == 0){
				if(i == 0 || tree->subtree_sizes[i - 1] == 0){
					tree->local_pids[i] = -1;
				}
			}

			if(tree->parent != NULL){
				struct pid_tree *parent = tree->parent;
				for(int j = PID_DIR_SIZE -1; j >= 0; j--){
					if(parent->local_pids[j] < pid){
						parent->subtree_sizes[j]--;
						int size = parent->subtree_sizes[j];
						if(size == 0){ 
							pid_destroy_tree(parent->subtrees[j]);
						}
						break;
					}
				}
			}
			return proc;	
		} else if(pid < tree->local_pids[i]){
			// we need to search between pid[i - 1] < pid < pid[i]
			if(i = 0){
				// falling off the front, there are no pids small enough to match
				return NULL;
			}
			return pid_remove_proc(tree->subtrees[i], pid);
		}
	}

	// check the last subtree
	return pid_remove_proc(tree->subtrees[PID_DIR_SIZE - 1], pid);
}

void pid_destroy_tree(struct pid_tree *tree){
	if(tree == NULL){
		return;
	}

	// verify tree is empty
	for(int i = 0; i < PID_DIR_SIZE; i++){
		bool check = false;
		check = check || (tree->local_pids[i] != -1);
		check = check || (tree->local_procs[i] != NULL);
		check = check || (tree->subtree_sizes[i] != 0);
		check = check || (tree->subtrees[i] != NULL);

		if(check){
			panic("Pid Tree should be empty before destroying ...");
		}
	}

	lock_destroy(tree->lock);
	kfree(tree);

	return;
}

void pid_acquire_lock(struct pid_tree *tree){
	lock_acquire(tree->lock);
}

void pid_release_lock(struct pid_tree *tree){
	lock_release(tree->lock);
}

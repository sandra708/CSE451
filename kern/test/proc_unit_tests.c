/*

Unit tests for multi-process support. 

*/

#include <types.h>
#include <current.h>
#include <proc.h>
#include <pid.h>
#include <limits.h>
#include <thread.h>

int test_processes(int nargs, char** args);
void test_pid_tree(void);
void verify_subtree_counts(struct pid_tree *tree);
void verify_ordering(struct pid_tree *tree);
void verify_number(struct pid_tree *tree, int goal);
int thread_helper(void *data, unsigned long num);
int get_max(struct pid_tree *tree);
int get_min(struct pid_tree *tree);

struct pid_tree *tree;


int test_processes(int nargs, char** args){
	(void) nargs;
	(void) args;

	test_pid_tree();

	return 0;
}

void test_pid_tree(void){

	tree = pid_create_tree(NULL);

	KASSERT(tree != NULL);
	KASSERT(tree->parent == NULL);
	verify_subtree_counts(tree);
	verify_number(tree, 0);

	kprintf("Pid directory tree successfully initialized.\n");

	// add processes to the tree, verifying at each one that we can retrieve it
	const int num_test = 257;
	int pids[num_test];
	for(int i = 0; i < num_test; i++){
		struct proc *proc;
		proc = kmalloc(sizeof(*proc));
		pid_acquire_lock(tree);
		int pid = pid_allocate(tree, proc);
		pids[i] = pid;
		pid_release_lock(tree);

		pid_acquire_lock(tree);
		struct proc *ret = pid_get_proc(tree, pid);
		KASSERT(ret == proc);
		pid_release_lock(tree);
	}

	verify_subtree_counts(tree);
	verify_ordering(tree);
	verify_number(tree, 257);

	kprintf("Adding and retrieving processes by a single thread is successful.\n");

	KASSERT(!pid_destroy_tree(tree));

	kprintf("Attempting to destroy a tree with active processes failed.\n");

	for(int i = 0; i < num_test; i++){
		pid_acquire_lock(tree);
		pid_remove_proc(tree, pids[i]);
		pid_release_lock(tree);
	}

	verify_subtree_counts(tree);
	verify_ordering(tree);
	verify_number(tree, 0);

	kprintf("Removing processes by a single thread is successful.\n");

	KASSERT(pid_destroy_tree(tree));
	tree = NULL;

	kprintf("Destroying an empty tree was successful.\n");

	KASSERT(!pid_destroy_tree(tree));

	kprintf("Attempting to destroy a NULL pointer failed.\n");

	struct proc *proc;
	proc = kmalloc(sizeof(*proc));
	tree = pid_create_tree(proc);

	const int num_threads = 7;
	struct thread *threads[num_threads];
	for(int i = 0; i < num_threads; i++){
		thread_fork_joinable("PID_TEST", NULL, thread_helper, NULL, 0, &threads[i]);
	}
	for(int i = 0; i < num_threads; i++){
		KASSERT(thread_join(threads[i]) == 1);
	}

	verify_ordering(tree);
	verify_subtree_counts(tree);
	verify_number(tree, 1);

	kprintf("Adding and removing processes with multiple threads succeeds.\n");

	struct proc *kproc = pid_remove_proc(tree, 0);
	KASSERT(kproc == proc);

	kprintf("Removing the initial kernel process at pre-determined pid = 0 succeeds.\n");

	KASSERT(pid_destroy_tree(tree));

}

int thread_helper(void *data, unsigned long num){
	(void) data;
	(void) num;

	const int num_procs = 128;

	for(int i = 0; i < num_procs; i++){

		struct proc *proc;
		proc = kmalloc(sizeof(*proc));

		// add the process to the tree, locking only for the add operation
		pid_acquire_lock(tree);
		int pid = pid_allocate(tree, proc);
		KASSERT(pid >= PID_MIN);
		KASSERT(pid <= PID_MAX);
		pid_release_lock(tree);

		// retrieve the process without changing the tree; verify that the tree is valid
		pid_acquire_lock(tree);
		struct proc *get = pid_get_proc(tree, pid);
		KASSERT(get == proc);
		verify_ordering(tree);
		verify_subtree_counts(tree);
		pid_release_lock(tree);

		// remove the process from the tree
		pid_acquire_lock(tree);
		struct proc *ret = pid_remove_proc(tree, pid);
		KASSERT(ret == proc);
		verify_ordering(tree);
		verify_subtree_counts(tree);
		pid_release_lock(tree);
	}

	return 1;
}

void verify_subtree_counts(struct pid_tree *tree){
	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(tree->subtrees[i] != NULL){
			KASSERT(tree->subtree_sizes[i] > 0);
			verify_number(tree->subtrees[i], tree->subtree_sizes[i]);
			verify_subtree_counts(tree->subtrees[i]);
		}
	}
}

void verify_number(struct pid_tree *tree, int goal){
	int count = 0;
	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(tree->local_procs[i] != NULL){
			KASSERT(tree->local_pids[i] != -1);
			count++;
		} if(tree->subtrees[i] == NULL){
			KASSERT(tree->subtree_sizes[i] == 0);
		} else{
			verify_number(tree->subtrees[i], tree->subtree_sizes[i]);
			count += tree->subtree_sizes[i];
		}
	}
	KASSERT(count == goal);
}

int get_min(struct pid_tree *tree){

	if(tree == NULL){
		return -1;
	}

	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(tree->local_pids[i] != -1){
			return tree->local_pids[i];
		} else if(get_min(tree->subtrees[i]) > -1){
			return get_min(tree->subtrees[i]);
		}
	}

	return -1;
}

int get_max(struct pid_tree *tree){
	if(tree == NULL){
		return -1;
	}

	for(int i = PID_DIR_SIZE -1; i >= 0; i++){
		if(get_max(tree->subtrees[i]) > -1){
			return get_max(tree->subtrees[i]);
		} else if(tree->local_pids[i] != -1){
			return tree->local_pids[i];
		}
	}

	return -1;
}

void verify_ordering(struct pid_tree *tree){
	// in one block, pids ordered from left to right
	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(tree->local_pids[i] != -1){
			KASSERT(tree->local_pids[i] <= PID_MAX);
			if(i == 0){
				KASSERT(tree->local_pids[i] == 0 || tree->local_pids[i] >= PID_MIN);
			} else{
				KASSERT(tree->local_pids[i] >= PID_MIN);
			}
			for(int j = i + 1; j < PID_DIR_SIZE; j++){
				if(tree->local_pids[i] != -1 && tree->local_pids[j] != -1){
					KASSERT(tree->local_pids[i] < tree->local_pids[j]);
				}
			}
		}
	}

	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(tree->subtrees[i] != NULL){
			verify_ordering(tree->subtrees[i]);
			int max = get_max(tree->subtrees[i]);
			int min = get_min(tree->subtrees[i]);
			if(max != -1){
				KASSERT(min != -1);
				KASSERT(tree->local_pids[i] < min);
				if(i < PID_DIR_SIZE - 1){
					KASSERT(max < tree->local_pids[i + 1]);
				}
			}
		}
	}
}

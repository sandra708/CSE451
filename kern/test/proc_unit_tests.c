/*

Unit tests for multi-process support. 

*/

#include <types.h>
#include <current.h>
#include <limits.h>
#include <thread.h>
#include <proc.h>

int test_processes(int nargs, char** args);
void test_pid_tree(void);
int thread_helper(void *data, unsigned long num);

void verify_subtree_counts(struct pid_tree *tree);
void verify_ordering(struct pid_tree *tree);
void verify_number(struct pid_tree *tree, int goal);
int get_max(struct pid_tree *tree);
int get_min(struct pid_tree *tree);

int test__exit(int nargs, char** args);

int test_exit_child_first(void);
int test_exit_parent_first(void);

void verify_proc(struct pid_tree *pids, struct proc *proc);
void verify_all_procs(struct pid_tree *tree);

struct pid_tree *tree;

int test__exit(int nargs, char** args){
	(void) nargs;
	(void) args;

	// since methods are embedded, this HAS to modify and test against the live pid directory

	// verify the tree of live processes (!) 
	pid_acquire_lock(pids);
	verify_all_procs(pids);
	pid_release_lock(pids);

	kprintf("The live process tree verified as correct.\n");

	// verify the live kproc (!)
	pid_acquire_lock(pids);
	verify_proc(pids, kproc);
	pid_release_lock(pids);

	kprintf("The live kernel process verified as correct.\n");

	test_exit_child_first();

	test_exit_parent_first();

	return 0;
}

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
	tree = NULL;
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

int test_exit_child_first(){

	// initialize 2 PCBs
	struct proc *parent = proc_create_runprogram("PARENT:UNITTEST");
	struct proc *child = proc_create_runprogram("CHILD:UNITTEST");

	KASSERT(child->parent == -1);
	KASSERT(parent->parent == -1);

	// assign as parent and child
	int parent_pid = parent->pid;
	int child_pid = child->pid;

	child->parent = parent_pid;
	int *node = kmalloc(sizeof(node));
	*node = child_pid;
	list_push_back(parent->children, node);

	// child exits
	pid_acquire_lock(pids);
	proc_exit(child, 1);

	verify_all_procs(pids);
	pid_release_lock(pids);

	pid_acquire_lock(pids);
	struct proc *remain = pid_get_proc(pids, *node);
	KASSERT(remain == child);
	KASSERT(child->exit_val == 1);
	pid_release_lock(pids);

	kprintf("Child exiting before parent remains in existence with correctly stored exit value.\n");

	// parent exits
	pid_acquire_lock(pids);

	proc_exit(parent, 1);
	verify_all_procs(pids);

	remain = pid_get_proc(pids, parent_pid);
	KASSERT(remain == NULL);

	kprintf("Orphan process exiting is fully destroyed.\n");

	remain = pid_get_proc(pids, child_pid);
	KASSERT(remain == NULL);

	kprintf("Parent exiting cleans up previously-exited child.\n");

	// the node pointer should have been freed by the parent's exit

	pid_release_lock(pids);

	return 0;
}

int test_exit_parent_first(){

	// initialize 2 PCBs
	struct proc *parent = proc_create_runprogram("PARENT:UNITTEST");
	struct proc *child = proc_create_runprogram("CHILD:UNITTEST");

	KASSERT(child->parent == -1);
	KASSERT(parent->parent == -1);

	// assign as parent and child
	int parent_pid = parent->pid;
	int child_pid = child->pid;

	child->parent = parent_pid;
	int *node = kmalloc(sizeof(node));
	*node = child_pid;
	list_push_back(parent->children, node);


	// parent exits
	pid_acquire_lock(pids);

	verify_all_procs(pids);

	proc_exit(parent, 1);

	// since we are still in the lock, the pid is NOT reassigned yet
	struct proc *remain = pid_get_proc(pids, parent_pid);
	KASSERT(remain == NULL);

	KASSERT(child->parent == -1);

	verify_all_procs(pids);

	pid_release_lock(pids);

	kprintf("Parent exiting first successfully orphans child.\n");

	// child exits
	pid_acquire_lock(pids);
	verify_all_procs(pids);

	remain = pid_get_proc(pids, child_pid);

	KASSERT(remain == child);

	proc_exit(child, 1);

	// since we are still in the lock, the pid is NOT reassigned yet
	remain = pid_get_proc(pids, child_pid);
	KASSERT(remain == NULL);

	verify_all_procs(pids);
	pid_release_lock(pids);

	kprintf("Newly orphaned child ceases to exist on exit.\n");

	return 0;
}

void verify_all_procs(struct pid_tree *pids){
	if(pids == NULL){
		return;
	}

	struct pid_tree *root = pids;
	while(root->parent != NULL){
		root = root->parent;
	}

	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(pids->local_procs[i] != NULL){
			verify_proc(root, pids->local_procs[i]);
		}
		verify_all_procs(pids->subtrees[i]);
	}
}

void verify_proc(struct pid_tree *pids, struct proc *proc){
	KASSERT(pids != NULL);
	KASSERT(proc != NULL);

	KASSERT(proc->pid == 0 || (proc->pid >= PID_MIN && proc->pid <= PID_MAX));
	KASSERT(proc->parent == -1 || proc->parent == 0 || (proc->parent >= PID_MIN && proc->parent <= PID_MAX));
	KASSERT(proc->waitpid == -1 || (proc->waitpid >= PID_MIN && proc->waitpid <= PID_MAX));

	if(proc->exited){
		// exited; most data structures already destroyed
		KASSERT(proc->parent != -1);
		KASSERT(proc->waitpid == -1);
		KASSERT(proc->pid != 0);

		KASSERT(proc->children == NULL);
		KASSERT(proc->files == NULL);
		KASSERT(proc->wait == NULL);

		KASSERT(proc->p_addrspace == NULL);
		KASSERT(proc->p_cwd == NULL);
	} else {
		// running;
		KASSERT(proc->exit_val == 0);
		list_assertvalid(proc->children);
		list_assertvalid(proc->files);
		// not much we can do to check validity of cv, addrspace, vnode
	}

	if(proc->parent != -1){
		struct proc *parent = pid_get_proc(pids, proc->parent);
		KASSERT(parent != NULL);
		// check this pid represented among parent's children
	}

	if(proc->waitpid != -1){
		struct proc *wait = pid_get_proc(pids, proc->waitpid);
		KASSERT(wait != NULL);
		KASSERT(wait->parent == proc->pid);
	}
}

/*

Unit tests for multi-process support. 

*/

#include <types.h>
#include <current.h>
#include <kern/errno.h>
#include <limits.h>
#include <clock.h>
#include <thread.h>
#include <proc.h>
#include <syscall.h>
#include <../arch/mips/include/trapframe.h>

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

int test_fork(int nargs, char** args);
void fork_pcb(void);
void fork_from_kernel(void);

int test_wait(int nargs, char** args);

void wait_for_non_child(void);
void wait_for_non_exist(void);
void bad_options(void);
void bad_status_fail_fast(void);
void run_waitpid(void);
int wait_parent_thread(void *data, unsigned long num);
int wait_child_thread(void *data, unsigned long num);

struct pid_tree *tree;

int test_wait(int nargs, char** args){
	(void) nargs;
	(void) args;

	wait_for_non_exist();
	wait_for_non_child();
	bad_options();
	bad_status_fail_fast();
	run_waitpid();

	return 0;
}

int test_fork(int nargs, char** args){
	(void) nargs;
	(void) args;
	fork_pcb();
	fork_from_kernel();

	return 0;
}

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

	KASSERT(curproc->pid == 0);

	kprintf("This test should now crash ...\n");

	sys__exit(0);

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

	for(int i = PID_DIR_SIZE -1; i >= 0; i--){
		if(tree->subtrees[i] != NULL){
			if(get_max(tree->subtrees[i]) > -1){
				return get_max(tree->subtrees[i]);
			} else if(tree->local_pids[i] != -1){
				return tree->local_pids[i];
			}
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
	int err = 0;

	// initialize 2 PCBs
	struct proc *parent = proc_create_runprogram("PARENT:UNITTEST", &err);
	KASSERT(err == 0);

	struct proc *child = proc_create_runprogram("CHILD:UNITTEST", &err);
	KASSERT(err == 0);

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
	int err = 0;

	// initialize 2 PCBs
	struct proc *parent = proc_create_runprogram("PARENT:UNITTEST", &err);
	KASSERT(err == 0);

	struct proc *child = proc_create_runprogram("CHILD:UNITTEST", &err);
	KASSERT(err == 0);

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
		hashtable_assertvalid(proc->files);
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

void fork_pcb(void){

	pid_acquire_lock(pids);

	int err = 0;
	struct proc *parent = proc_create_runprogram("UNITTEST:PARENT", &err);
	KASSERT(err == 0);

	struct proc *child = proc_create_fork("UNITTEST:CHILD", parent, &err);

	pid_release_lock(pids);

	KASSERT(err == 0);
	KASSERT(child->parent == parent->pid);
	KASSERT(!list_isempty(parent->children));
	KASSERT(list_getsize(parent->children) == 1);
	int *pid = list_front(parent->children);
	KASSERT(*pid == child->pid);

	KASSERT(child->p_cwd == parent->p_cwd);
	// how to assert about the address space contents?

	KASSERT(hashtable_isempty(child->files));
	KASSERT(list_isempty(child->children));
	KASSERT(child->exited == false);
	KASSERT(child->waitpid == -1);
	// can't assert that a cv is empty?

	// clean up memory
	proc_exit(child, 0);
	proc_exit(parent, 0);

	kprintf("Successfully forked a child PCB from the parent.\n");
}

void fork_from_kernel(void){
	kprintf("This is a handle for gdb, rather than a unit-test proper, since the trapframe is bogus.\n");
	kprintf("This test should crash.\n");

	int err = 0;
	struct trapframe *tf;
	tf = kmalloc(sizeof(*tf));
	int pid = sys_fork(tf, &err);
	if(pid == 0){
		kprintf("Successfully emerged into the child process!\n");
		sys__exit(0);
	} else if(pid == -1){
		kprintf("Error!\n");
	} else{
		kprintf("Successfully returned from fork().\n");
	}
	kfree(tf);
}

void wait_for_non_child(void){
	kprintf("Attempting to wait for a non-child; expecting error.\n");

	struct thread *cur = curthread;

	int err = 0;
	pid_acquire_lock(pids);
	struct proc *proc = proc_create_runprogram("PROC:TEST", &err);
	pid_release_lock(pids);
	KASSERT(err == 0);
	KASSERT(proc->exited == false);
	KASSERT(proc->parent != cur->t_proc->pid);

	err = sys_waitpid(proc->pid, NULL, 0);
	KASSERT(err == ECHILD);

	proc_exit(proc, 0);

	kprintf("... Passed.\n");
}

void wait_for_non_exist(void){
	kprintf("Attempting to wait for a non-existant process; expecting error.\n");

	int err = 0;
	int pid = 4095; /* A pid which is assigned later; we guess it is & remains un-used. */

	pid_acquire_lock(pids);
	struct proc *proc = pid_get_proc(pids, pid);
	KASSERT(proc == NULL);
	pid_release_lock(pids);

	err = sys_waitpid(pid, NULL, 0);
	KASSERT(err == ESRCH);

	kprintf("... Passed.\n");
}

void bad_options(void){
	kprintf("Attempting to wait using bad options; expecting error.\n");

	int err = 0;
	pid_acquire_lock(pids);
	struct proc *proc = proc_create_runprogram("PROC:TEST", &err);
	pid_release_lock(pids);
	KASSERT(err == 0);
	KASSERT(proc->exited == false);

	for(int i = 1; i < 4096; i += 3){
		err = sys_waitpid(proc->pid, NULL, i);
		KASSERT(err == EINVAL);
	}

	proc_exit(proc, 0);


	kprintf("... Passed.\n");
}

void bad_status_fail_fast(void){
	kprintf("Attempting to wait using a bad status pointer; expecting error.\n");

	int err = 0;
	pid_acquire_lock(pids);
	struct proc *proc = proc_create_runprogram("PROC:TEST", &err);
	pid_release_lock(pids);
	KASSERT(err == 0);
	KASSERT(proc->exited == false);

	err = sys_waitpid(proc->pid, (userptr_t) 1, 0);
	KASSERT(err == EFAULT);

	proc_exit(proc, 0);


	kprintf("... Passed.\n");
}

void run_waitpid(void){
	kprintf("Testing waitpid() in a multi-threaded environment.\n");	

	const int num_parent_threads = 3;
	
	struct proc *proc = curproc;

	int err = 0;

	int pid_id[num_parent_threads];
	
	for(int i = 0; i < num_parent_threads; i++){
		pid_acquire_lock(pids);
		struct proc *child = proc_create_fork("UNITTEST:PARENT", proc, &err);
		KASSERT(err == 0); 
		pid_id[i] = child->pid;
		verify_ordering(pids);
		thread_fork("UNITTEST", child, wait_parent_thread, NULL, pid_id[i]);
		pid_release_lock(pids);
	}
	
	for(int i = 0; i < num_parent_threads; i++){
		err = sys_waitpid(pid_id[i], NULL, 0);
		if(err){
			kprintf("Error %d in run_waitpid test.\n", err);
		}else{
			kprintf("Master thread waited for parent %d.\n",  pid_id[i]); 
		}
		KASSERT(err == 0);
		KASSERT(list_getsize(proc->children) == (unsigned) (num_parent_threads - i - 1)); 
		pid_acquire_lock(pids);
		verify_ordering(pids);
		struct proc *child = pid_get_proc(pids, pid_id[i]);
		KASSERT(child == NULL || child->parent != proc->pid);
		pid_release_lock(pids);
	}

	pid_acquire_lock(pids);
	//verify_number(pids, 1); /* kproc is the only active process */
	pid_release_lock(pids);
	kprintf("...Passed.\n");
}

int wait_parent_thread(void *data, unsigned long num){
	kprintf("Parent thread %lu starting.\n", num);
	(void) data;

	const int num_child_threads = 5;
	int err = 0;

	pid_acquire_lock(pids);
	struct proc *parent = pid_get_proc(pids, num);
	pid_release_lock(pids);

	int pid_id[num_child_threads];
	
	for(int i = 0; i < num_child_threads; i++){
		pid_acquire_lock(pids);
		verify_ordering(pids);
		struct proc *child = proc_create_fork("UNITTEST:CHILD", parent, &err);
		KASSERT(err == 0); 
		pid_id[i] = child->pid;
		thread_fork("UNITTEST:CHILD", child, wait_child_thread, NULL, pid_id[i]);
		pid_release_lock(pids);
	}
	
	for(int i = 0; i < num_child_threads; i++){
		err = sys_waitpid(pid_id[i], NULL,  0);
		if(err){
			kprintf("Error %d in wait_parent.\n", err);
		} else{
			kprintf("Parent thread %lu waited for child %d.\n", num, pid_id[i]);
		}
		KASSERT(err == 0);
		KASSERT(list_getsize(parent->children) == (unsigned) (num_child_threads - i - 1)); 
		pid_acquire_lock(pids);
		struct proc *child = pid_get_proc(pids, pid_id[i]);
		KASSERT(child == NULL || child->parent != parent->pid);
		verify_ordering(pids);
		pid_release_lock(pids);
	}

	sys__exit(parent->pid);

	kprintf("Parent thread %lu finished.\n", num);

	return 0;
}

int wait_child_thread(void *data, unsigned long num){
	(void) data;

	//kprintf("Child thread %lu starting.\n", num);

	// sleep anywhere from 0 to 4 seconds
	clocksleep(num % 5);

	kprintf("Child thread %lu exiting.\n", num);

	sys__exit(num);

	// should not return
	return 0;
}

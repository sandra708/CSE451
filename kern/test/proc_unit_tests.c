/*

Unit tests for multi-process support. 

*/

#include <proc.h>
#include <pid.h>
#include <limits.h>

void test_pid_tree(void);
void verify_subtree_counts(struct pid_tree *tree);
void verify_ordering(void);
void verify_number(struct pid_tree *tree, int goal);
void verify_bounds(void);

struct pid_tree *tree;


void test_processes(int nargs, char** args){
	test_pid_tree();
}

void test_pid_tree(void){

	tree = pid_tree_create(NULL);

	KASSERT(tree != NULL);
	KASSERT(tree->parent == NULL);
	verify_subtree_counts(tree);
	verify_number(tree, 0);

	kprintf("Pid directory tree successfully initialized.");

}

void verify_subtree_counts(struct pid_tree *tree){
	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(tree->subtrees[i] != NULL){
			KASSERT(verify_number(tree->subtrees[i], tree->subtree_counts[i]));
			KASSERT(verify_subtree_counts(tree->subtrees[i]));
		}
	}
}

void verify_number(struct pid_tree *tree, int goal){
	int count = 0;
	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(tree->local_pids[i] != -1){
			KASSERT(tree->local_procs[i] != NULL);
			count++;
		}
		if(tree->subtrees[i] == NULL){
			KASSERT(tree->subtree_counts[i] == 0);
		} else{
			verify_number(tree->subtrees[i], tree->subtree_counts[i]);
			count += tree->subtree_counts[i];
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
		} else if(get_min(tree->subtrees[i])) > -1){
			return get_min(tree->subtrees[i]);
		}
	}

	return -1;
}

int get_max(struct pid_tree *tree){
	if(tree == NULL){
		return -1;
	}

	for(int i = PID_DIR_MAX -1; i >= 0; i++){
		if(get_max(tree->subtrees[i] > -1){
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
		for(int j = i + 1; j < PID_DIR_SIZE; j++){
			if(tree->local_pids[i] != -1 && tree->local_pids[j] != -1){
				KASSERT(tree->local_pids[i] < tree->local_pids[j]);
			}
		}
	}

	for(int i = 0; i < PID_DIR_SIZE; i++){
		if(tree->subtrees[i] != NULL){
			verify_ordering(tree->subtrees[i]);
			// check min-maxes
		}
	}
}

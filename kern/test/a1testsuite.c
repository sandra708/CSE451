#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <synch.h>
#include <thread.h>
#include <current.h>
#include <clock.h>
#include <test.h>


#define NAMESTRING "lock-testing-name"


void init(void);
void testlockcreate(void);
void testlockholder(void);
void testlockstate(void);
void testlockdoihold(void);
void testlockmultiplethreads(void);
int multiplethreadhelper(void *data, unsigned long num);

static struct lock *global_lk;
static int global1;
static int global2;

int assignment1testsuite(int nargs, char **args) {

	init();

	(void)nargs; (void)args;

	/* Assignment 1 Lock unit tests */
    testlockcreate();
    testlockholder();
    testlockstate();
    testlockdoihold();
    testlockmultiplethreads();


	return 0;
}


void init() {
	global_lk = lock_create("global_lk");

	if(global_lk == NULL) {
		panic("testLockCreateCorrect: creating new lock failed\n");
	}
}

/*
 Creates the lock, and...
 	- Tests if the lock is null
 	- Tests that the name is equivlent to the passed string
 	- Tests that the pointer to the name is different form passed name
 	- Tests the the lock's wait channel is non-null
 	- Tests that the lock is unlocked initially
 	- Tests that the lock has no owner
*/
void testlockcreate() {
	struct lock *lk;
	const char *name = NAMESTRING;
	kprintf("Testing lock creation\n");

	lk = lock_create(name);

	if(lk == NULL) {
		panic("testLockCreateCorrect: creating new lock failed\n");
	}
	KASSERT(!strcmp(lk->lk_name, name));
	KASSERT(lk->lk_name != name);
	KASSERT(lk->lock_wchan != NULL);
	KASSERT(lk->is_locked == false);
	KASSERT(lk->lock_holder == NULL);

	lock_destroy(lk);
}

/*
	This test makes sure that the lock_holder field is correctly set with the lock
*/
void testlockholder() {
	struct lock *lk;
	const char *name = NAMESTRING;
	kprintf("Testing lock holder correct when aquired and released\n");

	lk = lock_create(name);

	if(lk == NULL) {
		panic("testLockCreateCorrect: creating new lock failed\n");
	}

	lock_acquire(lk);
	KASSERT(lk->lock_holder == curthread);
	lock_release(lk);
	KASSERT(lk->lock_holder == NULL);

}

/*
	These tests insure that the lock's is_locked state is always correct
	based on the lock_aquire and release calls
*/
void testlockstate() {
	struct lock *lk;
	const char *name = NAMESTRING;
	kprintf("Testing lock holder correct when aquired and released\n");

	lk = lock_create(name);

	if(lk == NULL) {
		panic("testLockCreateCorrect: creating new lock failed\n");
	}

	KASSERT(lk->is_locked == false);
	lock_acquire(lk);
	KASSERT(lk->is_locked == true);
	lock_release(lk);
	KASSERT(lk->is_locked == false);


}

/*
	This tests the lock_do_i_hold function, in states where
	the current thread holds the lock, makes sure that grabbing a lock
	does not interfere with the states of other locks
*/
void testlockdoihold() {
	struct lock *lk;
	const char *name = NAMESTRING;
	kprintf("Testing lock_do_i_hold function works correctly\n");

	lk = lock_create(name);

	if(lk == NULL) {
		panic("testLockCreateCorrect: creating new lock failed\n");
	}

	KASSERT(lock_do_i_hold(lk) == false);
	KASSERT(lock_do_i_hold(global_lk) == false);

	lock_acquire(lk);
		KASSERT(lk->lock_holder == curthread);
		KASSERT(lock_do_i_hold(lk) == true);
		KASSERT(lock_do_i_hold(global_lk) == false);
	lock_release(lk);

	KASSERT(lock_do_i_hold(lk) == false);
	KASSERT(lock_do_i_hold(global_lk) == false);
}

void testlockmultiplethreads() {
	struct lock *lk;
	int i, result;
	kprintf("Testing lock with multiple threads accessing same globals\n");
	const char *name = NAMESTRING;

	lk = lock_create(name);
	if(lk == NULL) {
		panic("testLockCreateCorrect: creating new lock failed\n");
	}

	for (i=0; i<50; i++) {
		result = thread_fork("locktest", NULL, multiplethreadhelper, NULL, i);
		if (result) {
			panic("locktest: thread_fork failed: %s\n",  strerror(result));
		}
		
	}
}

int multiplethreadhelper(void *data, unsigned long num) {
	int i;
	(void)data;

	for (i=0; i<32; i++) {
		lock_acquire(global_lk);
		global1 = num;
		global2 = num*num;

		if (global2 != global1*global1) {
			kprintf("Multiple Thread Test failed\n");
			lock_release(global_lk);
			thread_exit();
		}

		lock_release(global_lk);
	}
	return 0;
}
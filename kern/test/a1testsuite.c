#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <synch.h>
#include <thread.h>
#include <current.h>
#include <clock.h>
#include <test.h>


#define NAMESTRING "lock-testing-name"

void testlockcreate(void);
//int testlockmultiplethreads();
//int multiplethreadhelper();

int assignment1testsuite(int nargs, char **args) {
	(void)nargs; (void)args;
        testlockcreate();
	return 0;
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

	lk = lock_create(name);

	if(lk == NULL) {
		panic("testLockCreateCorrect: createing new lock failed\n");
	}
	KASSERT(!strcmp(lk->lk_name, name));
	KASSERT(lk->lk_name != name);
	KASSERT(lk->lock_wchan != NULL);
	KASSERT(lk->is_locked == false);
	KASSERT(lk->lock_holder == NULL);

	lock_destroy(lk);
}
/*
int testlockmultiplethreads() {
	struct lock *lk;
	int i, result;

	const char *name = NAMESTRING;

	(void)nargs; (void)args;

	lk = lock_create(name);

	void* data;
	*data = 0;
	for (i=0; i<10; i++) {
		result = thread_fork("synchtest", NULL, multipleThreadHelper,
				     data, i);
		kprintf("DATA: %d", *data);
		if (result) {
			panic("locktest: thread_fork failed: %s\n",
		}
		
	}
}

int multiplethreadhelper(void *data, unsigned long num) {
	int i;
	*data = *data + 1;
	return 0;
}*/

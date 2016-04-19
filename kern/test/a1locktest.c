#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <synch.h>
#include <thread.h>
#include <current.h>
#include <clock.h>
#include <test.h>


#define NAMESTRING "lock-testing-name"

/*
 Creates the lock, and...
 	- Tests if the lock is null
 	- Tests that the name is equivlent to the passed string
 	- Tests that the pointer to the name is different form passed name
 	- Tests the the lock's wait channel is non-null
 	- Tests that the lock's spinlock is non-null
 	- Tests that the lock is unlocked initially
 	- Tests that the lock has no owner
*/
int testLockCreateCorrect(int nargs, char **args) {
	struct lock *lk;
	const char *name = NAMESTRING;

	(void)nargs; (void)args;

	lk = lock_create(name);

	if(lock == NULL) {
		panic("testLockCreateCorrect: createing new lock failed\n");
	}
	KASSERT(!strcmp(lk->lk_name, name));
	KASSERT(lk->lk_name != name);
	KASSERT(lk->lock_wchan != NULL);
	KASSERT(lk->is_locked == false);
	KASSERT(lk->lock_spinlock != NULL);

	lock_destroy(lk);
	return 0;
}

int testLockCreateDoesNotAcceptNullName(int nargs, char **args) {
	struct lock *lk;

	(void)nargs; (void)args;

	kprintf("This should crash with a kernel null dereference\n");
	lk = lock_create(NULL);
	
	(void)lk;
	panic("testLockCreateDoesNotAcceptNullName: llock_create accepted a null name\n");
	return 0;
}


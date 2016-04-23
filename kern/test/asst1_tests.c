#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <synch.h>
#include <thread.h>
#include <current.h>
#include <clock.h>
#include <test.h>
#include <safelist.h>

#define NAMESTRING "lock-testing-name"


void init(void);
void testlockcreate(void);
void testlockholder(void);
void testlockstate(void);
void testlockdoihold(void);
void testlockmultiplethreads(void);
int multiplethreadhelper(void *data, unsigned long num);
void testcvcreate(void);
void testsignal(void);
int waiter(void *data, unsigned long num);
void testbroadcast(void);
int bwaiter(void *data, unsigned long num);
void testcvlock(void);
void testcvdestroy(void);

int appendThread(void *data, unsigned long num);
int appendAndPollThread(void *data, unsigned long num);
int pollThread(void *data, unsigned long num);
int frontThread(void *data, unsigned long num);
void testlist(void);

static struct lock *global_lk;
static struct cv *global_cv;
static int global1;
static int global2;
static struct safelist *global_list;

int asst1_tests(int nargs, char **args) {

	init();

	(void)nargs; (void)args;

	/* Assignment 1 Lock unit tests */
    testlockcreate();
    testlockholder();
    testlockstate();
    testlockdoihold();
    testlockmultiplethreads();
    testcvcreate();
    testsignal();
    testbroadcast();
    testcvlock();
    testcvdestroy();
    testlist();

    safelist_destroy(global_list);

	return 0;
}


void init() {
	global_lk = lock_create("global_lk");

	if(global_lk == NULL) {
		panic("testLockCreateCorrect: creating new lock failed\n");
	}

  global_cv = cv_create("global_cv");
  if(global_cv == NULL) {
		panic("testCvCreateCorrect: creating new cv failed\n");
	}

	global_list = safelist_create();
	if(global_list == NULL){
		panic("testListCreateCorrect: creating new list failed\n");
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
	kprintf("Testing lock holder correct when acquired and released\n");

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
	based on the lock_acquire and release calls
*/
void testlockstate() {
	struct lock *lk;
	const char *name = NAMESTRING;
	kprintf("Testing lock holder correct when acquired and released\n");

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
			return 1;
		}

		lock_release(global_lk);
	}
	return 0;
}

/*
  Tests that new cvs are created with the proper values.
*/
void testcvcreate() {
	struct cv *cv;
	const char *name = NAMESTRING;
	kprintf("Testing cv creation\n");

	cv = cv_create(name);

	if(cv == NULL) {
		panic("testCvCreateCorrect: creating new cv failed\n");
	}
	KASSERT(!strcmp(cv->cv_name, name));
	KASSERT(cv->cv_name != name);
	KASSERT(cv->cv_wchan != NULL);
	KASSERT(cv->cv_lock == NULL);

	cv_destroy(cv);
}

/*
  Tests that cv_signal properly wakes up a waiter.
*/
void testsignal() {
  kprintf("Testing cv signal\n");
  global1 = 1;
  thread_fork("cvtest", NULL, waiter, NULL, 1);
  while(global1 == 1) {
    global2 = global2;
  }
  lock_acquire(global_lk);
  KASSERT(global1 == 2);
  global1 = 3;
  cv_signal(global_cv, global_lk);
  KASSERT(global1 == 3);
  lock_release(global_lk);
  clocksleep(1);
  KASSERT(global1 == 4);
}

/*
  Helper for cv signal test method.
*/
int waiter(void *data, unsigned long num) {
  global2 = num;
  (void)data;
  lock_acquire(global_lk);
  global1 = 2;
  while(global1 == 2) {
    cv_wait(global_cv, global_lk);
  }
  KASSERT(global1 == 3);
  lock_release(global_lk);
  global1 = 4;
  return 0;
}

/*
  Tests that cv_broadcast properly wakes up all waiters.
*/
void testbroadcast() {
  kprintf("Testing cv broadcast\n");
  global1 = 1;
  thread_fork("cvtest1", NULL, bwaiter, NULL, 1);
  thread_fork("cvtest2", NULL, bwaiter, NULL, 1);
  thread_fork("cvtest3", NULL, bwaiter, NULL, 1);
  thread_fork("cvtest4", NULL, bwaiter, NULL, 1);
  while(global1 < 5) {
    global2 = global2;
  }
  lock_acquire(global_lk);
  KASSERT(global1 == 5);
  global1 = 6;
  cv_broadcast(global_cv, global_lk);
  KASSERT(global1 == 6);
  lock_release(global_lk);
  clocksleep(1);
  KASSERT(global1 == 10);
}

/*
  Helper for the cv broadcast test method.
*/
int bwaiter(void *data, unsigned long num) {
  global2 = num;
  (void)data;
  lock_acquire(global_lk);
  global1++;
  while(global1 < 6) {
    cv_wait(global_cv, global_lk);
  }
  lock_release(global_lk);
  global1++;
  return 0;
}

/*
  Tests that a cv is properly set to employ a certain lock.
*/
void testcvlock() {
  kprintf("Testing cv lock is set properly\n");
  global1 = 1;
  thread_fork("cvtest", NULL, waiter, NULL, 1);
  while(global1 == 1) {
    global2 = global2;
  }
  lock_acquire(global_lk);
  KASSERT(global_cv->cv_lock == global_lk);
  global1 = 3;
  cv_signal(global_cv, global_lk);
  KASSERT(global_cv->cv_lock == global_lk);
  lock_release(global_lk);
  clocksleep(1);
  KASSERT(global_cv->cv_lock == global_lk);
}

/*
  Tests that destroying a previously-used cv still works properly.
*/
void testcvdestroy() {
  kprintf("Testing cv destroy\n");
  cv_destroy(global_cv);
}

int frontThread(void *data, unsigned long num){
	(void) data;
	(void) num;
	if(safelist_front(global_list) == NULL){
		return 0;
	}
	return -1;
}  

int appendAndPollThread(void *data, unsigned long num){
	(void) data;
	unsigned long val = num;
	safelist_push_back(global_list, &val);
	void* res = safelist_next(global_list);
	unsigned int* pop = res;
	KASSERT(*pop == val);
	return *pop;
}

int pollThread(void *data, unsigned long num){
	(void) data;
	(void) num;
	void* res = safelist_next(global_list);
	int* pop = res;
	return *pop;
}

int appendThread(void *data, unsigned long num){
	(void) data;
	unsigned long val = num;
	safelist_push_back(global_list, &val);
	return 0;
}

void testlist(){
	kprintf("Testing thread-safe list.\n");
	struct thread *front;
	thread_fork_joinable("front_thread", NULL, frontThread, NULL, 1, &front);
	KASSERT(thread_join(front) == 0);
	kprintf("Testing fast return of head of list passed.\n");

	struct thread *first;
	struct thread *next;
	struct thread *third;
	thread_fork_joinable("append_thread", NULL, appendAndPollThread, NULL, 1, &first);
	thread_fork_joinable("append_thread", NULL, appendAndPollThread, NULL, 1, &next);
	thread_fork_joinable("append_thread", NULL, appendAndPollThread, NULL, 1, &third);

	KASSERT(thread_join(first) == 1);
	KASSERT(thread_join(next) == 1);
	KASSERT(thread_join(third) == 1);
	
	thread_fork_joinable("append_thread", NULL, appendAndPollThread, NULL, 2, &first);
	KASSERT(thread_join(first) == 2);

	KASSERT(safelist_getsize(global_list) == 0);
	KASSERT(safelist_isempty(global_list) == true);

	kprintf("Testing push_back and pop_front returns correct number of elements\n");

	thread_fork_joinable("poll_thread", NULL, pollThread, NULL, 1, &first);
	clocksleep(1);
	thread_fork("append_thread", NULL, appendThread, NULL, 3);
	KASSERT(thread_join(first) == 3);

	kprintf("Testing pop_front waited for element to be appended.\n");
}
  
  

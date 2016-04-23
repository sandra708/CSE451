#include <list.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <safelist.h>

struct safelist {
  struct list* list;
  struct lock* lock;

  struct list* writers;
  
  int AW;
  int AR;
  int WW;
  int WR;

  int nextWriter;
  int numWriters;

  struct cv* okToRead;
  struct cv* okToWrite;
  struct cv* dataAvailable;
};

static void readSetup(struct safelist* lst) {
  lock_acquire(lst->lock);
  while ((lst->AW + lst->WW) > 0) {
    lst->WR++;
    cv_wait(lst->okToRead, lst->lock);
    lst->WR--;
  }
  lst->AR++;
  lock_release(lst->lock);
}

static void readCleanup(struct safelist* lst) {
  lock_acquire(lst->lock);
  lst->AR--;
  if (lst->AR == 0 && lst->WW > 0) {
    cv_signal(lst->okToWrite, lst->lock);
  }
  lock_release(lst->lock);
}

static int writeSetup(struct safelist* lst) {
  lock_acquire(lst->lock);
  int pos = lst->numWriters++;
  struct cv *var = cv_create((char*) &pos);
  if (var == NULL) {
    return ENOMEM;
  }
  list_push_back(lst->writers, (void *) var);
  while ((lst->AW + lst->AR) > 0 && pos > lst->nextWriter) {
    lst->WW++;
    cv_wait(var, lst->lock);
    lst->WW--;
  }
  lst->AW++;
  kfree(var);
  lock_release(lst->lock);
  return 0;
}

static void writeCleanup(struct safelist* lst) {
  lock_acquire(lst->lock);
  lst->AW--;
  lst->nextWriter++;
  if (lst->WW > 0) {
    struct cv *nextvar = (struct cv*) list_front(lst->writers);
    cv_signal(nextvar, lst->lock);
  } else if (lst->WR > 0) {
    cv_broadcast(lst->okToRead, lst->lock);
  }
  lock_release(lst->lock);
}

struct safelist* safelist_create(void) {

  struct safelist *newlist = (struct safelist*) kmalloc(sizeof(struct safelist));
  newlist->lock = lock_create("Safelist");

  if (newlist == NULL) {
    return NULL;
  }

  newlist->list = list_create();
  if (newlist->list == NULL) {
    kfree(newlist);
    return NULL;
  }
  newlist->writers = list_create();
  if (newlist->writers == NULL) {
    kfree(newlist->list);
    kfree(newlist);
    return NULL;
  }
  newlist->okToRead = cv_create("read");
  if (newlist->okToRead == NULL) {
    kfree(newlist->writers);
    kfree(newlist->list);
    kfree(newlist);
    return NULL;
  }
  newlist->okToWrite = cv_create("write");
  if (newlist->okToWrite == NULL) {
    kfree(newlist->okToRead);
    kfree(newlist->writers);
    kfree(newlist->list);
    kfree(newlist);
    return NULL;
  }
  newlist->dataAvailable = cv_create("data");
  if (newlist->dataAvailable == NULL) {
    kfree(newlist->okToWrite);
    kfree(newlist->okToRead);
    kfree(newlist->writers);
    kfree(newlist->list);
    kfree(newlist);
    return NULL;
  }
  newlist->AW = 0;
  newlist->AR = 0;
  newlist->WW = 0;
  newlist->WR = 0;

  newlist->numWriters = 0;
  newlist->nextWriter = 0;

  return newlist;
}


int safelist_push_back(struct safelist* lst, void* newval) {
  if(writeSetup(lst) != 0) {
    return ENOMEM;
  }
  int result = list_push_back(lst->list, newval);
  writeCleanup(lst);
  lock_acquire(lst->lock);
  cv_broadcast(lst->dataAvailable, lst->lock);
  lock_release(lst->lock);
  return result;
}

void safelist_pop_front(struct safelist* lst) {
  if(writeSetup(lst) != 0) {
    return;
  }
  list_pop_front(lst->list);
  writeCleanup(lst);
}

void* safelist_front(struct safelist* lst) {
  readSetup(lst);
  void* result = list_front(lst->list);
  readCleanup(lst);
  return result;
}

void* safelist_next(struct safelist* lst) {
  void* result = safelist_front(lst);
  while (safelist_front(lst) == NULL) {
    lock_acquire(lst->lock);
    cv_wait(lst->dataAvailable, lst->lock);
    result = list_front(lst->list);
    lock_release(lst->lock);
  }
  return result;
}
    
void* safelist_find(struct safelist* lst, void* query_val, int(*comparator)(void* left, void* right)) {
  readSetup(lst);
  void* result = list_find(lst->list, query_val, comparator);
  readCleanup(lst);
  return result;
}

void* safelist_remove(struct safelist* lst, void* query_val, int(*comparator)(void* left, void* right)) {
  if (writeSetup(lst) != 0) {
    return NULL;
  }
  void* result = list_remove(lst->list, query_val, comparator);
  writeCleanup(lst);
  return result;
}

int safelist_isempty(struct safelist* lst) {
  readSetup(lst);
  int result = list_isempty(lst->list);
  readCleanup(lst);
  return result;
}

unsigned int safelist_getsize(struct safelist* lst) {
  readSetup(lst);
  unsigned int result = list_getsize(lst->list);
  readCleanup(lst);
  return result;
}

void safelist_destroy(struct safelist* lst) {
  writeSetup(lst);
  cv_destroy(lst->okToRead);
  cv_destroy(lst->okToWrite);
  cv_destroy(lst->dataAvailable);
  list_destroy(lst->list);
  list_destroy(lst->writers);
  lock_destroy(lst->lock);
  kfree(lst);
}

void safelist_assertvalid(struct safelist* lst) {
  readSetup(lst);
  list_assertvalid(lst->list);
  readCleanup(lst);
}

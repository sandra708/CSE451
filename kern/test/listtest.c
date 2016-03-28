#include <list.h>
#include <types.h>
#include <lib.h>
#include <test.h>

#define TESTSIZE 133

static
int
int_comparator(void* left, void* right)
{
    int l = *(int*)left;
    int r = *(int*)right;
    return l-r;
}

int
listtest(int nargs, char **args)
{
    (void)nargs;
    (void)args;

    kprintf("Beginning list test...\n");

    struct list* newlist;
    newlist = list_create();
    KASSERT(newlist != NULL);
    KASSERT(list_getsize(newlist) == 0);
    KASSERT(list_isempty(newlist));
    list_assertvalid(newlist);

    int i, found, removed;
    int* elem;
    /* push back TESTSIZE number of elements */
    for (i = 0; i < TESTSIZE; ++i) {
        elem = (int*)kmalloc(sizeof(int));
        KASSERT(elem != NULL);
        *elem = i;
        /* check for ENOMEM */
        KASSERT(list_push_back(newlist, (void*) elem) == 0);
        /* find the element added */
        found = *(int*)list_find(newlist, (void*) elem, &int_comparator);
        KASSERT(found == i);
    }
    KASSERT(list_getsize(newlist) == TESTSIZE);
    KASSERT(!list_isempty(newlist));
    list_assertvalid(newlist);

    /* remove TESTSIZE number of elements */
    for (i = 0; i < TESTSIZE; ++i) {
        elem = (int*)kmalloc(sizeof(int));
        KASSERT(elem != NULL);
        *elem = i;
        removed = *(int*)list_remove(newlist, (void*) elem, &int_comparator);
        KASSERT(removed == i);
    }
    KASSERT(list_getsize(newlist) == 0);
    KASSERT(list_isempty(newlist));
    list_assertvalid(newlist);

    /* push back TESTSIZE number of elements */
    for (i = 0; i < TESTSIZE; ++i) {
        elem = (int*)kmalloc(sizeof(int));
        KASSERT(elem != NULL);
        *elem = i;
        /* check for ENOMEM */
        KASSERT(list_push_back(newlist, (void*) elem) == 0);
    }
    KASSERT(list_getsize(newlist) == TESTSIZE);
    KASSERT(!list_isempty(newlist));
    list_assertvalid(newlist);

    /* pop front TESTSIZE number of elements */
    for (i = 0; i < TESTSIZE; ++i) {
        elem = (int*)list_front(newlist);
        KASSERT(*elem == i);
        list_pop_front(newlist);
        kfree(elem);
    }
    KASSERT(list_getsize(newlist) == 0);
    KASSERT(list_isempty(newlist));
    list_assertvalid(newlist);

    /* REPEAT to test if the list is reusable */

    /* push back TESTSIZE number of elements */
    for (i = 0; i < TESTSIZE; ++i) {
        elem = (int*)kmalloc(sizeof(int));
        KASSERT(elem != NULL);
        *elem = i;
        /* check for ENOMEM */
        KASSERT(list_push_back(newlist, (void*) elem) == 0);
        /* find the element added */
        found = *(int*)list_find(newlist, (void*) elem, &int_comparator);
        KASSERT(found == i);
    }
    KASSERT(list_getsize(newlist) == TESTSIZE);
    KASSERT(!list_isempty(newlist));
    list_assertvalid(newlist);

    /* remove TESTSIZE number of elements */
    for (i = 0; i < TESTSIZE; ++i) {
        elem = (int*)kmalloc(sizeof(int));
        KASSERT(elem != NULL);
        *elem = i;
        removed = *(int*)list_remove(newlist, (void*) elem, &int_comparator);
        KASSERT(removed == i);
    }
    KASSERT(list_getsize(newlist) == 0);
    KASSERT(list_isempty(newlist));
    list_assertvalid(newlist);

    /* push back TESTSIZE number of elements */
    for (i = 0; i < TESTSIZE; ++i) {
        elem = (int*)kmalloc(sizeof(int));
        KASSERT(elem != NULL);
        *elem = i;
        /* check for ENOMEM */
        KASSERT(list_push_back(newlist, (void*) elem) == 0);
    }
    KASSERT(list_getsize(newlist) == TESTSIZE);
    KASSERT(!list_isempty(newlist));
    list_assertvalid(newlist);

    /* pop front TESTSIZE number of elements */
    for (i = 0; i < TESTSIZE; ++i) {
        elem = (int*)list_front(newlist);
        KASSERT(*elem == i);
        list_pop_front(newlist);
        kfree(elem);
    }
    KASSERT(list_getsize(newlist) == 0);
    KASSERT(list_isempty(newlist));
    list_assertvalid(newlist);

    /* test for bug -- incorrect behavior when removing from end of list */
    for (i = 0; i < TESTSIZE; ++i) {
        elem = (int*)kmalloc(sizeof(int));
        KASSERT(elem != NULL);
        *elem = i;
        /* check for ENOMEM */
        KASSERT(list_push_back(newlist, (void*) elem) == 0);
    }
    KASSERT(list_getsize(newlist) == TESTSIZE);
    KASSERT(!list_isempty(newlist));
    i = TESTSIZE - 1;
    list_remove(newlist, &i, &int_comparator);
    list_assertvalid(newlist);

    /* destroys the list */
    list_destroy(newlist);

    kprintf("List test complete\n");

    return 0;
}

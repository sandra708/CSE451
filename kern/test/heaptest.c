#include <heap.h>
#include <types.h>
#include <lib.h>
#include <test.h>

#define TESTSIZE 133

/* less comparator for int */
static int int_lessthan(const void* left, const void* right) {
    int l = *(int*)left;
    int r = *(int*)right;
    return (l < r);
}

int
heaptest(int nargs, char **args)
{
    (void)nargs;
    (void)args;

    kprintf("Beginning heap test...\n");

    struct heap* newheap;
    newheap = heap_create(&int_lessthan);
    KASSERT(newheap != NULL);
    KASSERT(heap_getsize(newheap) == 0);
    KASSERT(heap_isempty(newheap));

    int i;
    int* elem;
    /* push TESTSIZE number of elements */
    for (i = TESTSIZE - 1; i >= 0; --i) {
        elem = (int*)kmalloc(sizeof(int));
        KASSERT(elem != NULL);
        *elem = i;
        /* check for ENOMEM */
        KASSERT(heap_push(newheap, (void*)elem) == 0);
    }
    KASSERT(heap_getsize(newheap) == TESTSIZE);
    KASSERT(!heap_isempty(newheap));

    /* pop TESTSIZE number of elements; expect numbers in increasing order */
    for (i = 0; i < TESTSIZE; ++i) {
        KASSERT(*(int*)heap_top(newheap) == i);
        elem = (int*)heap_pop(newheap);
        KASSERT(*elem == i);
        /* REMEMBER to kfree elements we allocated in the beginning */
        kfree(elem);
    }
    KASSERT(heap_getsize(newheap) == 0);
    KASSERT(heap_isempty(newheap));

    /* REPEAT to test if the heap is reusable */

    /* push TESTSIZE number of elements */
    for (i = TESTSIZE - 1; i >= 0; --i) {
        elem = (int*)kmalloc(sizeof(int));
        KASSERT(elem != NULL);
        *elem = i;
        /* check for ENOMEM */
        KASSERT(heap_push(newheap, (void*)elem) == 0);
    }
    KASSERT(heap_getsize(newheap) == TESTSIZE);
    KASSERT(!heap_isempty(newheap));

    /* pop TESTSIZE number of elements; expect numbers in increasing order */
    for (i = 0; i < TESTSIZE; ++i) {
        KASSERT(*(int*)heap_top(newheap) == i);
        elem = (int*)heap_pop(newheap);
        KASSERT(*elem == i);
        /* REMEMBER to kfree elements we allocated in the beginning */
        kfree(elem);
    }
    KASSERT(heap_getsize(newheap) == 0);
    KASSERT(heap_isempty(newheap));

    heap_destroy(newheap);

    kprintf("heap test complete\n");

    return 0;
}

#include <heap.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <array.h>

#define HEAPTYPE 3333

/* a lightweight canary to prevent use after free errors */
#define KASSERT_HEAP(h) KASSERT(h->datatype == HEAPTYPE)

#define PARENT(i) ((i) - 1) / 2
#define LEFT(i) 2 * (i) + 1
#define RIGHT(i) 2 * (i) + 2

struct heap {
    int datatype;
    int(*comparator)(const void*, const void*);
    struct array* vals;
};

void bubble_up(struct heap* h, int index);
void trickle_down(struct heap* h, int index);

struct heap*
heap_create(int(*comparator)(const void*, const void*))
{
    struct heap* h = (struct heap*)kmalloc(sizeof(struct heap));
    if (h == NULL) {
        return NULL;
    }
    h->comparator = comparator;
    h->vals = array_create();
    if (h->vals == NULL) {
        kfree(h);
        return NULL;
    }
    h->datatype = HEAPTYPE;
    return h;
}

int
heap_push(struct heap* h, void* newval)
{
    KASSERT(h != NULL);
    KASSERT_HEAP(h);
    unsigned newindex;
    int err = array_add(h->vals, newval, &newindex);
    if (err == ENOMEM) {
        array_destroy(h->vals);
        /* TODO: check for error */
        kfree(h);
        return ENOMEM;
    }
    /* heapify up from the last element until root */
    KASSERT(newindex == h->vals->num - 1);
    bubble_up(h, h->vals->num - 1);
    return 0;
}

void*
heap_pop(struct heap* h)
{
    KASSERT(h != NULL);
    KASSERT_HEAP(h);
    if (h->vals->num == 0) {
        return NULL;
    }
    void* top = array_get(h->vals, 0);
    void* last = array_get(h->vals, h->vals->num - 1);
    array_set(h->vals, 0, last);
    --h->vals->num;
    trickle_down(h, 0);
    /* TODO: array does not yet support shrinking */
    return top;
}

const void*
heap_top(struct heap* h)
{
    KASSERT(h != NULL);
    KASSERT_HEAP(h);
    return array_get(h->vals, 0);
}

int
heap_isempty(struct heap* h)
{
    KASSERT(h != NULL);
    KASSERT_HEAP(h);
    return (array_num(h->vals) == 0);
}

unsigned int
heap_getsize(struct heap* h)
{
    KASSERT(h != NULL);
    KASSERT_HEAP(h);
    return array_num(h->vals);
}

void
heap_destroy(struct heap* h)
{
    if (h != NULL) {
        KASSERT_HEAP(h);
        array_destroy(h->vals);
    }
    kfree(h);
}

void
bubble_up(struct heap* h, int index)
{
    KASSERT(h != NULL);
    KASSERT(index >= 0);
    KASSERT_HEAP(h);
    struct array* vals = h->vals;
    int p = PARENT(index);
    void* t;
    while (index > 0 &&
        h->comparator(array_get(vals, index), array_get(vals, p))) {
        /* SWAP */
        t = array_get(vals, index);
        array_set(vals, index, array_get(vals, p));
        array_set(vals, p, t);
        index = p;
        p = PARENT(index);
    }
}

void
trickle_down(struct heap* h, int index)
{
    KASSERT(h != NULL);
    KASSERT(index >= 0);
    KASSERT_HEAP(h);
    struct array* vals = h->vals;
    int j;
    unsigned int r, l;
    void* t;
    while (index >= 0) {
        j = -1;
        r = RIGHT(index);
        if (r < h->vals->num &&
            h->comparator(array_get(vals, r), array_get(vals, index))) {
            l = LEFT(index);
            if (h->comparator(array_get(vals, l), array_get(vals, r))) {
                j = l;
            } else {
                j = r;
            }
        } else {
            l = LEFT(index);
            if (l < h->vals->num &&
                h->comparator(array_get(vals, l), array_get(vals, index))) {
                j = l;
            }
        }
        if (j >= 0) {
            /* SWAP */
            t = array_get(vals, j);
            array_set(vals, j, array_get(vals, index));
            array_set(vals, index, t);
        }
        index = j;
    }
}

static
void assert_heap_property(struct array* vals, unsigned int index, unsigned int max,
                          int(*comparator)(const void* l, const void* r))
{
    if (index < max) {
        KASSERT(comparator(array_get(vals, index), array_get(vals, LEFT(index))));
        KASSERT(comparator(array_get(vals, index), array_get(vals, RIGHT(index))));
        assert_heap_property(vals, LEFT(index), max, comparator);
        assert_heap_property(vals, RIGHT(index), max, comparator);
    }
}

void
heap_assertvalid(struct heap* h)
{
    KASSERT(h != NULL);
    KASSERT_HEAP(h);
    assert_heap_property(h->vals, 0, array_num(h->vals), h->comparator);
}

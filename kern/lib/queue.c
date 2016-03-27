#include <queue.h>
#include <list.h>
#include <types.h>
#include <lib.h>

#define QUEUETYPE 2222

/* a lightweight canary to prevent use after free errors */
#define KASSERT_QUEUE(q) KASSERT(q->datatype == QUEUETYPE)

struct queue {
    int datatype;
    struct list* vals;
};

struct queue*
queue_create(void)
{
    struct queue* q = (struct queue*)kmalloc(sizeof(struct queue));
    if (q == NULL) {
        return NULL;
    }
    q->vals = list_create();
    if (q->vals == NULL) {
        kfree(q);
        return NULL;
    }
    q->datatype = QUEUETYPE;
    return q;
}

int
queue_push(struct queue* q, void* newval)
{
    KASSERT(q != NULL);
    KASSERT_QUEUE(q);
    return list_push_back(q->vals, newval);
}

void
queue_pop(struct queue* q)
{
    KASSERT(q != NULL);
    KASSERT_QUEUE(q);
    list_pop_front(q->vals);
}

void*
queue_front(struct queue* q)
{
    KASSERT(q != NULL);
    KASSERT_QUEUE(q);
    return list_front(q->vals);
}

int
queue_isempty(struct queue* q)
{
    KASSERT(q != NULL);
    KASSERT_QUEUE(q);
    return list_isempty(q->vals);
}

unsigned int
queue_getsize(struct queue* q)
{
    KASSERT(q != NULL);
    KASSERT_QUEUE(q);
    return list_getsize(q->vals);
}

void
queue_destroy(struct queue* q)
{
    if (q != NULL) {
        KASSERT_QUEUE(q);
        list_destroy(q->vals);
    }
    kfree(q);
}

void
queue_assertvalid(struct queue* q)
{
    KASSERT(q != NULL);
    KASSERT_QUEUE(q);
    list_assertvalid(q->vals);
}

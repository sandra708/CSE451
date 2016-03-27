#ifndef _QUEUE_H_
#define _QUEUE_H_

/*
 * FIFO queue of void pointers.
 *
 * Functions:
 *      queue_create        - Allocates and returns a new queue object.
 *                            Returns NULL on error.
 *      queue_push          - Append a new element to the back of the queue.
 *      queue_pop           - Removes the next element from the front of the queue
 *      queue_front         - Returns the next element from the front of the queue.
 *                            Does not remove the element from the queue.
 *                            Returns NULL if the queue is empty.
 *      queue_isempty       - Returns whether the queue is empty or not.
 *      queue_getsize       - Returns the number of elements in the queue.
 *      queue_destroy       - Destroys the queue: frees internal data structures,
 *                            frees the queue object.
 *                            DOES NOT free any elements contained in the queue.
 *                            To avoid memory leak, please free all elements
 *                            added to the queue.
 *      queue_assertvalid   - Validates the integrity of the queue data structure
 *                            e.g. whether the queue contains the stated number
 *                            of elements.
 */

struct queue; /* Opaque */

struct queue* queue_create(void);
int queue_push(struct queue* q, void* newval);
void queue_pop(struct queue* q);
void* queue_front(struct queue* q);
int queue_isempty(struct queue* q);
unsigned int queue_getsize(struct queue* q);
void queue_destroy(struct queue* q);
void queue_assertvalid(struct queue* q);

#endif /* _QUEUE_H_ */

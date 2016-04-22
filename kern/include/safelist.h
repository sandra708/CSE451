#ifndef _SAFELIST_H_
#define _SAFELIST_H_

/*
 * Singly-linked thread-safe list of void pointers.
 *
 * Functions:
 *      safelist_create     - Allocates and returns a new list object.
 *                          Returns NULL on error.
 *      safelist_push_back  - Append a new element to the back of the list.
 *      safelist_pop_front  - Removes the next element from the front of the list
 *      safelist_front      - Returns the next element from the front of the list.
 *                          Does not remove the element from the list.
 *                          Returns NULL if the list is empty.
 *      safelist_next       - Returns the next element from the front of the list.
 *                          Does not remove the element from the list.
 *                          Waits for an element to be added if the list is empty.
 *      safelist_find       - Returns the element equal to the given query element,
 *                          Two elements are equal if the given comparator
 *                          evaluates to zero.
 *                          Does not remove the element from the list.
 *                          Returns NULL if the element is not in the list.
 *      safelist_remove     - Removes the element equal to the given query element,
 *                          Two elements are equal if the given comparator
 *                          evaluates to zero.
 *                          Returns the element if it is found and removed from 
 *                          the list.
 *                          Returns NULL if the element is not in the list.
 *      safelist_isempty    - Returns whether the list is empty or not.
 *      safelist_getsize    - Returns the number of elements in the list.
 *      safelist_destroy    - Destroys the list: frees internal data structures.
 *                          frees the list object.
 *                          DOES NOT free any elements contained in the list.
 *                          To avoid memory leak, please free all elements
 *                          added to the list.
 *      safelist_assertvalid- Validates the integrity of the list data structure
 *                          e.g. whether the list contains the stated number
 *                          of elements, whether the tail is reachable from 
 *                          the head.
 */

struct safelist;  /* Opaque. */

struct safelist* safelist_create(void);
int safelist_push_back(struct safelist* lst, void* newval);
void safelist_pop_front(struct safelist* lst);
void* safelist_front(struct safelist* lst);
void* safelist_next(struct safelist* lst);
void* safelist_find(struct safelist* lst, void* query_val, int(*comparator)(void* left, void* right));
void* safelist_remove(struct safelist* lst, void* query_val, int(*comparator)(void* left, void* right));
int safelist_isempty(struct safelist* lst);
unsigned int safelist_getsize(struct safelist* lst);
void safelist_destroy(struct safelist* lst);
void safelist_assertvalid(struct safelist* lst);

#endif /* _SAFELIST_H_ */

#ifndef _COLLECTIONS_H_
#define _COLLECTIONS_H_

#include "list.h"
#include "queue.h"
#include "heap.h"
#include "hashtable.h"

/*
 * Overview of Memory Management for Collections
 *
 * All collection datatypes (list, queue, heap and hashtable) follow the same
 * pattern for memory management:
 *
 * 1. "_create", e.g. list_create(), hashtable_create() allocates memory for
 *    the collection.
 *
 * 2. "_add" or "_push", e.g. list_push_back(), hashtable_add() allocates memory
 *    for internal data structure to hold the new item.
 * 
 * 3. "_remove" or "_pop", e.g. list_pop_front(), hashtable_remove() deallocates
 *    memory for internal data structure that holds the item.
 *
 * 4. "_destroy" e.g. list_destroy(), hashtable_destroy() deallocates all
 *    internal data structure.
 * 
 * NOTE: User of the interface is responsible for allocating and deallocating
 *       memory for the data items in the collections as necessary.
 *       For example, if a user wants to have a list/queue of (struct task*)
 *       type, while list/queue allocates/deallocates internal data structure
 *       to hold (struct task*) item, user is responsible for allocating
 *       memory for (struct task*) item itself before "_add" and deallocating
 *       the item after "_remove".
 *       
 *       An important implication for using "_destroy" is that user should
 *       remove all items in the collection before calling "_destroy" if only
 *       the collection object has pointers to those data items. "_destroy"
 *       does not and cannot deallocate the data items, and thus will only
 *       deallocate internal data structures.
 *       
 */


#endif /* _COLLECTIONS_H_ */

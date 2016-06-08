#include <hashtable.h>
#include <list.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>

#define HASHTABLETYPE 4444
#define KVTYPE 4445

/* a lightweight canary to prevent use after free errors */
#define KASSERT_HASHTABLE(h) KASSERT(h->datatype == HASHTABLETYPE)
#define KASSERT_KV(h) KASSERT(h->datatype == KVTYPE)

/* LOAD FACTOR = 1 / LF_MULTIPLE */
#define LF_MULTIPLE 2
/* Minimum size of the underlying array */
#define MIN_SIZE 8

struct hashtable {
    int datatype;
    /* num of items in hashtable */
    unsigned int size;
    /* num of slots in the underlying array */
    unsigned int arraysize;
    /* minimum size of the underlying array */
    unsigned int init_size;
    /* chaining with linked list */
    struct list** vals;
};

struct kv_pair {
    int datatype;
    /* key is a C string */
    char* key;
    unsigned int keylen;
    /* value can be of any type */
    void* val;
};

static
struct kv_pair* kv_create(char* key, unsigned int keylen, void* val)
{
    struct kv_pair* new_item = (struct kv_pair*)kmalloc(sizeof(struct kv_pair));
    if (new_item == NULL) {
        return NULL;
    }
    new_item->datatype = KVTYPE;
    new_item->key = key;
    new_item->keylen = keylen;
    new_item->val = val;
    return new_item;
}

/* 
 * djb2 hash function
 * http://www.cse.yorku.ca/~oz/hash.html
 */
static
unsigned long
hash(char* key, unsigned int keylen)
{
    unsigned long hashval = 5381;
    unsigned int i;
    int c;
    for (i = 0; i < keylen; ++i) {
        c = key[i];
        /* hashval * 33 + c */
        hashval = ((hashval << 5) + hashval) + c;
    }
    return hashval;
}

/*
 * Fill the array with empty lists from start to end index.
 */
static
int
init_array_with_lists(struct list** vals, unsigned int start, unsigned int end)
{
    unsigned int i, j;
    int flag = 0;
    for (i = start; i < end; ++i) {
        vals[i] = list_create();
        if (vals[i] == NULL) {
            /* If out of memory, remember to kfree lists. */
            j = i;
            flag = 1;
            break;
        }
    }
    if (flag) {
        /* kfree all lists in the array */
        for (i = 0; i < j; ++i) {
            list_destroy(vals[i]);
        }
        return ENOMEM;
    }
    return 0;
}

/*
 * Cleanup the array with empty lists from start to end index.
 */
static
void
cleanup_array_with_lists(struct list** vals, unsigned int start, unsigned int end)
{
    unsigned int i;
    for (i = start; i < end; ++i) {
        KASSERT(list_isempty(vals[i]));
        list_destroy(vals[i]);
    }
}

struct hashtable*
hashtable_create(void)
{
    struct hashtable* h = (struct hashtable*)kmalloc(sizeof(struct hashtable));
    if (h == NULL) {
        return NULL;
    }
    h->size = 0;
    h->arraysize = MIN_SIZE;
    h->vals = (struct list**)kmalloc(MIN_SIZE * sizeof(struct list*));
    if (h->vals == NULL) {
        kfree(h);
        return NULL;
    }
    /* Allocate lists for the array. */
    int err = init_array_with_lists(h->vals, 0, h->arraysize);
    if (err == ENOMEM) {
        kfree(h->vals);
        kfree(h);
        return NULL;
    }
    h->datatype = HASHTABLETYPE;
    return h;
}

/* 
 * Typically, called after grow(...) or shrink(...).
 * Relocates all hashtable items from source array to the new array
 * in the hashtable.
 */
static
void
rehash(struct hashtable* h,
       struct list** source_array, unsigned int source_size)
{
    KASSERT(h != NULL);
    KASSERT_HASHTABLE(h);
    unsigned int i;
    struct list* chain;
    struct kv_pair* item;
    for (i = 0; i < source_size; ++i) {
        chain = source_array[i];
        KASSERT(chain != NULL);
        if (list_getsize(chain) > 0) {
            item = (struct kv_pair*)list_front(chain);
            while (item) {
                KASSERT_KV(item);
                hashtable_add(h, item->key, item->keylen, item->val);
                list_pop_front(chain);
                kfree(item);
                item = (struct kv_pair*)list_front(chain);
            }
            KASSERT(list_getsize(chain) == 0);
        }
    }
}

/*
 * Double the size of the array in hashtable.
 * Relocate the hashtable elements from old array to the expanded array
 * by calling rehash(...).
 */
static
int
grow(struct hashtable* h)
{
    KASSERT(h != NULL);
    KASSERT_HASHTABLE(h);
    struct list** old_array = h->vals;
    unsigned int old_arraysize = h->arraysize;

    /* allocate a new array twice the size. */
    struct list** new_array
        = (struct list**)kmalloc(old_arraysize * 2 * sizeof(struct list*));
    if (new_array == NULL) {
        return ENOMEM;         
    }
    /* allocate lists for the new array. */
    int err = init_array_with_lists(new_array, 0, old_arraysize * 2);
    if (err == ENOMEM) {
        return ENOMEM;
    }

    /* replace old array with new array in hash table. */
    h->vals = new_array;
    h->size = 0;
    h->arraysize = old_arraysize * 2;

    /* relocate all items from old array to new array */
    rehash(h, old_array, old_arraysize);
    
    /* cleanup list objects in old array and kfree old array. */
    cleanup_array_with_lists(old_array, 0, old_arraysize);
    kfree(old_array);

    return 0;
}

static
int
key_comparator(void* left, void* right)
{
    struct kv_pair* l = (struct kv_pair*)left;
    struct kv_pair* r = (struct kv_pair*)right;
    KASSERT_KV(l);
    KASSERT_KV(r);
    if (l->keylen == r->keylen) {
        return strcmp(l->key, r->key);
    }
    return (l->keylen - r->keylen);
}

int
hashtable_add(struct hashtable* h, char* key, unsigned int keylen, void* val)
{
    KASSERT(h != NULL);
    KASSERT_HASHTABLE(h);
    /* If LOAD_FACTOR exceeded, double the size of array. */
    if ((h->size + 1) * LF_MULTIPLE > h->arraysize) {
        grow(h);
    }
    /* Compute the hash to index into array. */
    int index = hash(key, keylen) % h->arraysize;
    struct list* chain = h->vals[index];
    KASSERT(chain != NULL);

    struct kv_pair query_item;
    query_item.datatype = KVTYPE;
    query_item.key = key;
    query_item.keylen = keylen;

    /* Remove any existing value with the same key. */
    struct kv_pair* removed
        = (struct kv_pair*)list_remove(chain, &query_item, &key_comparator);

    /* Append the new item to end of chain. */
    struct kv_pair* new_item = kv_create(key, keylen, val);
    if (new_item == NULL) {
        return ENOMEM;
    }
    int err = list_push_back(chain, new_item);
    if (err == ENOMEM) {
        return ENOMEM;
    }

    if (!removed) {
        ++h->size;
    }
    return 0;
}

void*
hashtable_find(struct hashtable* h, char* key, unsigned int keylen)
{
    KASSERT(h != NULL);
    KASSERT_HASHTABLE(h);

    /* Compute the hash to index into array. */
    int index = hash(key, keylen) % h->arraysize;
    struct list* chain = h->vals[index];
    KASSERT(chain != NULL);
    
    /* Build a kv_pair object with the query key. */
    struct kv_pair query_item;
    query_item.datatype = KVTYPE;
    query_item.key = key;
    query_item.keylen = keylen;

    struct kv_pair* found
        = (struct kv_pair*)list_find(chain, &query_item, &key_comparator);
    if (found == NULL) {
        return NULL;
    }
    KASSERT_KV(found);
    return found->val;
}

/*
 * Half the size of the array in hashtable.
 * Relocate the hashtable elements from old array to the shrunk array
 * by calling rehash(...).
 */
static
int
shrink(struct hashtable* h)
{
    KASSERT(h != NULL);
    KASSERT_HASHTABLE(h);
    struct list** old_array = h->vals;
    unsigned int old_arraysize = h->arraysize;
    unsigned int new_arraysize = old_arraysize / 2;

    /* Allocate a new array half the size. */
    struct list** new_array
        = (struct list**)kmalloc(new_arraysize * sizeof(struct list*));
    if (new_array == NULL) {
        return ENOMEM;         
    }
    /* Allocate lists for the new array. */
    int err = init_array_with_lists(new_array, 0, new_arraysize);
    if (err == ENOMEM) {
        return ENOMEM;
    }

    /* replace old array with new array in hash table. */
    h->vals = new_array;
    h->size = 0;
    h->arraysize = new_arraysize;

    /* relocate all items from old array to new array */
    rehash(h, old_array, old_arraysize);
    
    /* cleanup list objects in old array and kfree old array. */
    cleanup_array_with_lists(old_array, 0, old_arraysize);
    kfree(old_array);

    return 0;
}

void*
hashtable_remove(struct hashtable* h, char* key, unsigned int keylen)
{
    KASSERT(h != NULL);
    KASSERT_HASHTABLE(h);
    /* Compute the hash to index into array. */
    int index = hash(key, keylen) % h->arraysize;
    struct list* chain = h->vals[index];
    KASSERT(chain != NULL);
    /* Build a kv_pair object with the query key. */
    struct kv_pair query_item;
    query_item.datatype = KVTYPE;
    query_item.key = key;
    query_item.keylen = keylen;
    struct kv_pair* removed
        = (struct kv_pair*)list_remove(chain, &query_item, &key_comparator);
    if (removed == NULL) {
        /* Key does not exist. */
        return NULL;
    }
    KASSERT_KV(removed);
    /* Key value pair removed. */
    --h->size;
    if (h->arraysize > MIN_SIZE &&
        (h->size - 1) * LF_MULTIPLE < h->arraysize) {
        /* Half the size of array. */
        shrink(h);
    }
    void* res = removed->val;
    kfree(removed);
    return res;
}

int
hashtable_isempty(struct hashtable* h)
{
    KASSERT(h != NULL);
    KASSERT_HASHTABLE(h);
    return (h->size == 0);
}

unsigned int
hashtable_getsize(struct hashtable* h)
{
    KASSERT(h != NULL);
    KASSERT_HASHTABLE(h);
    return h->size;
}

void
hashtable_destroy(struct hashtable* h)
{
    if (h != NULL) {
        KASSERT_HASHTABLE(h);
        cleanup_array_with_lists(h->vals, 0, h->arraysize);
        kfree(h->vals);
    }
    kfree(h);
}

void
hashtable_assertvalid(struct hashtable* h)
{
    KASSERT(h != NULL);
    KASSERT_HASHTABLE(h);
    /* Validate if the size of items in the hashtable is correct. */
    unsigned int count = 0;
    unsigned int i, j, size;
    struct list* chain;
    struct kv_pair* kv;
    for (i = 0; i < h->arraysize; ++i) {
        chain = h->vals[i];
        KASSERT(chain != NULL);
        list_assertvalid(chain);
        size = list_getsize(chain);
        count += size;
        /* check if key hashes to the correct index */
        for (j = 0; j < size; ++j) {
            kv = (struct kv_pair*)list_front(chain);
            KASSERT_KV(kv);
            KASSERT(hash(kv->key, kv->keylen) % h->arraysize == i);
            list_pop_front(chain);
            list_push_back(chain, kv);
        }
    }
    KASSERT(count == h->size);
}

char* int_to_byte_string(int input)
{
  char* digits = kmalloc(21);
  if (digits == NULL)
  {
    return NULL;
  }
  int digitcount = 0;
  while(input > 0)
  {
    digits[digitcount] = input % 256;
    input /= 256;
    digitcount++;
  }
  digits[digitcount] = 0;
  return digits;
}


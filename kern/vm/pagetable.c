#include <types.h>
#include <lib.h>
#include <hashtable.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <spinlock.h>
#include <synch.h>
#include <coremap.h>
#include <vm.h>
#include <pagetable.h>

struct pagetable* pagetable_create(void)
{
  struct pagetable* table = kmalloc(sizeof(struct pagetable));
  if (table == NULL)
  {
    return NULL;
  }
  table->pagetable_lock = lock_create("pagetablelock");
  if (table->pagetable_lock == NULL)
  {
    kfree(table);
    return NULL;
  }
  table->maintable = hashtable_create();
  if (table->maintable == NULL)
  {
    lock_destroy(table->pagetable_lock);
    kfree(table);
    return NULL;
  }
  return table;
}

void pagetable_swap_in(struct pagetable_entry *entry, vaddr_t vaddr, int pid)
{
  paddr_t paddr = coremap_swap_page(entry->swap, (userptr_t) vaddr, pid);
  entry->addr = paddr >> 12;
  entry->flags |= PAGETABLE_INMEM;
  entry->flags &= ~(PAGETABLE_DIRTY);
  coremap_lock_release(entry->addr << 12);
}

paddr_t pagetable_pull(struct pagetable* table, vaddr_t addr, uint8_t flags)
{
  struct proc* cur = curproc;
	paddr_t newpage = coremap_allocate_page(false, cur->pid, 1, (userptr_t) addr);
  pagetable_add(table, addr, newpage, flags);
  coremap_lock_release(newpage);
  lock_release(table->pagetable_lock);
  return newpage;
}

struct pagetable_entry *pagetable_lookup(struct pagetable* table, vaddr_t addr)
{
  lock_acquire(table->pagetable_lock);
  int frame = addr >> 12;
  int mainindex = frame >> 10;
  char* mainkey = int_to_byte_string(mainindex);
  struct hashtable* subtable = (struct hashtable*) 
          hashtable_find(table->maintable, mainkey, strlen(mainkey));
  if (subtable == NULL)
  {
    lock_release(table->pagetable_lock);
    return NULL;
  }
  int subindex = frame & 1023;
  char* subkey = int_to_byte_string(subindex);
  struct pagetable_entry* entry = (struct pagetable_entry*) 
        hashtable_find(subtable, subkey, strlen(subkey));
  if (entry == NULL)
  {
    lock_release(table->pagetable_lock);
    return NULL;
  }
  // clear invalid entry
  if(!entry->flags | PAGETABLE_VALID){
    kfree(entry);
    entry = NULL;
    size_t subsize = hashtable_getsize(subtable);
    if (subsize == 0)
    {
      hashtable_destroy(subtable);
    }
  }

  lock_release(table->pagetable_lock);
  return entry;
}

bool pagetable_add(struct pagetable* table, vaddr_t vaddr, paddr_t paddr, uint8_t flags)
{
  lock_acquire(table->pagetable_lock);
  vaddr_t frame = vaddr >> 12;
  int mainindex = frame >> 10;
  char* mainkey = int_to_byte_string(mainindex);
  struct hashtable* subtable = (struct hashtable*) 
          hashtable_find(table->maintable, mainkey, strlen(mainkey));
  if (subtable == NULL)
  {
    subtable = hashtable_create();
    hashtable_add(table->maintable, mainkey, strlen(mainkey), subtable);
  }
  int subindex = frame & 1023;
  char* subkey = int_to_byte_string(subindex);
  struct pagetable_entry* entry = (struct pagetable_entry*) 
        hashtable_find(subtable, subkey, strlen(subkey));
  if (entry == NULL)
  {
    entry = kmalloc(sizeof(struct pagetable_entry));
    entry->addr = paddr >> 12;
    int err = swap_allocate(&entry->swap);
    if(err){
	// TODO: out of swap space
    }
    entry->flags = flags | PAGETABLE_VALID | PAGETABLE_INMEM;
    lock_release(table->pagetable_lock);
    return false;
  }
  entry->addr = paddr >> 12;
  int err = swap_allocate(&entry->swap);
  if(err){
	// TODO: out of swap space
  }
  entry->flags = flags | PAGETABLE_VALID | PAGETABLE_INMEM;
  lock_release(table->pagetable_lock);
  return true;
}

bool pagetable_remove(struct pagetable* table, vaddr_t vaddr)
{
  lock_acquire(table->pagetable_lock);
  int frame = vaddr >> 12;
  int mainindex = frame >> 10;
  char* mainkey = int_to_byte_string(mainindex);
  struct hashtable* subtable = (struct hashtable*) 
        hashtable_find(table->maintable, mainkey, strlen(mainkey));
  if (subtable == NULL)
  {
    lock_release(table->pagetable_lock);
    return false;
  }
  int subindex = frame & 1023;
  char* subkey = int_to_byte_string(subindex);
  struct pagetable_entry* entry = (struct pagetable_entry*) 
        hashtable_find(subtable, subkey, strlen(subkey));
  if (entry == NULL)
  {
    lock_release(table->pagetable_lock);
    return false;
  }

  // remove page from coremap and disk
  spinlock_acquire(&entry->lock);
  if(entry->flags | PAGETABLE_INMEM){
    bool inmem = coremap_lock_acquire(entry->addr << 12);
    if(inmem){
      spinlock_release(&entry->lock);
      coremap_free_page(entry->addr << 12);
      swap_free(entry->swap);
      struct pagetable_entry* entry = (struct pagetable_entry*) 
          hashtable_find(subtable, subkey, strlen(subkey));
      kfree(entry);
    }else{
      // the page is in the process of being swapped out by another process (can't free directly)
      entry->flags |= PAGETABLE_REQUEST_FREE;
      spinlock_release(&entry->lock);
    }
  } else {
    spinlock_release(&entry->lock);
    swap_free(entry->swap);

    struct pagetable_entry* entry = (struct pagetable_entry*) 
        hashtable_find(subtable, subkey, strlen(subkey));
    kfree(entry);
  }

  size_t subsize = hashtable_getsize(subtable);
  if (subsize == 0)
  {
    hashtable_destroy(subtable);
  }
  lock_release(table->pagetable_lock);
  return true;
}

bool pagetable_copy(struct pagetable *old, int oldpid, struct pagetable *copy, int copypid)
{
  (void) oldpid;

  lock_acquire(copy->pagetable_lock);
  lock_acquire(old->pagetable_lock);

  for(int i = 0; i < 1024; i++)
  {
    if(hashtable_getsize(old->maintable) == 0)
    {
      break;
    }
    char* index = int_to_byte_string(i);
    struct hashtable* subtable = (struct hashtable*) 
        hashtable_find(old->maintable, index, strlen(index));

    if(subtable == NULL)
      continue;

    struct hashtable *copy_subtable = hashtable_create();
    if(copy_subtable == NULL){
      lock_release(copy->pagetable_lock);
      lock_release(old->pagetable_lock);
      return false;
    }

    char *copy_index = int_to_byte_string(i);
    hashtable_add(copy->maintable, copy_index, strlen(copy_index), copy_subtable);

    for(int j = 0; j < 1024; j++)
    {
      if(hashtable_getsize(subtable) == 0)
      {
        break;
      }
      char* subindex = int_to_byte_string(j);
      struct pagetable_entry* entry = (struct pagetable_entry*)
            hashtable_find(subtable, subindex, strlen(subindex));

      if(entry->flags & PAGETABLE_VALID)
      {
        struct pagetable_entry *copy_entry = kmalloc(sizeof(struct pagetable_entry));
        vaddr_t vaddr = (i << 22) | (j << 12);

        //TODO: do this without doing I/O across a spinlock
        spinlock_acquire(&entry->lock); 
        if(entry->flags & PAGETABLE_DIRTY)
        {
          // force a write to disk
          swap_page_out((void*) PADDR_TO_KVADDR(entry->addr << 12), entry->swap);
	  entry->flags &= ~(PAGETABLE_DIRTY);
        }
        spinlock_release(&entry->lock);

        copy_entry->addr = coremap_allocate_page(false, copypid, 1, (userptr_t) vaddr) >> 12;
        swap_page_in((void*) PADDR_TO_KVADDR(copy_entry->addr << 12), entry->swap);

        swap_allocate(&copy_entry->swap);
        swap_page_out((void*) PADDR_TO_KVADDR(copy_entry->addr << 12), copy_entry->swap);
        copy_entry->flags = PAGETABLE_VALID | PAGETABLE_INMEM;
	      if(entry->flags | PAGETABLE_READABLE)
        {
          copy_entry->flags |= PAGETABLE_READABLE;
        }
        if(entry->flags | PAGETABLE_WRITEABLE)
        {
          copy_entry->flags |= PAGETABLE_WRITEABLE;
        }
        if(entry->flags | PAGETABLE_EXECUTABLE)
        {
          copy_entry->flags |= PAGETABLE_EXECUTABLE;
        }
        spinlock_init(&copy_entry->lock);

        char *copy_subindex = int_to_byte_string(j);
        hashtable_add(copy_subtable, copy_subindex, strlen(copy_subindex), copy_entry);
        coremap_lock_release(copy_entry->addr << 12);
      }
    }
  }

  lock_release(old->pagetable_lock);
  lock_release(copy->pagetable_lock);
  return true;
}

int pagetable_free_all(struct pagetable* table)
{
  int ref = 0;
  lock_acquire(table->pagetable_lock);
  for(int i = 0; i < 1024; i++)
  {
    if(hashtable_getsize(table->maintable) == 0)
    {
      break;
    }
    char* index = int_to_byte_string(i);
    struct hashtable* subtable = (struct hashtable*) 
        hashtable_find(table->maintable, index, strlen(index));
    for(int j = 0; j < 1024; j++)
    {
      if(hashtable_getsize(subtable) == 0)
      {
        break;
      }
      char* subindex = int_to_byte_string(j);
      struct pagetable_entry* entry = (struct pagetable_entry*)
            hashtable_find(subtable, subindex, strlen(subindex));

      if(!entry->flags & PAGETABLE_VALID)
      {
	hashtable_remove(subtable, subindex, strlen(subindex));
	kfree(entry);
        continue;
      }

      if(entry->flags & PAGETABLE_INMEM)
      {
        if(coremap_lock_acquire(entry->addr << 12))
        {
          coremap_free_page(entry->addr << 12);
          swap_free(entry->swap);
        } else {
          ref++;
        }
      } else {
	swap_free(entry->swap);
      }
    }
  }
  lock_release(table->pagetable_lock);
  return ref;
}

int pagetable_destroy(struct pagetable* table)
{
  lock_acquire(table->pagetable_lock);
  for(int i = 0; i < 1024; i++)
  {
    if(hashtable_getsize(table->maintable) == 0)
    {
      break;
    }
    char* index = int_to_byte_string(i);
    struct hashtable* subtable = (struct hashtable*) 
        hashtable_remove(table->maintable, index, strlen(index));
    for(int j = 0; j < 1024; j++)
    {
      if(hashtable_getsize(subtable) == 0)
      {
        break;
      }
      char* subindex = int_to_byte_string(j);
      struct pagetable_entry* entry = (struct pagetable_entry*)
            hashtable_remove(subtable, subindex, strlen(subindex));

      kfree(entry);
    }
    hashtable_destroy(subtable);
  }
  hashtable_destroy(table->maintable);
  lock_release(table->pagetable_lock);
  lock_destroy(table->pagetable_lock);
  kfree(table);
  table = NULL;
  return 0;
}


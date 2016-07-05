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

  table->valids = bitmap_create(1024);
  if (table->valids == NULL)
  {
    lock_destroy(table->pagetable_lock);
    kfree(table);
    return NULL;
  }

  table->ptr = kmalloc(sizeof(struct pagetable_ptr));
  if(table->ptr == NULL)
  {
    bitmap_destroy(table->valids);
    lock_destroy(table->pagetable_lock);
    kfree(table);
    return 0;
  }

  return table;
}

static
struct pagetable_subtable* pagetable_create_subtable(void)
{
  struct pagetable_subtable *subtable = kmalloc(sizeof(struct pagetable_subtable));
  if(subtable == NULL)
  {
    return NULL;
  }

  subtable->valids = bitmap_create(1024);
  if(subtable->valids == NULL)
  {
    kfree(subtable);
    return NULL;
  }

  subtable->ptr = kmalloc(sizeof(struct pagetable_subptr));
  if(subtable->ptr == NULL)
  {
    bitmap_destroy(subtable->valids);
    kfree(subtable);
    return NULL;
  }

  return subtable;
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
  return newpage;
}

struct pagetable_entry *pagetable_lookup(struct pagetable* table, vaddr_t addr)
{

  // we CAN'T kmalloc here, as we are potentially inside of a kmalloc call

  int frame = addr >> 12;
  int mainindex = frame >> 10;

  int subindex = frame & 1023;

  // we can get away without sleeplocking, because this particular vaddr-path won't change while we're looking up
  // either it is valid or it isn't, but it can only be invalidated while it is not in memory and by the given process

  if (!bitmap_isset(table->valids, mainindex))
  {
    return NULL;
  }

  struct pagetable_subtable *subtable = table->ptr->entries[mainindex];
  if(!bitmap_isset(subtable->valids, subindex))
  {
    return NULL;
  }

  struct pagetable_entry* entry = subtable->ptr->entries[subindex];
  if (entry == NULL)
  {
    return NULL;
  }

  // clear invalid entry
  if(!entry->flags & PAGETABLE_VALID){
    kfree(entry);
    bitmap_unmark(subtable->valids, subindex);
    entry = NULL;
  }

  return entry;
}

bool pagetable_add(struct pagetable* table, vaddr_t vaddr, paddr_t paddr, uint8_t flags)
{
  // anything that could call kmalloc can't hold the pagetable lock, ever

  vaddr_t frame = vaddr >> 12;
  int mainindex = frame >> 10;

  int subindex = frame & 1023;

  lock_acquire(table->pagetable_lock);
  struct pagetable_subtable *subtable = table->ptr->entries[mainindex];
  if (!bitmap_isset(table->valids, mainindex))
  {
    // release lock to use kmalloc
    lock_release(table->pagetable_lock);
    subtable = pagetable_create_subtable();
    lock_acquire(table->pagetable_lock);
    if(subtable == NULL)
    {
       //ENOMEM
       return false;
    }
    if(!bitmap_isset(table->valids, mainindex))
    {
      bitmap_mark(table->valids, mainindex);
      table->ptr->entries[mainindex] = subtable;
    }
    else 
    { // the subtable was allocated while we had released the lock
      kfree(subtable->valids);
      kfree(subtable->ptr);
      kfree(subtable);
    }
  } 

  if (!bitmap_isset(subtable->valids, subindex))
  {
    // since kmalloc can allocate pages, we might deadlock
    lock_release(table->pagetable_lock);
    struct pagetable_entry *entry = kmalloc(sizeof(struct pagetable_entry));
    entry->addr = paddr >> 12;
    int err = swap_allocate(&entry->swap);
    if(err){
	// TODO: out of swap space
        return false;
    }
    entry->flags = flags | PAGETABLE_VALID | PAGETABLE_INMEM;
    spinlock_init(&entry->lock);

    lock_acquire(table->pagetable_lock);
    bitmap_mark(subtable->valids, subindex);
    subtable->ptr->entries[subindex] = entry;
    lock_release(table->pagetable_lock);

    return true;
  }

  struct pagetable_entry *entry = subtable->ptr->entries[subindex];
  entry->addr = paddr >> 12;
  int err = swap_allocate(&entry->swap);
  if(err){
	// TODO: out of swap space
	return false;
  }
  entry->flags = flags | PAGETABLE_VALID | PAGETABLE_INMEM;
  lock_release(table->pagetable_lock);

  return true;
}

bool pagetable_remove(struct pagetable* table, vaddr_t vaddr)
{
  int frame = vaddr >> 12;
  int mainindex = frame >> 10;

  int subindex = frame & 1023;

  lock_acquire(table->pagetable_lock);
  if (!bitmap_isset(table->valids, mainindex))
  {
    lock_release(table->pagetable_lock);
    return false;
  }

  struct pagetable_subtable* subtable = table->ptr->entries[mainindex];
  if (!bitmap_isset(subtable->valids, subindex))
  {
    lock_release(table->pagetable_lock);
    return false;
  }

  struct pagetable_entry *entry = subtable->ptr->entries[subindex];

  // remove page from coremap and disk
  spinlock_acquire(&entry->lock);
  lock_release(table->pagetable_lock); // cannot touch coremap while holding ptbl lock

  if(entry->flags | PAGETABLE_INMEM){
    bool inmem = coremap_lock_acquire(entry->addr << 12);
    if(inmem){
      spinlock_release(&entry->lock);
      coremap_free_page(entry->addr << 12);
      swap_free(entry->swap);
      bitmap_unmark(subtable->valids, subindex);
      kfree(entry);
    }else{
      // the page is in the process of being swapped out by another process (can't free directly)
      entry->flags |= PAGETABLE_REQUEST_FREE;
      spinlock_release(&entry->lock);
    }
  } else {
    spinlock_release(&entry->lock);
    swap_free(entry->swap);
    bitmap_unmark(subtable->valids, subindex);
    kfree(entry);
  }

  return true;
}

bool pagetable_copy(struct pagetable *old, int oldpid, struct pagetable *copy, int copypid)
{
  (void) oldpid;

  lock_acquire(copy->pagetable_lock);
  lock_acquire(old->pagetable_lock);

  for(int i = 0; i < 1024; i++)
  {
    // we have to be inside the lock - so no kmalloc

    if(!bitmap_isset(old->valids, i))
    {
      continue;
    }

    struct pagetable_subtable *subtable = old->ptr->entries[i];

    lock_release(copy->pagetable_lock);
    lock_release(old->pagetable_lock);
    struct pagetable_subtable *copy_subtable = pagetable_create_subtable();
    if(copy_subtable == NULL){
      return false;
    }
    lock_acquire(old->pagetable_lock);
    lock_acquire(copy->pagetable_lock);

    bitmap_mark(copy->valids, i);
    copy->ptr->entries[i] = copy_subtable;

    for(int j = 0; j < 1024; j++)
    {
      struct pagetable_entry* entry = subtable->ptr->entries[j];

      if(bitmap_isset(subtable->valids, j) && (entry->flags & PAGETABLE_VALID))
      {
        lock_release(copy->pagetable_lock);
        lock_release(old->pagetable_lock);
        struct pagetable_entry *copy_entry = kmalloc(sizeof(struct pagetable_entry));
        vaddr_t vaddr = (i << 22) | (j << 12);

	// protect against race condition with the coremap bringing in a new pag
	// TODO: figure out how to do this best WITHOUT holding spinlocks
	spinlock_acquire(&entry->lock);
        if(entry->flags & PAGETABLE_DIRTY)
        {
          spinlock_release(&entry->lock);
          // force a write to disk
          swap_page_out((void*) PADDR_TO_KVADDR(entry->addr << 12), entry->swap);
        }
        else
        {
          spinlock_release(&entry->lock);
        }
        
        // copy by bringing the old page's disk into the copy's memory segment
        copy_entry->addr = coremap_allocate_page(false, copypid, 1, (userptr_t) vaddr) >> 12;
        swap_page_in((void*) PADDR_TO_KVADDR(copy_entry->addr << 12), entry->swap);

	// push the copy out to it's disk segment
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

        lock_acquire(copy->pagetable_lock);
        lock_acquire(old->pagetable_lock);
        bitmap_mark(copy_subtable->valids, j);
        copy_subtable->ptr->entries[j] = copy_entry;
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
    if(!bitmap_isset(table->valids, i))
    {
      continue;
    }

    struct pagetable_subtable* subtable = table->ptr->entries[i];
    for(int j = 0; j < 1024; j++)
    {
      if(!bitmap_isset(subtable->valids, j))
      {
        continue;
      }
      struct pagetable_entry* entry = subtable->ptr->entries[j];

      if(!entry->flags & PAGETABLE_VALID)
      {
        continue;
      }

      spinlock_acquire(&entry->lock);
      if(entry->flags & PAGETABLE_INMEM)
      {
        if(coremap_lock_acquire(entry->addr << 12))
        {
          spinlock_release(&entry->lock);
          coremap_free_page(entry->addr << 12);
          swap_free(entry->swap);
          entry->flags &= ~(PAGETABLE_VALID);
        } else {
          entry->flags |= PAGETABLE_REQUEST_FREE | PAGETABLE_REQUEST_DESTROY;
          ref++;
          spinlock_release(&entry->lock);
        }
      } else {
        spinlock_release(&entry->lock);
	swap_free(entry->swap);
        entry->flags &= ~(PAGETABLE_VALID);
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
    if(!bitmap_isset(table->valids, i))
    {
       continue;
    }
    struct pagetable_subtable* subtable = table->ptr->entries[i];
    for(int j = 0; j < 1024; j++)
    {
      if(!bitmap_isset(subtable->valids, j))
      {
        continue;
      }
      struct pagetable_entry* entry = subtable->ptr->entries[j];

      kfree(entry);
    }
    kfree(subtable->ptr);
    kfree(subtable->valids);
    kfree(subtable);
  }
  kfree(table->ptr);
  lock_release(table->pagetable_lock);
  lock_destroy(table->pagetable_lock);
  kfree(table);
  table = NULL;
  return 0;
}

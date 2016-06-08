#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <synch.h>
#include <coremap.h>
#include <vm.h>
#include <hashtable.h>
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

paddr_t pagetable_pull(struct pagetable* table, vaddr_t addr)
{
	//TODO
	(void) table;
	(void) addr;
  /*paddr_t newpage = coremap_swap_page();
  pagetable_add(table, addr, newpage);
  return newpage;*/
  return 0;
}

struct pagetable_entry pagetable_lookup(struct pagetable* table, vaddr_t addr)
{
  vaddr_t offset = addr & 4095;
  int frame = addr >> 12;
  int mainindex = frame >> 10;
  char* mainkey = int_to_byte_string(mainindex);
  struct hashtable* subtable = (struct hashtable*) 
          hashtable_find(table->maintable, mainkey, strlen(mainkey));
  if (subtable == NULL)
  {
	// ??
    return pagetable_pull(table, addr);
  }
  int subindex = frame & 1023;
  char* subkey = int_to_byte_string(subindex);
  struct pagetable_entry* entry = (struct pagetable_entry*) 
        hashtable_find(subtable, subkey, strlen(subkey));

	return entry;
	/*
  if (entry == NULL)
  {
    return pagetable_pull(table, addr);
  }
  return (entry->addr << 12) + offset; */
}

bool pagetable_add(struct pagetable* table, vaddr_t vaddr, paddr_t paddr)
{
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
    entry->addr = paddr >> 12; // why shift?
    return false;
  }
  entry->addr = paddr >> 12;
  return true;
}

bool pagetable_remove(struct pagetable* table, vaddr_t vaddr)
{
  int frame = vaddr >> 12;
  int mainindex = frame >> 10;
  char* mainkey = int_to_byte_string(mainindex);
  struct hashtable* subtable = (struct hashtable*) 
        hashtable_find(table->maintable, mainkey, strlen(mainkey));
  if (subtable == NULL)
  {
    return false;
  }
  int subindex = frame & 1023;
  char* subkey = int_to_byte_string(subindex);
  struct pagetable_entry* entry = (struct pagetable_entry*) 
        hashtable_remove(subtable, subkey, strlen(subkey));
  if (entry == NULL)
  {
    return false;
  }
  kfree(entry);
  size_t subsize = hashtable_getsize(subtable);
  if (subsize == 0)
  {
    hashtable_destroy(subtable);
  }
  return true;
}

int pagetable_destroy(struct pagetable* table)
{
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
  kfree(table);
  table = NULL;
  return 0;
}


/* Functions relating to the core-map used to track page frames */


#include <kern/limits.h>
#include <bitmap.h>
#include <synch.h>

struct coremap_entry *coremap;
struct bitmap *coremap_free;
struct bitmap *coremap_swappable;

struct lock *coremap_lock;

#define COREMAP_INUSE 4
#define COREMAP_EVICTABLE 2
#define COREMAP_DIRTY 1

struct coremap_entry{
	uint8_t flags;
	uint16_t pid; // limits.h restricts the PID to this size; if that changes, this must too
};

void
coremap_bootstrap(void);

paddr_t
coremap_allocate_page(bool iskern);

paddr_t
coremap_swap_page(/*param needed */ void);

void 
coremap_free_page(paddr_t paddr);

void 
coremap_mark_page_dirty(paddr_t paddr);

void 
coremap_mark_page_clean(paddr_t paddr);

/* An architecture-specific translator between a paddr and the index into that frame of the coremap */
int
coremap_translate(paddr_t paddr);

paddr_t 
coremap_untranslate(int idx);

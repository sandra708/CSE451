#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <coremap.h>

void
vm_bootstrap(){
	coremap_bootstrap();
	//TODO
}

void
coremap_bootstrap(){
	//TODO
}

paddr_t
coremap_untranslate(int idx){
	//TODO
	(void) idx;
	return 0;
}

int
coremap_translate(paddr_t paddr){
	(void) paddr;
	return 0;
}

/*

Unit tests for file syscalls. 

*/

#include <types.h>
#include <current.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <clock.h>
#include <thread.h>
#include <proc.h>
#include <syscall.h>
#include <../arch/mips/include/trapframe.h>
#include <test.h>

int test_file_syscalls(int nargs, char** args){
	(void) nargs;
	(void) args;
  int error;
  int fd = sys_open("test.txt", O_RDWR | O_CREAT, &error);
  int fd2 = sys_open("test.txt", O_RDWR | O_CREAT, &error);
  int fd3 = sys_open("foo.txt", O_RDWR | O_CREAT, &error);
  sys_close(fd);
  sys_close(fd2);
  sys_close(fd3);
  return 0;
}

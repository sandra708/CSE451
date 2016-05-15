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

int test_file_syscalls(int nargs, char** args);
void test_open_and_close(void);
void test_repeated_open_and_close(void);
void test_one_write_one_read(void);
void test_offset_read(void);
void test_offset_write(void);
void test_faulty_read(void);
void test_faulty_write(void);
void test_write_closed_file(void);
void test_open_nonexistent_file(void);

int test_file_syscalls(int nargs, char** args){
	(void) nargs;
	(void) args;
  test_open_and_close();
  kprintf("---Passed basic open and close...\n");
  test_repeated_open_and_close();
  kprintf("---Passed repeated open and close...\n");
  test_one_write_one_read();
  kprintf("---Passed basic write and read...\n");
  test_offset_read();
  kprintf("---Passed offset read...\n");
  test_offset_write();
  kprintf("---Passed offset write...\n");
  test_faulty_read();
  kprintf("---Passed faulty read...\n");
  test_faulty_write();
  kprintf("---Passed faulty write...\n");
  test_write_closed_file();
  kprintf("---Correctly refused to write closed file...\n");
  test_open_nonexistent_file();
  kprintf("---Correctly failed to open/close a nonexistent file...\n");
  kprintf("Tests passed!\n");
  return 0;
}

void test_open_and_close()
{
  int error;
  int fd = sys_open("test.txt", O_RDONLY, &error);
  KASSERT(fd > 2);
  KASSERT(sys_close(fd, &error) == 0);
}

void test_repeated_open_and_close()
{
  for(int i = 0; i < 100; i++)
  {
    test_open_and_close();
  }
}

void test_one_write_one_read()
{
  int error;
  const char* testtext = "Testing testing 1 2 3";
  char readresult[30];
  int fd = sys_open("test.txt", O_RDWR | O_CREAT, &error);
  int fd2 = sys_open("test.txt", O_RDWR | O_CREAT, &error);
  KASSERT(fd > 2);
  KASSERT(fd2 > 2);
  KASSERT(fd != fd2);
  sys_write(fd2, testtext, 21, &error);
  ssize_t sizeread = sys_read(fd, readresult, 25, &error);
  for(ssize_t i = 0; i < sizeread; i++)
  {
    KASSERT(readresult[i] == testtext[i]);
  }
  KASSERT(sys_close(fd, &error) == 0);
  KASSERT(sys_close(fd2, &error) == 0);
}

void test_offset_read()
{
  int error;
  const char* testtext = "Testing testing 1 2 3";
  char readresult[30];
  int fd = sys_open("test.txt", O_RDWR | O_CREAT, &error);
  int fd2 = sys_open("test.txt", O_RDWR | O_CREAT, &error);
  KASSERT(fd > 2);
  KASSERT(fd2 > 2);
  KASSERT(fd != fd2);
  sys_write(fd2, testtext, 21, &error);
  ssize_t sizeread = sys_read(fd, readresult, 16, &error);
  char readresult2[30];
  sizeread = sys_read(fd, readresult2, 12, &error);
  for(ssize_t i = 0; i < sizeread; i++)
  {
    KASSERT(readresult2[i] == testtext[i+16]);
  }
  KASSERT(sys_close(fd, &error) == 0);
  KASSERT(sys_close(fd2, &error) == 0);
}

void test_offset_write()
{
  int error;
  const char* testtext = "Testing ";
  const char* doubletext = "Testing Testing ";
  char readresult[30];
  int fd = sys_open("test2.txt", O_RDWR | O_CREAT, &error);
  int fd2 = sys_open("test2.txt", O_RDWR | O_CREAT, &error);
  KASSERT(fd > 2);
  KASSERT(fd2 > 2);
  KASSERT(fd != fd2);
  sys_write(fd2, testtext, 8, &error);
  sys_write(fd2, testtext, 8, &error);
  ssize_t sizeread = sys_read(fd, readresult, 25, &error);
  for(ssize_t i = 0; i < sizeread; i++)
  {
    KASSERT(readresult[i] == doubletext[i]);
  }
  KASSERT(sys_close(fd, &error) == 0);
  KASSERT(sys_close(fd2, &error) == 0);
}

void test_faulty_read()
{
  int error;
  char readresult[30];
  int fd = sys_open("test.txt", O_WRONLY, &error);
  KASSERT(fd > 2);
  KASSERT(sys_read(fd, readresult, 25, &error) == -1);
  KASSERT(sys_close(fd, &error) == 0);
}

void test_faulty_write()
{
  int error;
  const char* testtext = "Testing testing 1 2 3";
  int fd = sys_open("test.txt", O_RDONLY, &error);
  KASSERT(fd > 2);
  KASSERT(sys_write(fd, testtext, 21, &error) == -1);
  KASSERT(sys_close(fd, &error) == 0);
}

void test_write_closed_file()
{
  int error;
  const char* testtext = "Testing testing 1 2 3";
  int fd = sys_open("test.txt", O_WRONLY, &error);
  KASSERT(fd > 2);
  KASSERT(sys_close(fd, &error) == 0);
  KASSERT(sys_write(fd, testtext, 21, &error) == -1);
}

void test_open_nonexistent_file()
{
  int error;
  int fd = sys_open("bar.txt", O_WRONLY, &error);
  KASSERT(fd < 0);
  KASSERT(error == ENOENT);
  KASSERT(sys_close(fd, &error) == -1);
  KASSERT(error == EBADF);
}
  

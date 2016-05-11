#include <types.h>
#include <current.h>
#include <limits.h>
#include <thread.h>
#include <proc.h>
#include <syscall.h>
#include <kern/errno.h>

int test_execv(int nargs, char** args);
void test_arg_string_too_big(void);
void test_non_valid_directories(void);
void test_program_does_not_exist(void);
void test_pass_directory_in(void);
void test_non_executable_program(void);
void test_null_parameters(void);


int test_execv(int nargs, char** args) {
        (void) nargs;
        (void) args;
	test_null_parameters();
        test_arg_string_too_big();
        test_non_valid_directories();
        test_program_does_not_exist();
        test_pass_directory_in();
        test_non_executable_program();
        return 0;
}

void test_arg_string_too_big() {
	kprintf("Passing in too large of arg string into execv\n");
	KASSERT(false);
}

void test_non_valid_directories() {
	kprintf("Testing passing invalid directories/fake directories into execv\n");

	int retval;
	const char *empty_directory = "";
	char *args[2];
	args[0] = (char*)empty_directory;
	retval = sys_execv(empty_directory, args);
	KASSERT(retval == ENOTDIR);

	const char *invalid_directory = "/this_better_not_be_a_real_directory_name_ever/or_else"; 
	args[0] = (char*)invalid_directory;
	retval = sys_execv(invalid_directory, args);
	KASSERT(retval == ENOTDIR);
}

void test_program_does_not_exist() {
	kprintf("Passing a non-existent program into execv\n");
	int retval;

	const char *fake_program = "/this_better_not_be_a_program_name";
	char *args[2];
	args[0] = (char*)fake_program;
	retval = sys_execv(fake_program, args);
	KASSERT(retval == ENOENT);
}

void test_pass_directory_in() {
	kprintf("Passing a directory into execv\n");
	int retval;

	const char *directory = "";
	char *args[2];
	args[0] = (char*)directory;
	retval = sys_execv(directory, args);
	KASSERT(retval == EISDIR);
}

void test_non_executable_program() {
	kprintf("Passing a non-executable program into execv\n");
	KASSERT(false);
}

void test_null_parameters() {
	kprintf("Passing null into execv\n");
	int retval;

	retval = sys_execv(NULL, NULL);
	KASSERT(retval == EFAULT);

	const char* directory = "/testbin/"; // some valid directory
	char* args[2];
	args[0] = (char*) directory;
	
	retval = sys_execv(directory, NULL);
	KASSERT(retval == EFAULT);
	
	retval = sys_execv(NULL, args);
	KASSERT(retval == EFAULT);
}



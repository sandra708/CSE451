#include <types.h>
#include <current.h>
#include <limits.h>
#include <thread.h>
#include <proc.h>
#include <syscall.h>

int test_execv(int nargs, char** args);
void test_arg_string_too_big(void);
void test_not_valid_directory(void);
void test_program_does_not_exist(void);
void test_pass_directory_in(void);
void test_non_executable_program(void);
void test_bad_argument_pointer(void);


int test_execv(int nargs, char** args) {
        (void) nargs;
        (void) args;
        test_arg_string_too_big();
        test_not_valid_directory();
        test_program_does_not_exist();
        test_pass_directory_in();
        test_non_executable_program();
        test_bad_argument_pointer();
        return 0;
}

void test_arg_string_too_big() {
	char** blah = NULL;
	const char* foo = NULL;
        KASSERT(sys_execv(foo, blah));
}

void test_not_valid_directory() {

}

void test_program_does_not_exist() {

}
void test_pass_directory_in() {

}

void test_non_executable_program() {

}

void test_bad_argument_pointer() {

}


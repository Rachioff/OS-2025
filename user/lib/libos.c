#include <env.h>
#include <lib.h>
#include <mmu.h>

void exit(int status) { // Add status parameter
#if !defined(LAB) || LAB >= 5
    close_all();
#endif
    syscall_exit(status); // Use new syscall
}

const volatile struct Env *env;
extern int main(int, char **);

void libmain(int argc, char **argv) {
	// set env to point at our env structure in envs[].
	env = &envs[ENVX(syscall_getenvid())];

	// call user main routine
	main(argc, argv);

	// exit gracefully
	exit(1);
}

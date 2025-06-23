#include <env.h>
#include <lib.h>
#include <mmu.h>

void exit(int status) {
#if !defined(LAB) || LAB >= 5
	close_all();
#endif
	syscall_exit(status);
}

const volatile struct Env *env;
extern int main(int, char **);

void libmain(int argc, char **argv) {
	// set env to point at our env structure in envs[].
	env = &envs[ENVX(syscall_getenvid())];

	// call user main routine
	int ret = main(argc, argv);

	// exit gracefully
	exit(ret);
}

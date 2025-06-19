#include <env.h>
#include <lib.h>

int wait(u_int envid) {
	const volatile struct Env *e;

	e = &envs[ENVX(envid)];
	while (e->env_id == envid && e->env_status != ENV_FREE) {
		syscall_yield();
	}
	if (e->env_id == envid && e->env_status == ENV_FREE) {
		return e->env_exit_status; // Return the saved exit status
	}
	return -E_BAD_ENV;
}
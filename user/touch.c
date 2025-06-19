#include <lib.h>

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: touch <file>\n");
		return 1;
	}
	int fd;
	if ((fd = open(argv[1], O_CREAT | O_RDWR)) < 0) {
		printf("touch: cannot touch '%s': %e\n", argv[1], fd);
		return 1;
	}
	close(fd);
	return 0;
}
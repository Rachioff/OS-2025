#include <lib.h>

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: touch <file>\n");
		return 1;
	}
	int fd = open(argv[1], O_CREAT | O_RDWR);
	if (fd < 0) {
		printf("touch: cannot touch '%s': No such file or directory\n", argv[1]);
		return 1;
	}
	close(fd);
	return 0;
}
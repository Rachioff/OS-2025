#include <lib.h>

void mkdir_p(const char *path) {
	char p_path[MAXPATHLEN];
	strcpy(p_path, path);
	
	char *p = p_path;
	if (*p == '/') p++;

	for (; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			int r = open(p_path, O_MKDIR | O_CREAT);
			if (r >= 0) close(r);
			*p = '/';
		}
	}
	int r = open(p_path, O_MKDIR | O_CREAT);
	if (r >= 0) close(r);
}

int main(int argc, char **argv) {
	int p_flag = 0;
	int i = 1;

	if (argc < 2) {
		printf("Usage: mkdir [-p] <directory...>\n");
		return 1;
	}

	if (strcmp(argv[1], "-p") == 0) {
		p_flag = 1;
		i = 2;
		if (argc < 3) {
			printf("Usage: mkdir [-p] <directory...>\n");
			return 1;
		}
	}

	for (; i < argc; i++) {
		struct Stat st;
		// Check if it already exists
		if (stat(argv[i], &st) == 0) {
			if (!p_flag) { // Only show error if -p is not used
				printf("mkdir: cannot create directory '%s': File exists\n", argv[i]);
				return 1;
			}
			continue; // With -p, if it exists, do nothing and succeed.
		}

		if (p_flag) {
			mkdir_p(argv[i]);
		} else {
			int r = open(argv[i], O_MKDIR | O_CREAT);
			if (r < 0) {
				printf("mkdir: cannot create directory '%s': No such file or directory\n", argv[i]);
				return 1;
			}
			close(r);
		}
	}
	return 0;
}
#include <lib.h>

void mkdir_p(const char *path) {
    char p_path[MAXPATHLEN];
    strcpy(p_path, path);

    for (char *p = p_path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            // Try to create parent directory, ignore if it exists
            int r = open(p_path, O_MKDIR | O_CREAT);
            if (r >= 0) close(r);
            *p = '/';
        }
    }
    // Create the final directory
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
        if (p_flag) {
            mkdir_p(argv[i]);
        } else {
            int r = open(argv[i], O_MKDIR | O_CREAT);
            if (r < 0) {
                if (r == -E_FILE_EXISTS) {
                    printf("mkdir: cannot create directory '%s': File exists\n", argv[i]);
                } else {
                    printf("mkdir: cannot create directory '%s': No such file or directory\n", argv[i]);
                }
                return 1;
            }
            close(r);
        }
    }
    return 0;
}
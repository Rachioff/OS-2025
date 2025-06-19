#include <lib.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: touch <file>\n");
        return 1;
    }
    
    int fd = open(argv[1], O_WRONLY | O_CREAT);
    if (fd < 0) {
        char *slash = strrchr(argv[1], '/');
        if (slash && fd == -E_NOT_FOUND) {
            printf("touch: cannot touch '%s': No such file or directory\n", argv[1]);
        }
        return 1;
    }
    close(fd);
    return 0;
}
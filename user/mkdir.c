#include <lib.h>

void create_dir_in_fs(const char *path) {
    debugf("mkdir: creating directory is not fully implemented\n");
    debugf("mkdir: would create directory '%s'\n", path);
}

int main(int argc, char **argv) {
    int pflag = 0;
    int i = 1;
    
    if (argc > 1 && strcmp(argv[1], "-p") == 0) {
        pflag = 1;
        i = 2;
    }
    
    if (i >= argc) {
        printf("usage: mkdir [-p] <directory>\n");
        return 1;
    }
    
    for (; i < argc; i++) {
        create_dir_in_fs(argv[i]);
    }
    
    return 0;
}
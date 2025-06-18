#include <lib.h>

int main(int argc, char **argv) {
    int rflag = 0, fflag = 0;
    int i = 1;
    
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-r") == 0) {
            rflag = 1;
        } else if (strcmp(argv[i], "-rf") == 0) {
            rflag = fflag = 1;
        }
        i++;
    }
    
    if (i >= argc) {
        printf("usage: rm [-r] [-rf] <file>...\n");
        return 1;
    }
    
    for (; i < argc; i++) {
        struct Stat st;
        int r = stat(argv[i], &st);
        
        if (r < 0) {
            if (!fflag) {
                printf("rm: cannot remove '%s': No such file or directory\n", argv[i]);
                return 1;
            }
            continue;
        }
        
        if (st.st_isdir && !rflag) {
            printf("rm: cannot remove '%s': Is a directory\n", argv[i]);
            return 1;
        }
        
        r = remove(argv[i]);
        if (r < 0 && !fflag) {
            printf("rm: cannot remove '%s'\n", argv[i]);
            return 1;
        }
    }
    return 0;
}
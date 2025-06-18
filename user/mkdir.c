#include <lib.h>

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
        int r = syscall_mkdir(argv[i]);
        if (r < 0) {
            if (!pflag) {
                if (r == -E_FILE_EXISTS) {
                    printf("mkdir: cannot create directory '%s': File exists\n", argv[i]);
                } else if (r == -E_NOT_FOUND) {
                    printf("mkdir: cannot create directory '%s': No such file or directory\n", argv[i]);
                }
                return 1;
            } else if (r == -E_NOT_FOUND) {
                // 递归创建父目录
                // 实现略...
            }
        }
    }
    return 0;
}
#include <lib.h>

// 声明路径解析函数
extern void resolve_path(const char *path, char *resolved_path);

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: touch <file>\n");
        return 1;
    }
    
    char resolved_path[MAXPATHLEN];
    resolve_path(argv[1], resolved_path);
    
    int fd = open(resolved_path, O_CREAT | O_RDWR);
    if (fd < 0) {
        printf("touch: cannot touch '%s': No such file or directory\n", argv[1]);
        return 1;
    }
    close(fd);
    return 0;
}
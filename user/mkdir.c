#include <lib.h>

extern void resolve_path(const char *path, char *resolved_path);

void mkdir_p(const char *path) {
    char p_path[MAXPATHLEN];
    char resolved[MAXPATHLEN];
    
    // 先解析路径
    resolve_path(path, resolved);
    strcpy(p_path, resolved);

    char *p = p_path;
    if (*p == '/') {
        p++;
    }

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            int r = open(p_path, O_MKDIR | O_CREAT);
            if (r >= 0) {
                close(r);
            }
            *p = '/';
        }
    }
    int r = open(p_path, O_MKDIR | O_CREAT);
    if (r >= 0) {
        close(r);
    }
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
        char resolved[MAXPATHLEN];
        resolve_path(argv[i], resolved);
        
        struct Stat st;
        if (stat(resolved, &st) == 0) {
            if (!p_flag) { 
                printf("mkdir: cannot create directory '%s': File exists\n", argv[i]);
                return 1;
            }
            continue; 
        }

        if (p_flag) {
            mkdir_p(argv[i]);
        } else {
            int r = open(resolved, O_MKDIR | O_CREAT);
            if (r < 0) {
                printf("mkdir: cannot create directory '%s': No such file or directory\n", argv[i]);
                return 1;
            }
            close(r);
        }
    }
    return 0;
}
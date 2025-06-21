#include <lib.h>
#include <fs.h>

// --- Utility function to replace sprintf ---
char* strcat(char* dest, const char* src) {
    char* ptr = dest + strlen(dest);
    while (*src != '\0') {
        *ptr++ = *src++;
    }
    *ptr = '\0';
    return dest;
}

int flag_r = 0;
int flag_f = 0;

void rm_recursive(const char *path);

void rm_dir(const char *path) {
    int r;
    int fd;
    struct File f;
    
    if ((fd = open(path, O_RDONLY)) < 0) {
        if (!flag_f) printf("rm: cannot open '%s' for reading\n", path);
        return;
    }

    // Read directory entries and recursively delete them.
    while (readn(fd, &f, sizeof f) == sizeof f) {
        if (f.f_name[0] && strcmp(f.f_name, ".") != 0 && strcmp(f.f_name, "..") != 0) {
            char child_path[MAXPATHLEN];
            // *** THE CHANGE IS HERE: Replaced sprintf with strcpy/strcat ***
            if (strcmp(path, "/") == 0) {
                strcpy(child_path, "/");
                strcat(child_path, f.f_name);
            } else {
                strcpy(child_path, path);
                strcat(child_path, "/");
                strcat(child_path, f.f_name);
            }
            rm_recursive(child_path);
        }
    }
    close(fd);
    
    // After the directory is empty, remove it.
    if ((r = remove(path)) < 0) {
        if (!flag_f) printf("rm: cannot remove directory '%s': %d\n", path, r);
    }
}

void rm_recursive(const char *path) {
    struct Stat st;
    int r;

    if ((r = stat(path, &st)) < 0) {
        if (!flag_f) printf("rm: cannot remove '%s': No such file or directory\n", path);
        return;
    }

    if (st.st_isdir) {
        rm_dir(path);
    } else {
        if ((r = remove(path)) < 0) {
            if (!flag_f) printf("rm: cannot remove '%s': %d\n", path, r);
        }
    }
}

int main(int argc, char **argv) {
    int i = 1;

    // Parse flags like -r, -f, -rf
    while(i < argc && argv[i][0] == '-') {
        char *flags = argv[i];
        for(int j = 1; flags[j] != '\0'; j++) {
            if (flags[j] == 'r') flag_r = 1;
            if (flags[j] == 'f') flag_f = 1;
        }
        i++;
    }

    if (i >= argc) {
         if (!flag_f) printf("rm: missing operand\n");
         return 1;
    }

    for (; i < argc; i++) {
        struct Stat st;
        int r = stat(argv[i], &st);

        if (r < 0) {
            if (!flag_f) printf("rm: cannot remove '%s': No such file or directory\n", argv[i]);
            continue;
        }

        if (st.st_isdir) {
            if (flag_r) {
                rm_recursive(argv[i]);
            } else {
                printf("rm: cannot remove '%s': Is a directory\n", argv[i]);
                return 1;
            }
        } else {
            if ((r = remove(argv[i])) < 0) {
                 if (!flag_f) printf("rm: cannot remove file '%s': %d\n", argv[i], r);
                 return 1;
            }
        }
    }
    return 0;
}
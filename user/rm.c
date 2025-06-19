#include <lib.h>
#include <fs.h>

int flag_r = 0;
int flag_f = 0;

void rm_recursive(const char *path);

void rm_dir(const char *path) {
    int r;
    int fd;
    struct File f;
    
    if ((fd = open(path, O_RDONLY)) < 0) {
        if (!flag_f) printf("rm: cannot open '%s': %d\n", path, fd);
        return;
    }

    // Read directory entries and delete them recursively
    while (readn(fd, &f, sizeof f) == sizeof f) {
        if (f.f_name[0] && strcmp(f.f_name, ".") != 0 && strcmp(f.f_name, "..") != 0) {
            char child_path[MAXPATHLEN];
            // Build child path
            if (strcmp(path, "/") == 0) {
                sprintf(child_path, "/%s", f.f_name);
            } else {
                sprintf(child_path, "%s/%s", path, f.f_name);
            }
            rm_recursive(child_path);
        }
    }
    close(fd);
    
    // Remove the now-empty directory
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

    if (argc < 2) {
        if (!flag_f) printf("rm: missing operand\n");
        return 1;
    }
    
    // Argument parsing
    if (argv[1][0] == '-') {
        if (strchr(argv[1], 'r')) flag_r = 1;
        if (strchr(argv[1], 'f')) flag_f = 1;
        i = 2;
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
                 if (!flag_f) printf("rm: cannot remove '%s': No such file or directory\n", argv[i]);
                 return 1;
            }
        }
    }
    return 0;
}
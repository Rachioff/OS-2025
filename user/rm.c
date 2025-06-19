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
        if (!flag_f) printf("rm: cannot open '%s': %e\n", path, fd);
        return;
    }

    while ((r = readn(fd, &f, sizeof f)) == sizeof f) {
        if (f.f_name[0]) {
            char child_path[MAXPATHLEN];
            strcpy(child_path, path);
            if(child_path[strlen(child_path)-1] != '/') strcat(child_path, "/");
            strcat(child_path, f.f_name);
            rm_recursive(child_path);
        }
    }

    close(fd);
    
    if ((r = remove(path)) < 0) {
        if (!flag_f) printf("rm: cannot remove directory '%s': %e\n", path, r);
    }
}

void rm_recursive(const char *path) {
    struct Stat st;
    int r;

    if ((r = stat(path, &st)) < 0) {
        if (!flag_f) printf("rm: cannot stat '%s': No such file or directory\n", path);
        return;
    }

    if (st.st_isdir) {
        rm_dir(path);
    } else {
        if ((r = remove(path)) < 0) {
            if (!flag_f) printf("rm: cannot remove '%s': %e\n", path, r);
        }
    }
}

int main(int argc, char **argv) {
    int i;
    if (argc < 2) {
        printf("Usage: rm [-rf] <file...>\n");
        return 1;
    }
    
    // Simple argument parsing
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (strchr(argv[i], 'r')) flag_r = 1;
        if (strchr(argv[i], 'f')) flag_f = 1;
    }

    if (i == argc) {
         if (!flag_f) printf("rm: missing operand\n");
         return 1;
    }

    for (; i < argc; i++) {
        struct Stat st;
        int r = stat(argv[i], &st);

        if (r < 0) {
            if (!flag_f) {
                printf("rm: cannot remove '%s': No such file or directory\n", argv[i]);
            }
            continue;
        }

        if (st.st_isdir) {
            if (flag_r) {
                rm_recursive(argv[i]);
            } else {
                printf("rm: cannot remove '%s': Is a directory\n", argv[i]);
            }
        } else {
            remove(argv[i]);
        }
    }
    return 0;
}
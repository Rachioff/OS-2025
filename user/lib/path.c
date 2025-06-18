#include <lib.h>

void normalize_path(char *path) {
    char *src = path;
    char *dst = path;
    
    while (*src) {
        if (src[0] == '/' && src[1] == '/') {
            src++;
        } else if (src[0] == '/' && src[1] == '.' && src[2] == '/') {
            src += 2;
        } else if (src[0] == '/' && src[1] == '.' && src[2] == '.' && 
                  (src[3] == '/' || src[3] == '\0')) {
            if (dst > path) {
                dst--;
                while (dst > path && *dst != '/') {
                    dst--;
                }
            }
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    if (path[0] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    }
}

void resolve_path(const char *path, const char *cwd, char *result) {
    if (path[0] == '/') {
        strcpy(result, path);
    } else {
        strcpy(result, cwd);
        if (result[strlen(result) - 1] != '/') {
            strcat(result, "/");
        }
        strcat(result, path);
    }
    normalize_path(result);
}
#include <lib.h>

static char *my_strcat(char *dst, const char *src) {
    char *ret = dst;
    while (*dst) {
        dst++;
    }
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';
    return ret;
}

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
        // 复制 cwd
        strcpy(result, cwd);
        
        // 获取 cwd 的长度
        int len = strlen(result);
        
        // 如果 cwd 不以 '/' 结尾，添加 '/'
        if (len > 0 && result[len - 1] != '/') {
            result[len] = '/';
            len++;
        }
        
        // 复制 path 到 result 的末尾
        strcpy(result + len, path);
    }
    normalize_path(result);
}
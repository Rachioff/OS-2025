#include <lib.h>

// 全局变量存储当前工作目录
char g_cwd[MAXPATHLEN] = "/";

// 实现 strcat 函数
char* my_strcat(char* dest, const char* src) {
    char* ptr = dest + strlen(dest);
    while (*src != '\0') {
        *ptr++ = *src++;
    }
    *ptr = '\0';
    return dest;
}

// 安全的字符串复制函数
void safe_strncpy(char *dst, const char *src, int n) {
    int i;
    for(i = 0; i < n - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

// 解析路径（相对路径转绝对路径）
void resolve_path(const char *path, char *resolved_path) {
    char temp_path[MAXPATHLEN], temp_path_copy[MAXPATHLEN];
    
    if (path[0] == '/') {
        strcpy(temp_path, path);
    } else {
        // 处理相对路径
        if (strcmp(g_cwd, "/") == 0) {
            strcpy(temp_path, "/");
            my_strcat(temp_path, path);
        } else {
            strcpy(temp_path, g_cwd);
            my_strcat(temp_path, "/");
            my_strcat(temp_path, path);
        }
    }
    
    // 复制路径以便修改
    safe_strncpy(temp_path_copy, temp_path, MAXPATHLEN);
    
    char *components[128];
    int top = -1;
    char *p = temp_path_copy, *start = p;
    
    if (*p == '/') start++;
    
    // 分解路径
    for(p = start; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (strcmp(start, "..") == 0) { 
                if (top > -1) top--; 
            } else if (strcmp(start, ".") != 0 && strlen(start) > 0) { 
                if (top < 127) components[++top] = start; 
            }
            start = p + 1;
        }
    }
    
    // 处理最后一个组件
    if (strlen(start) > 0) {
        if (strcmp(start, "..") == 0) { 
            if (top > -1) top--; 
        } else if (strcmp(start, ".") != 0) { 
            if (top < 127) components[++top] = start; 
        }
    }
    
    // 重建路径
    if (top == -1) {
        strcpy(resolved_path, "/");
    } else {
        resolved_path[0] = '\0';
        for (int i = 0; i <= top; i++) { 
            my_strcat(resolved_path, "/"); 
            my_strcat(resolved_path, components[i]); 
        }
    }
}
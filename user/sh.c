#include <args.h>
#include <lib.h>

#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"
#define HISTORY_MAX 20
#define ENV_NAME_MAX 16
#define ENV_VAL_MAX 16
#define MAX_ENVS 64
#define MAX_INPUT 1024

struct EnvVar {
    char name[ENV_NAME_MAX + 1];
    char value[ENV_VAL_MAX + 1];
    int is_export;
    int is_readonly;
};

static struct EnvVar env_vars[MAX_ENVS];
static int env_count = 0;
static char *history[HISTORY_MAX];
static int history_count = 0;
static char current_dir[MAXPATHLEN] = "/";
static int exit_status = 0;

void normalize_path(char *path);
void resolve_path(const char *path, const char *cwd, char *result);
void runcmd(char *s);
void runcmd_multi(char *s);
char *expand_vars(char *str);
void save_history(const char *cmd);

int builtin_cd(int argc, char **argv) {
    char path[MAXPATHLEN];
    struct Stat st;
    
    if (argc > 2) {
        printf("Too many args for cd command\n");
        return 1;
    }
    
    if (argc == 1) {
        strcpy(path, "/");
    } else {
        resolve_path(argv[1], current_dir, path);
    }
    
    if (stat(path, &st) < 0) {
        printf("cd: The directory '%s' does not exist\n", argv[1]);
        return 1;
    }
    
    if (!st.st_isdir) {
        printf("cd: '%s' is not a directory\n", argv[1]);
        return 1;
    }
    
    strcpy(current_dir, path);
    return 0;
}

int builtin_pwd(int argc, char **argv) {
    if (argc != 1) {
        printf("pwd: expected 0 arguments; got %d\n", argc - 1);
        return 2;
    }
    printf("%s\n", current_dir);
    return 0;
}

int builtin_declare(int argc, char **argv) {
    int is_export = 0, is_readonly = 0;
    int arg_start = 1;
    
    while (arg_start < argc && argv[arg_start][0] == '-') {
        if (strcmp(argv[arg_start], "-x") == 0) {
            is_export = 1;
        } else if (strcmp(argv[arg_start], "-r") == 0) {
            is_readonly = 1;
        } else if (strcmp(argv[arg_start], "-xr") == 0 || 
                   strcmp(argv[arg_start], "-rx") == 0) {
            is_export = 1;
            is_readonly = 1;
        }
        arg_start++;
    }
    
    if (arg_start >= argc) {
        for (int i = 0; i < env_count; i++) {
            printf("%s=%s\n", env_vars[i].name, env_vars[i].value);
        }
        return 0;
    }
    
    char name[ENV_NAME_MAX + 1];
    char value[ENV_VAL_MAX + 1];
    const char *eq = strchr(argv[arg_start], '=');  // 使用 const char*
    
    if (eq) {
        int name_len = eq - argv[arg_start];
        if (name_len > ENV_NAME_MAX) name_len = ENV_NAME_MAX;
        memcpy(name, argv[arg_start], name_len);
        name[name_len] = '\0';
        
        int val_len = strlen(eq + 1);
        if (val_len > ENV_VAL_MAX) val_len = ENV_VAL_MAX;
        memcpy(value, eq + 1, val_len);
        value[val_len] = '\0';
    } else {
        int name_len = strlen(argv[arg_start]);
        if (name_len > ENV_NAME_MAX) name_len = ENV_NAME_MAX;
        memcpy(name, argv[arg_start], name_len);
        name[name_len] = '\0';
        value[0] = '\0';
    }
    
    int found = -1;
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_vars[i].name, name) == 0) {
            found = i;
            break;
        }
    }
    
    if (found >= 0) {
        if (env_vars[found].is_readonly) {
            return 1;
        }
        strcpy(env_vars[found].value, value);
        if (is_export) env_vars[found].is_export = 1;
        if (is_readonly) env_vars[found].is_readonly = 1;
    } else {
        if (env_count >= MAX_ENVS) {
            return 1;
        }
        strcpy(env_vars[env_count].name, name);
        strcpy(env_vars[env_count].value, value);
        env_vars[env_count].is_export = is_export;
        env_vars[env_count].is_readonly = is_readonly;
        env_count++;
    }
    
    return 0;
}

int builtin_unset(int argc, char **argv) {
    if (argc != 2) {
        return 1;
    }
    
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_vars[i].name, argv[1]) == 0) {
            if (env_vars[i].is_readonly) {
                return 1;
            }
            for (int j = i; j < env_count - 1; j++) {
                env_vars[j] = env_vars[j + 1];
            }
            env_count--;
            return 0;
        }
    }
    return 0;
}

int builtin_exit(int argc, char **argv) {
    exit();
    return 0;
}

int builtin_history(int argc, char **argv) {
    int fd = open("/.mos_history", O_RDONLY);
    if (fd >= 0) {
        char buf[MAX_INPUT];
        int n;
        while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            printf("%s", buf);
        }
        close(fd);
    }
    return 0;
}

void save_history(const char *cmd) {
    if (strlen(cmd) == 0) return;
    
    if (history_count < HISTORY_MAX) {
        history[history_count] = (char*)cmd;
        history_count++;
    }
    
    int fd = open("/.mos_history", O_WRONLY | O_CREAT);
    if (fd >= 0) {
        lseek(fd, 0, SEEK_END);
        write(fd, cmd, strlen(cmd));
        write(fd, "\n", 1);
        close(fd);
    }
}

char *expand_vars(char *str) {
    static char result[MAX_INPUT];
    char *dst = result;
    char *src = str;
    
    while (*src && dst - result < MAX_INPUT - 1) {
        if (*src == '$' && ((src[1] >= 'a' && src[1] <= 'z') || (src[1] >= 'A' && src[1] <= 'Z'))) {
            src++;
            char varname[ENV_NAME_MAX + 1];
            int i = 0;
            while (((*src >= 'a' && *src <= 'z') || (*src >= 'A' && *src <= 'Z') || 
                   (*src >= '0' && *src <= '9') || *src == '_') && i < ENV_NAME_MAX) {
                varname[i++] = *src++;
            }
            varname[i] = '\0';
            
            for (int j = 0; j < env_count; j++) {
                if (strcmp(env_vars[j].name, varname) == 0) {
                    strcpy(dst, env_vars[j].value);
                    dst += strlen(env_vars[j].value);
                    break;
                }
            }
        } else if (*src == '`') {
            src++;
            char cmd[MAX_INPUT];
            int i = 0;
            while (*src && *src != '`' && i < MAX_INPUT - 1) {
                cmd[i++] = *src++;
            }
            cmd[i] = '\0';
            if (*src == '`') src++;
            
            int p[2];
            if (pipe(p) == 0) {
                int pid = fork();
                if (pid == 0) {
                    close(p[0]);
                    dup(p[1], 1);
                    close(p[1]);
                    runcmd(cmd);
                    exit();
                } else if (pid > 0) {
                    close(p[1]);
                    char output[MAX_INPUT];
                    int n = read(p[0], output, MAX_INPUT - 1);
                    if (n > 0) {
                        output[n] = '\0';
                        if (n > 0 && output[n-1] == '\n') {
                            output[n-1] = '\0';
                        }
                        strcpy(dst, output);
                        dst += strlen(output);
                    }
                    close(p[0]);
                    wait(pid);
                }
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return result;
}

void readline(char *buf, u_int n) {
    int pos = 0;
    int len = 0;
    int hist_idx = -1;
    char c;
    int r;
    
    pos = len = 0;
    hist_idx = history_count;
    buf[0] = '\0';
    
    while (1) {
        if ((r = read(0, &c, 1)) != 1) {
            if (r < 0) {
                debugf("read error: %d\n", r);
            }
            exit();
        }
        
        if (c == '\x1b') {
            if (read(0, &c, 1) == 1 && c == '[') {
                if (read(0, &c, 1) == 1) {
                    switch (c) {
                        case 'A':
                            if (hist_idx > 0) {
                                hist_idx--;
                                while (pos > 0) {
                                    printf("\b \b");
                                    pos--;
                                }
                                strcpy(buf, history[hist_idx]);
                                printf("%s", buf);
                                pos = len = strlen(buf);
                            }
                            break;
                        case 'B':
                            if (hist_idx < history_count - 1) {
                                hist_idx++;
                                while (pos > 0) {
                                    printf("\b \b");
                                    pos--;
                                }
                                strcpy(buf, history[hist_idx]);
                                printf("%s", buf);
                                pos = len = strlen(buf);
                            } else if (hist_idx == history_count - 1) {
                                hist_idx++;
                                while (pos > 0) {
                                    printf("\b \b");
                                    pos--;
                                }
                                buf[0] = '\0';
                                pos = len = 0;
                            }
                            break;
                        case 'C':
                            if (pos < len) {
                                printf("%c", buf[pos]);
                                pos++;
                            }
                            break;
                        case 'D':
                            if (pos > 0) {
                                printf("\b");
                                pos--;
                            }
                            break;
                    }
                }
            }
        } else if (c == '\x01') {
            while (pos > 0) {
                printf("\b");
                pos--;
            }
        } else if (c == '\x05') {
            while (pos < len) {
                printf("%c", buf[pos]);
                pos++;
            }
        } else if (c == '\x0b') {
            int old_len = len;
            len = pos;
            buf[len] = '\0';
            for (int i = pos; i < old_len; i++) {
                printf(" ");
            }
            for (int i = pos; i < old_len; i++) {
                printf("\b");
            }
        } else if (c == '\x15') {
            int shift = pos;
            for (int i = pos; i <= len; i++) {
                buf[i - shift] = buf[i];
            }
            len -= shift;
            while (pos > 0) {
                printf("\b");
                pos--;
            }
            printf("%s", buf);
            for (int i = 0; i < shift; i++) {
                printf(" ");
            }
            for (int i = 0; i < shift + len; i++) {
                printf("\b");
            }
        } else if (c == '\x17') {
            int end = pos;
            while (pos > 0 && buf[pos - 1] == ' ') {
                pos--;
                printf("\b");
            }
            while (pos > 0 && buf[pos - 1] != ' ') {
                pos--;
                printf("\b");
            }
            int shift = end - pos;
            for (int i = end; i <= len; i++) {
                buf[i - shift] = buf[i];
            }
            len -= shift;
            printf("%s", buf + pos);
            for (int i = 0; i < shift; i++) {
                printf(" ");
            }
            for (int i = 0; i < shift + (len - pos); i++) {
                printf("\b");
            }
        } else if (c == '\b' || c == 0x7f) {
            if (pos > 0) {
                for (int i = pos; i < len; i++) {
                    buf[i - 1] = buf[i];
                }
                len--;
                pos--;
                printf("\b");
                for (int i = pos; i < len; i++) {
                    printf("%c", buf[i]);
                }
                printf(" \b");
                for (int i = pos; i < len; i++) {
                    printf("\b");
                }
            }
        } else if (c == '\r' || c == '\n') {
            buf[len] = '\0';
            printf("\n");
            save_history(buf);
            return;
        } else if (c >= 32 && c < 127) {
            if (len < n - 1) {
                for (int i = len; i > pos; i--) {
                    buf[i] = buf[i - 1];
                }
                buf[pos] = c;
                len++;
                buf[len] = '\0';
                for (int i = pos; i < len; i++) {
                    printf("%c", buf[i]);
                }
                pos++;
                for (int i = pos; i < len; i++) {
                    printf("\b");
                }
            }
        }
    }
}

int _gettoken(char *s, char **p1, char **p2) {
    *p1 = 0;
    *p2 = 0;
    if (s == 0) {
        return 0;
    }
    
    while (strchr(WHITESPACE, *s)) {
        *s++ = 0;
    }
    if (*s == 0) {
        return 0;
    }
    
    if (*s == '#') {
        *s = 0;
        return 0;
    }
    
    if (*s == '>' && *(s+1) == '>') {
        *p1 = s;
        *s++ = 0;
        *s++ = 0;
        *p2 = s;
        return '>';
    }
    
    if (*s == '&' && *(s+1) == '&') {
        *p1 = s;
        *s++ = 0;
        *s++ = 0;
        *p2 = s;
        return '&';
    }
    
    if (*s == '|' && *(s+1) == '|') {
        *p1 = s;
        *s++ = 0;
        *s++ = 0;
        *p2 = s;
        return 'o';
    }
    
    if (strchr(SYMBOLS, *s)) {
        int t = *s;
        *p1 = s;
        *s++ = 0;
        *p2 = s;
        return t;
    }
    
    *p1 = s;
    while (*s && !strchr(WHITESPACE SYMBOLS, *s)) {
        s++;
    }
    *p2 = s;
    return 'w';
}

int gettoken(char *s, char **p1) {
    static int c, nc;
    static char *np1, *np2;
    
    if (s) {
        nc = _gettoken(s, &np1, &np2);
        return 0;
    }
    c = nc;
    *p1 = np1;
    nc = _gettoken(np2, &np1, &np2);
    return c;
}

#define MAXARGS 128

int parsecmd(char **argv, int *rightpipe) {
    int argc = 0;
    static int append_mode = 0;
    
    while (1) {
        char *t;
        int fd, r;
        int c = gettoken(0, &t);
        switch (c) {
        case 0:
            return argc;
        case 'w':
            if (argc >= MAXARGS) {
                debugf("too many arguments\n");
                exit();
            }
            argv[argc++] = t;
            break;
        case '<':
            if (gettoken(0, &t) != 'w') {
                debugf("syntax error: < not followed by word\n");
                exit();
            }
            fd = open(t, O_RDONLY);
            if (fd < 0) {
                debugf("open %s for read: %d\n", t, fd);
                exit();
            }
            if (fd != 0) {
                dup(fd, 0);
                close(fd);
            }
            break;
        case '>':
            append_mode = 0;
            c = gettoken(0, &t);
            if (c == '>') {
                append_mode = 1;
                c = gettoken(0, &t);
            }
            if (c != 'w') {
                debugf("syntax error: > not followed by word\n");
                exit();
            }
            if (append_mode) {
                fd = open(t, O_WRONLY);
                if (fd < 0) {
                    fd = open(t, O_WRONLY | O_CREAT);
                }
                if (fd >= 0) {
                    lseek(fd, 0, SEEK_END);
                }
            } else {
                fd = open(t, O_WRONLY | O_CREAT | O_TRUNC);
            }
            if (fd < 0) {
                debugf("open %s for write: %d\n", t, fd);
                exit();
            }
            if (fd != 1) {
                dup(fd, 1);
                close(fd);
            }
            break;
        case '|':;
            int p[2];
            if (pipe(p) < 0) {
                debugf("pipe: %d\n", r);
                exit();
            }
            *rightpipe = fork();
            if (*rightpipe == 0) {
                dup(p[0], 0);
                close(p[0]);
                close(p[1]);
                return parsecmd(argv, rightpipe);
            } else if (*rightpipe > 0) {
                dup(p[1], 1);
                close(p[1]);
                close(p[0]);
                return argc;
            } else {
                debugf("fork: %d\n", *rightpipe);
                exit();
            }
            break;
        }
    }
    
    return argc;
}

void runcmd(char *s) {
    char *expanded = expand_vars(s);
    
    const char *comment = strchr(expanded, '#');  // 使用 const char*
    if (comment) {
        expanded[comment - expanded] = '\0';  // 修改字符串而不是指针
    }
    
    gettoken(expanded, 0);
    
    char *argv[MAXARGS];
    int rightpipe = 0;
    int argc = parsecmd(argv, &rightpipe);
    if (argc == 0) {
        return;
    }
    argv[argc] = 0;
    
    if (strcmp(argv[0], "cd") == 0) {
        exit_status = builtin_cd(argc, argv);
        return;
    } else if (strcmp(argv[0], "pwd") == 0) {
        exit_status = builtin_pwd(argc, argv);
        return;
    } else if (strcmp(argv[0], "declare") == 0) {
        exit_status = builtin_declare(argc, argv);
        return;
    } else if (strcmp(argv[0], "unset") == 0) {
        exit_status = builtin_unset(argc, argv);
        return;
    } else if (strcmp(argv[0], "exit") == 0) {
        builtin_exit(argc, argv);
        return;
    } else if (strcmp(argv[0], "history") == 0) {
        exit_status = builtin_history(argc, argv);
        return;
    }
    
    char prog[MAXPATHLEN];
    strcpy(prog, argv[0]);
    if (!strchr(prog, '.')) {
        // 手动添加 .b 后缀，避免使用 strcat
        int len = strlen(prog);
        prog[len] = '.';
        prog[len + 1] = 'b';
        prog[len + 2] = '\0';
    }
    
    int child = spawn(prog, argv);
    close_all();
    if (child >= 0) {
        wait(child);
        exit_status = 0;
    } else {
        debugf("spawn %s: %d\n", prog, child);
        exit_status = 1;
    }
    if (rightpipe) {
        wait(rightpipe);
    }
    exit();
}

void runcmd_multi(char *s) {
    char *cmds[100];
    char *types[100];
    int ncmd = 0;
    
    char *start = s;
    char *p = s;
    
    while (*p) {
        if (*p == ';') {
            *p = '\0';
            cmds[ncmd] = start;
            types[ncmd] = ";";
            ncmd++;
            start = p + 1;
        } else if (*p == '&' && *(p+1) == '&') {
            *p = '\0';
            cmds[ncmd] = start;
            types[ncmd] = "&&";
            ncmd++;
            p++;
            start = p + 1;
        } else if (*p == '|' && *(p+1) == '|') {
            *p = '\0';
            cmds[ncmd] = start;
            types[ncmd] = "||";
            ncmd++;
            p++;
            start = p + 1;
        }
        p++;
    }
    
    if (start < p && *start) {
        cmds[ncmd] = start;
        types[ncmd] = "";
        ncmd++;
    }
    
    int last_status = 0;
    for (int i = 0; i < ncmd; i++) {
        if (i > 0) {
            if (strcmp(types[i-1], "&&") == 0 && last_status != 0) {
                continue;
            }
            if (strcmp(types[i-1], "||") == 0 && last_status == 0) {
                continue;
            }
        }
        
        char *expanded = expand_vars(cmds[i]);
        gettoken(expanded, 0);
        char *argv[MAXARGS];
        int rightpipe = 0;
        int argc = parsecmd(argv, &rightpipe);
        
        if (argc > 0 && (strcmp(argv[0], "cd") == 0 || 
                         strcmp(argv[0], "pwd") == 0 ||
                         strcmp(argv[0], "declare") == 0 ||
                         strcmp(argv[0], "unset") == 0 ||
                         strcmp(argv[0], "history") == 0)) {
            runcmd(cmds[i]);
            last_status = exit_status;
        } else {
            int r = fork();
            if (r < 0) {
                user_panic("fork: %d", r);
            }
            if (r == 0) {
                runcmd(cmds[i]);
                exit();
            } else {
                wait(r);
                last_status = 0;
            }
        }
    }
}

char buf[1024];

void usage(void) {
    printf("usage: sh [-ix] [script-file]\n");
    exit();
}

int main(int argc, char **argv) {
    int r;
    int interactive = iscons(0);
    int echocmds = 0;
    printf("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
    printf("::                                                         ::\n");
    printf("::                     MOS Shell 2024                      ::\n");
    printf("::                                                         ::\n");
    printf(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
    ARGBEGIN {
    case 'i':
        interactive = 1;
        break;
    case 'x':
        echocmds = 1;
        break;
    default:
        usage();
    }
    ARGEND
    
    if (argc > 1) {
        usage();
    }
    if (argc == 1) {
        close(0);
        if ((r = open(argv[0], O_RDONLY)) < 0) {
            user_panic("open %s: %d", argv[0], r);
        }
        user_assert(r == 0);
    }
    
    int fd = open("/.mos_history", O_RDONLY);
    if (fd >= 0) {
        char line[MAX_INPUT];
        char c;
        int i = 0;
        while (read(fd, &c, 1) == 1 && history_count < HISTORY_MAX) {
            if (c == '\n') {
                line[i] = '\0';
                if (i > 0) {
                    char *h = (char*)malloc(i + 1);
                    strcpy(h, line);
                    history[history_count++] = h;
                }
                i = 0;
            } else if (i < MAX_INPUT - 1) {
                line[i++] = c;
            }
        }
        close(fd);
    }
    
    for (;;) {
        if (interactive) {
            printf("\n$ ");
        }
        readline(buf, sizeof buf);
        
        if (buf[0] == '#') {
            continue;
        }
        if (echocmds) {
            printf("# %s\n", buf);
        }
        if ((r = fork()) < 0) {
            user_panic("fork: %d", r);
        }
        if (r == 0) {
            runcmd_multi(buf);
            exit();
        } else {
            wait(r);
        }
    }
    return 0;
}
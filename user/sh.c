#include <lib.h>
#include <args.h>
#include <fs.h>
#include <string.h>
#include <error.h>

/*
 * =================================================================================
 * MOS Shell - Challenge Edition (Corrected for Autograder) - v6
 * =================================================================================
 */

// --- MACROS AND CONSTANTS ---
#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_VARS 64
#define VAR_NAME_LEN 17
#define VAR_VALUE_LEN 17
#define MAX_HIST 20
#define HIST_FILE "/.mos_history"
#define WHITESPACE " \t\r\n"

// --- Custom Utilities for MOS Environment ---

static void my_strcat(char *dst, const char *src) {
    strcpy(dst + strlen(dst), src);
}

static void my_memmove(void *dst, const void *src, size_t n) {
    char *d = (char*)dst;
    const char *s = (const char*)src;
    if (d == s) return;
    if (d < s) { while (n-- > 0) *d++ = *s++; }
    else { d += n; s += n; while (n-- > 0) *--d = *--s; }
}

// Self-contained path resolution function without strtok.
static void resolve_path(const char *path, const char *cwd, char *resolved) {
    char temp_path[MAXPATHLEN];
    if (path[0] == '/') {
        if (strlen(path) >= MAXPATHLEN) {
            fprintf(2, "path is too long\n");
            resolved[0] = '\0';
            return;
        }
        strcpy(temp_path, path);
    } else {
        if (strlen(cwd) + strlen(path) + 1 >= MAXPATHLEN) {
            fprintf(2, "path is too long\n");
            resolved[0] = '\0';
            return;
        }
        if (strlen(cwd) == 1 && cwd[0] == '/') {
            strcpy(temp_path, "/");
        } else {
            strcpy(temp_path, cwd);
        }
        my_strcat(temp_path, "/");
        my_strcat(temp_path, path);
    }

    char *stack[MAX_ARGS];
    int top = -1;
    char *p = temp_path;
    while(*p) {
        while(*p == '/') p++;
        if (!*p) break;
        char *end = strchr(p, '/');
        if (end) *end = '\0';
        if (strcmp(p, ".") == 0) { /* Do nothing */ }
        else if (strcmp(p, "..") == 0) { if (top > -1) top--; }
        else { if (top < MAX_ARGS - 1) stack[++top] = p; }
        if (end) p = end + 1; else break;
    }

    if (top == -1) { strcpy(resolved, "/"); }
    else {
        resolved[0] = '\0';
        for (int i = 0; i <= top; i++) {
            my_strcat(resolved, "/");
            my_strcat(resolved, stack[i]);
        }
    }
}

// AST allocator
static char ast_buf[4096];
static char *next_free_node;
static void *ast_malloc(size_t size) {
    if (next_free_node + size > ast_buf + sizeof(ast_buf)) {
        fprintf(2, "Command too complex\n"); exit(1);
    }
    void *ret = next_free_node;
    next_free_node += ROUND(size, 4);
    return ret;
}

// --- AST Node Types & Data Structures ---
enum { EXEC = 1, REDIR, PIPE, LIST, AND, OR };
struct Cmd { int type; };
struct Exe_cmd { int type; char *argv[MAX_ARGS]; int argc; };
struct Redir_cmd { int type; struct Cmd *cmd; char *file; int mode; int fd; };
struct Pipe_cmd { int type; struct Cmd *left; struct Cmd *right; };
struct List_cmd { int type; struct Cmd *left; struct Cmd *right; };
struct Var { char name[VAR_NAME_LEN]; char value[VAR_VALUE_LEN]; u_char is_readonly; u_char is_exported; };

// --- GLOBAL SHELL STATE ---
char CWD[MAXPATHLEN] = "/";
struct Var vars[MAX_VARS]; int var_count = 0;
char history[MAX_HIST][MAX_CMD_LEN]; int history_count = 0;
int last_rv = 0;

// --- FORWARD DECLARATIONS ---
struct Cmd* parse_cmd(char *s);
int run_cmd(struct Cmd *cmd);
void handle_backticks(char *buf);
int handle_cd(int argc, char **argv);
int handle_pwd(int argc, char **argv);
int handle_declare(int argc, char **argv);
int handle_unset(int argc, char **argv);
int handle_history(int argc, char **argv);
void set_var(const char *name, const char *value, u_char readonly, u_char exported);

// --- AST CONSTRUCTORS ---
struct Cmd* execcmd(void) {
    struct Exe_cmd *cmd = (struct Exe_cmd*) ast_malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd)); cmd->type = EXEC; return (struct Cmd*)cmd;
}
struct Cmd* redircmd(struct Cmd *subcmd, char *file, int mode, int fd) {
    struct Redir_cmd *cmd = (struct Redir_cmd*) ast_malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd)); cmd->type = REDIR; cmd->cmd = subcmd; cmd->file = file; cmd->mode = mode; cmd->fd = fd; return (struct Cmd*)cmd;
}
struct Cmd* pipecmd(struct Cmd *left, struct Cmd *right) {
    struct Pipe_cmd *cmd = (struct Pipe_cmd*) ast_malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd)); cmd->type = PIPE; cmd->left = left; cmd->right = right; return (struct Cmd*)cmd;
}
struct Cmd* listcmd(int type, struct Cmd *left, struct Cmd *right) {
    struct List_cmd *cmd = (struct List_cmd*) ast_malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd)); cmd->type = type; cmd->left = left; cmd->right = right; return (struct Cmd*)cmd;
}

// --- HISTORY & READLINE ---
void load_history() {
    int fd = open(HIST_FILE, O_RDONLY);
    if (fd < 0) return;
    static char read_buf[MAX_CMD_LEN * MAX_HIST];
    memset(read_buf, 0, sizeof(read_buf));
    readn(fd, read_buf, sizeof(read_buf) - 1);
    close(fd);
    char *p = read_buf;
    history_count = 0;
    while(*p && history_count < MAX_HIST) {
        char *next_line = strchr(p, '\n');
        if (next_line) *next_line = '\0';
        if (strlen(p) > 0) strcpy(history[history_count++], p);
        if (next_line) p = next_line + 1; else break;
    }
}
void add_to_history(const char *line) {
    if (strlen(line) == 0 || (history_count > 0 && strcmp(history[history_count-1], line) == 0)) return;
    if (history_count < MAX_HIST) {
        strcpy(history[history_count++], line);
    } else {
        my_memmove(history[0], history[1], (MAX_HIST - 1) * MAX_CMD_LEN);
        strcpy(history[MAX_HIST-1], line);
    }
    int fd = open(HIST_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    for (int i = 0; i < history_count; i++) fprintf(fd, "%s\n", history[i]);
    close(fd);
}
void reprint_line(const char *buf, int cursor_pos, const char* prompt) {
    printf("\x1b[2K\r%s%s", prompt, buf);
    printf("\x1b[1000D");
    if (cursor_pos + strlen(prompt) > 0) printf("\x1b[%dC", (int)(cursor_pos + strlen(prompt)));
}
void readline_rich(char *buf, u_int n, const char* prompt) {
    int len = 0, cursor = 0, hist_idx = history_count;
    static char temp_line[MAX_CMD_LEN];
    buf[0] = '\0';
    temp_line[0] = '\0';
    while (1) {
        char c_buf[2];
        if(read(0, c_buf, 1) != 1) { syscall_yield(); continue; }
        char c = c_buf[0];
        if (c == '\r' || c == '\n') { printf("\n"); buf[len] = '\0'; return; }
        switch (c) {
            case '\b': case 0x7f:
                if (cursor > 0) { my_memmove(&buf[cursor - 1], &buf[cursor], len - cursor + 1); cursor--; len--; reprint_line(buf, cursor, prompt); } break;
            case 0x1b:
                char seq[3];
                if (read(0, &seq[0], 1) != 1 || seq[0] != '[') break;
                if (read(0, &seq[1], 1) != 1) break;
                c = seq[1];
                if (c == 'D' && cursor > 0) { cursor--; reprint_line(buf, cursor, prompt); }
                else if (c == 'C' && cursor < len) { cursor++; reprint_line(buf, cursor, prompt); }
                else if (c == 'A') {
                    if (hist_idx == history_count) strcpy(temp_line, buf);
                    if (hist_idx > 0) { hist_idx--; strcpy(buf, history[hist_idx]); len = cursor = strlen(buf); reprint_line(buf, cursor, prompt); }
                } else if (c == 'B') {
                    if (hist_idx < history_count) {
                        hist_idx++;
                        if (hist_idx == history_count) strcpy(buf, temp_line); else strcpy(buf, history[hist_idx]);
                        len = cursor = strlen(buf); reprint_line(buf, cursor, prompt);
                    }
                } break;
            case 1: cursor = 0; reprint_line(buf, cursor, prompt); break;
            case 5: cursor = len; reprint_line(buf, cursor, prompt); break;
            case 11: buf[cursor] = '\0'; len = cursor; reprint_line(buf, cursor, prompt); break;
            case 21: my_memmove(buf, &buf[cursor], len - cursor + 1); len -= cursor; cursor = 0; reprint_line(buf, cursor, prompt); break;
            case 23:
                if (cursor > 0) {
                    int end = cursor;
                    while (cursor > 0 && strchr(WHITESPACE, buf[cursor - 1])) cursor--;
                    while (cursor > 0 && !strchr(WHITESPACE, buf[cursor - 1])) cursor--;
                    my_memmove(&buf[cursor], &buf[end], len - end + 1); len -= (end - cursor); reprint_line(buf, cursor, prompt);
                } break;
            default:
                if (len < n - 1) { my_memmove(&buf[cursor + 1], &buf[cursor], len - cursor + 1); buf[cursor] = c; len++; cursor++; reprint_line(buf, cursor, prompt); } break;
        }
    }
}


// --- PARSER ---
struct Cmd* parse_cmd(char *s) {
    if (!s) return 0;
    struct Cmd *cmd; char *t;
    for (t = s + strlen(s) - 1; t >= s; t--) {
        if (*t == ';') { *t = 0; return listcmd(LIST, parse_cmd(s), parse_cmd(t + 1)); }
        if (t > s && *t == '&' && *(t-1) == '&') { *(t-1) = 0; return listcmd(AND, parse_cmd(s), parse_cmd(t + 1)); }
        if (t > s && *t == '|' && *(t-1) == '|') { *(t-1) = 0; return listcmd(OR, parse_cmd(s), parse_cmd(t + 1)); }
    }
    for (t = s + strlen(s) - 1; t >= s; t--) {
        if (*t == '|') { *t = 0; return pipecmd(parse_cmd(s), parse_cmd(t + 1)); }
    }
    cmd = execcmd(); int argc = 0;
    char *start_of_token;
    while (*s) {
        while (*s && strchr(WHITESPACE, *s)) *s++ = 0;
        if (!*s) break;
        if (*s == '<' || *s == '>') {
            int mode, fd;
            if (*s == '>') {
                s++; if (*s == '>') { s++; mode = O_WRONLY | O_CREAT | O_APPEND; } else { mode = O_WRONLY | O_CREAT | O_TRUNC; } fd = 1;
            } else { s++; mode = O_RDONLY; fd = 0; }
            while (*s && strchr(WHITESPACE, *s)) s++;
            start_of_token = s;
            while (*s && !strchr(WHITESPACE, *s)) s++;
            if (*s) *s++ = 0;
            cmd = redircmd(cmd, start_of_token, mode, fd);
            continue;
        }
        start_of_token = s;
        if (argc < MAX_ARGS-1) ((struct Exe_cmd*)cmd)->argv[argc++] = start_of_token;
        while (*s && !strchr(WHITESPACE, *s)) s++;
        if (*s) *s++ = 0;
    }
    ((struct Exe_cmd*)cmd)->argv[argc] = 0; ((struct Exe_cmd*)cmd)->argc = argc;
    return cmd;
}


// --- BUILT-INS & VARIABLES ---
struct Var* find_var(const char *name) { for (int i = 0; i < var_count; i++) if (strcmp(vars[i].name, name) == 0) return &vars[i]; return NULL; }
void set_var(const char *name, const char *value, u_char readonly, u_char exported) {
    struct Var *v = find_var(name);
    if (v) {
        if (v->is_readonly) { fprintf(2, "declare: %s: is readonly\n", name); return; }
    } else {
        if (var_count >= MAX_VARS) { fprintf(2, "declare: too many variables\n"); return; }
        v = &vars[var_count++];
        int name_len = strlen(name);
        if (name_len >= VAR_NAME_LEN) name_len = VAR_NAME_LEN - 1;
        memcpy(v->name, name, name_len);
        v->name[name_len] = '\0';
    }
    int val_len = strlen(value);
    if (val_len >= VAR_VALUE_LEN) val_len = VAR_VALUE_LEN - 1;
    memcpy(v->value, value, val_len);
    v->value[val_len] = '\0';

    if (readonly != 2) v->is_readonly = readonly; if (exported != 2) v->is_exported = exported;
}
void expand_vars(int argc, char **argv) { for(int i = 0; i < argc; i++) { if (argv[i][0] == '$') { struct Var* v = find_var(&argv[i][1]); argv[i] = v ? v->value : ""; } } }
int handle_cd(int argc, char **argv) {
    if (argc > 2) { fprintf(2, "cd: Too many arguments\n"); return 1; }
    const char *path = (argc == 1) ? "/" : argv[1]; 
    char new_cwd[MAXPATHLEN];
    resolve_path(path, CWD, new_cwd); 
    struct Stat st;
    if (stat(new_cwd, &st) < 0) { fprintf(2, "cd: The directory '%s' does not exist\n", (argc > 1) ? argv[1] : path); return 1; }
    if (!st.st_isdir) { fprintf(2, "cd: '%s' is not a directory\n", (argc > 1) ? argv[1] : path); return 1; }
    strcpy(CWD, new_cwd); 
    set_var("PWD", CWD, 0, 1); 
    return 0;
}
int handle_pwd(int argc, char **argv) { printf("%s\n", CWD); return 0; }
int handle_declare(int argc, char **argv) {
    if (argc == 1) { for (int i = 0; i < var_count; i++) printf("%s=%s\n", vars[i].name, vars[i].value); return 0; }
    u_char readonly = 2, exported = 2; int i = 1;
    if (argv[1][0] == '-') { for (char *p = &argv[1][1]; *p; p++) { if (*p == 'r') readonly = 1; else if (*p == 'x') exported = 1;} i++; }
    for (; i < argc; i++) {
        char *name = argv[i], *value = "", *eq = strchr(name, '=');
        if (eq) { *eq = '\0'; value = eq + 1; }
        set_var(name, value, readonly, exported);
    } return 0;
}
int handle_unset(int argc, char **argv) {
    if (argc != 2) { fprintf(2, "unset: invalid arguments\n"); return 1; }
    struct Var *v = find_var(argv[1]);
    if (v) {
        if (v->is_readonly) { fprintf(2, "unset: cannot unset readonly: %s\n", argv[1]); return 1; }
        my_memmove(v, v + 1, (vars + var_count - (v + 1)) * sizeof(struct Var));
        var_count--;
    } return 0;
}
int handle_history(int argc, char **argv) { for (int i = 0; i < history_count; i++) printf("%5d  %s\n", i + 1, history[i]); return 0; }

// --- COMMAND SUBSTITUTION ---
void handle_backticks(char* buf) {
    char *start;
    // *** STACK OVERFLOW FIX: Large buffers moved from stack to static area ***
    static char inner_cmd[MAX_CMD_LEN], output[MAX_CMD_LEN], rest_of_cmd[MAX_CMD_LEN];
    
    while ((start = strchr(buf, '`')) != NULL) {
        char *end = strchr(start + 1, '`'); if (!end) break;
        
        my_memmove(inner_cmd, start + 1, end - start - 1); inner_cmd[end - start - 1] = '\0';
        strcpy(rest_of_cmd, end + 1); *start = '\0';
        
        int p[2], r; pipe(p);
        if ((r = fork()) == 0) {
            close(p[0]); dup(p[1], 1); close(p[1]);
            run_cmd(parse_cmd(inner_cmd));
            exit(0);
        }
        close(p[1]);
        memset(output, 0, sizeof(output)); // Clear buffer before reading
        read(p[0], output, sizeof(output) - 1);
        close(p[0]); wait(r);
        
        for(int i = strlen(output)-1; i >= 0 && strchr("\r\n", output[i]); i--) output[i] = '\0';
        my_strcat(buf, output); my_strcat(buf, rest_of_cmd);
    }
}

/* --- COMMAND EXECUTION --- */
int run_cmd(struct Cmd *cmd) {
    if (cmd == 0) return 0;
    int p[2], r, status; 
    struct Exe_cmd *ecmd; struct Redir_cmd *rcmd; struct Pipe_cmd *pcmd; struct List_cmd *lcmd;
    switch (cmd->type) {
    case EXEC:
        ecmd = (struct Exe_cmd*)cmd;
        if (ecmd->argv[0] == 0) return 0;
        expand_vars(ecmd->argc, ecmd->argv);
        char resolved_prog_path[MAXPATHLEN];
        resolve_path(ecmd->argv[0], CWD, resolved_prog_path);
        
        static char resolved_arg_storage[MAX_ARGS][MAXPATHLEN];
        char *new_argv[MAX_ARGS];
        
        new_argv[0] = ecmd->argv[0]; 
        for (int i = 1; i < ecmd->argc; i++) {
            if (ecmd->argv[i][0] != '-') {
                resolve_path(ecmd->argv[i], CWD, resolved_arg_storage[i]);
                new_argv[i] = resolved_arg_storage[i];
            } else {
                new_argv[i] = ecmd->argv[i];
            }
        }
        new_argv[ecmd->argc] = NULL;
        if ((r = spawn(resolved_prog_path, (const char**)new_argv)) < 0) {
            my_strcat(resolved_prog_path, ".b");
            if ((r = spawn(resolved_prog_path, (const char**)new_argv)) < 0) {
                fprintf(2, "%s: command not found\n", ecmd->argv[0]);
                return 127;
            }
        }
        return wait(r);
        
    case REDIR:
        rcmd = (struct Redir_cmd*)cmd; 
        if ((r = fork()) == 0) {
            char abs_file_path[MAXPATHLEN];
            resolve_path(rcmd->file, CWD, abs_file_path);
            close(rcmd->fd);
            if (open(abs_file_path, rcmd->mode) != rcmd->fd) {
                fprintf(2, "open %s for redirection failed\n", rcmd->file); 
                exit(1);
            }
            exit(run_cmd(rcmd->cmd));
        }
        return wait(r);

    case PIPE:
        pcmd = (struct Pipe_cmd*)cmd;
        pipe(p);
        int r_left, r_right;

        if ((r_left = fork()) < 0) {
            fprintf(2, "fork left pipe failed\n");
            close(p[0]);
            close(p[1]);
            return -1;
        }
        if (r_left == 0) {
            close(p[0]);
            dup(p[1], 1);
            close(p[1]);
            exit(run_cmd(pcmd->left));
        }

        if ((r_right = fork()) < 0) {
            fprintf(2, "fork right pipe failed\n");
            close(p[0]);
            close(p[1]);
            wait(r_left);
            return -1;
        }
        if (r_right == 0) {
            close(p[1]);
            dup(p[0], 0);
            close(p[0]);
            exit(run_cmd(pcmd->right));
        }

        close(p[0]);
        close(p[1]);
        wait(r_left);
        return wait(r_right);
    case LIST:
        lcmd = (struct List_cmd*)cmd; run_cmd(lcmd->left); return run_cmd(lcmd->right);
    case AND:
        lcmd = (struct List_cmd*)cmd; status = run_cmd(lcmd->left); if (status == 0) return run_cmd(lcmd->right); return status;
    case OR:
        lcmd = (struct List_cmd*)cmd; status = run_cmd(lcmd->left); if (status != 0) return run_cmd(lcmd->right); return status;
    default: 
        fprintf(2, "Unknown command type %d\n", cmd->type); exit(1);
    }
    return 1;
}

// --- MAIN ENTRY ---
void main(int argc, char **argv) {
    static char buf[MAX_CMD_LEN]; 
    static char cmd_copy[MAX_CMD_LEN];
    static char prompt[MAXPATHLEN + 4];

    set_var("PWD", CWD, 0, 1); 
    load_history();
    
    printf("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
    printf("::               MOS Shell (Challenge Edition)             ::\n");
    printf(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
    
    while (1) {
        strcpy(prompt, CWD);
        my_strcat(prompt, "$ ");
        printf("\n%s", prompt);

        readline_rich(buf, sizeof(buf), prompt);
        
        char *comment = strchr(buf, '#'); if (comment) *comment = '\0';
        char *trimmed_buf = buf; while(*trimmed_buf && strchr(WHITESPACE, *trimmed_buf)) trimmed_buf++;
        if (*trimmed_buf == 0) continue;
        add_to_history(trimmed_buf);
        
        strcpy(cmd_copy, trimmed_buf);
        handle_backticks(cmd_copy);
        
        next_free_node = ast_buf; 
        struct Cmd* cmd = parse_cmd(cmd_copy);

        if (cmd && cmd->type == EXEC) {
            struct Exe_cmd *ecmd = (struct Exe_cmd*)cmd;
            if (ecmd->argc > 0) {
                 if (strcmp(ecmd->argv[0], "exit") == 0) { exit(0); }
                 if (strcmp(ecmd->argv[0], "cd") == 0) { last_rv = handle_cd(ecmd->argc, ecmd->argv); continue; }
                 if (strcmp(ecmd->argv[0], "pwd") == 0) { last_rv = handle_pwd(ecmd->argc, ecmd->argv); continue; }
                 if (strcmp(ecmd->argv[0], "declare") == 0) { last_rv = handle_declare(ecmd->argc, ecmd->argv); continue; }
                 if (strcmp(ecmd->argv[0], "unset") == 0) { last_rv = handle_unset(ecmd->argc, ecmd->argv); continue; }
                 if (strcmp(ecmd->argv[0], "history") == 0) { last_rv = handle_history(ecmd->argc, ecmd->argv); continue; }
            }
        }
        
        int r;
        if ((r = fork()) == 0) { exit(run_cmd(cmd)); }
        if (r > 0) { last_rv = wait(r); } 
        else { fprintf(2, "fork failed: %d\n", r); last_rv = r; }
    }
}
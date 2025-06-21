#include <lib.h>
#include <args.h>
#include <fs.h>

// --- START: Self-implemented standard functions ---
char* strcat(char* dest, const char* src) {
    char* ptr = dest + strlen(dest);
    while (*src != '\0') *ptr++ = *src++;
    *ptr = '\0';
    return dest;
}
void* my_memmove(void* dst, const void* src, u_int n) {
    char* d = dst;
    const char* s = src;
    if (d == s) return d;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}
// --- END: Self-implemented standard functions ---

// --- Global State ---
char CWD[MAXPATHLEN];
#define MAX_VARS 64 
#define VAR_MAX_NAME_LEN 16
#define VAR_MAX_VALUE_LEN 16
struct Var { char name[VAR_MAX_NAME_LEN + 1]; char value[VAR_MAX_VALUE_LEN + 1]; u_char is_exported; u_char is_readonly; u_char in_use; };
struct Var vars[MAX_VARS];

#define BUF_MAX 1024
char line_buffer[BUF_MAX];
int line_len = 0;
int line_pos = 0;

// --- History Storage ---
#define HISTFILESIZE 20
char history[HISTFILESIZE][BUF_MAX];
int history_count = 0;
int history_pos = 0;

// --- Function Declarations ---
void redraw_line(const char*);
char read_char();
void save_history();
void add_to_history(const char*);
int handle_history(int, char**);
int readline_from_fd(int, char*, int);
void load_history();

// --- History Management ---
void add_to_history(const char* cmd) {
    if (cmd[0] == '\0' || (history_count > 0 && strcmp(history[history_count - 1], cmd) == 0)) return;
    if (history_count == HISTFILESIZE) {
        for (int i = 0; i < HISTFILESIZE - 1; i++) strcpy(history[i], history[i + 1]);
        strcpy(history[HISTFILESIZE - 1], cmd);
    } else {
        strcpy(history[history_count], cmd);
        history_count++;
    }
    save_history();
}
void save_history() {
    int fd = open("/.mos_history", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    for (int i = 0; i < history_count; i++) {
        write(fd, history[i], strlen(history[i]));
        write(fd, "\n", 1);
    }
    close(fd);
}
int handle_history(int argc, char **argv) {
    if (argc > 1) { printf("history: too many arguments\n"); return 1; }
    for (int i = 0; i < history_count; i++) printf("%d: %s\n", i + 1, history[i]);
    return 0;
}
int readline_from_fd(int fd, char *buf, int size) {
    int i = 0; char c = 0;
    while (i < size - 1) {
        if (read(fd, &c, 1) != 1) { i = -1; break; }
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (i == -1 && c != '\n') ? -1 : i;
}
void load_history() {
    int fd = open("/.mos_history", O_RDONLY);
    if (fd < 0) return;
    while (history_count < HISTFILESIZE) {
        if (readline_from_fd(fd, history[history_count], BUF_MAX) < 0) break;
        if (history[history_count][0] != '\0') history_count++;
    }
    close(fd);
}


// --- Rich Readline Implementation ---
void redraw_line(const char *prompt) {
    printf("\r\x1b[K"); printf("%s%s", prompt, line_buffer); printf("\r\x1b[%dC", (int)(strlen(prompt) + line_pos));
}
void insert_char(char c) {
    if (line_len < BUF_MAX - 1) {
        my_memmove(&line_buffer[line_pos + 1], &line_buffer[line_pos], line_len - line_pos);
        line_buffer[line_pos] = c;
        line_len++; line_pos++; line_buffer[line_len] = '\0';
    }
}
void delete_char() {
    if (line_pos < line_len) {
        my_memmove(&line_buffer[line_pos], &line_buffer[line_pos + 1], line_len - line_pos - 1);
        line_len--; line_buffer[line_len] = '\0';
    }
}
void backspace_char() { if (line_pos > 0) { line_pos--; delete_char(); } }
char read_char() { char c; if (read(0, &c, 1) != 1) return 0; return c; }
void readline_rich(const char *prompt, char *dst_buf) {
    history_pos = history_count;
    line_len = 0; line_pos = 0; line_buffer[0] = '\0';
    redraw_line(prompt);
    while (1) {
        char c = read_char();
        switch (c) {
            case '\n': case '\r': strcpy(dst_buf, line_buffer); printf("\n"); return;
            case 0x7f: backspace_char(); break;
            case '\x1b': // Escape Sequence
                if (read_char() == '[') {
                    char next_c = read_char();
                    if (next_c == 'A') { // Up Arrow
                        if (history_pos > 0) {
                            // Go down, clear line, go back up.
                            printf("\x1b[1B\r\x1b[K\x1b[1A");
                            history_pos--;
                            strcpy(line_buffer, history[history_pos]);
                            line_len = strlen(line_buffer); line_pos = line_len;
                        }
                    } else if (next_c == 'B') { // Down Arrow
                        if (history_pos < history_count) {
                             // Go up, clear line, go back down.
                            printf("\x1b[1A\r\x1b[K\x1b[1B");
                            history_pos++;
                            if (history_pos == history_count) line_buffer[0] = '\0';
                            else strcpy(line_buffer, history[history_pos]);
                            line_len = strlen(line_buffer); line_pos = line_len;
                        }
                    } else if (next_c == 'D' && line_pos > 0) { // Left
                        line_pos--;
                    } else if (next_c == 'C' && line_pos < line_len) { // Right
                        line_pos++;
                    }
                }
                break;
            case 0x01: line_pos = 0; break;
            case 0x05: line_pos = line_len; break;
            case 0x0b: line_buffer[line_pos] = '\0'; line_len = line_pos; break;
            case 0x15:
                if(line_pos > 0){
                    my_memmove(&line_buffer[0], &line_buffer[line_pos], line_len - line_pos);
                    line_len -= line_pos; line_buffer[line_len] = '\0'; line_pos = 0;
                }
                break;
            case 0x17: {
                int prev_pos = line_pos;
                while (line_pos > 0 && line_buffer[line_pos - 1] == ' ') line_pos--;
                while (line_pos > 0 && line_buffer[line_pos - 1] != ' ') line_pos--;
                int num_to_delete = prev_pos - line_pos;
                if(num_to_delete > 0){
                    my_memmove(&line_buffer[line_pos], &line_buffer[prev_pos], line_len - prev_pos);
                    line_len -= num_to_delete; line_buffer[line_len] = '\0';
                }
                break;
            }
            default: if (c >= 0x20 && c < 0x7f) insert_char(c); break;
        }
        redraw_line(prompt);
    }
}

// --- The rest of the shell logic (unchanged from the working version) ---
void resolve_path(const char*, char*);
int handle_cd(int, char**);
int handle_pwd(int, char**);
int run_cmd(char*);
int handle_declare(int, char**);
int handle_unset(int, char**);
void expand_vars(char*, const char*, int);

void resolve_path(const char *path, char *resolved_path) {
    char temp_path[MAXPATHLEN], temp_path_copy[MAXPATHLEN];
    if (path[0] == '/') strcpy(temp_path, path);
    else {
        if (strcmp(CWD, "/") == 0) strcpy(temp_path, "/");
        else strcpy(temp_path, CWD);
        strcat(temp_path, "/");
        strcat(temp_path, path);
    }
    strcpy(temp_path_copy, temp_path);
    char *components[MAX_ARGS];
    int top = -1;
    char *p = temp_path_copy, *start = p;
    if (*p == '/') start++;
    for(p = start; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (strcmp(start, "..") == 0) { if (top > -1) top--; }
            else if (strcmp(start, ".") != 0 && strlen(start) > 0) { if (top < MAX_ARGS - 1) components[++top] = start; }
            start = p + 1;
        }
    }
    if (strlen(start) > 0) {
        if (strcmp(start, "..") == 0) { if (top > -1) top--; }
        else if (strcmp(start, ".") != 0) { if (top < MAX_ARGS - 1) components[++top] = start; }
    }
    if (top == -1) strcpy(resolved_path, "/");
    else {
        resolved_path[0] = '\0';
        for (int i = 0; i <= top; i++) {
            strcat(resolved_path, "/");
            strcat(resolved_path, components[i]);
        }
    }
}

int handle_cd(int argc, char **argv) {
    if (argc > 2) { printf("Too many args for cd command\n"); return 1; }
    const char *target_path = (argc == 1) ? "/" : argv[1];
    char resolved[MAXPATHLEN];
    resolve_path(target_path, resolved);
    struct Stat st;
    if (stat(resolved, &st) < 0) { printf("cd: The directory '%s' does not exist\n", target_path); return 1; }
    if (!st.st_isdir) { printf("cd: '%s' is not a directory\n", target_path); return 1; }
    strcpy(CWD, resolved);
    return 0;
}

int handle_pwd(int argc, char **argv) {
    if (argc != 1) { printf("pwd: expected 0 arguments; got %d\n", argc-1); return 2; }
    printf("%s\n", CWD);
    return 0;
}

struct Var* find_var(const char *name) {
    for (int i = 0; i < MAX_VARS; i++) if (vars[i].in_use && strcmp(vars[i].name, name) == 0) return &vars[i];
    return NULL;
}

struct Var* find_free_var_slot() {
    for (int i = 0; i < MAX_VARS; i++) if (!vars[i].in_use) return &vars[i];
    return NULL;
}

int handle_declare(int argc, char **argv) {
    if (argc == 1) { for (int i = 0; i < MAX_VARS; i++) if (vars[i].in_use) printf("%s=%s\n", vars[i].name, vars[i].value); return 0; }
    int i = 1, export_flag = 0, readonly_flag = 0;
    while(i < argc && argv[i][0] == '-') {
        for(int j = 1; argv[i][j] != '\0'; j++) {
            if (argv[i][j] == 'x') export_flag = 1;
            if (argv[i][j] == 'r') readonly_flag = 1;
        }
        i++;
    }
    if (i >= argc) return 0;
    char *eq_ptr = strchr(argv[i], '='), var_name[VAR_MAX_NAME_LEN + 1], var_value[VAR_MAX_VALUE_LEN + 1] = "";
    if (eq_ptr != NULL) {
        int name_len = eq_ptr - argv[i]; if (name_len > VAR_MAX_NAME_LEN) name_len = VAR_MAX_NAME_LEN;
        for(int k=0; k<name_len; k++) var_name[k] = argv[i][k]; var_name[name_len] = '\0';
        const char* value_start = eq_ptr + 1; int value_len = strlen(value_start); if (value_len > VAR_MAX_VALUE_LEN) value_len = VAR_MAX_VALUE_LEN;
        for(int k=0; k<value_len; k++) var_value[k] = value_start[k]; var_value[value_len] = '\0';
    } else {
        int name_len = strlen(argv[i]); if (name_len > VAR_MAX_NAME_LEN) name_len = VAR_MAX_NAME_LEN;
        for(int k=0; k<name_len; k++) var_name[k] = argv[i][k]; var_name[name_len] = '\0';
    }
    struct Var *v = find_var(var_name);
    if (v != NULL) {
        if (v->is_readonly) { printf("declare: cannot assign to readonly variable '%s'\n", var_name); return 1; }
        strcpy(v->value, var_value);
    } else {
        v = find_free_var_slot();
        if (v == NULL) { printf("declare: maximum number of variables reached\n"); return 1; }
        v->in_use = 1; strcpy(v->name, var_name); strcpy(v->value, var_value);
    }
    if (export_flag) v->is_exported = 1; if (readonly_flag) v->is_readonly = 1;
    return 0;
}

int handle_unset(int argc, char **argv) {
    if (argc != 2) { printf("unset: expected 1 argument\n"); return 1; }
    struct Var *v = find_var(argv[1]);
    if (v == NULL) return 0;
    if (v->is_readonly) { printf("unset: cannot unset readonly variable '%s'\n", argv[1]); return 1; }
    v->in_use = 0;
    return 0;
}

void expand_vars(char* dst, const char* src, int dst_size) {
    char* d_end = dst + dst_size - 1, *d = dst; const char* s = src;
    while (*s && d < d_end) {
        if (*s != '$') { *d++ = *s++; continue; }
        s++; char var_name[VAR_MAX_NAME_LEN + 1]; int name_len = 0;
        char c = *s; int is_alnum = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
        while (is_alnum && name_len < VAR_MAX_NAME_LEN) {
            var_name[name_len++] = *s++; c = *s;
            is_alnum = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
        }
        var_name[name_len] = '\0';
        if (name_len > 0) {
            struct Var* v = find_var(var_name);
            if (v != NULL) { const char* val = v->value; while (*val && d < d_end) *d++ = *val++; }
        } else if (d < d_end) *d++ = '$';
    }
    *d = '\0';
}

int run_cmd(char *buf) {
    // Use a stack-allocated array instead of malloc to save the command before stripping comments.
    char pre_comment_buf[BUF_MAX];
    strcpy(pre_comment_buf, buf);

    // Strip comments from the original buffer 'buf'.
    char *comment_start = strchr(buf, '#');
    if (comment_start != NULL) {
        *comment_start = '\0';
    }

    // Add the original command (with comments) to history, 
    // but only if the command is not empty after stripping comments.
    if (buf[0] != '\0') {
        add_to_history(pre_comment_buf);
    }

    // --- The rest of the function proceeds as before, using the comment-stripped 'buf' ---
    char *argv[MAX_ARGS];
    int argc = 0;
    char* p = buf;
    while(*p) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (*p == '\0') break;
        if (argc < MAX_ARGS - 1) argv[argc++] = p;
        while (*p != ' ' && *p != '\t' && *p != '\0') p++;
    }
    argv[argc] = NULL;

    if (argc == 0) return 0;
    if (strcmp(argv[0], "cd") == 0) return handle_cd(argc, argv);
    if (strcmp(argv[0], "pwd") == 0) return handle_pwd(argc, argv);
    if (strcmp(argv[0], "exit") == 0) exit(0);
    if (strcmp(argv[0], "declare") == 0) return handle_declare(argc, argv);
    if (strcmp(argv[0], "unset") == 0) return handle_unset(argc, argv);
    if (strcmp(argv[0], "history") == 0) return handle_history(argc, argv);

    char prog_path_abs[MAXPATHLEN], arg_paths_abs_storage[MAX_ARGS][MAXPATHLEN], *arg_paths_abs[MAX_ARGS];
    for(int i = 0; i < argc; ++i) {
        arg_paths_abs[i] = arg_paths_abs_storage[i];
        if (argv[i][0] == '-' || i == 0) strcpy(arg_paths_abs[i], argv[i]);
        else resolve_path(argv[i], arg_paths_abs[i]);
    }
    arg_paths_abs[argc] = NULL;

    resolve_path(argv[0], prog_path_abs);
    strcpy(arg_paths_abs_storage[0], prog_path_abs);

    int r;
    if ((r = spawn(prog_path_abs, (const char**)arg_paths_abs)) < 0) {
        strcat(prog_path_abs, ".b");
        strcpy(arg_paths_abs_storage[0], prog_path_abs);
        if ((r = spawn(prog_path_abs, (const char**)arg_paths_abs)) < 0) {
            printf("command not found: %s\n", argv[0]);
            return 127;
        }
    }
    if (r >= 0) wait(r);
    return r;
}

void main() {
    static char buf[BUF_MAX];
    static char expanded_buf[BUF_MAX * 2];
    char prompt[MAXPATHLEN + 4];
    strcpy(CWD, "/");
    for (int i = 0; i < MAX_VARS; i++) vars[i].in_use = 0;
    
    load_history();
    
    printf("\n"); 
    for (;;) {
        strcpy(prompt, CWD);
        strcat(prompt, "$ ");
        readline_rich(prompt, buf);
        expand_vars(expanded_buf, buf, sizeof(expanded_buf));
        run_cmd(expanded_buf);
    }
}
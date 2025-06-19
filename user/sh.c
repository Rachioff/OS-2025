#include <lib.h>
#include <args.h>
#include <fs.h>
#include <string.h>
#include <error.h>

/*
 * =================================================================================
 * MOS Shell - Challenge Edition (Corrected for Autograder)
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

// BUGFIX 2: Correct implementation of memmove to handle overlapping memory.
static void my_memmove(void *dst, const void *src, size_t n) {
    char *d = (char*)dst;
    const char *s = (const char*)src;
    if (d == s) {
        return;
    }
    // Handle overlapping regions correctly.
    if (d < s) {
        // Copy forwards.
        while (n-- > 0) {
            *d++ = *s++;
        }
    } else {
        // Copy backwards.
        d += n;
        s += n;
        while (n-- > 0) {
            *--d = *--s;
        }
    }
}


// Self-contained path resolution function to fix linking issues.
static void resolve_path(const char *path, const char *cwd, char *resolved) {
	char temp_path[MAXPATHLEN];
    // 1. Create initial absolute path string
	if (path[0] == '/') {
		strcpy(temp_path, path);
	} else {
		if (strlen(cwd) == 1 && cwd[0] == '/') {
            strcpy(temp_path, "/");
            my_strcat(temp_path, path);
        } else {
            strcpy(temp_path, cwd);
            my_strcat(temp_path, "/");
            my_strcat(temp_path, path);
        }
	}

	// 2. Normalize path by processing "." and ".." using a stack
	char *stack[MAX_ARGS];
	int top = -1;
    
    char *p = temp_path;
	while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        
        char *end = strchr(p, '/');
        if (end) *end = '\0';
        
        if (strcmp(p, ".") == 0) {
            // Do nothing
        } else if (strcmp(p, "..") == 0) {
			if (top > -1) top--; // Pop
		} else {
			if (top < MAX_ARGS - 1) stack[++top] = p; // Push
		}
        
        if (end) p = end + 1;
        else break;
	}

    // 3. Reconstruct the final path
	if (top == -1) {
		strcpy(resolved, "/");
	} else {
		resolved[0] = '\0';
		for (int i = 0; i <= top; i++) {
			my_strcat(resolved, "/");
			my_strcat(resolved, stack[i]);
		}
	}
}

// A simple bump allocator for AST nodes. Resets on each command line.
static char ast_buf[4096];
static char *next_free_node;

static void *malloc(size_t size) {
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
struct Var vars[MAX_VARS];
int var_count = 0;
char history[MAX_HIST][MAX_CMD_LEN];
int history_count = 0;

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
	struct Exe_cmd *cmd = (struct Exe_cmd*) malloc(sizeof(*cmd));
	memset(cmd, 0, sizeof(*cmd)); cmd->type = EXEC; return (struct Cmd*)cmd;
}
struct Cmd* redircmd(struct Cmd *subcmd, char *file, int mode, int fd) {
	struct Redir_cmd *cmd = (struct Redir_cmd*) malloc(sizeof(*cmd));
	memset(cmd, 0, sizeof(*cmd)); cmd->type = REDIR; cmd->cmd = subcmd; cmd->file = file; cmd->mode = mode; cmd->fd = fd; return (struct Cmd*)cmd;
}
struct Cmd* pipecmd(struct Cmd *left, struct Cmd *right) {
	struct Pipe_cmd *cmd = (struct Pipe_cmd*) malloc(sizeof(*cmd));
	memset(cmd, 0, sizeof(*cmd)); cmd->type = PIPE; cmd->left = left; cmd->right = right; return (struct Cmd*)cmd;
}
struct Cmd* listcmd(int type, struct Cmd *left, struct Cmd *right) {
	struct List_cmd *cmd = (struct List_cmd*) malloc(sizeof(*cmd));
	memset(cmd, 0, sizeof(*cmd)); cmd->type = type; cmd->left = left; cmd->right = right; return (struct Cmd*)cmd;
}

// --- HISTORY ---
void load_history() {
    int fd = open(HIST_FILE, O_RDONLY);
    if (fd < 0) return;
    char read_buf[MAX_CMD_LEN * MAX_HIST] = {0};
    readn(fd, read_buf, sizeof(read_buf) - 1);
    close(fd);
    char *p = read_buf;
    while(*p && history_count < MAX_HIST) {
        char *next_line = strchr(p, '\n');
        if (next_line) *next_line = '\0';
        strcpy(history[history_count++], p);
        if (next_line) p = next_line + 1; else break;
    }
}
void add_to_history(const char *line) {
    if (strlen(line) == 0 || (history_count > 0 && strcmp(history[history_count-1], line) == 0)) return;
    if (history_count < MAX_HIST) strcpy(history[history_count++], line);
    else {
        my_memmove(history[0], history[1], (MAX_HIST - 1) * MAX_CMD_LEN);
        strcpy(history[MAX_HIST-1], line);
    }
    int fd = open(HIST_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    for (int i = 0; i < history_count; i++) fprintf(fd, "%s\n", history[i]);
    close(fd);
}

// --- READLINE ---
void reprint_line(const char *buf, int cursor_pos) {
    printf("\x1b[2K\r$ %s", buf);
    printf("\x1b[1000D");
    if (cursor_pos + 2 > 0) printf("\x1b[%dC", cursor_pos + 2);
}
void readline(char *buf, u_int n) {
    int len = 0, cursor = 0, hist_idx = history_count;
    char temp_line[MAX_CMD_LEN] = {0}; buf[0] = '\0';
    while (1) {
        char c_buf[1];
        if (read(0, c_buf, 1) != 1) continue;
        int c = (int)c_buf[0];
        if (c == '\r' || c == '\n') { printf("\n"); buf[len] = '\0'; return; }
        switch (c) {
            case '\b': case 0x7f:
                if (cursor > 0) {
                    my_memmove(&buf[cursor - 1], &buf[cursor], len - cursor + 1);
                    cursor--; len--; reprint_line(buf, cursor);
                } break;
            case 0x1b:
                char seq[2];
                if (read(0, &seq[0], 1) != 1 || seq[0] != '[') break;
                if (read(0, &seq[1], 1) != 1) break;
                c = seq[1];
                if (c == 'D' && cursor > 0) { cursor--; reprint_line(buf, cursor); }
                else if (c == 'C' && cursor < len) { cursor++; reprint_line(buf, cursor); }
                else if (c == 'A') {
                    if (hist_idx == history_count) strcpy(temp_line, buf);
                    if (hist_idx > 0) {
                        hist_idx--; strcpy(buf, history[hist_idx]); len = cursor = strlen(buf); reprint_line(buf, cursor);
                    }
                } else if (c == 'B') {
                    if (hist_idx < history_count) {
                        hist_idx++;
                        if (hist_idx == history_count) strcpy(buf, temp_line); else strcpy(buf, history[hist_idx]);
                        len = cursor = strlen(buf); reprint_line(buf, cursor);
                    }
                } break;
            case 1: cursor = 0; reprint_line(buf, cursor); break;
            case 5: cursor = len; reprint_line(buf, cursor); break;
            case 11: buf[cursor] = '\0'; len = cursor; reprint_line(buf, cursor); break;
            case 21: my_memmove(buf, &buf[cursor], len - cursor + 1); len -= cursor; cursor = 0; reprint_line(buf, cursor); break;
            case 23:
                if (cursor > 0) {
                    int end = cursor;
                    while (cursor > 0 && strchr(WHITESPACE, buf[cursor - 1])) cursor--;
                    while (cursor > 0 && !strchr(WHITESPACE, buf[cursor - 1])) cursor--;
                    my_memmove(&buf[cursor], &buf[end], len - end + 1); len -= (end - cursor); reprint_line(buf, cursor);
                } break;
            default:
                if (len < n - 1) {
                    my_memmove(&buf[cursor + 1], &buf[cursor], len - cursor + 1);
                    buf[cursor] = c; len++; cursor++; reprint_line(buf, cursor);
                } break;
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
    while (*s) {
        while (*s && strchr(WHITESPACE, *s)) *s++ = 0;
        if (!*s) break;
        if (*s == '<' || *s == '>') {
            int mode, fd; char* file_start;
            if (*s == '>') {
                s++; if (*s == '>') { s++; mode = O_WRONLY | O_CREAT | O_APPEND; } else { mode = O_WRONLY | O_CREAT | O_TRUNC; } fd = 1;
            } else { s++; mode = O_RDONLY; fd = 0; }
            while (*s && strchr(WHITESPACE, *s)) s++;
            file_start = s;
            while (*s && !strchr(WHITESPACE, *s)) s++;
            if (*s) *s++ = 0;
            cmd = redircmd(cmd, file_start, mode, fd);
            continue;
        }
        if (argc < MAX_ARGS-1) ((struct Exe_cmd*)cmd)->argv[argc++] = s;
        while (*s && !strchr(WHITESPACE, *s)) s++;
        if (*s) *s++ = 0;
    }
    ((struct Exe_cmd*)cmd)->argv[argc] = 0; ((struct Exe_cmd*)cmd)->argc = argc;
    return cmd;
}

// --- BUILT-INS & VARIABLES ---
struct Var* find_var(const char *name) {
	for (int i = 0; i < var_count; i++) if (strcmp(vars[i].name, name) == 0) return &vars[i];
	return NULL;
}
void set_var(const char *name, const char *value, u_char readonly, u_char exported) {
	struct Var *v = find_var(name);
	if (v) {
		if (v->is_readonly) { fprintf(2, "declare: %s: is readonly\n", name); return; }
	} else {
		if (var_count >= MAX_VARS) { fprintf(2, "declare: too many variables\n"); return; }
		v = &vars[var_count++]; strcpy(v->name, name);
	}
	strcpy(v->value, value);
	if (readonly != 2) v->is_readonly = readonly; if (exported != 2) v->is_exported = exported;
}
void expand_vars(int argc, char **argv) {
	for(int i = 0; i < argc; i++) {
		if (argv[i][0] == '$') { struct Var* v = find_var(&argv[i][1]); argv[i] = v ? v->value : ""; }
	}
}
int handle_cd(int argc, char **argv) {
	if (argc > 2) { fprintf(2, "Too many args for cd command\n"); return 1; }
	const char *path = (argc == 1) ? "/" : argv[1]; char new_cwd[MAXPATHLEN];
	resolve_path(path, CWD, new_cwd); struct Stat st;
	if (stat(new_cwd, &st) < 0) { fprintf(2, "cd: The directory '%s' does not exist\n", path); return 1; }
	if (!st.st_isdir) { fprintf(2, "cd: '%s' is not a directory\n", path); return 1; }
	strcpy(CWD, new_cwd); set_var("PWD", CWD, 0, 1); return 0;
}
int handle_pwd(int argc, char **argv) {
    if (argc > 1) { fprintf(2, "pwd: expected 0 arguments; got %d\n", argc-1); return 2; }
    printf("%s\n", CWD); return 0;
}
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
        for (int i = (v - vars); i < var_count - 1; i++) vars[i] = vars[i+1];
        var_count--;
    } return 0;
}
int handle_history(int argc, char **argv) {
    for (int i = 0; i < history_count; i++) printf("  %d  %s\n", i + 1, history[i]);
    return 0;
}

// --- COMMAND SUBSTITUTION ---
void handle_backticks(char* buf) {
    char *start;
    while ((start = strchr(buf, '`')) != NULL) {
        char *end = strchr(start + 1, '`'); if (!end) break;
        char inner_cmd[MAX_CMD_LEN], output[MAX_CMD_LEN] = {0}, rest_of_cmd[MAX_CMD_LEN];
        my_memmove(inner_cmd, start + 1, end - start - 1); inner_cmd[end - start - 1] = '\0';
        strcpy(rest_of_cmd, end + 1); *start = '\0';
        int p[2], r; pipe(p);
        if ((r = fork()) == 0) {
            close(p[0]); dup(p[1], 1); close(p[1]); handle_backticks(inner_cmd);
            if (fork() == 0) { exit(run_cmd(parse_cmd(inner_cmd))); }
            wait(0); exit(0);
        }
        close(p[1]); wait(r);
        read(p[0], output, sizeof(output) - 1); close(p[0]);
        for(int i = strlen(output)-1; i >= 0 && strchr("\r\n", output[i]); i--) output[i] = '\0';
        my_strcat(buf, output); my_strcat(buf, rest_of_cmd);
    }
}

/* --- COMMAND EXECUTION --- */
int run_cmd(struct Cmd *cmd) {
    if (cmd == 0) return 0;
    int p[2], r; struct Exe_cmd *ecmd; struct Redir_cmd *rcmd; struct Pipe_cmd *pcmd; struct List_cmd *lcmd;
    switch (cmd->type) {
    case EXEC:
        ecmd = (struct Exe_cmd*)cmd;
        if (ecmd->argv[0] == 0) return 0;
		if (strcmp(ecmd->argv[0], "exit") == 0) exit(0);
		if (strcmp(ecmd->argv[0], "cd") == 0) return handle_cd(ecmd->argc, ecmd->argv);
		if (strcmp(ecmd->argv[0], "pwd") == 0) return handle_pwd(ecmd->argc, ecmd->argv);
        if (strcmp(ecmd->argv[0], "declare") == 0) return handle_declare(ecmd->argc, ecmd->argv);
        if (strcmp(ecmd->argv[0], "unset") == 0) return handle_unset(ecmd->argc, ecmd->argv);
        if (strcmp(ecmd->argv[0], "history") == 0) return handle_history(ecmd->argc, ecmd->argv);
        
        expand_vars(ecmd->argc, ecmd->argv);
        
        char resolved_argv_storage[MAX_ARGS][MAXPATHLEN];
        char* new_argv[MAX_ARGS];

        resolve_path(ecmd->argv[0], CWD, resolved_argv_storage[0]);
        new_argv[0] = resolved_argv_storage[0];

		for (int i = 1; i < ecmd->argc; i++) {
            if (ecmd->argv[i][0] != '-' && strlen(ecmd->argv[i]) > 0) {
                resolve_path(ecmd->argv[i], CWD, resolved_argv_storage[i]);
                new_argv[i] = resolved_argv_storage[i];
            } else {
                new_argv[i] = ecmd->argv[i];
            }
        }
        new_argv[ecmd->argc] = 0;

        char *original_cmd_name = ecmd->argv[0];
        if ((r = spawn(new_argv[0], (const char**)new_argv)) < 0) {
            // BUGFIX 1: Only add ".b" suffix if the original command doesn't already have it.
            int len = strlen(original_cmd_name);
            if (len <= 2 || strcmp(original_cmd_name + len - 2, ".b") != 0) {
                char prog_with_b[MAXPATHLEN + 3];
                strcpy(prog_with_b, new_argv[0]);
                my_strcat(prog_with_b, ".b");
                if ((r = spawn(prog_with_b, (const char**)new_argv)) < 0) {
                    fprintf(2, "%s: command not found\n", original_cmd_name);
                    return 1;
                }
            } else {
                 fprintf(2, "%s: command not found\n", original_cmd_name);
                 return 1;
            }
        }
        return wait(r);
        
    case REDIR:
        rcmd = (struct Redir_cmd*)cmd; close(rcmd->fd); char abs_file_path[MAXPATHLEN];
        resolve_path(rcmd->file, CWD, abs_file_path);
        if (open(abs_file_path, rcmd->mode) != rcmd->fd) {
            fprintf(2, "open %s failed or not on expected fd\n", rcmd->file); return 1;
        } return run_cmd(rcmd->cmd);
    case PIPE:
        pcmd = (struct Pipe_cmd*)cmd; pipe(p);
        if ((r = fork()) == 0) { close(p[0]); dup(p[1], 1); close(p[1]); exit(run_cmd(pcmd->left)); }
        if (fork() == 0) { close(p[1]); dup(p[0], 0); close(p[0]); exit(run_cmd(pcmd->right)); }
        close(p[0]); close(p[1]); wait(r); return wait(0);
    case LIST:
        lcmd = (struct List_cmd*)cmd; if (fork() == 0) exit(run_cmd(lcmd->left));
        wait(0); return run_cmd(lcmd->right);
    case AND:
        lcmd = (struct List_cmd*)cmd; if (fork() == 0) exit(run_cmd(lcmd->left));
        if (wait(0) == 0) return run_cmd(lcmd->right); return 1;
    case OR:
        lcmd = (struct List_cmd*)cmd; if (fork() == 0) exit(run_cmd(lcmd->left));
        if (wait(0) != 0) return run_cmd(lcmd->right); return 0;
    default: fprintf(2, "Unknown command type %d\n", cmd->type); exit(1);
    }
}

// --- MAIN ENTRY ---
void main(int argc, char **argv) {
    static char buf[MAX_CMD_LEN]; int r;
    set_var("PWD", CWD, 0, 1); load_history();
    printf("\n--- MOS Shell (Challenge Ready) ---\n");
    while (1) {
        printf("\n%s$ ", CWD);
        readline(buf, sizeof(buf));
        char *comment = strchr(buf, '#'); if (comment) *comment = '\0';
        char *trimmed_buf = buf; while(*trimmed_buf && strchr(WHITESPACE, *trimmed_buf)) trimmed_buf++;
        if (*trimmed_buf == 0) continue;
        add_to_history(trimmed_buf);
        char cmd_copy[MAX_CMD_LEN]; strcpy(cmd_copy, trimmed_buf);
        handle_backticks(cmd_copy);
        next_free_node = ast_buf;
        if ((r = fork()) == 0) { exit(run_cmd(parse_cmd(cmd_copy))); }
        wait(r);
    }
}
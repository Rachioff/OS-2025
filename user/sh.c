#include <lib.h>
#include <args.h>
#include <fs.h>
#include <string.h>
#include <error.h>
#include <malloc.h>

/* --- Custom Utilities (since standard ctype.h might not be available) --- */
int isalnum(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* --- MACROS AND CONSTANTS --- */
#define MAX_CMD_LEN 1024
#define MAX_ARGS 32
#define MAX_VARS 64
#define VAR_NAME_LEN 17
#define VAR_VALUE_LEN 17
#define MAX_HIST 20
#define HIST_FILE "/.mos_history"

#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|;&"

enum { EXEC = 1, REDIR, PIPE, LIST, AND, OR };

/* --- DATA STRUCTURES --- */
struct Var { char name[VAR_NAME_LEN]; char value[VAR_VALUE_LEN]; u_char is_readonly; };
struct Cmd { int type; };
struct Exe_cmd { int type; char *argv[MAX_ARGS]; int argc; };
struct Redir_cmd { int type; struct Cmd *cmd; char *file; int mode; int fd; };
struct Pipe_cmd { int type; struct Cmd *left; struct Cmd *right; };
struct List_cmd { int type; struct Cmd *left; struct Cmd *right; };

/* --- GLOBAL SHELL STATE --- */
char CWD[MAXPATHLEN] = "/";
struct Var vars[MAX_VARS];
int var_count = 0;
char history[MAX_HIST + 1][MAX_CMD_LEN];
int history_count = 0;
char cur_line_buf[MAX_CMD_LEN];

/* --- FORWARD DECLARATIONS --- */
int run_cmd(struct Cmd *cmd);
struct Cmd *parse_line(char **ps, char *es);
struct Cmd *parse_pipe(char **ps, char *es);
struct Cmd *parse_exec(char **ps, char *es);
struct Cmd *parse_redirs(struct Cmd *cmd, char **ps, char *es);
int get_token(char **ps, char *es, char **q, char **eq);
int peek(char **ps, char *es, char *toks);
struct Cmd *execcmd(void);
struct Cmd *redircmd(struct Cmd *subcmd, char *file, int mode, int fd);
struct Cmd *pipecmd(struct Cmd *left, struct Cmd *right);
struct Cmd *listcmd(struct Cmd *left, struct Cmd *right, int type);
static void resolve_path(const char *path, char *resolved);
const char *getenv(const char *name);
void setenv(const char *name, const char *value, int is_readonly);
void builtin_declare(int argc, char **argv);
void builtin_unset(int argc, char **argv);
void builtin_history(void);
void load_history(void);
void save_history(void);
void add_to_history(const char *line);
void readline(char *buf, u_int n);
void preprocess_cmd(char *buf);
static void reprint_line(const char *buf, int cursor);

/* --- MAIN SHELL LOGIC --- */
int main(int argc, char **argv) {
	static char buf[MAX_CMD_LEN];
	int r;
	load_history();
	setenv("PWD", "/", 0);
	printf("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
	printf("::               MOS Shell (Challenge Edition)             ::\n");
	printf(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
	while (1) {
		printf("\n$ ");
		readline(buf, sizeof(buf));
		if (buf[0] == 0) continue;
		preprocess_cmd(buf);
		add_to_history(buf);
		char* p = buf;
		if ((r = fork()) < 0) panic("fork: %e", r);
		if (r == 0) {
			run_cmd(parse_line(&p, p + strlen(p)));
			exit(0);
		} else {
			wait(r);
		}
	}
	return 0;
}

/* --- COMMAND EXECUTION (AST TRAVERSAL) --- */
int run_cmd(struct Cmd *cmd) {
	if (cmd == 0) return 0;
	int p[2], status = 0;
	struct Exe_cmd *ecmd;
	struct Redir_cmd *rcmd;
	struct Pipe_cmd *pcmd;
	struct List_cmd *lcmd;

	switch (cmd->type) {
	case EXEC:
		ecmd = (struct Exe_cmd *)cmd;
		if (ecmd->argv[0] == 0) exit(0);
		if (strcmp(ecmd->argv[0], "cd") == 0) {
            if (ecmd->argc > 2) { fprintf(2, "Too many args for cd command\n"); return 1; }
            const char *path = (ecmd->argc == 1) ? "/" : ecmd->argv[1];
            char new_cwd[MAXPATHLEN];
            resolve_path(path, new_cwd);
            struct Stat st;
            if (stat(new_cwd, &st) < 0) { fprintf(2, "cd: The directory '%s' does not exist\n", path); return 1; }
            if (!st.st_isdir) { fprintf(2, "cd: '%s' is not a directory\n", path); return 1; }
            strcpy(CWD, new_cwd);
            setenv("PWD", CWD, 0);
            return 0;
        }
		if (strcmp(ecmd->argv[0], "exit") == 0) { exit(0); }
        if (strcmp(ecmd->argv[0], "pwd") == 0) { printf("%s\n", CWD); return 0; }
        if (strcmp(ecmd->argv[0], "declare") == 0) { builtin_declare(ecmd->argc, ecmd->argv); return 0; }
        if (strcmp(ecmd->argv[0], "unset") == 0) { builtin_unset(ecmd->argc, ecmd->argv); return 0; }
        if (strcmp(ecmd->argv[0], "history") == 0) { builtin_history(); return 0; }
		char prog_path_abs[MAXPATHLEN];
		resolve_path(ecmd->argv[0], prog_path_abs);
		int child_pid;
		if ((child_pid = spawn(prog_path_abs, (char **)ecmd->argv)) < 0) {
			char prog_with_b[MAXPATHLEN];
			snprintf(prog_with_b, sizeof(prog_with_b), "%s.b", prog_path_abs);
			if ((child_pid = spawn(prog_with_b, (char **)ecmd->argv)) < 0) {
				fprintf(2, "command not found: %s\n", ecmd->argv[0]);
				return 1;
			}
		}
		return wait(child_pid);
	case REDIR:
		rcmd = (struct Redir_cmd *)cmd;
		char file_path_abs[MAXPATHLEN];
		resolve_path(rcmd->file, file_path_abs);
		close(rcmd->fd);
		int r;
		if ((r = open(file_path_abs, rcmd->mode)) < 0) {
			fprintf(2, "open %s: %e\n", rcmd->file, r);
			return 1;
		}
		if (r != rcmd->fd) { dup(r, rcmd->fd); close(r); }
		return run_cmd(rcmd->cmd);
	case PIPE:
		pcmd = (struct Pipe_cmd *)cmd;
		if (pipe(p) < 0) panic("pipe");
		int pid1;
		if ((pid1 = fork()) == 0) { close(p[0]); dup(p[1], 1); close(p[1]); exit(run_cmd(pcmd->left)); }
		int pid2;
		if ((pid2 = fork()) == 0) { close(p[1]); dup(p[0], 0); close(p[0]); exit(run_cmd(pcmd->right)); }
		close(p[0]); close(p[1]);
		wait(pid1);
		return wait(pid2);
	case LIST:
		lcmd = (struct List_cmd *)cmd;
		if (fork() == 0) exit(run_cmd(lcmd->left));
		wait(0);
		return run_cmd(lcmd->right);
	case AND:
		lcmd = (struct List_cmd *)cmd;
		if (fork() == 0) exit(run_cmd(lcmd->left));
		status = wait(0);
		if (status == 0) return run_cmd(lcmd->right);
		return status;
	case OR:
		lcmd = (struct List_cmd *)cmd;
		if (fork() == 0) exit(run_cmd(lcmd->left));
		status = wait(0);
		if (status != 0) return run_cmd(lcmd->right);
		return status;
	default: panic("run_cmd: bad cmd type");
	}
	return 0;
}

/* --- PATH RESOLUTION & PRE-PROCESSING --- */
static void resolve_path(const char *path, char *resolved) {
	if (path[0] == '/') { strcpy(resolved, path); }
    else {
		if (strlen(CWD) == 1 && CWD[0] == '/') sprintf(resolved, "/%s", path);
		else sprintf(resolved, "%s/%s", CWD, path);
	}
	char *stack[MAXPATHLEN / 2];
	int top = -1;
	char temp_path[MAXPATHLEN];
	strcpy(temp_path, resolved);
	char *p = temp_path;
	while (*p == '/') p++;
	char *token = strtok(p, "/");
	while (token != NULL) {
		if (strcmp(token, ".") == 0) {}
        else if (strcmp(token, "..") == 0) { if (top > -1) top--; }
        else { stack[++top] = token; }
		token = strtok(NULL, "/");
	}
	if (top == -1) { strcpy(resolved, "/"); }
    else {
		resolved[0] = '\0';
		for (int i = 0; i <= top; i++) { strcat(resolved, "/"); strcat(resolved, stack[i]); }
	}
}
void preprocess_cmd(char *buf) {
    char *comment = strchr(buf, '#');
    if (comment) *comment = '\0';
}

/* --- PARSER AND LEXER --- */
struct Cmd *execcmd(void) {
	struct Exe_cmd *cmd = (struct Exe_cmd *)malloc(sizeof(*cmd));
	memset(cmd, 0, sizeof(*cmd));
	cmd->type = EXEC;
	return (struct Cmd *)cmd;
}
struct Cmd *redircmd(struct Cmd *subcmd, char *file, int mode, int fd) {
	struct Redir_cmd *cmd = (struct Redir_cmd *)malloc(sizeof(*cmd));
	memset(cmd, 0, sizeof(*cmd));
	cmd->type = REDIR; cmd->cmd = subcmd; cmd->file = file; cmd->mode = mode; cmd->fd = fd;
	return (struct Cmd *)cmd;
}
struct Cmd *pipecmd(struct Cmd *left, struct Cmd *right) {
	struct Pipe_cmd *cmd = (struct Pipe_cmd *)malloc(sizeof(*cmd));
	memset(cmd, 0, sizeof(*cmd));
	cmd->type = PIPE; cmd->left = left; cmd->right = right;
	return (struct Cmd *)cmd;
}
struct Cmd *listcmd(struct Cmd *left, struct Cmd *right, int type) {
	struct List_cmd *cmd = (struct List_cmd *)malloc(sizeof(*cmd));
	memset(cmd, 0, sizeof(*cmd));
	cmd->type = type; cmd->left = left; cmd->right = right;
	return (struct Cmd *)cmd;
}
struct Cmd *parse_line(char **ps, char *es) {
	struct Cmd *cmd = parse_pipe(ps, es);
	while (peek(ps, es, ";")) { get_token(ps, es, 0, 0); cmd = listcmd(cmd, parse_line(ps, es), LIST); }
	return cmd;
}
struct Cmd *parse_pipe(char **ps, char *es) {
    struct Cmd *cmd = parse_exec(ps, es);
    if (peek(ps, es, "|")) {
		get_token(ps, es, 0, 0);
		if (peek(ps, es, "|")) { get_token(ps, es, 0, 0); cmd = listcmd(cmd, parse_pipe(ps, es), OR); }
        else { cmd = pipecmd(cmd, parse_pipe(ps, es)); }
    } else if (peek(ps, es, "&")) {
		get_token(ps, es, 0, 0);
		if (peek(ps, es, "&")) { get_token(ps, es, 0, 0); cmd = listcmd(cmd, parse_pipe(ps, es), AND); }
	}
    return cmd;
}
struct Cmd *parse_exec(char **ps, char *es) {
	struct Exe_cmd *cmd; struct Cmd *ret;
	char *q, *eq; int tok, argc = 0;
	ret = execcmd();
	cmd = (struct Exe_cmd *)ret;
	ret = parse_redirs(ret, ps, es);
	while (!peek(ps, es, "|;&")) {
		if ((tok = get_token(ps, es, &q, &eq)) == 0) break;
		if (tok != 'a') { fprintf(2, "Syntax error\n"); exit(0); }
		cmd->argv[argc++] = q;
		if (argc >= MAX_ARGS) { fprintf(2, "Too many args\n"); exit(0); }
		ret = parse_redirs(ret, ps, es);
	}
	cmd->argv[argc] = 0; cmd->argc = argc;
	return ret;
}
struct Cmd *parse_redirs(struct Cmd *cmd, char **ps, char *es) {
	int tok; char *q, *eq;
	while (peek(ps, es, "<>")) {
		tok = get_token(ps, es, 0, 0);
		if (get_token(ps, es, &q, &eq) != 'a') { fprintf(2, "Missing file for redirection\n"); exit(0); }
		if (tok == '<') { cmd = redircmd(cmd, q, O_RDONLY, 0); }
        else if (tok == '>') {
			if (peek(ps, es, ">")) { get_token(ps, es, 0, 0); cmd = redircmd(cmd, q, O_WRONLY | O_CREAT | O_APPEND, 1); }
            else { cmd = redircmd(cmd, q, O_WRONLY | O_CREAT | O_TRUNC, 1); }
		}
	}
	return cmd;
}
int get_token(char **ps, char *es, char **q, char **eq) {
	char *s = *ps;
	while (s < es && strchr(WHITESPACE, *s)) s++;
	if (q) *q = s;
	int ret = *s;
	switch (*s) {
	case 0: break;
	case '|': case ';': case '<': case '&': s++; break;
	case '>': s++; if (*s == '>') { ret = '+'; s++; } break;
	default:
		ret = 'a';
		while (s < es && !strchr(WHITESPACE, *s) && !strchr(SYMBOLS, *s)) s++;
		break;
	}
	if (eq) *eq = s;
	char *whitespace_ptr = s;
	while (whitespace_ptr < es && strchr(WHITESPACE, *whitespace_ptr)) *whitespace_ptr++ = 0;
	*ps = s;
	return ret;
}
int peek(char **ps, char *es, char *toks) {
	char *s = *ps;
	while (s < es && strchr(WHITESPACE, *s)) s++;
	*ps = s;
	return *s && strchr(toks, *s);
}

/* --- ADVANCED READLINE --- */
static void reprint_line(const char *buf, int cursor) {
    // \x1b[2K clears the entire line, \r moves cursor to beginning
    printf("\x1b[2K\r$ %s", buf);
    // Move cursor to correct position
    printf("\x1b[1000D"); // Move far left (to beginning of line)
    if (cursor + 2 > 0) {
        printf("\x1b[%dC", cursor + 2); // Move cursor forward from beginning
    }
}

void readline(char *buf, u_int n) {
    int i = 0, cursor = 0, hist_idx = history_count;
    buf[0] = '\0'; // Ensure buffer is initially empty

    while (1) {
        int c = getchar();
        if (c < 0) continue;
        if (c == '\r' || c == '\n') { printf("\n"); buf[i] = 0; return; }
        switch (c) {
            case '\b': case 0x7f:
                if (cursor > 0) {
                    memmove(&buf[cursor - 1], &buf[cursor], i - cursor);
                    cursor--; i--; buf[i] = '\0'; reprint_line(buf, cursor);
                } break;
            case 0x1b: // Escape sequence
                if (getchar() != '[') break;
                c = getchar();
                if (c == 'D' && cursor > 0) { cursor--; reprint_line(buf, cursor); }
                else if (c == 'C' && cursor < i) { cursor++; reprint_line(buf, cursor); }
                else if (c == 'A') { // Up
                    if (hist_idx == history_count) strncpy(cur_line_buf, buf, sizeof(cur_line_buf)-1);
                    if (hist_idx > 0) { hist_idx--; strcpy(buf, history[hist_idx]); i = cursor = strlen(buf); reprint_line(buf, cursor); }
                } else if (c == 'B') { // Down
                    if (hist_idx < history_count) {
                        hist_idx++;
                        if (hist_idx == history_count) strcpy(buf, cur_line_buf); else strcpy(buf, history[hist_idx]);
                        i = cursor = strlen(buf); reprint_line(buf, cursor);
                    }
                } break;
            case 1: cursor = 0; reprint_line(buf, cursor); break; // Ctrl-A
            case 5: cursor = i; reprint_line(buf, cursor); break; // Ctrl-E
            case 11: buf[cursor] = '\0'; i = cursor; reprint_line(buf, cursor); break; // Ctrl-K
            case 21: memmove(buf, &buf[cursor], i - cursor + 1); i -= cursor; cursor = 0; reprint_line(buf, cursor); break; // Ctrl-U
            case 23: // Ctrl-W
                if (cursor > 0) {
                    int end = cursor;
                    while (cursor > 0 && strchr(WHITESPACE, buf[cursor - 1])) cursor--;
                    while (cursor > 0 && !strchr(WHITESPACE, buf[cursor - 1])) cursor--;
                    memmove(&buf[cursor], &buf[end], i - end + 1);
                    i -= (end - cursor);
                    reprint_line(buf, cursor);
                } break;
            default:
                if (i < n - 1) {
                    memmove(&buf[cursor + 1], &buf[cursor], i - cursor + 1);
                    buf[cursor] = c;
                    i++; cursor++;
                    reprint_line(buf, cursor);
                } break;
        }
    }
}


/* --- VARIABLE & HISTORY MANAGEMENT --- */
const char *getenv(const char *name) {
	for (int i = 0; i < var_count; i++) if (strcmp(vars[i].name, name) == 0) return vars[i].value;
	return NULL;
}
void setenv(const char *name, const char *value, int is_readonly) {
	for (int i = 0; i < var_count; i++) {
		if (strcmp(vars[i].name, name) == 0) {
			if (vars[i].is_readonly) { fprintf(2, "declare: cannot modify readonly variable %s\n", name); return; }
			strcpy(vars[i].value, value);
			if (is_readonly) vars[i].is_readonly = 1;
			return;
		}
	}
	if (var_count < MAX_VARS) {
		strcpy(vars[var_count].name, name);
		strcpy(vars[var_count].value, value);
		vars[var_count].is_readonly = is_readonly;
		var_count++;
	}
}
void builtin_declare(int argc, char **argv) {
	if (argc == 1) {
		for (int i = 0; i < var_count; i++) printf("%s=%s\n", vars[i].name, vars[i].value);
		return;
	}
	int is_readonly = 0, i = 1;
	// Note: export (-x) is not implemented as environment is not inherited by spawn
	if (argv[1][0] == '-') { if (strchr(argv[1], 'r')) is_readonly = 1; i++; }
	for (; i < argc; i++) {
		char *name_val = argv[i];
        char *name = name_val;
		char *value = strchr(name_val, '=');
		if (value) {
			*value = '\0'; // Split string
			value++;
		} else {
			value = "";
		}
		setenv(name, value, is_readonly);
	}
}
void builtin_unset(int argc, char **argv) {
    if (argc != 2) { fprintf(2, "unset: incorrect number of arguments\n"); return; }
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, argv[1]) == 0) {
            if (vars[i].is_readonly) { fprintf(2, "unset: cannot unset readonly variable %s\n", argv[1]); return; }
            for (int j = i; j < var_count - 1; j++) vars[j] = vars[j + 1];
            var_count--; return;
        }
    }
}
void load_history() {
    int fd = open(HIST_FILE, O_RDONLY);
    if (fd < 0) return;
    char buf[MAX_CMD_LEN * MAX_HIST] = {0};
    readn(fd, buf, sizeof(buf) - 1);
    close(fd);
    char *p = buf, *line;
    while ((line = strtok(p, "\n")) != NULL) {
        if (history_count < MAX_HIST) strcpy(history[history_count++], line);
        p = NULL;
    }
}
void save_history() {
    int fd = open(HIST_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    for (int i = 0; i < history_count; i++) fprintf(fd, "%s\n", history[i]);
    close(fd);
}
void add_to_history(const char *line) {
    if (line[0] == 0 || (history_count > 0 && strcmp(history[history_count - 1], line) == 0)) return;
    if (history_count < MAX_HIST) strcpy(history[history_count++], line);
    else {
        memmove(history[0], history[1], (MAX_HIST - 1) * MAX_CMD_LEN);
        strcpy(history[MAX_HIST - 1], line);
    }
    save_history();
}
void builtin_history(void) {
    for (int i = 0; i < history_count; i++) printf("  %d  %s\n", i + 1, history[i]);
}
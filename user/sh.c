#include <lib.h>
#include <args.h>
#include <fs.h>

// 声明全局变量和函数
extern char g_cwd[MAXPATHLEN];
extern void resolve_path(const char *path, char *resolved_path);

// --- START: Self-implemented standard functions ---
// 安全的字符串拼接
char* strcat(char* dest, const char* src) {
    char* ptr = dest + strlen(dest);
    while (*src != '\0') *ptr++ = *src++;
    *ptr = '\0';
    return dest;
}

// 支持重叠内存的安全移动
void* my_memmove(void* dst, const void* src, u_int n) {
    char* d = dst;
    const char* s = src;
    if (d == s) return d;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}
// --- END: Self-implemented standard functions ---

// 环境变量存储
#define MAX_VARS 64 
#define VAR_MAX_NAME_LEN 16
#define VAR_MAX_VALUE_LEN 16
struct Var { char name[VAR_MAX_NAME_LEN + 1]; char value[VAR_MAX_VALUE_LEN + 1]; u_char is_exported; u_char is_readonly; u_char in_use; };
struct Var vars[MAX_VARS];

// 行编辑与历史指令存储
#define BUF_MAX 1024
char line_buffer[BUF_MAX];
int line_len = 0, line_pos = 0;
#define HISTFILESIZE 20
char history[HISTFILESIZE][BUF_MAX];
int history_count = 0, history_pos = 0;


// --- START: AST Parser and Executor ---

// Token类型
#define T_EOF   0
#define T_WORD  1
#define T_REDIR_IN  '<'
#define T_REDIR_OUT '>'
#define T_REDIR_APP 'a' // 代表 >>
#define T_PIPE  '|'
#define T_AND   '&' // 代表 &&
#define T_OR    'o' // 代表 ||
#define T_SEMI  ';'

// AST 节点类型
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4 // For semicolon ;
#define AND   5 // For &&
#define OR    6 // For ||

// AST 节点结构体
struct cmd { int type; };
struct execcmd { int type; int argc; char *argv[MAX_ARGS]; };
struct redircmd { int type; struct cmd *cmd; char *file; int mode; int fd; };
struct pipecmd { int type; struct cmd *left; struct cmd *right; };
struct listcmd { int type; struct cmd *left; struct cmd *right; };
struct logiccmd { int type; struct cmd *left; struct cmd *right; };


// --- FIX: 使用union来定义内存池，确保大小正确 ---
union cmd_union {
    struct execcmd exe;
    struct redircmd redir;
    struct pipecmd pipe;
    struct listcmd list;
    struct logiccmd logic;
};

// --- 静态内存池 ---
#define CMD_POOL_SIZE 50
union cmd_union cmd_pool[CMD_POOL_SIZE];
int next_cmd_node = 0;

#define STR_POOL_SIZE 4096
char str_pool[STR_POOL_SIZE];
int next_str_pos = 0;

void reset_pools() {
    next_cmd_node = 0;
    next_str_pos = 0;
}

// --- FIX: node_alloc现在可以正确工作，因为它总是分配一个足够大的union块 ---
void* node_alloc(int size) {
    if(next_cmd_node >= CMD_POOL_SIZE) user_panic("AST node pool exhausted");
    // size参数被忽略，因为我们总是分配一个union大小的块
    return &cmd_pool[next_cmd_node++];
}

char* str_alloc(int n) {
    if (next_str_pos + n > STR_POOL_SIZE) user_panic("String pool exhausted");
    char* ret = &str_pool[next_str_pos];
    next_str_pos += n;
    return ret;
}

// Parser state
char *ps, *es;
int tok;
char *q, *eq;

// --- Forward declarations for parser ---
struct cmd* parse_cmd(char*s);
struct cmd* parse_line(void);
struct cmd* parse_pipe(void);
struct cmd* parse_with_redir(void);
struct cmd* parse_exec(void);
int run_cmd(struct cmd* cmd);

// AST节点构造函数
struct cmd* execcmd(void) {
    struct execcmd *cmd = (struct execcmd*)node_alloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = EXEC;
    return (struct cmd*)cmd;
}
struct cmd* redircmd(struct cmd *subcmd, char *file, int type) {
    struct redircmd *cmd = (struct redircmd*)node_alloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = REDIR;
    cmd->cmd = subcmd;
    cmd->file = file;
    if (type == T_REDIR_OUT) cmd->mode = O_WRONLY | O_CREAT | O_TRUNC;
    else if (type == T_REDIR_APP) cmd->mode = O_WRONLY | O_CREAT | O_APPEND;
    else cmd->mode = O_RDONLY;
    cmd->fd = (type == T_REDIR_IN) ? 0 : 1;
    return (struct cmd*)cmd;
}
struct cmd* pipecmd(struct cmd *left, struct cmd *right) {
    struct pipecmd *cmd = (struct pipecmd*)node_alloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = PIPE;
    cmd->left = left;
    cmd->right = right;
    return (struct cmd*)cmd;
}
struct cmd* listcmd(struct cmd *left, struct cmd *right) {
    struct listcmd *cmd = (struct listcmd*)node_alloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = LIST;
    cmd->left = left;
    cmd->right = right;
    return (struct cmd*)cmd;
}
struct cmd* logiccmd(int type, struct cmd *left, struct cmd *right) {
    struct logiccmd *cmd = (struct logiccmd*)node_alloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = type;
    cmd->left = left;
    cmd->right = right;
    return (struct cmd*)cmd;
}

// --- Lexer & Parser ---

int peek(char *s, char *es, char *toks) {
    while(s < es && strchr(" \t\r\n", *s)) s++;
    return s < es && strchr(toks, *s);
}
int gettoken(char **ps_ptr, char *es, char **q_ptr, char **eq_ptr) {
    char *s = *ps_ptr;
    int ret;
    while(s < es && strchr(" \t\r\n", *s)) s++;
    if(q_ptr) *q_ptr = s;
    ret = *s;
    switch(*s) {
    case 0: break;
    case '|': s++; if (*s == '|') { ret = T_OR; s++; } break;
    case '&': s++; if (*s == '&') { ret = T_AND; s++; } break;
    case ';': case '<': s++; break;
    case '>': s++; if (*s == '>') { ret = T_REDIR_APP; s++; } break;
    default:
        ret = T_WORD;
        while(s < es && !strchr(" \t\r\n<|&;>", *s)) s++;
        break;
    }
    if(eq_ptr) *eq_ptr = s;
    while(s < es && strchr(" \t\r\n", *s)) s++;
    *ps_ptr = s;
    return ret;
}

struct cmd* parse_exec(void) {
    struct execcmd *cmd = (struct execcmd*)execcmd();
    cmd->argc = 0;
    while(!peek(ps, es, "<>|&;")){
        int tok_type, len;
        if((tok_type = gettoken(&ps, es, &q, &eq)) == T_EOF) break;
        if(tok_type != T_WORD) user_panic("parse_exec syntax error: expected word");
        len = eq - q;
        cmd->argv[cmd->argc] = str_alloc(len + 1);
        safe_strncpy(cmd->argv[cmd->argc], q, len + 1);
        cmd->argc++;
    }
    cmd->argv[cmd->argc] = 0;
    return (struct cmd*)cmd;
}
struct cmd* parse_with_redir(void) {
    struct cmd* cmd = parse_exec();
    while(peek(ps, es, "<>")){
        int tok_type = gettoken(&ps, es, 0, 0);
        if(gettoken(&ps, es, &q, &eq) != T_WORD)
            user_panic("syntax error: expected filename after redirection");
        int len = eq - q;
        char* file = str_alloc(len + 1);
        safe_strncpy(file, q, len + 1);
        if (tok_type == '>') { // Simple redirection ">"
             cmd = redircmd(cmd, file, T_REDIR_OUT);
        } else if (tok_type == T_REDIR_APP) { // Append redirection ">>"
             cmd = redircmd(cmd, file, T_REDIR_APP);
        } else { // Input redirection "<"
             cmd = redircmd(cmd, file, T_REDIR_IN);
        }
    }
    return cmd;
}
struct cmd* parse_pipe(void) {
    struct cmd *cmd = parse_with_redir();
    if(peek(ps, es, "|")){
        gettoken(&ps, es, 0, 0);
        cmd = pipecmd(cmd, parse_pipe());
    }
    return cmd;
}
struct cmd* parse_and_or(void){
    struct cmd *cmd = parse_pipe();
    while(peek(ps, es, "&|")){
        int t = gettoken(&ps, es, 0, 0);
        if(t == T_AND) cmd = logiccmd(AND, cmd, parse_pipe());
        else if(t == T_OR) cmd = logiccmd(OR, cmd, parse_pipe());
    }
    return cmd;
}
struct cmd* parse_list(void){
    struct cmd *cmd = parse_and_or();
    while(peek(ps, es, ";")){
        gettoken(&ps, es, 0, 0);
        if (ps >= es || *ps == '\0') break; // Stop if we hit end of string
        cmd = listcmd(cmd, parse_and_or());
    }
    return cmd;
}
struct cmd* parse_cmd(char *s) {
    ps = s;
    es = s + strlen(s);
    struct cmd *cmd = parse_list();
    peek(ps, es, "");
    if(ps != es) {
        printf("syntax error: unexpected characters at end of command: %s\n", ps);
        return NULL;
    }
    return cmd;
}

// Forward declaration for handlers
int handle_cd(int, char**);
int handle_pwd(int, char**);
int handle_declare(int, char**);
int handle_unset(int, char**);
int handle_history(int, char**);


// --- Executor ---
int run_cmd(struct cmd* cmd) {
    if (cmd == 0) return 0;
    
    int p[2], status = 0;
    int r;
    struct execcmd *ecmd;
    struct redircmd *rcmd;
    struct listcmd *lcmd;
    struct pipecmd *pcmd;
    struct logiccmd *logcmd;

    switch(cmd->type){
    case EXEC:
        ecmd = (struct execcmd*)cmd;
        if (ecmd->argv[0] == 0) return 0;
        if (strcmp(ecmd->argv[0], "cd") == 0) return handle_cd(ecmd->argc, ecmd->argv);
        if (strcmp(ecmd->argv[0], "pwd") == 0) return handle_pwd(ecmd->argc, ecmd->argv);
        if (strcmp(ecmd->argv[0], "exit") == 0) exit(0);
        if (strcmp(ecmd->argv[0], "declare") == 0) return handle_declare(ecmd->argc, ecmd->argv);
        if (strcmp(ecmd->argv[0], "unset") == 0) return handle_unset(ecmd->argc, ecmd->argv);
        if (strcmp(ecmd->argv[0], "history") == 0) return handle_history(ecmd->argc, ecmd->argv);
        
        char prog_path_abs[MAXPATHLEN];
        resolve_path(ecmd->argv[0], prog_path_abs);

        if ((r = spawn(prog_path_abs, (const char**)ecmd->argv)) < 0) {
            strcat(prog_path_abs, ".b");
            if ((r = spawn(prog_path_abs, (const char**)ecmd->argv)) < 0) {
                printf("command not found: %s\n", ecmd->argv[0]);
                return -1;
            }
        }
        if (r >= 0) status = wait(r);
        return status;

    case REDIR:
        rcmd = (struct redircmd*)cmd;
        // Fork a child process to handle redirection
        if ((r = fork()) < 0) user_panic("fork failed");
        if (r == 0) { // Child process
            close(rcmd->fd); // Close standard in/out
            char resolved_file[MAXPATHLEN];
            resolve_path(rcmd->file, resolved_file);
            int fd_opened = open(resolved_file, rcmd->mode); // Open the specified file
            if (fd_opened < 0) {
                printf("open %s failed\n", rcmd->file);
                exit(1);
            }
            // After setting up redirection, execute the sub-command
            status = run_cmd(rcmd->cmd);
            exit(status);
        }
        // Parent waits for the child to complete
        status = wait(r);
        break;

    case LIST:
        lcmd = (struct listcmd*)cmd;
        run_cmd(lcmd->left);
        status = run_cmd(lcmd->right);
        break;

    case PIPE:
        pcmd = (struct pipecmd*)cmd;
        if(pipe(p) < 0) user_panic("pipe failed");
        
        int pid1;
        if((pid1 = fork()) == 0){
            close(1);
            dup(p[1], 1);
            close(p[0]); close(p[1]);
            status = run_cmd(pcmd->left);
            exit(status);
        }
        
        int pid2;
        if((pid2 = fork()) == 0){
            close(0);
            dup(p[0], 0);
            close(p[0]); close(p[1]);
            status = run_cmd(pcmd->right);
            exit(status);
        }
        
        close(p[0]); close(p[1]);
        wait(pid1); 
        status = wait(pid2);
        break;

    case AND:
        logcmd = (struct logiccmd*)cmd;
        if (run_cmd(logcmd->left) == 0) status = run_cmd(logcmd->right);
        else status = 1;
        break;

    case OR:
        logcmd = (struct logiccmd*)cmd;
        if (run_cmd(logcmd->left) != 0) status = run_cmd(logcmd->right);
        else status = 0;
        break;

    default: 
        user_panic("unimplemented command type %d", cmd->type);
    }
    return status;
}

// --- Backtick Handling ---
void handle_backticks(char* dst, const char* src, int dst_size) {
    const char *s = src;
    char *d = dst;
    char *d_end = dst + dst_size - 1;
    while(*s && d < d_end) {
        if(*s != '`') { *d++ = *s++; continue; }
        
        s++; // Skip the first '`'
        const char *sub_cmd_start = s;
        while(*s && *s != '`') s++;
        
        if(!*s) { *d++ = '`'; s = sub_cmd_start; continue; }
        
        char sub_cmd[BUF_MAX];
        int sub_len = s - sub_cmd_start;
        safe_strncpy(sub_cmd, sub_cmd_start, sub_len + 1);
        s++; // Skip the closing '`'
        
        int p[2]; pipe(p);
        int pid;
        if ((pid = fork()) == 0) {
            // Child process for backtick execution
            reset_pools(); // Use fresh pools for sub-command
            close(p[0]);
            dup(p[1], 1);
            close(p[1]);
            struct cmd* parsed_sub_cmd = parse_cmd(sub_cmd);
            if (parsed_sub_cmd) run_cmd(parsed_sub_cmd);
            exit(0);
        }
        
        close(p[1]);
        wait(pid);
        
        char c;
        int last_char_is_space = 0;
        while(d < d_end && read(p[0], &c, 1) > 0) {
            if (c == '\n' || c == '\r') {
                if (!last_char_is_space) {
                    *d++ = ' ';
                    last_char_is_space = 1;
                }
            } else {
                *d++ = c;
                last_char_is_space = 0;
            }
        }

        if (d > dst && *(d-1) == ' ') d--; // Trim trailing space
        
        close(p[0]);
    }
    *d = '\0';
}

// All other helper functions...
void expand_vars(char*, const char*, int);
void redraw_line(const char*);
void insert_char(char);
void delete_char();
void backspace_char();
char read_char();
void readline_rich(const char*, char*);
void add_to_history(const char*);
void save_history();
int readline_from_fd(int, char*, int);
void load_history();


// --- Main Loop ---
int main() { 
    static char buf[BUF_MAX];
    static char backticked_buf[BUF_MAX * 2];
    static char expanded_buf[BUF_MAX * 4];
    char prompt[MAXPATHLEN + 4];

    strcpy(g_cwd, "/");

    for (int i = 0; i < MAX_VARS; i++) {
        vars[i].in_use = 0;
    }

    load_history();

    printf("\n"); 

    for (;;) {
        reset_pools();

        strcpy(prompt, g_cwd);
        strcat(prompt, "$ ");

        readline_rich(prompt, buf);

        if (buf[0] == '\0') continue;

        add_to_history(buf);

        handle_backticks(backticked_buf, buf, sizeof(backticked_buf));

        expand_vars(expanded_buf, backticked_buf, sizeof(expanded_buf));

        char* comment_start = strchr(expanded_buf, '#');
        if (comment_start != NULL) *comment_start = '\0';

        if (expanded_buf[0] == '\0') continue;

        struct cmd* parsed_cmd = parse_cmd(expanded_buf);

        if (parsed_cmd) {
            run_cmd(parsed_cmd);
        }
    }
    return 0;
}


// --- Function Implementations ---
int handle_cd(int argc, char **argv) {
    if (argc > 2) { 
        printf("Too many args for cd command\n"); 
        return 1; 
    }
    const char *target_path = (argc == 1) ? "/" : argv[1];
    char resolved[MAXPATHLEN]; 
    resolve_path(target_path, resolved);
    struct Stat st;
    if (stat(resolved, &st) < 0) { 
        printf("cd: The directory '%s' does not exist\n", (argc == 1) ? "/" : argv[1]); 
        return 1; 
    }
    if (!st.st_isdir) { 
        printf("cd: '%s' is not a directory\n", (argc == 1) ? "/" : argv[1]); 
        return 1; 
    }
    strcpy(g_cwd, resolved);
    
    // Save current working directory for other programs
    int fd = open("/.cwd", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0) {
        write(fd, g_cwd, strlen(g_cwd));
        close(fd);
    }
    
    return 0;
}

int handle_pwd(int argc, char **argv) {
    if (argc != 1) { 
        printf("pwd: expected 0 arguments; got %d\n", argc - 1); 
        return 2; 
    }
    printf("%s\n", g_cwd); 
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
            else if (argv[i][j] == 'r') readonly_flag = 1;
        }
        i++;
    }
    if (i >= argc) return 0;
    char *eq_ptr = strchr(argv[i], '=');
    char var_name[VAR_MAX_NAME_LEN + 1];
    char var_value[VAR_MAX_VALUE_LEN + 1] = "";
    if (eq_ptr != NULL) {
        int name_len = eq_ptr - argv[i];
        safe_strncpy(var_name, argv[i], name_len + 1);
        safe_strncpy(var_value, eq_ptr + 1, VAR_MAX_VALUE_LEN + 1);
    } else {
        safe_strncpy(var_name, argv[i], VAR_MAX_NAME_LEN + 1);
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
    if (export_flag) v->is_exported = 1; 
    if (readonly_flag) v->is_readonly = 1;
    return 0;
}
int handle_unset(int argc, char **argv) {
    if (argc != 2) { printf("unset: expected 1 argument\n"); return 1; }
    struct Var *v = find_var(argv[1]);
    if (v == NULL) return 0;
    if (v->is_readonly) { printf("unset: cannot unset readonly variable '%s'\n", argv[1]); return 1; }
    v->in_use = 0; return 0;
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
            case '\n': case '\r': strcpy(dst_buf, line_buffer); return;
            case 0x7f: backspace_char(); break;
            case '\x1b':
                if (read_char() == '[') {
                    char next_c = read_char();
                     if (next_c == 'A') { // Up arrow
                        if (history_pos > 0) {
                            history_pos--; 
                            strcpy(line_buffer, history[history_pos % HISTFILESIZE]);
                            line_len = strlen(line_buffer); line_pos = line_len;
                        }
                    } else if (next_c == 'B') { // Down arrow
                        if (history_pos < history_count) {
                            history_pos++;
                            if (history_pos == history_count) line_buffer[0] = '\0';
                            else strcpy(line_buffer, history[history_pos % HISTFILESIZE]);
                            line_len = strlen(line_buffer); line_pos = line_len;
                        }
                    } else if (next_c == 'D' && line_pos > 0) line_pos--; // Left
                    else if (next_c == 'C' && line_pos < line_len) line_pos++; // Right
                }
                break;
            case 0x01: line_pos = 0; break; // Ctrl-A
            case 0x05: line_pos = line_len; break; // Ctrl-E
            case 0x0b: line_buffer[line_pos] = '\0'; line_len = line_pos; break; // Ctrl-K
            case 0x15: // Ctrl-U
                if(line_pos > 0){
                    my_memmove(&line_buffer[0], &line_buffer[line_pos], line_len - line_pos + 1);
                    line_len -= line_pos; line_pos = 0;
                }
                break;
            case 0x17: { // Ctrl-W
                int prev_pos = line_pos;
                while (line_pos > 0 && line_buffer[line_pos - 1] == ' ') line_pos--;
                while (line_pos > 0 && line_buffer[line_pos - 1] != ' ') line_pos--;
                int num_to_delete = prev_pos - line_pos;
                if(num_to_delete > 0){
                    my_memmove(&line_buffer[line_pos], &line_buffer[prev_pos], line_len - prev_pos + 1);
                    line_len -= num_to_delete;
                }
                break;
            }
            default: if (c >= 0x20 && c < 0x7f) insert_char(c); break;
        }
        redraw_line(prompt);
    }
}
void add_to_history(const char* cmd) {
    if (cmd[0] == '\0') return;
	char *temp = (char*)cmd;
	int is_space = 1;
	while(*temp) { if(*temp != ' ' && *temp != '\t') { is_space = 0; break; } temp++; }
	if(is_space) return;
    if (history_count > 0 && strcmp(history[(history_count - 1) % HISTFILESIZE], cmd) == 0) return;
    
    if (history_count >= HISTFILESIZE) {
        // Shift history up
        for(int i = 0; i < HISTFILESIZE - 1; i++) {
            strcpy(history[i], history[i+1]);
        }
        strcpy(history[HISTFILESIZE - 1], cmd);
    } else {
        strcpy(history[history_count], cmd);
        history_count++;
    }
    save_history();
}

void save_history() {
    int fd;
	if ((fd = open("/.mos_history", O_WRONLY | O_CREAT | O_TRUNC)) < 0) return;
	for (int i = 0; i < history_count; i++) {
		write(fd, history[i], strlen(history[i]));
		write(fd, "\n", 1);
	}
	close(fd);
}

int handle_history(int argc, char **argv) {
    if (argc > 1) { printf("history: too many arguments\n"); return 1; }
    for (int i = 0; i < history_count; i++) printf("  %d\t%s\n", i + 1, history[i]);
    return 0;
}
int readline_from_fd(int fd, char *buf, int size) {
    int i = 0; char c = 0;
    while (i < size - 1) {
        if (read(fd, &c, 1) != 1) { i = -1; break; }
        if (c == '\n' || c == '\r') break;
        buf[i++] = c;
    }
    if (i >= 0) buf[i] = '\0';
    return i;
}
void load_history() {
    int fd = open("/.mos_history", O_RDONLY);
    if (fd < 0) return;
    history_count = 0;
    while(history_count < HISTFILESIZE) {
        if (readline_from_fd(fd, history[history_count], BUF_MAX) < 0) break;
        if (history[history_count][0] != '\0') history_count++;
        else break;
    }
    close(fd);
}
#include <lib.h>
#include <args.h>
#include <fs.h>

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
int history_count = 0; // 当前历史记录数量
int history_pos = 0;   // 导航时的逻辑位置
int history_start = 0; // 循环缓冲区的起始物理索引


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

void* node_alloc(int size) {
    if(next_cmd_node >= CMD_POOL_SIZE) user_panic("AST node pool exhausted");
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
char *q, *eq;

// --- Forward declarations for parser ---
struct cmd* parse_cmd(char*s);
struct cmd* parse_list(void);
struct cmd* parse_and_or(void);
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
        if (tok_type == '>') { 
             cmd = redircmd(cmd, file, T_REDIR_OUT);
        } else if (tok_type == T_REDIR_APP) { 
             cmd = redircmd(cmd, file, T_REDIR_APP);
        } else {
             cmd = redircmd(cmd, file, T_REDIR_IN);
        }
    }
    return cmd;
}
struct cmd* parse_pipe(void) {
    struct cmd *cmd = parse_with_redir();
    char* next_char = ps;
    while(*next_char && strchr(" \t\r\n", *next_char)) next_char++;
    
    if (*next_char == '|' && *(next_char + 1) != '|') {
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
        if (ps >= es || *ps == '\0') break;
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

int try_spawn_and_wait(char *prog, const char **argv) {
    int r;
    char path_with_b[MAXPATHLEN];

    if ((r = spawn(prog, argv)) >= 0) {
        if (r == 0) return 0;
        return wait(r);
    }

    safe_strncpy(path_with_b, prog, sizeof(path_with_b));
    strcat(path_with_b, ".b");
    if ((r = spawn(path_with_b, argv)) >= 0) {
        if (r == 0) return 0;
        return wait(r);
    }

    return -1;
}

// Forward declaration for handlers
int handle_cd(int, char**);
int handle_pwd(int, char**);
int handle_declare(int, char**);
int handle_unset(int, char**);
int handle_history(int, char**);
void save_environment();
void load_environment();

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

        save_environment();

        char path_to_try[MAXPATHLEN];
        int spawn_status;

        resolve_path(ecmd->argv[0], path_to_try);
        if ((spawn_status = try_spawn_and_wait(path_to_try, (const char**)ecmd->argv)) >= 0) {
            return spawn_status;
        }

        if (ecmd->argv[0][0] != '/') {
            strcpy(path_to_try, "/");
            strcat(path_to_try, ecmd->argv[0]);
            if ((spawn_status = try_spawn_and_wait(path_to_try, (const char**)ecmd->argv)) >= 0) {
                return spawn_status;
            }
        }

        printf("command not found: %s\n", ecmd->argv[0]);
        return 1;

    case REDIR:
        rcmd = (struct redircmd*)cmd;
        if ((r = fork()) < 0) user_panic("fork failed");
        if (r == 0) {
            close(rcmd->fd);
            char resolved_file[MAXPATHLEN];
            resolve_path(rcmd->file, resolved_file);
            int fd_opened = open(resolved_file, rcmd->mode);
            if (fd_opened < 0) {
                printf("open %s failed\n", rcmd->file);
                exit(1);
            }
            status = run_cmd(rcmd->cmd);
            exit(status);
        }
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
        status = run_cmd(logcmd->left);
        if (status == 0) status = run_cmd(logcmd->right);
        break;

    case OR:
        logcmd = (struct logiccmd*)cmd;
        status = run_cmd(logcmd->left);
        if (status != 0) status = run_cmd(logcmd->right);
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
        if(*s != '`') {
            *d++ = *s++;
            continue;
        }
        
        // 遇到第一个反引号，开始寻找匹配的第二个
        s++;
        const char *sub_cmd_start = s;
        while(*s && *s != '`') {
            s++;
        }
        
        // 如果没有找到匹配的结束反引号，则将第一个反引号当作普通字符处理
        if(!*s) {
            *d++ = '`';
            s = sub_cmd_start;
            continue;
        }
        
        // 提取子命令
        char sub_cmd[BUF_MAX];
        int sub_len = s - sub_cmd_start;
        safe_strncpy(sub_cmd, sub_cmd_start, sub_len + 1);
        s++; // 跳过第二个反引号
        
        // --- 执行子命令并捕获输出 ---
        int p[2];
        if (pipe(p) < 0) user_panic("pipe failed");

        int pid;
        if ((pid = fork()) == 0) {
            // 子进程: 将标准输出重定向到管道的写端
            reset_pools();
            close(p[0]); // 关闭读端
            dup(p[1], 1);  // 复制写端到 stdout
            close(p[1]);
            
            struct cmd* parsed_sub_cmd = parse_cmd(sub_cmd);
            if (parsed_sub_cmd) {
                run_cmd(parsed_sub_cmd);
            }
            exit(0);
        }
        
        // 父进程: 从管道的读端读取子命令的输出
        close(p[1]); // 关闭写端
        
        // 将输出读入一个临时缓冲区
        char sub_output_buf[BUF_MAX * 2]; // 使用一个足够大的缓冲区
        int bytes_read = 0;
        int n;
        while (bytes_read < sizeof(sub_output_buf) - 1 && 
               (n = read(p[0], sub_output_buf + bytes_read, sizeof(sub_output_buf) - 1 - bytes_read)) > 0) {
            bytes_read += n;
        }
        sub_output_buf[bytes_read] = '\0';
        
        close(p[0]);
        wait(pid);
        
        // --- 处理输出：只移除末尾的换行符 ---
        int len = strlen(sub_output_buf);
        while (len > 0 && (sub_output_buf[len - 1] == '\n' || sub_output_buf[len - 1] == '\r')) {
            len--;
        }
        sub_output_buf[len] = '\0';

        // --- 将处理后的结果复制到主命令缓冲区 ---
        char* sub_out_ptr = sub_output_buf;
        while(*sub_out_ptr && d < d_end) {
            *d++ = *sub_out_ptr++;
        }
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

    load_environment();

    int fd_cwd;
    if ((fd_cwd = open("/.cwd", O_RDONLY)) >= 0) {
        char cwd_buf[MAXPATHLEN];
        int n = read(fd_cwd, cwd_buf, sizeof(cwd_buf) - 1);
        if (n > 0) {
            cwd_buf[n] = '\0';
            strcpy(g_cwd, cwd_buf);
        }
        close(fd_cwd);
    }

    load_history();

    printf("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
	printf("::               MOS Shell (Challenge Edition)             ::\n");
	printf(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");

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
        char c = *s; int is_alnum = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
        while (is_alnum && name_len < VAR_MAX_NAME_LEN) {
            var_name[name_len++] = *s++; c = *s;
            is_alnum = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
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
    printf("\x1b[G\x1b[K");

    printf("%s%s", prompt, line_buffer);

    int prompt_len = strlen(prompt);
    printf("\x1b[%dG", prompt_len + line_pos + 1);
}
void insert_char(char c) {
    if (line_len < BUF_MAX - 1) {
        my_memmove(&line_buffer[line_pos + 1], &line_buffer[line_pos], line_len - line_pos);
        line_buffer[line_pos] = c;
        line_len++;
        line_pos++;
        line_buffer[line_len] = '\0';
    }
}
void delete_char() {
    if (line_pos < line_len) {
        my_memmove(&line_buffer[line_pos], &line_buffer[line_pos + 1], line_len - line_pos);
        line_len--;
    }
}
void backspace_char() {
    if (line_pos > 0) {
        line_pos--;
        delete_char();
    }
}
char read_char() { char c; if (read(0, &c, 1) != 1) c = 0; return c; }

void readline_rich(const char *prompt, char *dst_buf) {
    // 指向历史记录的指针现在直接使用全局的 history_pos。
    // 在进入行编辑前，保存原始行，以便用户可以从历史导航中返回到他最初输入的内容。
    char original_line[BUF_MAX];
    strcpy(original_line, line_buffer); 

    // 将导航指针设置到历史记录的末尾（即新的空行）。
    history_pos = history_count;
    line_len = 0; line_pos = 0; line_buffer[0] = '\0';
    
    // 我们需要在历史记录的“末尾”存储用户当前正在输入的行。
    // 我们直接使用 history 数组的下一个可用槽位，但要小心不改变 history_count。
    int current_line_idx = (history_start + history_count) % HISTFILESIZE;
    strcpy(history[current_line_idx], ""); // 逻辑上的“新行”

    redraw_line(prompt);

    while (1) {
        char c = read_char();
        switch (c) {
            case '\n': case '\r': 
                strcpy(dst_buf, line_buffer); 
                return;
            case '\b':
            case 0x7f: backspace_char(); break;
            case '\x1b':
                if (read_char() == '[') {
                    // 在导航之前，将当前行的任何修改写回它在全局 history 数组中的位置。
                    int C_idx = (history_start + history_pos) % HISTFILESIZE;
                    strcpy(history[C_idx], line_buffer);

                    char next_c = read_char();
                     if (next_c == 'A') { // 上箭头
                        printf("\x1b[B"); // 终端行为补偿
                        if (history_pos > 0) {
                            history_pos--;
                            int p_idx = (history_start + history_pos) % HISTFILESIZE;
                            strcpy(line_buffer, history[p_idx]);
                            line_len = strlen(line_buffer); 
                            line_pos = line_len;
                        }
                    } else if (next_c == 'B') { // 下箭头
                        if (history_pos < history_count) {
                            history_pos++;
                            int n_idx = (history_start + history_pos) % HISTFILESIZE;
                            strcpy(line_buffer, history[n_idx]);
                            line_len = strlen(line_buffer); 
                            line_pos = line_len;
                        }
                    } else if (next_c == 'D' && line_pos > 0) { // 左
                        line_pos--; 
                    } else if (next_c == 'C' && line_pos < line_len) { // 右
                        line_pos++; 
                    }
                }
                break;
            // ... 其他快捷键保持不变 ...
            case 0x01: line_pos = 0; break;
            case 0x05: line_pos = line_len; break;
            case 0x0b: 
                line_buffer[line_pos] = '\0';
                line_len = line_pos;
                break;
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

    int last_idx = (history_start + history_count - 1) % HISTFILESIZE;
    if (history_count > 0 && strcmp(history[last_idx], cmd) == 0) return;

    int new_idx = (history_start + history_count) % HISTFILESIZE;
    strcpy(history[new_idx], cmd);

    if (history_count < HISTFILESIZE) {
        history_count++;
    } else {
        history_start = (history_start + 1) % HISTFILESIZE;
    }
    save_history();
}

void save_history() {
    int fd;
	if ((fd = open("/.mos_history", O_WRONLY | O_CREAT | O_TRUNC)) < 0) return;
	for (int i = 0; i < history_count; i++) {
        int idx = (history_start + i) % HISTFILESIZE;
		write(fd, history[idx], strlen(history[idx]));
		write(fd, "\n", 1);
	}
	close(fd);
}

int handle_history(int argc, char **argv) {
    if (argc > 1) { printf("history: too many arguments\n"); return 1; }
    for (int i = 0; i < history_count; i++) {
        int idx = (history_start + i) % HISTFILESIZE;
        printf("%s\n", history[idx]);
    }
    return 0;
}

void save_environment() {
    int fd;
    if ((fd = open("/.mos_env", O_WRONLY | O_CREAT | O_TRUNC)) < 0) {
        return;
    }

    for (int i = 0; i < MAX_VARS; i++) {
        if (vars[i].in_use && vars[i].is_exported) {
            char line_buf[VAR_MAX_NAME_LEN + VAR_MAX_VALUE_LEN + 4];
            
            line_buf[0] = vars[i].is_readonly ? 'r' : 'n';
            line_buf[1] = ':';
            line_buf[2] = '\0';
            
            strcat(line_buf, vars[i].name);
            strcat(line_buf, "=");
            strcat(line_buf, vars[i].value);
            strcat(line_buf, "\n");
            
            write(fd, line_buf, strlen(line_buf));
        }
    }
    close(fd);
}

void load_environment() {
    int fd;
    if ((fd = open("/.mos_env", O_RDONLY)) < 0) {
        return;
    }
    
    char line[BUF_MAX];
    while (readline_from_fd(fd, line, sizeof(line)) >= 0) {
        if (line[0] == '\0') continue;

        char *name_ptr, *val_ptr;
        
        if (line[1] != ':' || (name_ptr = &line[2]) == NULL) continue;
        
        val_ptr = strchr(name_ptr, '=');
        if (val_ptr == NULL) continue;
        
        *val_ptr = '\0';
        val_ptr++;
        
        struct Var* v = find_free_var_slot();
        if (v == NULL) {
            printf("shell: warning: environment variable space is full, cannot load more.\n");
            break; 
        }

        v->in_use = 1;
        v->is_exported = 1;
        v->is_readonly = (line[0] == 'r');
        
        safe_strncpy(v->name, name_ptr, VAR_MAX_NAME_LEN + 1);
        safe_strncpy(v->value, val_ptr, VAR_MAX_VALUE_LEN + 1);
    }
    
    close(fd);
}

int readline_from_fd(int fd, char *buf, int size) {
    int i = 0; char c = 0;
    while (i < size - 1) {
        if (read(fd, &c, 1) != 1) {
             if (i == 0) return -1; // EOF or error
             else break; 
        }
        if (c == '\n' || c == '\r') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}
void load_history() {
    int fd = open("/.mos_history", O_RDONLY);
    if (fd < 0) return;
    
    history_count = 0;
    history_start = 0;
    
    while(history_count < HISTFILESIZE) {
        if (readline_from_fd(fd, history[history_count], BUF_MAX) >= 0) {
             if (history[history_count][0] != '\0') {
                history_count++;
             }
        } else {
            break; // EOF
        }
    }
    close(fd);
}
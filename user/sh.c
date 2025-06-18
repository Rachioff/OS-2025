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
static int history_pos = 0;
static char cwd[MAXPATHLEN] = "/";

/* Overview:
 *   Parse the next token from the string at s.
 *
 * Post-Condition:
 *   Set '*p1' to the beginning of the token and '*p2' to just past the token.
 *   Return:
 *     - 0 if the end of string is reached.
 *     - '<' for < (stdin redirection).
 *     - '>' for > (stdout redirection).
 *     - '|' for | (pipe).
 *     - 'w' for a word (command, argument, or file name).
 *
 *   The buffer is modified to turn the spaces after words into zero bytes ('\0'), so that the
 *   returned token is a null-terminated string.
 */
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
			// Open 't' for reading, dup it onto fd 0, and then close the original fd.
			// If the 'open' function encounters an error,
			// utilize 'debugf' to print relevant messages,
			// and subsequently terminate the process using 'exit'.
			/* Exercise 6.5: Your code here. (1/3) */
			fd = open(t, O_RDONLY);
			if (fd < 0) {
				debugf("failed to open '%s'\n", t);
				exit();
			}
			dup(fd, 0);
			close(fd);
			// user_panic("< redirection not implemented");

			break;
		case '>':
			if (gettoken(0, &t) != 'w') {
				debugf("syntax error: > not followed by word\n");
				exit();
			}
			// Open 't' for writing, create it if not exist and trunc it if exist, dup
			// it onto fd 1, and then close the original fd.
			// If the 'open' function encounters an error,
			// utilize 'debugf' to print relevant messages,
			// and subsequently terminate the process using 'exit'.
			/* Exercise 6.5: Your code here. (2/3) */
			fd = open(t, O_WRONLY);
			if (fd < 0) {
				debugf("failed to open '%s'\n", t);
				exit();
			}
			dup(fd, 1);
			close(fd);
			// user_panic("> redirection not implemented");

			break;
		case '|':;
			/*
			 * First, allocate a pipe.
			 * Then fork, set '*rightpipe' to the returned child envid or zero.
			 * The child runs the right side of the pipe:
			 * - dup the read end of the pipe onto 0
			 * - close the read end of the pipe
			 * - close the write end of the pipe
			 * - and 'return parsecmd(argv, rightpipe)' again, to parse the rest of the
			 *   command line.
			 * The parent runs the left side of the pipe:
			 * - dup the write end of the pipe onto 1
			 * - close the write end of the pipe
			 * - close the read end of the pipe
			 * - and 'return argc', to execute the left of the pipeline.
			 */
			int p[2];
			/* Exercise 6.5: Your code here. (3/3) */
			pipe(p);
			*rightpipe = fork();
			if (*rightpipe == 0) {
				dup(p[0], 0);
				close(p[0]);
				close(p[1]);
				return parsecmd(argv, rightpipe);
			}  else if (*rightpipe > 0) {
				dup(p[1], 1);
				close(p[1]);
				close(p[0]);
				return argc;
			}
			// user_panic("| not implemented");

			break;
		}
	}

	return argc;
}

void runcmd(char *s) {
	gettoken(s, 0);

	char *argv[MAXARGS];
	int rightpipe = 0;
	int argc = parsecmd(argv, &rightpipe);
	if (argc == 0) {
		return;
	}
	argv[argc] = 0;

	int child = spawn(argv[0], argv);
	close_all();
	if (child >= 0) {
		wait(child);
	} else {
		debugf("spawn %s: %d\n", argv[0], child);
	}
	if (rightpipe) {
		wait(rightpipe);
	}
	exit();
}

void readline(char *buf, u_int n) {
	int r;
	for (int i = 0; i < n; i++) {
		if ((r = read(0, buf + i, 1)) != 1) {
			if (r < 0) {
				debugf("read error: %d\n", r);
			}
			exit();
		}
		if (buf[i] == '\b' || buf[i] == 0x7f) {
			if (i > 0) {
				i -= 2;
			} else {
				i = -1;
			}
			if (buf[i] != '\b') {
				printf("\b");
			}
		}
		if (buf[i] == '\r' || buf[i] == '\n') {
			buf[i] = 0;
			return;
		}
	}
	debugf("line too long\n");
	while ((r = read(0, buf, 1)) == 1 && buf[0] != '\r' && buf[0] != '\n') {
		;
	}
	buf[0] = 0;
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
			runcmd(buf);
			exit();
		} else {
			wait(r);
		}
	}
	return 0;
}

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
        resolve_path(argv[1], cwd, path);
    }
    
    if (stat(path, &st) < 0) {
        printf("cd: The directory '%s' does not exist\n", argv[1]);
        return 1;
    }
    
    if (!st.st_isdir) {
        printf("cd: '%s' is not a directory\n", argv[1]);
        return 1;
    }
    
    strcpy(cwd, path);
    return 0;
}

int builtin_pwd(int argc, char **argv) {
    if (argc != 1) {
        printf("pwd: expected 0 arguments; got %d\n", argc - 1);
        return 2;
    }
    printf("%s\n", cwd);
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
    
    char *name = argv[arg_start];
    char *value = strchr(name, '=');
    if (value) {
        *value = '\0';
        value++;
    } else {
        value = "";
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
            printf("declare: %s: readonly variable\n", name);
            return 1;
        }
        strcpy(env_vars[found].value, value);
        if (is_export) env_vars[found].is_export = 1;
        if (is_readonly) env_vars[found].is_readonly = 1;
    } else {
        if (env_count >= MAX_ENVS) {
            printf("declare: too many environment variables\n");
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
        printf("unset: expected 1 argument\n");
        return 1;
    }
    
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_vars[i].name, argv[1]) == 0) {
            if (env_vars[i].is_readonly) {
                printf("unset: %s: cannot unset: readonly variable\n", argv[1]);
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
    for (int i = 0; i < history_count; i++) {
        printf("%s\n", history[i]);
    }
    return 0;
}

void readline_advanced(char *buf, u_int n) {
    int pos = 0;
    int len = 0;
    int hist_index = history_count;
    char temp[MAX_INPUT];
    
    buf[0] = '\0';
    
    while (1) {
        int c = syscall_cgetc();
        
        if (c == '\x1b') {
            c = syscall_cgetc();
            if (c == '[') {
                c = syscall_cgetc();
                switch (c) {
                    case 'A':
                        if (hist_index > 0) {
                            hist_index--;
                            while (pos > 0) {
                                printf("\b \b");
                                pos--;
                            }
                            strcpy(buf, history[hist_index]);
                            printf("%s", buf);
                            pos = len = strlen(buf);
                        }
                        break;
                    case 'B':
                        if (hist_index < history_count - 1) {
                            hist_index++;
                            while (pos > 0) {
                                printf("\b \b");
                                pos--;
                            }
                            strcpy(buf, history[hist_index]);
                            printf("%s", buf);
                            pos = len = strlen(buf);
                        } else if (hist_index == history_count - 1) {
                            hist_index++;
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

void save_history(const char *cmd) {
    if (strlen(cmd) == 0) return;
    
    int fd = open("/.mos_history", O_WRONLY | O_CREAT);
    if (fd >= 0) {
        seek(fd, 0);
        char buf[MAX_INPUT];
        int lines = 0;
        int n;
        
        close(fd);
        fd = open("/.mos_history", O_RDONLY);
        if (fd >= 0) {
            char temp[HISTORY_MAX][MAX_INPUT];
            while ((n = readline_from_fd(fd, buf, MAX_INPUT)) > 0 && lines < HISTORY_MAX - 1) {
                strcpy(temp[lines++], buf);
            }
            close(fd);
            
            fd = open("/.mos_history", O_WRONLY | O_TRUNC);
            if (fd >= 0) {
                for (int i = 0; i < lines; i++) {
                    write(fd, temp[i], strlen(temp[i]));
                    write(fd, "\n", 1);
                }
            }
        }
        
        write(fd, cmd, strlen(cmd));
        write(fd, "\n", 1);
        close(fd);
    }
    
    if (history_count < HISTORY_MAX) {
        history[history_count] = malloc(strlen(cmd) + 1);
        strcpy(history[history_count], cmd);
        history_count++;
    } else {
        free(history[0]);
        for (int i = 0; i < HISTORY_MAX - 1; i++) {
            history[i] = history[i + 1];
        }
        history[HISTORY_MAX - 1] = malloc(strlen(cmd) + 1);
        strcpy(history[HISTORY_MAX - 1], cmd);
    }
}

char *expand_vars(char *str) {
    static char result[MAX_INPUT];
    char *dst = result;
    char *src = str;
    
    while (*src) {
        if (*src == '$' && isalpha(src[1])) {
            src++;
            char varname[ENV_NAME_MAX + 1];
            int i = 0;
            while (isalnum(*src) || *src == '_') {
                if (i < ENV_NAME_MAX) {
                    varname[i++] = *src;
                }
                src++;
            }
            varname[i] = '\0';
            
            int found = 0;
            for (int j = 0; j < env_count; j++) {
                if (strcmp(env_vars[j].name, varname) == 0) {
                    strcpy(dst, env_vars[j].value);
                    dst += strlen(env_vars[j].value);
                    found = 1;
                    break;
                }
            }
            if (!found) {

            }
        } else if (*src == '`') {
            src++;
            char cmd[MAX_INPUT];
            int i = 0;
            while (*src && *src != '`') {
                cmd[i++] = *src++;
            }
            cmd[i] = '\0';
            if (*src == '`') src++;
            
            int p[2];
            pipe(p);
            int pid = fork();
            if (pid == 0) {
                close(p[0]);
                dup(p[1], 1);
                close(p[1]);
                runcmd(cmd);
                exit();
            } else {
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
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return result;
}

void runcmd_enhanced(char *s) {
    char *comment = strchr(s, '#');
    if (comment) {
        *comment = '\0';
    }
    
    char *cmds[100];
    int cmd_count = 0;
    char *cmd_types[100];
    
    char *p = s;
    char *start = p;
    while (*p) {
        if (*p == ';') {
            *p = '\0';
            cmds[cmd_count] = start;
            cmd_types[cmd_count] = ";";
            cmd_count++;
            start = p + 1;
        } else if (*p == '&' && *(p+1) == '&') {
            *p = '\0';
            cmds[cmd_count] = start;
            cmd_types[cmd_count] = "&&";
            cmd_count++;
            p++;
            start = p + 1;
        } else if (*p == '|' && *(p+1) == '|') {
            *p = '\0';
            cmds[cmd_count] = start;
            cmd_types[cmd_count] = "||";
            cmd_count++;
            p++;
            start = p + 1;
        }
        p++;
    }
    if (start < p) {
        cmds[cmd_count] = start;
        cmd_types[cmd_count] = "";
        cmd_count++;
    }
    
    int last_status = 0;
    for (int i = 0; i < cmd_count; i++) {
        while (*cmds[i] == ' ' || *cmds[i] == '\t') cmds[i]++;
        if (*cmds[i] == '\0') continue;

        if (i > 0) {
            if (strcmp(cmd_types[i-1], "&&") == 0 && last_status != 0) {
                continue;
            }
            if (strcmp(cmd_types[i-1], "||") == 0 && last_status == 0) {
                continue;
            }
        }

        char *expanded = expand_vars(cmds[i]);

        gettoken(expanded, 0);
        char *argv[MAXARGS];
        int rightpipe = 0;
        int argc = parsecmd_enhanced(argv, &rightpipe);
        
        if (argc == 0) continue;
        argv[argc] = 0;

        int is_builtin = 0;
        if (strcmp(argv[0], "cd") == 0) {
            last_status = builtin_cd(argc, argv);
            is_builtin = 1;
        } else if (strcmp(argv[0], "pwd") == 0) {
            last_status = builtin_pwd(argc, argv);
            is_builtin = 1;
        } else if (strcmp(argv[0], "declare") == 0) {
            last_status = builtin_declare(argc, argv);
            is_builtin = 1;
        } else if (strcmp(argv[0], "unset") == 0) {
            last_status = builtin_unset(argc, argv);
            is_builtin = 1;
        } else if (strcmp(argv[0], "exit") == 0) {
            builtin_exit(argc, argv);
        } else if (strcmp(argv[0], "history") == 0) {
            last_status = builtin_history(argc, argv);
            is_builtin = 1;
        }
        
        if (!is_builtin) {
            char prog[MAXPATHLEN];
            strcpy(prog, argv[0]);
            if (!strchr(prog, '.')) {
                strcat(prog, ".b");
            }
            
            int pid = fork();
            if (pid == 0) {
                int child = spawn(prog, argv);
                if (child >= 0) {
                    wait(child);
                    exit();
                } else {
                    debugf("spawn %s: %d\n", prog, child);
                    exit();
                }
            } else {
                wait(pid);
                last_status = 0;
            }
        }
        
        close_all();
        if (rightpipe) {
            wait(rightpipe);
        }
    }
}

int parsecmd_enhanced(char **argv, int *rightpipe) {
    int argc = 0;
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
            c = gettoken(0, &t);
            if (c == '>') {
                if (gettoken(0, &t) != 'w') {
                    debugf("syntax error: >> not followed by word\n");
                    exit();
                }
                fd = open(t, O_WRONLY);
                if (fd < 0) {
                    fd = open(t, O_WRONLY | O_CREAT);
                }
                if (fd < 0) {
                    debugf("open %s for append: %d\n", t, fd);
                    exit();
                }
                seek(fd, 0xfffffff);
                if (fd != 1) {
                    dup(fd, 1);
                    close(fd);
                }
            } else if (c == 'w') {
                fd = open(t, O_WRONLY | O_CREAT | O_TRUNC);
                if (fd < 0) {
                    debugf("open %s for write: %d\n", t, fd);
                    exit();
                }
                if (fd != 1) {
                    dup(fd, 1);
                    close(fd);
                }
            } else {
                debugf("syntax error: > not followed by > or word\n");
                exit();
            }
            break;
        case '|':
            // ... pipe处理代码 ...
            break;
        }
    }
    return argc;
}


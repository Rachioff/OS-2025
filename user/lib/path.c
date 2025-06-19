#include <string.h>
#include <lib.h>
#include <fs.h>

// Resolve 'path' relative to 'cwd' and store the absolute path in 'resolved'.
void resolve_path(const char *path, char *resolved, const char *cwd) {
	if (path[0] == '/') {
		strcpy(resolved, path);
	} else {
		if (strlen(cwd) > 1 && cwd[strlen(cwd) - 1] == '/') {
			sprintf(resolved, "%s%s", cwd, path);
		} else if (strlen(cwd) == 1 && cwd[0] == '/') {
			sprintf(resolved, "/%s", path);
		} else {
			sprintf(resolved, "%s/%s", cwd, path);
		}
	}

	// Normalize path by processing "." and ".."
	char *stack[MAXPATHLEN / 2];
	int top = -1;
	char temp_path[MAXPATHLEN];
	strcpy(temp_path, resolved);
	
	char *p = temp_path;
	while (*p == '/') p++; // Skip leading slashes

	char *token = strtok(p, "/");
	while (token != NULL) {
		if (strcmp(token, ".") == 0) {
			// Do nothing
		} else if (strcmp(token, "..") == 0) {
			if (top > -1) {
				top--; // Pop from stack
			}
		} else {
			stack[++top] = token; // Push to stack
		}
		token = strtok(NULL, "/");
	}

	if (top == -1) {
		strcpy(resolved, "/");
	} else {
		resolved[0] = '\0';
		for (int i = 0; i <= top; i++) {
			strcat(resolved, "/");
			strcat(resolved, stack[i]);
		}
	}
}
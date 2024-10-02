#include "lab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <termios.h>
#include <readline/readline.h>
#include <readline/history.h>

void execute_command(struct shell *sh, char *command, char *args[], int background);
void add_background_job(struct shell *sh, pid_t pid, char *args[]);
void check_background_jobs(struct shell *sh);
void print_jobs(struct shell *sh);

// Get the shell prompt 
char *get_prompt(const char *env) {
    char *env_prompt = getenv(env);
    char *prompt;
    if (env_prompt) {
        prompt = strdup(env_prompt);
    } else {
        prompt = strdup("shell>");
    }
    return prompt;
}

// Change directory 
int change_dir(char **dir) {
    char *path = dir[1];  // dir[0] is "cd", dir[1] is the path
    if (path == NULL) {
        char *home = getenv("HOME");
        if (home == NULL) {
            struct passwd *pw = getpwuid(getuid());
            if (pw == NULL) {
                fprintf(stderr, "Error: Could not determine home directory.\n");
                return -1;
            }
            home = pw->pw_dir;
        }
        if (chdir(home) != 0) {
            perror("cd");
            return -1;
        }
    } else {
        if (chdir(path) != 0) {
            perror("cd");
            return -1;
        }
    }
    return 0;
}

// Parse the command line 
char **cmd_parse(const char *line) {
    long arg_max = sysconf(_SC_ARG_MAX);
    char **args = malloc((arg_max / 2) * sizeof(char *));
    if (!args) {
        perror("malloc");
        return NULL;
    }
    char *line_copy = strdup(line);
    if (!line_copy) {
        perror("strdup");
        free(args);
        return NULL;
    }
    int arg_count = 0;
    char *token = strtok(line_copy, " ");
    while (token != NULL && arg_count < (arg_max / 2 - 1)) {
        args[arg_count++] = strdup(token);
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;
    free(line_copy);
    return args;
}

// Free the command arguments 
void cmd_free(char **line) {
    if (line) {
        for (int i = 0; line[i] != NULL; i++) {
            free(line[i]);
        }
        free(line);
    }
}

// Trim whitespace from a string 
char *trim_white(char *line) {
    char *end;
    // Trim leading space
    while (isspace((unsigned char)*line)) line++;
    if (*line == 0)  // All spaces
        return line;
    // Trim trailing space
    end = line + strlen(line) - 1;
    while (end > line && isspace((unsigned char)*end)) end--;
    // Write new null terminator
    *(end + 1) = '\0';
    return line;
}

// Handle built-in commands 
bool do_builtin(struct shell *sh, char **argv) {
    if (strcmp(argv[0], "exit") == 0) {
        printf("Exiting shell...\n");
        sh_destroy(sh);
        exit(0);
    } else if (strcmp(argv[0], "cd") == 0) {
        change_dir(argv);
        return true;
    } else if (strcmp(argv[0], "history") == 0) {
        int history_len = history_length;
        for (int i = 1; i <= history_len; i++) {
            HIST_ENTRY *entry = history_get(i);
            if (entry) {
                printf("%d: %s\n", i, entry->line);
            }
        }
        return true;
    } else if (strcmp(argv[0], "jobs") == 0) {
        print_jobs(sh);
        return true;
    }
    return false;
}

// Initialize the shell 
void sh_init(struct shell *sh) {
    sh->shell_terminal = STDIN_FILENO;
    sh->shell_pgid = getpid();
    sh->job_count = 0;
    sh->prompt = NULL;
    // Set shell as the foreground process group for the terminal
    setpgid(sh->shell_pgid, sh->shell_pgid);
    tcsetpgrp(sh->shell_terminal, sh->shell_pgid);
    // Save the shell's terminal modes
    tcgetattr(sh->shell_terminal, &sh->shell_tmodes);
    // Handle signals
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

// Destroy the shell 
void sh_destroy(struct shell *sh) {
    // Restore terminal modes
    tcsetattr(sh->shell_terminal, TCSANOW, &sh->shell_tmodes);
    if (sh->prompt)
        free(sh->prompt);
}

// Parse command line arguments 
void parse_args(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "v")) != -1) {
        switch (opt) {
            case 'v':
                printf("Shell version: %d.%d\n", lab_VERSION_MAJOR, lab_VERSION_MINOR);
                exit(0);
            default:
                fprintf(stderr, "Usage: %s [-v]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
}

// Execute external commands 
void execute_command(struct shell *sh, char *command, char *args[], int background) {
    pid_t pid = fork();  // Create a new process
    if (pid == 0) {
        // This is the child process
        pid_t child_pgid = getpid();
        setpgid(child_pgid, child_pgid);  // Set child process group
        if (!background) {
            tcsetpgrp(sh->shell_terminal, child_pgid);  // Set terminal control to child if foreground
        }

        // Reset signals to default behavior
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        // Execute the command
        if (execvp(command, args) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    } else if (pid > 0) {
        // This is the parent (shell)
        if (background) {
            // Add the background job to the job list
            add_background_job(sh, pid, args);
        } else {
            int status;
            waitpid(pid, &status, WUNTRACED);  // Wait for the child to finish or stop
            tcsetpgrp(sh->shell_terminal, sh->shell_pgid);  // Return terminal control to the shell
        }
    } else {
        // Fork failed
        perror("fork");
    }
}

// Add a background job 
void add_background_job(struct shell *sh, pid_t pid, char *args[]) {
    int job_index = sh->job_count;
    sh->jobs[job_index].job_num = job_index + 1;
    sh->jobs[job_index].pid = pid;

    // Concatenate the full command with arguments
    strcpy(sh->jobs[job_index].command, args[0]);
    for (int i = 1; args[i] != NULL; i++) {
        strcat(sh->jobs[job_index].command, " ");
        strcat(sh->jobs[job_index].command, args[i]);
    }
    strcpy(sh->jobs[job_index].status, "Running");  // Set status to "Running"
    printf("[%d] %d %s\n", sh->jobs[job_index].job_num, sh->jobs[job_index].pid, sh->jobs[job_index].command);
    sh->job_count++;
}

// Check background jobs for completion 
void check_background_jobs(struct shell *sh) {
    for (int i = 0; i < sh->job_count; i++) {
        int status;
        pid_t result = waitpid(sh->jobs[i].pid, &status, WNOHANG);
        if (result > 0 && strcmp(sh->jobs[i].status, "Running") == 0) {
            strcpy(sh->jobs[i].status, "Done");  // Mark job as Done
            printf("[%d] Done %s\n", sh->jobs[i].job_num, sh->jobs[i].command);
        }
    }
}

// Print all background jobs 
void print_jobs(struct shell *sh) {
    for (int i = 0; i < sh->job_count; i++) {
        if (strcmp(sh->jobs[i].status, "Running") == 0 || strcmp(sh->jobs[i].status, "Done") == 0) {
            printf("[%d] %d %s %s &\n", sh->jobs[i].job_num, sh->jobs[i].pid, sh->jobs[i].status, sh->jobs[i].command);
        }
    }
}

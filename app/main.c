#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <signal.h>
#include <termios.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "../src/lab.h"

// Shell's process group ID and terminal
pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;

// Structure to track background jobs
typedef struct {
    int job_num;
    pid_t pid;
    char command[256];
    char status[10];  // "Running" or "Done"
} BackgroundJob;

BackgroundJob jobs[256];
int job_count = 0;

void change_directory(char *path) {
    if (path == NULL) {
        char *home = getenv("HOME");
        if (home == NULL) {
            struct passwd *pw = getpwuid(getuid());
            if (pw == NULL) {
                fprintf(stderr, "Error: Could not determine home directory.\n");
                return;
            }
            home = pw->pw_dir;
        }
        if (chdir(home) != 0) {
            perror("cd");
        }
    } else {
        if (chdir(path) != 0) {
            perror("cd");
        }
    }
}

void print_history() {
    int history_len = history_length;
    for (int i = 1; i <= history_len; i++) {
        HIST_ENTRY *entry = history_get(i);
        if (entry) {
            printf("%d: %s\n", i, entry->line);
        }
    }
}

void handle_signals() {
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

// Function to track and add background jobs
void add_background_job(pid_t pid, char *args[]) {
    jobs[job_count].job_num = job_count + 1;
    jobs[job_count].pid = pid;

    // Concatenate the full command with arguments
    strcpy(jobs[job_count].command, args[0]);
    for (int i = 1; args[i] != NULL; i++) {
        strcat(jobs[job_count].command, " ");
        strcat(jobs[job_count].command, args[i]);
    }
    strcpy(jobs[job_count].status, "Running");  // Set status to "Running"
    printf("[%d] %d %s\n", jobs[job_count].job_num, jobs[job_count].pid, jobs[job_count].command);
    job_count++;
}

// Function to clean up and report finished background jobs
void check_background_jobs() {
    for (int i = 0; i < job_count; i++) {
        int status;
        pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);
        if (result > 0 && strcmp(jobs[i].status, "Running") == 0) {
            strcpy(jobs[i].status, "Done");  // Mark job as Done
            printf("[%d] Done %s\n", jobs[i].job_num, jobs[i].command);
        }
    }
}

// Function to print out all background jobs
void print_jobs() {
    for (int i = 0; i < job_count; i++) {
        if (strcmp(jobs[i].status, "Running") == 0 || strcmp(jobs[i].status, "Done") == 0) {
            printf("[%d] %d %s %s &\n", jobs[i].job_num, jobs[i].pid, jobs[i].status, jobs[i].command);
        }
    }
}

void execute_command(char *command, char *args[], int background) {
    pid_t pid = fork();  // Create a new process
    if (pid == 0) {
        // This is the child process
        pid_t child_pgid = getpid();
        setpgid(child_pgid, child_pgid);  // Set child process group
        if (!background) {
            tcsetpgrp(shell_terminal, child_pgid);  // Set terminal control to child if foreground
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
            add_background_job(pid, args);
        } else {
            int status;
            waitpid(pid, &status, WUNTRACED);  // Wait for the child to finish or stop
            tcsetpgrp(shell_terminal, shell_pgid);  // Return terminal control to the shell
        }
    } else {
        // Fork failed
        perror("fork");
    }
}

int main(int argc, char **argv) {
    shell_terminal = STDIN_FILENO;  // Shell terminal input
    shell_pgid = getpid();  // Shell's process group ID

    // Set shell as the foreground process group for the terminal
    setpgid(shell_pgid, shell_pgid);
    tcsetpgrp(shell_terminal, shell_pgid);

    // Save the shell's terminal modes
    tcgetattr(shell_terminal, &shell_tmodes);

    // Handle signals
    handle_signals();

    int opt;
    char *line;
    char *prompt;

    // Use getopt to parse command line arguments
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

    // Check if the MY_PROMPT environment variable is set
    char *env_prompt = getenv("MY_PROMPT");
    if (env_prompt) {
        prompt = env_prompt;
    } else {
        prompt = "$ ";
    }

    using_history();

    long arg_max = sysconf(_SC_ARG_MAX);

    // Continuously read input from the user
    while ((line = readline(prompt))) {
        if (line == NULL || strlen(line) == 0) {
            check_background_jobs();  // Check for finished background jobs
            continue;
        }

        char *trimmed_line = strtok(line, "\n");

        if (trimmed_line == NULL || strlen(trimmed_line) == 0) {
            free(line);
            continue;
        }

        // Store the original command before processing it
        char full_command[100];
        strcpy(full_command, trimmed_line);

        // Check if the command should be run in the background
        int background = 0;
        if (trimmed_line[strlen(trimmed_line) - 1] == '&') {
            background = 1;
            trimmed_line[strlen(trimmed_line) - 1] = '\0';  // Remove the '&'
        }

        char *args[arg_max / 2];
        int arg_count = 0;

        char *token = strtok(trimmed_line, " ");
        while (token != NULL && arg_count < (arg_max / 2 - 1)) {
            args[arg_count++] = token;
            token = strtok(NULL, " ");
        }
        args[arg_count] = NULL;

        // Handle built-in commands
        if (strcmp(args[0], "exit") == 0) {
            free(line);
            printf("Exiting shell...\n");
            exit(0);
        }

        if (strcmp(args[0], "cd") == 0) {
            change_directory(args[1]);
        } else if (strcmp(args[0], "history") == 0) {
            print_history();
        } else if (strcmp(args[0], "jobs") == 0) {
            print_jobs();
        } else {
            execute_command(args[0], args, background);
        }

        add_history(full_command);

        free(line);
    }

    tcsetattr(shell_terminal, TCSANOW, &shell_tmodes);  // Restore terminal modes
    printf("EOF received. Exiting shell...\n");
    return 0;
}


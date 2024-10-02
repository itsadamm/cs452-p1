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

int main(int argc, char **argv) {
    struct shell sh;
    sh_init(&sh);
    parse_args(argc, argv);
    sh.prompt = get_prompt("MY_PROMPT");
    using_history();

    // Continuously read input from the user
    char *line;
    while ((line = readline(sh.prompt))) {
        if (line == NULL || strlen(line) == 0) {
            check_background_jobs(&sh);  // Check for finished background jobs
            continue;
        }
        char *trimmed_line = trim_white(line);
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
            trimmed_line = trim_white(trimmed_line);
        }
        char **args = cmd_parse(trimmed_line);
        if (args == NULL || args[0] == NULL) {
            free(line);
            cmd_free(args);
            continue;
        }
        // Handle built-in commands or execute external commands
        if (!do_builtin(&sh, args)) {
            execute_command(&sh, args[0], args, background);
        }
        add_history(full_command);
        free(line);
        cmd_free(args);
    }

    sh_destroy(&sh);
    printf("EOF received. Exiting shell...\n");
    return 0;
}
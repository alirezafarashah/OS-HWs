#include "process.h"
#include "shell.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>
#include "parse.h"
#include <string.h>

/**
 * Executes the process p.
 * If the shell is in interactive mode and the process is a foreground process,
 * then p should take control of the terminal.
 */
void launch_process(process *proc) {
    dup2(proc->stdin, STDIN_FILENO);
    dup2(proc->stdout, STDOUT_FILENO);
    if (access(proc->argv[0], F_OK) == 0) {
        execv(proc->argv[0], proc->argv);
        exit(0);
    } else {
        char *path = getenv("PATH");
        tok_t *tokens = getToks(path);
        char *addr = malloc(80 * sizeof(char));
        int i = 0;
        while (tokens[i] != NULL) {
            strcpy(addr, tokens[i]);
            strcat(strcat(addr, "/"), proc->argv[0]);
            if (access(addr, F_OK) == 0) {
                execv(addr, proc->argv);
                exit(0);
            }
            i++;
        }
        printf("not found");
        exit(0);
    }
}

/* Put a process in the foreground. This function assumes that the shell
 * is in interactive mode. If the cont argument is true, send the process
 * group a SIGCONT signal to wake it up.
 */
void
put_process_in_foreground(process *p, int cont) {
    /** YOUR CODE HERE */
}

/* Put a process in the background. If the cont argument is true, send
 * the process group a SIGCONT signal to wake it up. */
void
put_process_in_background(process *p, int cont) {
    /** YOUR CODE HERE */
}

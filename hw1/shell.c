#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdbool.h>

#define FALSE 0
#define TRUE 1
#define INPUT_STRING_SIZE 80

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

int cmd_quit(tok_t arg[]) {
    printf("Bye\n");
    exit(0);
    return 1;
}

int num_of_args(tok_t *t);

int cmd_help(tok_t arg[]);

int cmd_pwd(tok_t arg[]);

int cmd_cd(tok_t arg[]);

int cmd_wait(tok_t arg[]);

bool existPID(__pid_t waitpid);

/* Command Lookup table */
typedef int cmd_fun_t(tok_t args[]); /* cmd functions take token array and return int */
typedef struct fun_desc {
    cmd_fun_t *fun;
    char *cmd;
    char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
        {cmd_help, "?",    "show this help menu"},
        {cmd_quit, "quit", "quit the command shell"},
        {cmd_cd,   "cd",   "move to dir"},
        {cmd_pwd,  "pwd",  "get current dir"},
        {cmd_wait, "wait", "wait for children processes"}
};


int cmd_help(tok_t arg[]) {
    int i;
    for (i = 0; i < (sizeof(cmd_table) / sizeof(fun_desc_t)); i++) {
        printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
    }
    return 1;
}

//new
int cmd_wait(tok_t arg[]) {
    while (waitpid(-1, NULL, 0) > 0) {
    }
    return 1;

}

int cmd_cd(tok_t args[]) {
    int cd = chdir(args[0]);
    if (cd != 0) {
        printf("cd: %s: No such file or directory\n", args[0]);
    }
    return 1;
}

int cmd_pwd(tok_t args[]) {
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    printf("%s\n", cwd);
    return 1;
}


int lookup(char cmd[]) {
    int i;
    for (i = 0; i < (sizeof(cmd_table) / sizeof(fun_desc_t)); i++) {
        if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
    }
    return -1;
}

void init_shell() {
    /* Check if we are running interactively */
    shell_terminal = STDIN_FILENO;

    /** Note that we cannot take control of the terminal if the shell
        is not interactive */
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive) {

        /* force into foreground */
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);


        shell_pgid = getpid();

        /* Put shell in its own process group */
        if (setpgid(shell_pgid, shell_pgid) < 0) {
            perror("Couldn't put the shell in its own process group");
            exit(1);
        }


        /* Take control of the terminal */
        tcsetpgrp(shell_terminal, shell_pgid);
        tcgetattr(shell_terminal, &shell_tmodes);

        struct sigaction sa;
        sa.sa_handler = SIG_IGN;
        sigaction(SIGINT, &sa, 0);
        sigaction(SIGQUIT, &sa, 0);
        sigaction(SIGTERM, &sa, 0);
        sigaction(SIGKILL, &sa, 0);
        sigaction(SIGTSTP, &sa, 0);
        sigaction(SIGTTOU, &sa, 0);
        sigaction(SIGCHLD, &sa, 0);
        sigaction(SIGTTIN, &sa, 0);
    }

    // init first_process
    first_process = (process *) malloc(sizeof(process));
    first_process->pid = getpid();
    first_process->next = NULL;
    first_process->prev = NULL;
    first_process->stdout = STDOUT_FILENO;
    first_process->stdin = STDIN_FILENO;
    first_process->stderr = STDERR_FILENO;
    first_process->background = 0;


}

/**
 * Add a process to our process list
 */
void add_process(process *p) {
    process *proc = first_process;
    while (proc->next != NULL) {
        proc = proc->next;
    }
    proc->next = p;
    p->prev = proc;
}

/**
 * Creates a process given the inputString from stdin
 */
process *create_process(tok_t *arg) {

    process *proc = (process *) malloc(sizeof(process));
    proc->next = NULL;
    proc->prev = NULL;
    proc->background = 0;
    proc->stderr = STDERR_FILENO;
    proc->stdout = STDOUT_FILENO;
    proc->stdin = STDIN_FILENO;
    proc->argc = num_of_args(arg);
    char *fileName = NULL;
    proc->background = 0;

    if (arg != NULL) {
        if (strcmp(arg[proc->argc - 1], "&") == 0) {
            proc->background = 1;
            arg[proc->argc - 1] = NULL;
            proc->argc = proc->argc - 1;
        }
    }
    if (proc->argc > 2) {
        if (strcmp(">", arg[proc->argc - 2]) == 0) {
            fileName = arg[proc->argc - 1];
            arg[proc->argc - 2] = NULL;
            proc->argc = proc->argc - 2;
            proc->stdout = open(fileName, O_CREAT | O_WRONLY);
        }
        if (strcmp("<", arg[proc->argc - 2]) == 0) {
            fileName = arg[proc->argc - 1];
            arg[proc->argc - 2] = NULL;
            proc->argc = proc->argc - 2;
            proc->stdin = open(fileName, O_RDONLY);
        }
    }

    proc->argv = arg;
    return proc;
}


int shell(int argc, char *argv[]) {
    char *s = malloc(INPUT_STRING_SIZE + 1);            /* user input string */
    tok_t *t;            /* tokens parsed from input */
    int lineNum = 0;
    int fundex = -1;
    pid_t pid = getpid();        /* get current processes PID */
    pid_t ppid = getppid();    /* get parents PID */
    pid_t cpid, tcpid, cpgid;

    init_shell();
    // printf("%s running as PID %d under %d\n",argv[0],pid,ppid);

    lineNum = 0;
    // fprintf(stdout, "%d: ", lineNum);
    while ((s = freadln(stdin))) {
        t = getToks(s); /* break the line into tokens */
        fundex = lookup(t[0]); /* Is first token a shell literal */
        if (fundex >= 0) cmd_table[fundex].fun(&t[1]);
        else {
            process *proc = create_process(t);
            add_process(proc);
            cpid = fork();
            if (cpid > 0) {
                setpgid(cpid, cpid);
                proc->pid = getpid();
                if (!proc->background && tcgetpgrp(shell_terminal) == shell_pgid) {
                    tcsetpgrp(shell_terminal, cpid);
                }
                if (!proc->background) {
                    waitpid(cpid, NULL, WUNTRACED);
                    tcsetpgrp(shell_terminal, shell_pgid);
                } else {
                    tcsetpgrp(shell_terminal, shell_pgid);
                }
            } else if (cpid == 0) {
                proc->pid = getpid();
                struct sigaction sa;
                if (!proc->background && tcgetpgrp(shell_terminal) == shell_pgid) {
                    tcsetpgrp(shell_terminal, getppid());
                }
                sa.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sa, 0);
                sigaction(SIGQUIT, &sa, 0);
                sigaction(SIGTERM, &sa, 0);
                sigaction(SIGKILL, &sa, 0);
                sigaction(SIGTSTP, &sa, 0);
                sigaction(SIGTTOU, &sa, 0);
                sigaction(SIGCHLD, &sa, 0);
                sigaction(SIGTTIN, &sa, 0);
                launch_process(proc);
                tcsetpgrp(shell_terminal, shell_pgid);
//                printf("hello again");
//                fflush(stdout);

            }
            //fprintf(stdout, "This shell only supports built-ins. Replace this to run programs as commands.\n");
        }
//        while(waitpid(-1,NULL,WNOHANG|WUNTRACED)>0){
//
//        }
        // fprintf(stdout, "%d: ", lineNum);
    }

    return 0;
}


int num_of_args(tok_t *t) {
    int num = 0;
    while (t[num] != 0) {
        num++;
    }
    return num;
}
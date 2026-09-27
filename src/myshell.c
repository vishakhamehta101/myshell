/* ============================================================================
 * myshell.c — A Unix-like shell built from scratch in C
 *
 * Supports:
 *   - External command execution  (ls, pwd, cat, grep ...)
 *   - Built-in commands           (cd, exit)
 *   - Output redirection          (ls > file.txt)
 *   - Append redirection          (ls >> file.txt)
 *   - Input redirection           (wc < file.txt)
 *   - Single pipes                (ls | grep shell)
 *
 * Build:  make            (or: cc -Wall -Wextra src/myshell.c -o myshell)
 * Run:    ./myshell
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

/* ── Constants ────────────────────────────────────────────────────────────── */

#define MAX_INPUT 1024   /* maximum characters accepted per input line */
#define MAX_ARGS  128    /* maximum number of argument tokens per command */

/* ── parse ────────────────────────────────────────────────────────────────── *
 *
 * Splits a raw input string into an array of argument tokens.
 * Detects and extracts redirection operators (>, >> and <).
 *
 * Parameters:
 *   input      — raw input string (will be modified by strtok)
 *   args       — output array of token pointers, NULL-terminated
 *   outfile    — set to the filename after >,  or NULL if none
 *   infile     — set to the filename after <,  or NULL if none
 *   appendfile — set to the filename after >>, or NULL if none
 *
 * Example:
 *   "ls -la > out.txt"
 *   → args       = ["ls", "-la", NULL]
 *   → outfile    = "out.txt"
 *   → infile     = NULL
 *   → appendfile = NULL
 * -------------------------------------------------------------------------- */
void parse(char *input, char *args[], char **outfile, char **infile, char **appendfile) {
    *outfile = NULL;
    *infile  = NULL;
    *appendfile = NULL;

    int i = 0;
    char *token = strtok(input, " \t\n");

    while (token != NULL) {

        if (strcmp(token, ">>") == 0) {
            /* next token is the append filename — don't add to args */
            *appendfile = strtok(NULL, " \t\n");
            token = strtok(NULL, " \t\n");

        }
        else if (strcmp(token, ">") == 0) {
            /* next token is the output filename — don't add to args */
            *outfile = strtok(NULL, " \t\n");
            token = strtok(NULL, " \t\n");

        }
        else if (strcmp(token, "<") == 0) {
            /* next token is the input filename — don't add to args */
            *infile = strtok(NULL, " \t\n");
            token = strtok(NULL, " \t\n");

        }
        else {
            args[i++] = token;
            token = strtok(NULL, " \t\n");
        }
    }

    args[i] = NULL;  /* NULL-terminate — required by execvp */
}

/* ── execute ──────────────────────────────────────────────────────────────── *
 *
 * Forks a child process to run an external command.
 * Handles input/output/append redirection inside the child before exec.
 * Parent waits for the child to finish before returning.
 *
 * Parameters:
 *   args       — NULL-terminated array of command + arguments
 *   outfile    — filename to redirect stdout to (truncate), or NULL
 *   infile     — filename to redirect stdin from, or NULL
 *   appendfile — filename to redirect stdout to (append), or NULL
 * -------------------------------------------------------------------------- */
void execute(char *args[], char **outfile, char **infile, char **appendfile) {
    pid_t pid = fork();

    if (pid < 0) {
        /* fork failed — very rare, system out of resources */
        perror("fork failed");
        return;
    }

    if (pid == 0) {
        /* ── Child process ──────────────────────────────────────────────── */

        /* restore default signal behavior in child */
        signal(SIGINT,  SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        /* output redirection: point stdout at the file (truncate) */
        if (*outfile != NULL) {
            int fd = open(*outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror("open failed"); exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        /* append redirection: point stdout at the file (append) */
        if (*appendfile != NULL) {
            int fd = open(*appendfile, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror("open failed"); exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        /* input redirection: point stdin at the file */
        if (*infile != NULL) {
            int fd = open(*infile, O_RDONLY);
            if (fd < 0) { perror("open failed"); exit(1); }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        execvp(args[0], args);

        /* execvp only returns on failure */
        perror("execvp failed");
        exit(1);
    }

    /* ── Parent process (shell) ─────────────────────────────────────────── */
    wait(NULL);  /* sleep until child finishes */
}

/* ── execute_pipe ─────────────────────────────────────────────────────────── *
 *
 * Handles a single pipe: "left_cmd | right_cmd"
 *
 * Splits input on '|', parses each half, creates a kernel pipe,
 * forks two children, and wires them together:
 *   - Child 1 (left):  stdout → pipe write end
 *   - Child 2 (right): stdin  → pipe read end
 *
 * Parameters:
 *   input — raw input string containing exactly one '|'
 * -------------------------------------------------------------------------- */
void execute_pipe(char *input) {
    /* split the input string on '|' */
    char *pipe_pos = strchr(input, '|');
    if (pipe_pos == NULL) return;

    *pipe_pos = '\0';                   /* terminate left half */
    char *left_string  = input;         /* "ls -la "           */
    char *right_string = pipe_pos + 1;  /* " grep shell"       */

    /* parse both halves into their own argument arrays */
    char *left_args[MAX_ARGS], *right_args[MAX_ARGS];
    char *left_outfile, *left_infile;
    char *right_outfile, *right_infile;
    char *left_appendfile, *right_appendfile;

    parse(left_string,  left_args,  &left_outfile,  &left_infile,  &left_appendfile);
    parse(right_string, right_args, &right_outfile, &right_infile, &right_appendfile);

    /* sanity check — both sides must have a command */
    if (left_args[0] == NULL || right_args[0] == NULL) {
        fprintf(stderr, "myshell: invalid pipe\n");
        return;
    }

    /* create the kernel pipe */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe failed");
        return;
    }

    /* ── Child 1 — runs the left command ──────────────────────────────── */
    pid_t pid1 = fork();
    if (pid1 < 0) {
        perror("fork failed");
        return;
    }

    if (pid1 == 0) {
        signal(SIGINT,  SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        dup2(pipefd[1], STDOUT_FILENO);  /* stdout → pipe write end */
        close(pipefd[0]);                /* don't need read end     */
        close(pipefd[1]);                /* already redirected      */
        execvp(left_args[0], left_args);
        perror("execvp failed");
        exit(1);
    }

    /* ── Child 2 — runs the right command ─────────────────────────────── */
    pid_t pid2 = fork();
    if (pid2 < 0) {
        perror("fork failed");
        return;
    }

    if (pid2 == 0) {
        signal(SIGINT,  SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        dup2(pipefd[0], STDIN_FILENO);   /* stdin → pipe read end   */
        close(pipefd[1]);                /* don't need write end    */
        close(pipefd[0]);                /* already redirected      */
        execvp(right_args[0], right_args);
        perror("execvp failed");
        exit(1);
    }

    /* ── Parent — close pipe ends and wait for both children ──────────── */
    close(pipefd[0]);
    close(pipefd[1]);
    wait(NULL);   /* wait for child 1 */
    wait(NULL);   /* wait for child 2 */
}

/* ── my_signal_handler ────────────────────────────────────────────────────── *
 *
 * Runs when Ctrl+C (SIGINT) reaches the shell. Instead of letting the shell
 * die, it just prints a newline. Child processes restore the default
 * handler, so Ctrl+C still kills whatever command is running.
 * -------------------------------------------------------------------------- */
void my_signal_handler(int sig) {
    (void)sig;
    printf("\n");
}

/* ── handle_builtins ──────────────────────────────────────────────────────── *
 *
 * Runs commands that must execute inside the shell process itself.
 * These cannot be forked because they need to affect the shell's own state.
 *
 * Returns 1 if a built-in was matched and handled.
 * Returns 0 if not a built-in (caller should fork and exec).
 * -------------------------------------------------------------------------- */
int handle_builtins(char *args[]) {

    /* exit — terminate the shell cleanly */
    if (strcmp(args[0], "exit") == 0) {
        exit(0);
    }

    /* cd — change the shell's own working directory */
    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "cd: missing argument\n");
        } else if (chdir(args[1]) < 0) {
            perror("cd failed");
        }
        return 1;
    }

    return 0;  /* not a built-in */
}

/* ── setup_signals ────────────────────────────────────────────────────────── *
 *
 * Installs the shell's signal behavior:
 *   - SIGINT  (Ctrl+C): caught by my_signal_handler, shell keeps running
 *   - SIGTSTP (Ctrl+Z): ignored, so the shell itself can't be suspended
 * -------------------------------------------------------------------------- */
void setup_signals(void) {
    /* SIGINT handler */
    struct sigaction sa;
    sa.sa_handler = my_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    /* SIGTSTP ignored */
    struct sigaction sa_tstp;
    sa_tstp.sa_handler = SIG_IGN;
    sigemptyset(&sa_tstp.sa_mask);
    sa_tstp.sa_flags = 0;
    sigaction(SIGTSTP, &sa_tstp, NULL);
}

/* ── main ─────────────────────────────────────────────────────────────────── *
 *
 * The REPL loop: Read → Parse → Execute → Repeat
 *
 * Each iteration:
 *   1. Print a prompt and flush stdout
 *   2. Read a line of input
 *   3. Check for pipe — dispatch to execute_pipe if found
 *   4. Otherwise parse, check built-ins, then fork and exec
 * -------------------------------------------------------------------------- */
int main(void) {

    setup_signals();

    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    char *outfile;
    char *infile;
    char *appendfile;

    while (1) {

        /* ── Prompt ──────────────────────────────────────────────────────── */
        printf("myshell> ");
        fflush(stdout);

        /* ── Read ────────────────────────────────────────────────────────── */
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;  /* Ctrl+D — exit cleanly */
        }

        /* strip the trailing newline fgets includes */
        input[strcspn(input, "\n")] = '\0';

        /* ── Dispatch ────────────────────────────────────────────────────── */
        if (strchr(input, '|') != NULL) {
            /* input contains a pipe — handle separately */
            execute_pipe(input);

        } else {
            /* standard command — parse then run */
            parse(input, args, &outfile, &infile, &appendfile);

            if (args[0] == NULL) continue;  /* empty input, re-prompt */

            if (!handle_builtins(args)) {
                execute(args, &outfile, &infile, &appendfile);
            }
        }
    }

    return 0;
}

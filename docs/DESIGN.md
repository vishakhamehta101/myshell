# Design notes

This document explains how `myshell` is put together and why it's built the way it is. The [README](../README.md) covers usage; this is for anyone who wants to read the code.

## The big picture

A shell is just a regular program. It doesn't run commands itself; it asks the kernel to create processes and then gets out of the way. Almost everything interesting in this project comes down to a handful of system calls used in the right order.

```text
          ┌──────────────┐
          │  read line   │ ◄─────────────────────┐
          └──────┬───────┘                       │
                 │                               │
          contains '|' ?                         │
           ┌─────┴─────┐                         │
          yes          no                        │
           │           │                         │
   execute_pipe()   parse()                      │
           │           │                         │
           │     built-in? ── yes ──► handle it ─┤
           │           │                         │
           │           no                        │
           │           │                         │
           │      execute()                      │
           │           │                         │
           └───────────┴─────────────────────────┘
```

## Functions

### `main`

Installs signal handling, then loops forever: print a prompt, read a line with `fgets`, strip the newline, and dispatch. `fgets` returning `NULL` means end of input (Ctrl+D), which ends the loop.

The prompt is followed by `fflush(stdout)` because stdout is line-buffered when connected to a terminal. The prompt has no newline, so without the flush it might not appear before the shell blocks waiting for input.

The check for `|` happens *before* parsing. That's because `parse` uses `strtok`, which destroys the original string by writing `\0` bytes into it, so the pipe must be found and split first.

### `parse`

Splits the line into tokens on spaces, tabs, and newlines using `strtok`. When it sees `>`, `>>`, or `<`, it takes the *next* token as a filename and stores it separately instead of adding it to `args`. The argument array is terminated with `NULL`, which `execvp` requires to know where the list ends.

Note that `>>` is checked before `>`. Because tokens are compared as whole strings with `strcmp`, the order doesn't strictly matter here, but it would matter if the parser ever switched to prefix matching.

The tokens are pointers into the original input buffer, not copies. This avoids memory allocation entirely, but it means the buffer must stay alive until the command has been executed.

### `handle_builtins`

Checks for `exit` and `cd`. These must run in the shell's own process:

- `cd` changes the *current process's* working directory. If it ran in a child, the child would change directory and immediately exit, leaving the shell exactly where it was.
- `exit` has to end the shell itself, not a child.

Returns `1` if it handled the command, `0` otherwise.

### `execute`

The classic fork-exec pattern:

1. `fork()` creates a copy of the shell.
2. In the child: restore default signal behavior, set up redirection, then call `execvp()` to become the requested program. `execvp` searches `PATH`, so `ls` resolves to `/bin/ls` automatically.
3. In the parent: `wait()` until the child finishes.

Redirection works because a process's stdin and stdout are just file descriptors 0 and 1. The child opens the file, uses `dup2(fd, STDOUT_FILENO)` to make descriptor 1 refer to that file, and closes the original `fd` since it's no longer needed. The program being executed has no idea; it just writes to stdout as normal.

Doing this *after* `fork` and *before* `exec` is the key idea. Redirecting in the parent would redirect the shell itself.

File flags:

| Operator | Flags | Meaning |
|----------|-------|---------|
| `>`  | `O_WRONLY \| O_CREAT \| O_TRUNC`  | create if missing, empty it if it exists |
| `>>` | `O_WRONLY \| O_CREAT \| O_APPEND` | create if missing, write at the end |
| `<`  | `O_RDONLY` | read only |

New files are created with mode `0644` (owner can read and write, everyone else can read).

If `execvp` returns at all, it failed (for example, the command doesn't exist), so the child prints an error and calls `exit(1)`. Without that `exit`, the failed child would fall back into the shell's main loop and you'd have two shells running.

### `execute_pipe`

Handles `left | right`:

1. Find the `|`, replace it with `\0`, and treat the two halves as separate strings.
2. Parse each half into its own argument array.
3. `pipe(pipefd)` creates a one-way channel: `pipefd[1]` is the write end, `pipefd[0]` is the read end.
4. Fork the left child: `dup2(pipefd[1], STDOUT_FILENO)`, so its output goes into the pipe.
5. Fork the right child: `dup2(pipefd[0], STDIN_FILENO)`, so its input comes from the pipe.
6. The parent closes both ends and waits for both children.

**Why closing matters:** a reader only sees end-of-file when *every* copy of the write end is closed. After `fork`, the parent and both children all hold both ends. If the parent (or the right child) kept the write end open, a command like `grep` or `wc` would wait for more input forever. That's why every process closes the ends it isn't using.

Both children run at the same time, so data streams from left to right as it's produced rather than all at once.

### `setup_signals` and `my_signal_handler`

When you press Ctrl+C, the terminal sends `SIGINT` to every process in the foreground process group, which includes both the shell and whatever command is running. The default action is to terminate, which would kill the shell.

- `SIGINT` is caught by `my_signal_handler`, which just prints a newline. `SA_RESTART` tells the kernel to resume interrupted system calls like the `read` inside `fgets` instead of making them fail.
- `SIGTSTP` (Ctrl+Z) is ignored so the shell can't be suspended.

Children call `signal(SIGINT, SIG_DFL)` to restore the default. Strictly speaking, `exec` already resets *caught* signals to default, but it keeps *ignored* signals ignored, so the `SIGTSTP` reset is genuinely needed. Resetting both explicitly makes the intent clear.

`sigaction` is used rather than `signal` in the shell because its behavior is consistent across systems, while `signal`'s semantics historically varied.

## Design decisions and trade-offs

**Single file.** The project is small enough that splitting it across files would add navigation cost without much benefit. This would change if features like job control or a proper tokenizer were added.

**No dynamic allocation.** Fixed arrays (`MAX_INPUT`, `MAX_ARGS`) keep the code simple and free of memory leaks, at the cost of hard limits on input size.

**`strtok` for parsing.** Simple and good enough for space-separated words, but it can't handle quotes and isn't reentrant. A real tokenizer (a small state machine that walks the string character by character) is the right next step.

**Pipe detected with `strchr` before parsing.** Simple, but it means a `|` anywhere, even one that should be inside quotes, will be treated as a pipe. That's acceptable only because quotes aren't supported yet.

**`wait(NULL)` instead of `waitpid`.** Works because there's only ever one foreground job and no background processes. Once background jobs exist, the shell will need `waitpid` with a specific PID so it doesn't accidentally collect the wrong child.

## Known issues

See the [Limitations](../README.md#limitations) section of the README. The most interesting one technically: pressing Ctrl+Z while a command runs stops the child, but `wait()` only returns when a child *terminates*, so the shell blocks forever. The fix is `waitpid(pid, &status, WUNTRACED)` and checking `WIFSTOPPED(status)`, which is also the first step toward job control.

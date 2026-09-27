# myshell

A small Unix shell written from scratch in C, built to understand how shells actually work under the hood: how commands get launched, how `>` and `|` are wired up, and how signals like Ctrl+C are handled.

It is intentionally minimal (about 300 lines, one source file, no dependencies beyond the C standard library and POSIX), so the whole thing can be read and understood in one sitting.

> **Status:** work in progress. The core features below work; see [Limitations](#limitations) and [Roadmap](#roadmap) for what's missing.

## Features

- Runs external programs found on your `PATH` (`ls`, `grep`, `cat`, ...)
- Built-in commands: `cd` and `exit`
- Output redirection: `cmd > file` (overwrite)
- Append redirection: `cmd >> file`
- Input redirection: `cmd < file`
- A single pipe: `cmd1 | cmd2`
- Ctrl+C stops the running command, not the shell
- Ctrl+D exits the shell

## Demo

```text
$ ./myshell
myshell> echo hello > notes.txt
myshell> echo world >> notes.txt
myshell> cat < notes.txt
hello
world
myshell> ls | grep src
src
myshell> cd /tmp
myshell> pwd
/tmp
myshell> exit
```

## Getting started

You need a C compiler (`gcc` or `clang`) and `make`, on Linux or macOS.

```bash
git clone https://github.com/<your-username>/myshell.git
cd myshell
make
./myshell
```

Other useful targets:

```bash
make run     # build and start the shell
make clean   # remove the compiled binary
```

To build without `make`:

```bash
cc -Wall -Wextra src/myshell.c -o myshell
```

## How it works

The shell is a loop that reads a line, figures out what it means, runs it, and repeats (a REPL).

1. **Read:** print `myshell> ` and read a line with `fgets`.
2. **Parse:** split the line into words with `strtok`. Redirection operators (`>`, `>>`, `<`) and the filename after them are pulled out separately, so they're never passed to the program as arguments.
3. **Built-ins:** `cd` and `exit` run inside the shell itself. They *have* to: a child process changing its directory wouldn't affect the shell, because each process has its own working directory.
4. **Execute:** for everything else, the shell calls `fork()` to create a copy of itself. The child sets up any redirection with `open()` + `dup2()`, then replaces itself with the real program using `execvp()`. The parent waits with `wait()`.
5. **Pipes:** if the line contains `|`, the shell creates a kernel pipe with `pipe()`, forks two children, and connects the left command's output to the right command's input using `dup2()`.

For a function-by-function walkthrough and the reasoning behind each design decision, see [docs/DESIGN.md](docs/DESIGN.md).

### System calls used

| Call | What it's used for |
|------|--------------------|
| `fork` | Create a child process to run a command |
| `execvp` | Replace the child with the requested program |
| `wait` | Make the shell pause until the command finishes |
| `open` | Open files for redirection |
| `dup2` | Point stdin/stdout at a file or pipe |
| `pipe` | Create the channel between two piped commands |
| `chdir` | Implement `cd` |
| `sigaction` | Keep Ctrl+C / Ctrl+Z from killing or suspending the shell |

## Project structure

```text
myshell/
├── src/
│   └── myshell.c     # the entire shell
├── docs/
│   └── DESIGN.md     # design notes and code walkthrough
├── Makefile
├── LICENSE
└── README.md
```

## Limitations

These are known and deliberate gaps, listed here so nobody is surprised:

- **Only one pipe.** `a | b` works; `a | b | c` does not.
- **No redirection inside pipes.** `ls | grep x > out.txt` parses the `> out.txt` but ignores it.
- **No quotes or escapes.** `echo "hello world"` is split into two arguments, `"hello` and `world"`.
- **No background jobs.** `&` isn't supported, and there's no job control (`fg`, `bg`, `jobs`).
- **Ctrl+Z on a running command hangs the shell.** The child stops, but the shell's `wait()` only returns for children that *exit*, so it keeps waiting.
- **Fixed-size buffers.** Input is limited to 1024 characters and 128 arguments per command.
- **No `cd` with no arguments.** Real shells go to `$HOME`; this one prints an error.
- **No environment variables, globbing (`*.c`), `;`, `&&`, or `||`.**
- The Ctrl+C handler uses `printf`, which isn't technically safe to call from a signal handler, and the prompt isn't reprinted after Ctrl+C.

## Roadmap

- [ ] Multiple pipes (`a | b | c`)
- [ ] Redirection combined with pipes
- [ ] Quoted arguments
- [ ] `cd` with no argument goes to `$HOME`
- [ ] Background processes with `&`
- [ ] Handle stopped children (`waitpid` with `WUNTRACED`)
- [ ] Use `write()` in the signal handler and reprint the prompt
- [ ] Command history

## What I learned

- Why `cd` must be a built-in while `ls` can't be (processes don't share a working directory).
- How `fork` + `exec` splits "make a new process" from "run a program", and why that split makes redirection easy: the child can rearrange its own file descriptors before `exec` without touching the shell.
- Why every unused pipe end has to be closed: if any process still holds the write end open, the reader never sees end-of-file and hangs.
- How signal handlers are inherited through `fork` but reset on `exec` when set to a custom function, and why children need to restore default behaviour explicitly.

## License

[MIT](LICENSE)

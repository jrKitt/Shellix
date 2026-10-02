# Shellix

> **A Lightweight Unix Shell for Linux**

Shellix is a lightweight graphical Unix shell built with **C** and **GTK4** for Linux.

The project is designed to combine a modern terminal interface with core **Operating System concepts**, including process management, system calls, inter-process communication, file I/O, and signal handling.

---

## Overview

Traditional Unix shells provide powerful command-line interfaces, but they can be intimidating for beginners and provide limited visual feedback.

**Shellix** aims to provide a modern graphical interface while maintaining the fundamental behavior and architecture of a Unix shell.

```text
User
  │
  ▼
┌──────────────────────┐
│     Shellix GUI      │
│       GTK4 / C       │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│     Shell Engine     │
│   Command Processing  │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│     Linux / POSIX    │
│ fork / exec / wait   │
└──────────────────────┘
```

---

## Features

### v0.1

* Modern graphical terminal interface
* Dark mode interface
* Terminal output display
* Command input
* Application status bar
* Linux-oriented terminal environment

### Planned

* Command execution
* Process creation with `fork()`
* Program execution with `execvp()`
* Parent process synchronization with `waitpid()`
* Pipe support
* Input/output redirection
* Background processes
* Signal handling
* Command history
* Multiple terminal tabs
* Terminal settings

---

## Technology Stack

| Technology | Purpose                   |
| ---------- | ------------------------- |
| **C**      | Core programming language |
| **GTK4**   | Graphical User Interface  |
| **Linux**  | Target operating system   |
| **POSIX**  | System interface          |
| **GCC**    | C compiler                |
| **Make**   | Build system              |

---

## Operating System Concepts

Shellix is designed around several fundamental Operating System concepts.

### Process Management

```c
fork()
execvp()
waitpid()
```

Used to create, execute, and synchronize processes.

### Inter-Process Communication

```c
pipe()
```

Used to allow processes to communicate with each other.

Example:

```bash
ls | grep txt
```

### File Management

Shellix will support Unix file descriptors for:

```text
stdin   → 0
stdout  → 1
stderr  → 2
```

This enables input and output redirection.

```bash
ls > output.txt
cat < input.txt
```

### Signal Handling

Signals allow Shellix to interact with running processes.

Examples:

```text
SIGINT
SIGCHLD
SIGTERM
```

---

## Project Structure

```text
shellix/
├── src/
│   ├── main.c
│   ├── window.c
│   ├── window.h
│   ├── terminal.c
│   ├── terminal.h
│   ├── shell.c
│   └── shell.h
│
├── assets/
│   └── icons/
│
├── out/
│   └── shellix
│
├── Makefile
└── README.md
```

### Source Code

**`main.c`**

Application entry point and GTK application initialization.

**`window.c`**

Handles the main Shellix application window and GUI layout.

**`window.h`**

Header definitions for the window module.

**`terminal.c`**

Handles terminal interface and terminal output.

**`terminal.h`**

Header definitions for the terminal module.

**`shell.c`**

Shell engine responsible for command processing and process management.

**`shell.h`**

Header definitions for the shell module.

---

## Installation

### Requirements

Shellix currently targets Linux systems.

Install the required dependencies on Arch Linux:

```bash
sudo pacman -S gcc gtk4 make
```

Verify GTK4:

```bash
pkg-config --modversion gtk4
```

---

## Build

Clone the repository:

```bash
git clone <repository-url>
cd shellix
```

Build the project:

```bash
make
```

The executable will be generated at:

```text
out/shellix
```

---

## Run

Run Shellix with:

```bash
./out/shellix
```

Or use:

```bash
make run
```

---

## Clean

Remove the compiled executable:

```bash
make clean
```

---

## Development Roadmap

```text
v0.1
 │
 ├── GUI
 ├── Terminal Interface
 ├── Command Input
 └── Output Display
 │
 ▼
v0.2
 │
 ├── fork()
 ├── exec()
 └── waitpid()
 │
 ▼
v0.3
 │
 ├── Pipe |
 ├── Output Redirection >
 └── Input Redirection <
 │
 ▼
v0.4
 │
 ├── Background Processes &
 ├── Process Management
 └── Signal Handling
 │
 ▼
v1.0
 │
 ├── Command History
 ├── Multiple Tabs
 ├── Settings
 └── UI Improvements
```

---

## Architecture

Shellix is separated into three main layers:

```text
┌─────────────────────────────────────────┐
│                GUI Layer                │
│             GTK4 / C                   │
│                                         │
│  Window · Header · Input · Terminal     │
└────────────────────┬────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────┐
│             Terminal Layer              │
│                                         │
│      Input Handling · Output Display    │
└────────────────────┬────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────┐
│              Shell Layer                │
│                                         │
│ Command Parser · Process Management     │
│ fork() · exec() · waitpid() · pipe()    │
└────────────────────┬────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────┐
│              Linux / POSIX              │
└─────────────────────────────────────────┘
```

---

## Example

The intended Shellix experience:

```text
┌──────────────────────────────────────────────────────────────┐
│  ●  ●  ●                         Shellix                     │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Shellix Terminal                                            │
│                                                              │
│  jrKitt@arch ~ $ ls                                          │
│  Desktop  Documents  Downloads  Music  Pictures             │
│                                                              │
│  jrKitt@arch ~ $ pwd                                         │
│  /home/jrKitt                                                 │
│                                                              │
│  jrKitt@arch ~ $                                             │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│  ~/home/jrKitt                                    ● Connected │
└──────────────────────────────────────────────────────────────┘
```

---

## Project Goals

Shellix focuses on understanding how a Unix shell interacts with the Operating System.

The main goals are:

1. Understand process creation and execution.
2. Understand Unix system calls.
3. Understand parent and child processes.
4. Understand inter-process communication.
5. Understand file descriptors and I/O redirection.
6. Understand Unix signal handling.
7. Build a usable GUI application using C and GTK4.

---

## Academic Context

**Project:** Shellix v0.1
**Course:** Operating Systems
**Language:** C
**Framework:** GTK4
**Platform:** Linux
**Architecture:** POSIX

---

## License

This project is developed for educational purposes.

License information will be added in a future release.

---

## Shellix

```text
C · GTK4 · Linux · POSIX

shellix $
```

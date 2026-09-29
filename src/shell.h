#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>

/* exitStatus values that do not come from a child process. */
#define SHELLIX_EXIT_SYNTAX_ERROR 2
#define SHELLIX_EXIT_INTERNAL_ERROR (-1)

/* Side effect the caller has to carry out, since only it owns the UI. */
typedef enum {
    SHELL_ACTION_NONE,
    SHELL_ACTION_CLEAR,
    SHELL_ACTION_EXIT
} ShellAction;

/*
 * Result of running one command line.
 *
 * output      combined stdout/stderr of the pipeline, always a valid
 *             NUL-terminated string (may be empty), owned by the caller
 * length      number of bytes in output, excluding the terminator
 * exitStatus  exit code of the last pipeline stage, 128 + signal number when
 *             that stage was killed, 127 when the program could not be
 *             executed, or one of the SHELLIX_EXIT_* values above
 * action      UI side effect requested by a builtin such as clear or exit
 */
typedef struct {
    char *output;
    size_t length;
    int exitStatus;
    ShellAction action;
} CommandResult;

/* Runs command as a pipeline of real processes (fork + execvp + pipe). */
CommandResult executeCommand(const char *command);

/* Releases memory held by a result produced by executeCommand. */
void freeCommandResult(CommandResult *result);

/* Login name and short host name, cached, never NULL. */
const char *shellixUserName(void);
const char *shellixHostName(void);

/* Current directory with $HOME collapsed to "~". Caller frees. */
char *shellixDirectoryLabel(void);

#endif

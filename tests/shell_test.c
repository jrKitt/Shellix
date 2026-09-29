/*
 * Tests for the Shellix command execution engine.
 *
 * Builds against src/shell.c only, so it needs no GTK. Run with `make test`.
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shell.h"

static int failures = 0;

static void check(const char *name, const char *command,
                  const char *expectedOutput, int expectedStatus) {
    CommandResult result = executeCommand(command);
    int okOutput = (expectedOutput == NULL) ||
                   (result.output && strcmp(result.output, expectedOutput) == 0);
    int okStatus = (result.exitStatus == expectedStatus);

    if (okOutput && okStatus) {
        printf("ok   %s\n", name);
    } else {
        failures++;
        printf("FAIL %s\n", name);
        printf("       command: %s\n", command ? command : "(null)");
        if (!okOutput) {
            printf("       want output: [%s]\n", expectedOutput);
            printf("       got  output: [%s]\n",
                   result.output ? result.output : "(null)");
        }
        if (!okStatus) {
            printf("       want status: %d got: %d\n", expectedStatus,
                   result.exitStatus);
        }
    }

    if (result.output && strlen(result.output) != result.length) {
        failures++;
        printf("FAIL %s: length field disagrees with strlen\n", name);
    }

    freeCommandResult(&result);
}

static void checkContains(const char *name, const char *command,
                          const char *needle, int expectedStatus) {
    CommandResult result = executeCommand(command);
    int okOutput = result.output && strstr(result.output, needle) != NULL;
    int okStatus = result.exitStatus == expectedStatus;

    if (okOutput && okStatus) {
        printf("ok   %s\n", name);
    } else {
        failures++;
        printf("FAIL %s\n", name);
        printf("       command: %s\n", command);
        if (!okOutput) {
            printf("       want substring: [%s]\n", needle);
            printf("       got output: [%s]\n",
                   result.output ? result.output : "(null)");
        }
        if (!okStatus) {
            printf("       want status: %d got: %d\n", expectedStatus,
                   result.exitStatus);
        }
    }

    freeCommandResult(&result);
}

static int openFdCount(void) {
    DIR *dir = opendir("/proc/self/fd");
    struct dirent *entry;
    int count = 0;

    if (dir == NULL) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }

    closedir(dir);

    return count;
}

/* Descriptors and children must not accumulate across many pipelines. */
static void checkResourceHygiene(void) {
    int before;
    int after;
    int round;

    for (round = 0; round < 3; round++) {
        CommandResult warmup = executeCommand("echo warmup | cat | cat");
        freeCommandResult(&warmup);
    }

    before = openFdCount();

    for (round = 0; round < 200; round++) {
        CommandResult a = executeCommand("echo x | cat | tr a-z A-Z | cat");
        CommandResult b = executeCommand("no_such_binary_here | cat");
        CommandResult c = executeCommand("seq 1 100000 | head -1");

        freeCommandResult(&a);
        freeCommandResult(&b);
        freeCommandResult(&c);
    }

    after = openFdCount();

    if (before > 0 && before == after) {
        printf("ok   no descriptor leak over 600 pipelines\n");
    } else {
        failures++;
        printf("FAIL descriptor count went from %d to %d\n", before, after);
    }

    {
        int status = 0;
        pid_t leftover = waitpid(-1, &status, WNOHANG);

        if (leftover == -1 && errno == ECHILD) {
            printf("ok   no unreaped children\n");
        } else {
            failures++;
            printf("FAIL leftover child pid=%d\n", (int)leftover);
        }
    }
}

/* Builtins run in-process, so their effects must outlive the call. */
static void checkBuiltins(void) {
    CommandResult result;

    result = executeCommand("cd /tmp");
    if (result.exitStatus == 0) {
        printf("ok   cd succeeds\n");
    } else {
        failures++;
        printf("FAIL cd /tmp returned %d: %s\n", result.exitStatus,
               result.output ? result.output : "");
    }
    freeCommandResult(&result);

    /* The directory change must persist in this process, which is the whole
     * reason cd cannot be a forked child. */
    result = executeCommand("pwd");
    if (result.output && strcmp(result.output, "/tmp\n") == 0) {
        printf("ok   cd persists into the next command\n");
    } else {
        failures++;
        printf("FAIL pwd after cd gave [%s]\n",
               result.output ? result.output : "(null)");
    }
    freeCommandResult(&result);

    /* An external program must see the new directory too. */
    check("child inherits cwd", "sh -c 'pwd'", "/tmp\n", 0);

    result = executeCommand("cd -");
    freeCommandResult(&result);
    result = executeCommand("pwd");
    if (result.output && strcmp(result.output, "/tmp\n") != 0) {
        printf("ok   cd - returns to the previous directory\n");
    } else {
        failures++;
        printf("FAIL cd - stayed at /tmp\n");
    }
    freeCommandResult(&result);

    checkContains("cd into missing directory", "cd /no/such/place/xyz",
                  "No such file", 1);
    checkContains("cd rejects extra arguments", "cd a b", "too many", 1);

    result = executeCommand("clear");
    if (result.action == SHELL_ACTION_CLEAR) {
        printf("ok   clear requests a clear action\n");
    } else {
        failures++;
        printf("FAIL clear action was %d\n", (int)result.action);
    }
    freeCommandResult(&result);

    result = executeCommand("exit 3");
    if (result.action == SHELL_ACTION_EXIT && result.exitStatus == 3) {
        printf("ok   exit carries its status\n");
    } else {
        failures++;
        printf("FAIL exit action=%d status=%d\n", (int)result.action,
               result.exitStatus);
    }
    freeCommandResult(&result);

    /* In a pipeline the name is treated as an ordinary program, not a builtin. */
    result = executeCommand("cd /tmp");
    freeCommandResult(&result);
    check("pwd inside a pipeline still works", "pwd | tr -d '\\n'", "/tmp", 0);
}

static void checkDeepPipeline(void) {
    char command[4096];
    CommandResult result;
    size_t used = 0;
    int stage;

    used += (size_t)snprintf(command + used, sizeof(command) - used, "echo deep");
    for (stage = 0; stage < 60; stage++) {
        used += (size_t)snprintf(command + used, sizeof(command) - used, " | cat");
    }

    result = executeCommand(command);

    if (result.output && strcmp(result.output, "deep\n") == 0 &&
        result.exitStatus == 0) {
        printf("ok   61-stage pipeline\n");
    } else {
        failures++;
        printf("FAIL 61-stage pipeline: [%s] status=%d\n",
               result.output ? result.output : "(null)", result.exitStatus);
    }

    freeCommandResult(&result);
}

int main(void) {
    /* fork + exec */
    check("simple command", "echo hello", "hello\n", 0);
    check("argument splitting", "echo a   b\tc", "a b c\n", 0);
    check("exit status propagates", "false", "", 1);
    check("exit code value", "sh -c 'exit 42'", "", 42);

    /* quoting */
    check("single quotes", "echo 'a  b'", "a  b\n", 0);
    check("double quotes", "echo \"a  b\"", "a  b\n", 0);
    check("escaped space", "echo a\\ b", "a b\n", 0);
    check("quoted pipe stays literal", "echo 'a | b'", "a | b\n", 0);
    check("empty quoted argument", "printf '[%s]' ''", "[]", 0);

    /* pipe */
    check("two stage pipe", "echo hello | tr a-z A-Z", "HELLO\n", 0);
    check("three stage pipe", "printf 'b\\na\\nc\\n' | sort | tr -d '\\n'",
          "abc", 0);
    check("status comes from last stage", "false | echo done", "done\n", 0);
    check("last stage failure surfaces", "echo x | false", "", 1);

    /* stderr shares the capture pipe */
    check("stderr is captured", "sh -c 'echo oops >&2'", "oops\n", 0);

    /* output larger than one pipe buffer exercises the drain loop */
    checkContains("output larger than pipe buffer", "seq 1 200000 | tail -1",
                  "200000", 0);

    /* a reader that exits early must not leave the writer spinning */
    checkContains("reader exits early", "seq 1 500000 | head -2", "1\n2\n", 0);

    /* exec failures */
    checkContains("missing program", "definitely_not_a_real_binary_xyz",
                  "definitely_not_a_real_binary_xyz", 127);
    checkContains("missing program inside pipeline",
                  "echo hi | definitely_not_a_real_binary_xyz", "shellix:", 127);

    /* signal death reports 128 + signal */
    check("killed by SIGTERM", "sh -c 'kill -TERM $$'", "", 128 + 15);

    /* syntax errors */
    checkContains("leading pipe", "| cat", "syntax error", 2);
    checkContains("trailing pipe", "echo hi |", "syntax error", 2);
    checkContains("empty stage between pipes", "echo hi || cat", "syntax error", 2);
    checkContains("unterminated quote", "echo 'abc", "unterminated quote", 2);

    /* empty input */
    check("empty string", "", "", 0);
    check("whitespace only", "   \t  ", "", 0);
    check("null pointer", NULL, "", 0);

    /* stdin is /dev/null, so a reader must finish instead of blocking */
    check("stdin reads empty", "cat", "", 0);

    checkBuiltins();
    checkDeepPipeline();
    checkResourceHygiene();

    printf("\n%s\n", failures == 0 ? "all tests passed" : "some tests failed");

    return failures == 0 ? 0 : 1;
}

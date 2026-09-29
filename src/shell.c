/*
 * Command execution engine for Shellix.
 *
 * A command line is tokenized, split on '|' into stages, and each stage is run
 * as its own process. Stages are connected with pipe(2) and dup2(2); the last
 * stage writes into a capture pipe that the parent drains, so the caller gets
 * the pipeline output as a string instead of it landing on the real terminal.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shell.h"

#define READ_CHUNK 4096

/* ---------- growable byte buffer ---------- */

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} ByteBuffer;

static void bufferInit(ByteBuffer *buffer) {
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

/* Keeps one spare byte so the contents can always be NUL-terminated. */
static int bufferReserve(ByteBuffer *buffer, size_t extra) {
    size_t needed = buffer->length + extra + 1;
    size_t capacity;
    char *grown;

    if (needed <= buffer->capacity) {
        return 0;
    }

    capacity = buffer->capacity ? buffer->capacity : 64;
    while (capacity < needed) {
        capacity *= 2;
    }

    grown = realloc(buffer->data, capacity);
    if (grown == NULL) {
        return -1;
    }

    buffer->data = grown;
    buffer->capacity = capacity;

    return 0;
}

static int bufferAppend(ByteBuffer *buffer, const char *bytes, size_t count) {
    if (bufferReserve(buffer, count) != 0) {
        return -1;
    }

    memcpy(buffer->data + buffer->length, bytes, count);
    buffer->length += count;
    buffer->data[buffer->length] = '\0';

    return 0;
}

static int bufferAppendChar(ByteBuffer *buffer, char value) {
    return bufferAppend(buffer, &value, 1);
}

static int bufferAppendText(ByteBuffer *buffer, const char *text) {
    return bufferAppend(buffer, text, strlen(text));
}

static void bufferFree(ByteBuffer *buffer) {
    free(buffer->data);
    bufferInit(buffer);
}

/* ---------- tokenizer ---------- */

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE
} TokenKind;

typedef struct {
    TokenKind kind;
    char *text; /* owned, NULL for TOKEN_PIPE */
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

static void tokenListInit(TokenList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void tokenListFree(TokenList *list) {
    size_t index;

    for (index = 0; index < list->count; index++) {
        free(list->items[index].text);
    }

    free(list->items);
    tokenListInit(list);
}

static int tokenListPush(TokenList *list, TokenKind kind, char *text) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 8;
        Token *grown = realloc(list->items, capacity * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }

        list->items = grown;
        list->capacity = capacity;
    }

    list->items[list->count].kind = kind;
    list->items[list->count].text = text;
    list->count++;

    return 0;
}

/*
 * Splits command into words and pipe operators. Understands single quotes
 * (literal), double quotes (backslash escapes allowed) and bare backslash
 * escapes. Returns 0 on success, -1 on allocation failure, and 1 when a quote
 * is left open.
 */
static int tokenize(const char *command, TokenList *list) {
    ByteBuffer word;
    int haveWord = 0;
    size_t index = 0;
    int status = 0;

    bufferInit(&word);

    while (command[index] != '\0') {
        char current = command[index];

        if (current == ' ' || current == '\t' || current == '\n' ||
            current == '\r') {
            if (haveWord) {
                if (tokenListPush(list, TOKEN_WORD, word.data) != 0) {
                    status = -1;
                    goto done;
                }
                bufferInit(&word);
                haveWord = 0;
            }
            index++;
            continue;
        }

        if (current == '|') {
            if (haveWord) {
                if (tokenListPush(list, TOKEN_WORD, word.data) != 0) {
                    status = -1;
                    goto done;
                }
                bufferInit(&word);
                haveWord = 0;
            }
            if (tokenListPush(list, TOKEN_PIPE, NULL) != 0) {
                status = -1;
                goto done;
            }
            index++;
            continue;
        }

        /* Anything else contributes to the current word. */
        haveWord = 1;

        if (current == '\'') {
            index++;
            while (command[index] != '\0' && command[index] != '\'') {
                if (bufferAppendChar(&word, command[index]) != 0) {
                    status = -1;
                    goto done;
                }
                index++;
            }
            if (command[index] == '\0') {
                status = 1; /* unterminated */
                goto done;
            }
            index++;
            continue;
        }

        if (current == '"') {
            index++;
            while (command[index] != '\0' && command[index] != '"') {
                char inner = command[index];

                if (inner == '\\' && command[index + 1] != '\0') {
                    char next = command[index + 1];

                    if (next == '"' || next == '\\' || next == '$' ||
                        next == '`') {
                        inner = next;
                        index++;
                    }
                }

                if (bufferAppendChar(&word, inner) != 0) {
                    status = -1;
                    goto done;
                }
                index++;
            }
            if (command[index] == '\0') {
                status = 1; /* unterminated */
                goto done;
            }
            index++;
            continue;
        }

        if (current == '\\' && command[index + 1] != '\0') {
            index++;
            if (bufferAppendChar(&word, command[index]) != 0) {
                status = -1;
                goto done;
            }
            index++;
            continue;
        }

        if (bufferAppendChar(&word, current) != 0) {
            status = -1;
            goto done;
        }
        index++;
    }

    if (haveWord) {
        /* A quoted empty string still needs backing storage. */
        if (word.data == NULL && bufferAppendChar(&word, '\0') != 0) {
            status = -1;
            goto done;
        }
        if (tokenListPush(list, TOKEN_WORD, word.data) != 0) {
            status = -1;
            goto done;
        }
        bufferInit(&word);
    }

done:
    bufferFree(&word);

    return status;
}

/* ---------- pipeline stages ---------- */

/* argv entries point into the TokenList, so a stage never owns the strings. */
typedef struct {
    char **argv;
    size_t count;
    size_t capacity;
} Stage;

typedef struct {
    Stage *items;
    size_t count;
    size_t capacity;
} StageList;

static void stageListInit(StageList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void stageListFree(StageList *list) {
    size_t index;

    for (index = 0; index < list->count; index++) {
        free(list->items[index].argv);
    }

    free(list->items);
    stageListInit(list);
}

static Stage *stageListAppend(StageList *list) {
    Stage *stage;

    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 4;
        Stage *grown = realloc(list->items, capacity * sizeof(*grown));

        if (grown == NULL) {
            return NULL;
        }

        list->items = grown;
        list->capacity = capacity;
    }

    stage = &list->items[list->count];
    stage->argv = NULL;
    stage->count = 0;
    stage->capacity = 0;
    list->count++;

    return stage;
}

static int stagePushArgument(Stage *stage, char *argument) {
    if (stage->count + 1 >= stage->capacity) {
        size_t capacity = stage->capacity ? stage->capacity * 2 : 8;
        char **grown = realloc(stage->argv, capacity * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }

        stage->argv = grown;
        stage->capacity = capacity;
    }

    stage->argv[stage->count] = argument;
    stage->count++;
    stage->argv[stage->count] = NULL; /* room reserved above */

    return 0;
}

/*
 * Groups tokens into stages. Returns 0 on success, -1 on allocation failure,
 * and 1 when a '|' has no command on one side.
 */
static int buildStages(TokenList *tokens, StageList *stages) {
    Stage *current = NULL;
    size_t index;

    for (index = 0; index < tokens->count; index++) {
        Token *token = &tokens->items[index];

        if (token->kind == TOKEN_PIPE) {
            if (current == NULL || current->count == 0) {
                return 1; /* nothing to the left of '|' */
            }
            current = NULL;
            continue;
        }

        if (current == NULL) {
            current = stageListAppend(stages);
            if (current == NULL) {
                return -1;
            }
        }

        if (stagePushArgument(current, token->text) != 0) {
            return -1;
        }
    }

    /* Trailing '|' leaves the last stage unopened. */
    if (tokens->count > 0 && tokens->items[tokens->count - 1].kind == TOKEN_PIPE) {
        return 1;
    }

    return 0;
}

/* ---------- child processes ---------- */

/* write(2) loop: the child runs after fork(), where stdio is best avoided. */
static void writeAll(int fd, const char *text) {
    size_t remaining = strlen(text);

    while (remaining > 0) {
        ssize_t written = write(fd, text, remaining);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }

        text += (size_t)written;
        remaining -= (size_t)written;
    }
}

/* Never closes the standard descriptors. */
static void closeExtraFd(int fd) {
    if (fd > STDERR_FILENO) {
        close(fd);
    }
}

typedef struct {
    int stdinFd;      /* becomes stdin */
    int stdoutFd;     /* becomes stdout and stderr */
    int closeFds[3];  /* pipe ends this stage must not hold open, -1 to skip */
} ChildFds;

/* Runs in the forked child and never returns. */
static void runChild(Stage *stage, const ChildFds *fds) {
    size_t index;

    /* The parent ignores SIGPIPE; children need the default so that a reader
     * exiting early (head, grep -q) terminates the writer instead of making it
     * spin on EPIPE. */
    signal(SIGPIPE, SIG_DFL);

    if (fds->stdinFd != STDIN_FILENO &&
        dup2(fds->stdinFd, STDIN_FILENO) < 0) {
        writeAll(STDERR_FILENO, "shellix: cannot redirect stdin\n");
        _exit(126);
    }

    if (dup2(fds->stdoutFd, STDOUT_FILENO) < 0 ||
        dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
        writeAll(STDERR_FILENO, "shellix: cannot redirect stdout\n");
        _exit(126);
    }

    for (index = 0; index < sizeof(fds->closeFds) / sizeof(fds->closeFds[0]);
         index++) {
        if (fds->closeFds[index] >= 0) {
            closeExtraFd(fds->closeFds[index]);
        }
    }

    closeExtraFd(fds->stdinFd);
    closeExtraFd(fds->stdoutFd);

    execvp(stage->argv[0], stage->argv);

    /* Only reached when the program could not be started. */
    writeAll(STDERR_FILENO, "shellix: ");
    writeAll(STDERR_FILENO, stage->argv[0]);
    writeAll(STDERR_FILENO, ": ");
    writeAll(STDERR_FILENO, strerror(errno));
    writeAll(STDERR_FILENO, "\n");
    _exit(127);
}

/* ---------- pipeline ---------- */

static int statusToExitCode(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return SHELLIX_EXIT_INTERNAL_ERROR;
}

/* Drains fd to completion so a child can never block on a full pipe. */
static int drainFd(int fd, ByteBuffer *out) {
    char chunk[READ_CHUNK];

    for (;;) {
        ssize_t got = read(fd, chunk, sizeof(chunk));

        if (got > 0) {
            if (bufferAppend(out, chunk, (size_t)got) != 0) {
                return -1;
            }
            continue;
        }

        if (got == 0) {
            return 0;
        }

        if (errno == EINTR) {
            continue;
        }

        return -1;
    }
}

static void runPipeline(StageList *stages, CommandResult *result) {
    ByteBuffer output;
    pid_t *pids;
    int captureFds[2];
    int prevRead = -1;
    int devNull;
    void (*previousSigpipe)(int);
    size_t spawned = 0;
    size_t index;
    int spawnFailed = 0;
    int spawnErrno = 0;
    int readFailed = 0;
    int lastStatus = 0;

    bufferInit(&output);

    pids = calloc(stages->count, sizeof(*pids));
    if (pids == NULL) {
        result->output = NULL;
        result->length = 0;
        result->exitStatus = SHELLIX_EXIT_INTERNAL_ERROR;
        return;
    }

    if (pipe(captureFds) < 0) {
        free(pids);
        bufferAppendText(&output, "shellix: pipe: ");
        bufferAppendText(&output, strerror(errno));
        bufferAppendChar(&output, '\n');
        result->output = output.data;
        result->length = output.length;
        result->exitStatus = SHELLIX_EXIT_INTERNAL_ERROR;
        return;
    }

    /* Feed the head of the pipeline from /dev/null: there is no interactive
     * input behind a GUI text view, and inheriting stdin would let a stage
     * such as `cat` block forever. */
    devNull = open("/dev/null", O_RDONLY);

    previousSigpipe = signal(SIGPIPE, SIG_IGN);

    for (index = 0; index < stages->count; index++) {
        int isLast = (index + 1 == stages->count);
        int stageOut;
        int nextRead = -1;
        int stagePipe[2];
        ChildFds fds;
        pid_t pid;

        if (isLast) {
            stageOut = captureFds[1];
        } else {
            if (pipe(stagePipe) < 0) {
                spawnFailed = 1;
                spawnErrno = errno;
                break;
            }
            nextRead = stagePipe[0];
            stageOut = stagePipe[1];
        }

        fds.stdinFd = (prevRead >= 0) ? prevRead : devNull;
        fds.stdoutFd = stageOut;
        fds.closeFds[0] = captureFds[0];
        fds.closeFds[1] = isLast ? -1 : captureFds[1];
        fds.closeFds[2] = nextRead;

        if (fds.stdinFd < 0) {
            fds.stdinFd = STDIN_FILENO; /* /dev/null unavailable */
        }

        pid = fork();
        if (pid < 0) {
            spawnFailed = 1;
            spawnErrno = errno;
            closeExtraFd(nextRead);
            if (!isLast) {
                closeExtraFd(stageOut);
            }
            break;
        }

        if (pid == 0) {
            runChild(&stages->items[index], &fds);
        }

        pids[spawned] = pid;
        spawned++;

        closeExtraFd(prevRead);
        if (!isLast) {
            closeExtraFd(stageOut);
        }
        prevRead = nextRead;
    }

    closeExtraFd(prevRead);
    closeExtraFd(devNull);
    close(captureFds[1]); /* children hold the remaining write ends */

    if (drainFd(captureFds[0], &output) != 0) {
        readFailed = 1;
    }
    close(captureFds[0]);

    for (index = 0; index < spawned; index++) {
        int status = 0;

        while (waitpid(pids[index], &status, 0) < 0) {
            if (errno != EINTR) {
                status = 0;
                break;
            }
        }

        if (index + 1 == spawned) {
            lastStatus = status;
        }
    }

    signal(SIGPIPE, previousSigpipe);
    free(pids);

    if (spawnFailed) {
        bufferAppendText(&output, "shellix: cannot start pipeline: ");
        bufferAppendText(&output, strerror(spawnErrno));
        bufferAppendChar(&output, '\n');
        result->exitStatus = SHELLIX_EXIT_INTERNAL_ERROR;
    } else if (readFailed) {
        bufferAppendText(&output, "shellix: failed to read command output\n");
        result->exitStatus = SHELLIX_EXIT_INTERNAL_ERROR;
    } else {
        result->exitStatus = statusToExitCode(lastStatus);
    }

    if (output.data == NULL) {
        output.data = strdup("");
    }

    result->output = output.data;
    result->length = output.length;
}

/* ---------- identity helpers ---------- */

const char *shellixUserName(void) {
    static char cached[256];

    if (cached[0] == '\0') {
        const char *name = getenv("USER");

        if (name == NULL || name[0] == '\0') {
            struct passwd *entry = getpwuid(getuid());

            name = (entry != NULL) ? entry->pw_name : "user";
        }

        snprintf(cached, sizeof(cached), "%s", name);
    }

    return cached;
}

const char *shellixHostName(void) {
    static char cached[256];

    if (cached[0] == '\0') {
        char raw[256];
        char *dot;

        if (gethostname(raw, sizeof(raw)) != 0) {
            snprintf(raw, sizeof(raw), "localhost");
        }
        raw[sizeof(raw) - 1] = '\0';

        /* Keep the short name only: arch, not arch.local. */
        dot = strchr(raw, '.');
        if (dot != NULL) {
            *dot = '\0';
        }

        snprintf(cached, sizeof(cached), "%s", raw);
    }

    return cached;
}

char *shellixDirectoryLabel(void) {
    char path[PATH_MAX];
    const char *home = getenv("HOME");
    size_t homeLength;

    if (getcwd(path, sizeof(path)) == NULL) {
        return strdup("?");
    }

    if (home == NULL || home[0] == '\0') {
        return strdup(path);
    }

    homeLength = strlen(home);
    if (strncmp(path, home, homeLength) == 0 &&
        (path[homeLength] == '\0' || path[homeLength] == '/')) {
        ByteBuffer label;

        bufferInit(&label);
        if (bufferAppendChar(&label, '~') != 0 ||
            bufferAppendText(&label, path + homeLength) != 0) {
            bufferFree(&label);
            return strdup(path);
        }

        return label.data;
    }

    return strdup(path);
}

/* ---------- builtins ---------- */

static CommandResult makeMessageResult(const char *message, int exitStatus) {
    CommandResult result;
    char *copy = strdup(message);

    result.output = copy;
    result.length = (copy != NULL) ? strlen(copy) : 0;
    result.exitStatus = (copy != NULL) ? exitStatus : SHELLIX_EXIT_INTERNAL_ERROR;
    result.action = SHELL_ACTION_NONE;

    return result;
}

static const char *builtinNames[] = {"cd", "pwd", "clear", "exit", NULL};

static int isBuiltin(const char *name) {
    size_t index;

    for (index = 0; builtinNames[index] != NULL; index++) {
        if (strcmp(name, builtinNames[index]) == 0) {
            return 1;
        }
    }

    return 0;
}

/* cd has to run in this process; a child's chdir would die with the child. */
static CommandResult runCd(Stage *stage) {
    CommandResult result = {NULL, 0, 0, SHELL_ACTION_NONE};
    ByteBuffer message;
    const char *target;
    char previous[PATH_MAX];
    char updated[PATH_MAX];

    if (stage->count > 2) {
        return makeMessageResult("shellix: cd: too many arguments\n", 1);
    }

    if (stage->count == 1) {
        target = getenv("HOME");
        if (target == NULL || target[0] == '\0') {
            return makeMessageResult("shellix: cd: HOME is not set\n", 1);
        }
    } else if (strcmp(stage->argv[1], "-") == 0) {
        target = getenv("OLDPWD");
        if (target == NULL || target[0] == '\0') {
            return makeMessageResult("shellix: cd: OLDPWD is not set\n", 1);
        }
    } else {
        target = stage->argv[1];
    }

    if (getcwd(previous, sizeof(previous)) == NULL) {
        previous[0] = '\0';
    }

    if (chdir(target) != 0) {
        int failure = errno;

        bufferInit(&message);
        bufferAppendText(&message, "shellix: cd: ");
        bufferAppendText(&message, target);
        bufferAppendText(&message, ": ");
        bufferAppendText(&message, strerror(failure));
        bufferAppendChar(&message, '\n');

        result.output = message.data ? message.data : strdup("");
        result.length = message.length;
        result.exitStatus = 1;

        return result;
    }

    if (previous[0] != '\0') {
        setenv("OLDPWD", previous, 1);
    }
    if (getcwd(updated, sizeof(updated)) != NULL) {
        setenv("PWD", updated, 1);
    }

    return makeMessageResult("", 0);
}

static CommandResult runPwd(void) {
    CommandResult result = {NULL, 0, 0, SHELL_ACTION_NONE};
    ByteBuffer message;
    char path[PATH_MAX];

    if (getcwd(path, sizeof(path)) == NULL) {
        return makeMessageResult("shellix: pwd: cannot read directory\n", 1);
    }

    bufferInit(&message);
    bufferAppendText(&message, path);
    bufferAppendChar(&message, '\n');

    result.output = message.data ? message.data : strdup("");
    result.length = message.length;

    return result;
}

static CommandResult runBuiltin(Stage *stage) {
    const char *name = stage->argv[0];
    CommandResult result;

    if (strcmp(name, "cd") == 0) {
        return runCd(stage);
    }

    if (strcmp(name, "pwd") == 0) {
        return runPwd();
    }

    result = makeMessageResult("", 0);

    if (strcmp(name, "clear") == 0) {
        result.action = SHELL_ACTION_CLEAR;
    } else if (strcmp(name, "exit") == 0) {
        result.action = SHELL_ACTION_EXIT;
        if (stage->count > 1) {
            result.exitStatus = atoi(stage->argv[1]);
        }
    }

    return result;
}

/* ---------- public API ---------- */

CommandResult executeCommand(const char *command) {
    CommandResult result = {NULL, 0, 0, SHELL_ACTION_NONE};
    TokenList tokens;
    StageList stages;
    int status;

    tokenListInit(&tokens);
    stageListInit(&stages);

    if (command == NULL) {
        return makeMessageResult("", 0);
    }

    status = tokenize(command, &tokens);
    if (status != 0) {
        tokenListFree(&tokens);
        if (status == 1) {
            return makeMessageResult("shellix: unexpected end of input: "
                                     "unterminated quote\n",
                                     SHELLIX_EXIT_SYNTAX_ERROR);
        }
        return makeMessageResult("shellix: out of memory\n",
                                 SHELLIX_EXIT_INTERNAL_ERROR);
    }

    status = buildStages(&tokens, &stages);
    if (status != 0) {
        stageListFree(&stages);
        tokenListFree(&tokens);
        if (status == 1) {
            return makeMessageResult("shellix: syntax error near '|'\n",
                                     SHELLIX_EXIT_SYNTAX_ERROR);
        }
        return makeMessageResult("shellix: out of memory\n",
                                 SHELLIX_EXIT_INTERNAL_ERROR);
    }

    if (stages.count == 0) {
        /* Blank line. */
        stageListFree(&stages);
        tokenListFree(&tokens);
        return makeMessageResult("", 0);
    }

    /* Builtins must run in this process: a forked child could not change our
     * directory or close the window. Only meaningful for a lone stage. */
    if (stages.count == 1 && isBuiltin(stages.items[0].argv[0])) {
        result = runBuiltin(&stages.items[0]);
        stageListFree(&stages);
        tokenListFree(&tokens);
        return result;
    }

    runPipeline(&stages, &result);

    stageListFree(&stages);
    tokenListFree(&tokens);

    return result;
}

void freeCommandResult(CommandResult *result) {
    if (result == NULL) {
        return;
    }

    free(result->output);
    result->output = NULL;
    result->length = 0;
}

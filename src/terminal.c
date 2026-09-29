/*
 * Interactive terminal view.
 *
 * The text buffer holds the whole transcript. Everything before the mark
 * "input" is finished output and must not change; the text after it is the line
 * the user is typing. Pressing Return hands that line to executeCommand(),
 * which forks the real processes, and the captured output is appended.
 */

#include <stdlib.h>
#include <string.h>

#include "shell.h"
#include "terminal.h"

#define HISTORY_LIMIT 256

struct TerminalSession {
    GtkTextView *view;
    GtkTextBuffer *buffer;
    GtkLabel *pathLabel;
    GtkWindow *window;

    /* Start of the editable input line. */
    GtkTextMark *inputMark;

    char *history[HISTORY_LIMIT];
    size_t historyCount;
    /* Cursor while browsing with Up/Down; equals historyCount when idle. */
    size_t historyCursor;
};

/* ---------- output ---------- */

static void appendTagged(TerminalSession *session, const char *text,
                         const char *tagName) {
    GtkTextIter end;

    if (text == NULL || text[0] == '\0') {
        return;
    }

    gtk_text_buffer_get_end_iter(session->buffer, &end);

    if (tagName != NULL) {
        gtk_text_buffer_insert_with_tags_by_name(session->buffer, &end, text, -1,
                                                 tagName, NULL);
    } else {
        gtk_text_buffer_insert(session->buffer, &end, text, -1);
    }
}

static void scrollToEnd(TerminalSession *session) {
    GtkTextIter end;

    gtk_text_buffer_get_end_iter(session->buffer, &end);
    gtk_text_view_scroll_to_iter(session->view, &end, 0.0, TRUE, 0.0, 1.0);
}

static void refreshPathLabel(TerminalSession *session) {
    char *label;

    if (session->pathLabel == NULL) {
        return;
    }

    label = shellixDirectoryLabel();
    gtk_label_set_text(session->pathLabel, label);
    free(label);
}

/* Writes "user@host path $ " and parks the input mark after it. */
static void showPrompt(TerminalSession *session) {
    GtkTextIter end;
    char *directory = shellixDirectoryLabel();
    char *identity = g_strdup_printf("%s@%s", shellixUserName(),
                                     shellixHostName());

    gtk_text_buffer_get_end_iter(session->buffer, &end);
    if (gtk_text_iter_get_line_offset(&end) != 0) {
        appendTagged(session, "\n", NULL);
    }

    appendTagged(session, identity, "promptUser");
    appendTagged(session, " ", NULL);
    appendTagged(session, directory, "promptPath");
    appendTagged(session, " $ ", "promptSign");

    gtk_text_buffer_get_end_iter(session->buffer, &end);
    gtk_text_buffer_move_mark(session->buffer, session->inputMark, &end);
    gtk_text_buffer_place_cursor(session->buffer, &end);

    g_free(identity);
    free(directory);

    scrollToEnd(session);
}

/* ---------- input line ---------- */

static char *readInputLine(TerminalSession *session) {
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_iter_at_mark(session->buffer, &start, session->inputMark);
    gtk_text_buffer_get_end_iter(session->buffer, &end);

    return gtk_text_buffer_get_text(session->buffer, &start, &end, FALSE);
}

static void replaceInputLine(TerminalSession *session, const char *text) {
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_iter_at_mark(session->buffer, &start, session->inputMark);
    gtk_text_buffer_get_end_iter(session->buffer, &end);
    gtk_text_buffer_delete(session->buffer, &start, &end);

    if (text != NULL && text[0] != '\0') {
        gtk_text_buffer_get_end_iter(session->buffer, &end);
        gtk_text_buffer_insert(session->buffer, &end, text, -1);
    }

    gtk_text_buffer_get_end_iter(session->buffer, &end);
    gtk_text_buffer_place_cursor(session->buffer, &end);
    scrollToEnd(session);
}

/* True when the cursor or selection reaches into the finished transcript. */
static gboolean cursorIsBeforeInput(TerminalSession *session) {
    GtkTextIter inputStart;
    GtkTextIter selectionStart;
    GtkTextIter selectionEnd;

    gtk_text_buffer_get_iter_at_mark(session->buffer, &inputStart,
                                     session->inputMark);
    gtk_text_buffer_get_selection_bounds(session->buffer, &selectionStart,
                                         &selectionEnd);

    return gtk_text_iter_compare(&selectionStart, &inputStart) < 0;
}

/* Keeps edits confined to the input line. */
static void clampCursorToInput(TerminalSession *session) {
    GtkTextIter end;

    if (!cursorIsBeforeInput(session)) {
        return;
    }

    gtk_text_buffer_get_end_iter(session->buffer, &end);
    gtk_text_buffer_place_cursor(session->buffer, &end);
}

static void historyAdd(TerminalSession *session, const char *command) {
    if (command[0] == '\0') {
        return;
    }

    /* Skip an immediate repeat, like a shell with ignoredups. */
    if (session->historyCount > 0 &&
        strcmp(session->history[session->historyCount - 1], command) == 0) {
        session->historyCursor = session->historyCount;
        return;
    }

    if (session->historyCount == HISTORY_LIMIT) {
        size_t index;

        free(session->history[0]);
        for (index = 1; index < HISTORY_LIMIT; index++) {
            session->history[index - 1] = session->history[index];
        }
        session->historyCount--;
    }

    session->history[session->historyCount] = g_strdup(command);
    session->historyCount++;
    session->historyCursor = session->historyCount;
}

/* ---------- running a command ---------- */

static void clearTranscript(TerminalSession *session) {
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_bounds(session->buffer, &start, &end);
    gtk_text_buffer_delete(session->buffer, &start, &end);
}

static void submitLine(TerminalSession *session) {
    char *line = readInputLine(session);
    CommandResult result;

    appendTagged(session, "\n", NULL);
    historyAdd(session, line);

    result = executeCommand(line);

    if (result.action == SHELL_ACTION_EXIT) {
        g_free(line);
        freeCommandResult(&result);
        if (session->window != NULL) {
            gtk_window_close(session->window);
        }
        return;
    }

    if (result.action == SHELL_ACTION_CLEAR) {
        clearTranscript(session);
    } else if (result.output != NULL && result.output[0] != '\0') {
        /* A failing command's text is almost always a diagnostic, so it gets
         * the error colour; successful output stays neutral. */
        const char *tag = (result.exitStatus == 0) ? "output" : "error";

        appendTagged(session, result.output, tag);

        if (result.output[result.length - 1] != '\n') {
            appendTagged(session, "\n", NULL);
        }
    }

    refreshPathLabel(session);
    freeCommandResult(&result);
    g_free(line);

    showPrompt(session);
}

/* ---------- keyboard ---------- */

static void browseHistory(TerminalSession *session, int direction) {
    if (session->historyCount == 0) {
        return;
    }

    if (direction < 0) {
        if (session->historyCursor == 0) {
            return;
        }
        session->historyCursor--;
        replaceInputLine(session, session->history[session->historyCursor]);
        return;
    }

    if (session->historyCursor >= session->historyCount) {
        return;
    }

    session->historyCursor++;
    if (session->historyCursor == session->historyCount) {
        replaceInputLine(session, "");
    } else {
        replaceInputLine(session, session->history[session->historyCursor]);
    }
}

static gboolean onKeyPressed(GtkEventControllerKey *controller, guint keyval,
                             guint keycode, GdkModifierType state,
                             gpointer userData) {
    TerminalSession *session = userData;
    gboolean control = (state & GDK_CONTROL_MASK) != 0;

    (void)controller;
    (void)keycode;

    if (control && (keyval == GDK_KEY_l || keyval == GDK_KEY_L)) {
        clearTranscript(session);
        showPrompt(session);
        return TRUE;
    }

    if (control && (keyval == GDK_KEY_c || keyval == GDK_KEY_C)) {
        /* Copy when something is selected, otherwise abandon the line. */
        if (gtk_text_buffer_get_has_selection(session->buffer)) {
            return FALSE;
        }
        appendTagged(session, "^C\n", "promptSign");
        showPrompt(session);
        return TRUE;
    }

    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        clampCursorToInput(session);
        submitLine(session);
        return TRUE;
    }

    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up) {
        browseHistory(session, -1);
        return TRUE;
    }

    if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down) {
        browseHistory(session, 1);
        return TRUE;
    }

    if (keyval == GDK_KEY_Home || keyval == GDK_KEY_KP_Home) {
        GtkTextIter inputStart;

        gtk_text_buffer_get_iter_at_mark(session->buffer, &inputStart,
                                         session->inputMark);
        gtk_text_buffer_place_cursor(session->buffer, &inputStart);
        return TRUE;
    }

    /* Backspace must not eat into the prompt. */
    if (keyval == GDK_KEY_BackSpace) {
        GtkTextIter inputStart;
        GtkTextIter cursor;

        gtk_text_buffer_get_iter_at_mark(session->buffer, &inputStart,
                                         session->inputMark);
        gtk_text_buffer_get_iter_at_mark(session->buffer, &cursor,
                                         gtk_text_buffer_get_insert(session->buffer));

        if (!gtk_text_buffer_get_has_selection(session->buffer) &&
            gtk_text_iter_compare(&cursor, &inputStart) <= 0) {
            return TRUE;
        }
    }

    /* Plain typing: make sure it lands in the editable region. */
    if (!control) {
        clampCursorToInput(session);
    }

    return FALSE;
}

/* ---------- setup ---------- */

static void createTags(TerminalSession *session) {
    gtk_text_buffer_create_tag(session->buffer, "promptUser", "foreground",
                               "#4ADE80", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(session->buffer, "promptPath", "foreground",
                               "#38BDF8", NULL);
    gtk_text_buffer_create_tag(session->buffer, "promptSign", "foreground",
                               "#64748B", NULL);
    gtk_text_buffer_create_tag(session->buffer, "output", "foreground",
                               "#E5E7EB", NULL);
    gtk_text_buffer_create_tag(session->buffer, "error", "foreground",
                               "#F87171", NULL);
    gtk_text_buffer_create_tag(session->buffer, "banner", "foreground",
                               "#38BDF8", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(session->buffer, "hint", "foreground",
                               "#64748B", NULL);
}

static void onSessionDestroy(gpointer data, GObject *whereTheObjectWas) {
    TerminalSession *session = data;
    size_t index;

    (void)whereTheObjectWas;

    for (index = 0; index < session->historyCount; index++) {
        g_free(session->history[index]);
    }

    free(session);
}

TerminalSession *terminalSessionNew(GtkTextView *terminalView,
                                    GtkLabel *pathLabel, GtkWindow *window) {
    TerminalSession *session = calloc(1, sizeof(*session));
    GtkEventController *keys;
    GtkTextIter start;

    if (session == NULL) {
        return NULL;
    }

    session->view = terminalView;
    session->buffer = gtk_text_view_get_buffer(terminalView);
    session->pathLabel = pathLabel;
    session->window = window;

    createTags(session);

    gtk_text_buffer_get_end_iter(session->buffer, &start);
    session->inputMark = gtk_text_buffer_create_mark(session->buffer, "input",
                                                     &start, TRUE);

    /* The view is editable so the caret behaves normally; the key handler is
     * what keeps the transcript itself read-only. */
    gtk_text_view_set_editable(terminalView, TRUE);
    gtk_text_view_set_cursor_visible(terminalView, TRUE);

    keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(onKeyPressed), session);
    gtk_widget_add_controller(GTK_WIDGET(terminalView), keys);

    g_object_weak_ref(G_OBJECT(terminalView), onSessionDestroy, session);

    return session;
}

void terminalSessionStart(TerminalSession *session) {
    appendTagged(session, "Shellix Terminal", "banner");
    appendTagged(session, "  v0.1\n", "hint");
    appendTagged(session,
                 "real fork/exec/pipe  ·  try: ls | sort  ·  Ctrl+L clears\n",
                 "hint");

    refreshPathLabel(session);
    showPrompt(session);

    gtk_widget_grab_focus(GTK_WIDGET(session->view));
}

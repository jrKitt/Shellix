/*
 * Drives the real TerminalSession the way a user would: insert text into the
 * input line, emit the Return key, then inspect the transcript.
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"
#include "terminal.h"

static int failures = 0;
static GtkTextView *view;
static GtkTextBuffer *buffer;
static GtkEventController *keys;

static char *transcript(void) {
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);

    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void typeText(const char *text) {
    GtkTextIter end;

    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text, -1);
}

static void pressKey(guint keyval, GdkModifierType state) {
    gboolean handled = FALSE;

    g_signal_emit_by_name(keys, "key-pressed", keyval, 0u, state, &handled);
}

static void expectContains(const char *name, const char *needle) {
    char *text = transcript();

    if (strstr(text, needle) != NULL) {
        printf("ok   %s\n", name);
    } else {
        failures++;
        printf("FAIL %s: missing [%s]\n", name, needle);
        printf("       transcript: %s\n", text);
    }

    g_free(text);
}

static void expectMissing(const char *name, const char *needle) {
    char *text = transcript();

    if (strstr(text, needle) == NULL) {
        printf("ok   %s\n", name);
    } else {
        failures++;
        printf("FAIL %s: should not contain [%s]\n", name, needle);
    }

    g_free(text);
}

static GtkEventController *findKeyController(GtkWidget *widget) {
    GListModel *controllers = gtk_widget_observe_controllers(widget);
    guint count = g_list_model_get_n_items(controllers);
    GtkEventController *found = NULL;
    guint index;

    for (index = 0; index < count; index++) {
        GtkEventController *candidate = g_list_model_get_item(controllers, index);

        if (GTK_IS_EVENT_CONTROLLER_KEY(candidate)) {
            found = candidate;
            break;
        }
        g_object_unref(candidate);
    }

    g_object_unref(controllers);

    return found;
}

static void runTests(void) {
    char *label;

    /* fork + exec through the GUI */
    typeText("echo hello_from_gui");
    pressKey(GDK_KEY_Return, 0);
    expectContains("echo runs", "hello_from_gui");

    /* pipe through the GUI */
    typeText("printf 'b\\na\\n' | sort | tr -d '\\n'");
    pressKey(GDK_KEY_Return, 0);
    expectContains("pipeline runs", "ab");

    /* prompt is redrawn with user@host */
    expectContains("prompt shown", "@");

    /* cd changes the process directory and the prompt follows */
    typeText("cd /tmp");
    pressKey(GDK_KEY_Return, 0);
    typeText("pwd");
    pressKey(GDK_KEY_Return, 0);
    expectContains("cd then pwd", "/tmp");

    /* error output is captured */
    typeText("no_such_command_abc");
    pressKey(GDK_KEY_Return, 0);
    expectContains("missing command reported", "no_such_command_abc");

    /* history: Up should refill the previous line */
    pressKey(GDK_KEY_Up, 0);
    {
        char *text = transcript();
        if (g_str_has_suffix(text, "no_such_command_abc")) {
            printf("ok   history recalls last command\n");
        } else {
            failures++;
            printf("FAIL history recall, tail was: %s\n",
                   text + (strlen(text) > 40 ? strlen(text) - 40 : 0));
        }
        g_free(text);
    }

    /* Ctrl+C abandons the line */
    pressKey(GDK_KEY_c, GDK_CONTROL_MASK);
    expectContains("ctrl+c cancels", "^C");

    /* clear wipes the transcript */
    typeText("clear");
    pressKey(GDK_KEY_Return, 0);
    expectMissing("clear wipes transcript", "hello_from_gui");

    /* backspace at the prompt must not delete the prompt itself */
    {
        char *before;
        char *after;

        before = transcript();
        pressKey(GDK_KEY_BackSpace, 0);
        after = transcript();

        if (strcmp(before, after) == 0) {
            printf("ok   backspace stops at prompt\n");
        } else {
            failures++;
            printf("FAIL backspace ate into the prompt\n");
        }

        g_free(before);
        g_free(after);
    }

    label = shellixDirectoryLabel();
    printf("status path label: %s\n", label ? label : "(null)");
    free(label);
}

static void onActivate(GtkApplication *app, gpointer data) {
    GtkWidget *window;
    GtkWidget *textView;
    GtkWidget *pathLabel;
    TerminalSession *session;

    (void)data;

    window = gtk_application_window_new(app);
    textView = gtk_text_view_new();
    pathLabel = gtk_label_new("~");
    gtk_window_set_child(GTK_WINDOW(window), textView);

    view = GTK_TEXT_VIEW(textView);
    buffer = gtk_text_view_get_buffer(view);

    session = terminalSessionNew(view, GTK_LABEL(pathLabel), GTK_WINDOW(window));
    terminalSessionStart(session);

    keys = findKeyController(textView);
    if (keys == NULL) {
        printf("FAIL no key controller attached\n");
        failures++;
    } else {
        runTests();
    }

    printf("\n%s\n", failures == 0 ? "gui tests passed" : "gui tests failed");

    g_application_quit(G_APPLICATION(app));
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("dev.shellix.guitest",
                                              G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(onActivate), NULL);
    g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return failures == 0 ? 0 : 1;
}

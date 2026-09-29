#ifndef TERMINAL_H
#define TERMINAL_H

#include <gtk/gtk.h>

/*
 * Owns the interactive state of one terminal view: the read-only transcript,
 * the editable input line after the prompt, and the command history.
 */
typedef struct TerminalSession TerminalSession;

/*
 * Attaches input handling to terminalView. pathLabel is updated with the
 * working directory after every command; window is closed by `exit`.
 * The session lives as long as terminalView.
 */
TerminalSession *terminalSessionNew(GtkTextView *terminalView,
                                    GtkLabel *pathLabel,
                                    GtkWindow *window);

/* Prints the banner and the first prompt. */
void terminalSessionStart(TerminalSession *session);

#endif

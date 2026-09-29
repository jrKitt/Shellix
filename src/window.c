#include <gtk/gtk.h>

#include "terminal.h"
#include "window.h"

static const char *STYLE =
    "window {"
    "  background-color: #0A0D12;"
    "}"
    ".titlebar {"
    "  background-color: #11151C;"
    "  border-bottom: 1px solid #1E293B;"
    "  min-height: 38px;"
    "}"
    ".titlebar-title {"
    "  color: #94A3B8;"
    "  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
    "  font-size: 13px;"
    "}"
    ".dot {"
    "  min-width: 12px;"
    "  min-height: 12px;"
    "  padding: 0;"
    "  margin: 0 3px;"
    "  border-radius: 6px;"
    "  border: none;"
    "  box-shadow: none;"
    "  background-image: none;"
    "}"
    ".dot-close { background-color: #FF5F57; }"
    ".dot-min { background-color: #FEBC2E; }"
    ".dot-max { background-color: #28C840; }"
    ".dot:hover { filter: brightness(1.25); }"
    ".terminal-view {"
    "  background-color: #0A0D12;"
    "  color: #E5E7EB;"
    "  font-family: 'JetBrains Mono', 'Fira Code', 'Consolas', monospace;"
    "  font-size: 14px;"
    "  caret-color: #38BDF8;"
    "}"
    ".terminal-view text { background-color: #0A0D12; }"
    "scrolledwindow { background-color: #0A0D12; }"
    ".statusbar {"
    "  background-color: #11151C;"
    "  border-top: 1px solid #1E293B;"
    "  padding: 5px 14px;"
    "}"
    ".status-path {"
    "  color: #64748B;"
    "  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
    "  font-size: 12px;"
    "}"
    ".status-state {"
    "  color: #4ADE80;"
    "  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
    "  font-size: 12px;"
    "}";

static void onCloseClicked(GtkButton *button, gpointer userData) {
    (void)button;
    gtk_window_close(GTK_WINDOW(userData));
}

static void onMinimizeClicked(GtkButton *button, gpointer userData) {
    (void)button;
    gtk_window_minimize(GTK_WINDOW(userData));
}

static void onMaximizeClicked(GtkButton *button, gpointer userData) {
    GtkWindow *window = GTK_WINDOW(userData);

    (void)button;

    if (gtk_window_is_maximized(window)) {
        gtk_window_unmaximize(window);
    } else {
        gtk_window_maximize(window);
    }
}

static GtkWidget *makeDot(const char *styleClass, GCallback handler,
                          GtkWidget *window) {
    GtkWidget *dot = gtk_button_new();

    gtk_widget_add_css_class(dot, "dot");
    gtk_widget_add_css_class(dot, styleClass);
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    gtk_widget_set_can_focus(dot, FALSE);
    g_signal_connect(dot, "clicked", handler, window);

    return dot;
}

/* Traffic-light dots on the left, title centred. */
static GtkWidget *buildTitlebar(GtkWidget *window) {
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *dots = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *title = gtk_label_new("Shellix");

    gtk_widget_add_css_class(header, "titlebar");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), FALSE);

    gtk_widget_set_margin_start(dots, 6);
    gtk_box_append(GTK_BOX(dots),
                   makeDot("dot-close", G_CALLBACK(onCloseClicked), window));
    gtk_box_append(GTK_BOX(dots),
                   makeDot("dot-min", G_CALLBACK(onMinimizeClicked), window));
    gtk_box_append(GTK_BOX(dots),
                   makeDot("dot-max", G_CALLBACK(onMaximizeClicked), window));
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), dots);

    gtk_widget_add_css_class(title, "titlebar-title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title);

    return header;
}

/* Working directory on the left, connection state on the right. */
static GtkWidget *buildStatusbar(GtkWidget **pathLabelOut) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *path = gtk_label_new("~");
    GtkWidget *state = gtk_label_new("● Connected");

    gtk_widget_add_css_class(bar, "statusbar");

    gtk_widget_add_css_class(path, "status-path");
    gtk_label_set_xalign(GTK_LABEL(path), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(path), PANGO_ELLIPSIZE_START);
    gtk_widget_set_hexpand(path, TRUE);
    gtk_box_append(GTK_BOX(bar), path);

    gtk_widget_add_css_class(state, "status-state");
    gtk_box_append(GTK_BOX(bar), state);

    *pathLabelOut = path;

    return bar;
}

void createWindow(GtkApplication *app) {
    GtkWidget *window;
    GtkWidget *mainBox;
    GtkWidget *scrolledWindow;
    GtkWidget *terminalView;
    GtkWidget *statusbar;
    GtkWidget *pathLabel = NULL;
    GtkCssProvider *cssProvider;
    TerminalSession *session;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Shellix");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 650);
    gtk_window_set_titlebar(GTK_WINDOW(window), buildTitlebar(window));

    mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), mainBox);

    scrolledWindow = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolledWindow, TRUE);
    gtk_widget_set_hexpand(scrolledWindow, TRUE);

    terminalView = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(terminalView), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(terminalView), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(terminalView), 18);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(terminalView), 18);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(terminalView), 14);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(terminalView), 14);
    gtk_widget_add_css_class(terminalView, "terminal-view");

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolledWindow),
                                  terminalView);
    gtk_box_append(GTK_BOX(mainBox), scrolledWindow);

    statusbar = buildStatusbar(&pathLabel);
    gtk_box_append(GTK_BOX(mainBox), statusbar);

    cssProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(cssProvider, STYLE);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(cssProvider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(cssProvider);

    session = terminalSessionNew(GTK_TEXT_VIEW(terminalView),
                                 GTK_LABEL(pathLabel), GTK_WINDOW(window));
    if (session != NULL) {
        terminalSessionStart(session);
    }

    gtk_window_present(GTK_WINDOW(window));
}

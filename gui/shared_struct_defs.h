/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef SHARED_STRUCT_DEFS_H
#define SHARED_STRUCT_DEFS_H

#include <gtk/gtk.h>

/**
 * @brief Used to capture auth info when a user logs into the TERA server.
 */
typedef struct {
    char user_no[FIXED_STRING_FIELD_SZ];
    char auth_key[FIXED_STRING_FIELD_SZ];
    char character_count[FIXED_STRING_FIELD_SZ];
    char welcome_label_msg[FIXED_STRING_FIELD_SZ];
} LoginData;

/**
 * @brief Used to help with the manual window dragging callback and storing the
 * state thereof.
 */
typedef struct {
    gboolean dragging;
} DragData;

/**
 * @brief Used to store all the GUI widgets and possibly some state relating to
 * window moves and other things.
 */
typedef struct {
    GtkWidget *window;
    GtkWidget *base_overlay;

    // Login pane
    GtkWidget *login_overlay;
    GtkWindowHandle *login_window_handle;
    GtkWidget *user_entry;
    GtkWidget *pass_entry;
    GtkWidget *login_btn;
    GtkWidget *close_login_btn;

    // Patch/Play pane
    GtkWidget *patch_overlay;
    GtkWindowHandle *patch_window_handle;
    GtkWidget *welcome_label;
    GtkWidget *welcome_label_hbox;
    GtkWidget *footer_label;
    GtkWidget *play_btn;
    GtkWidget *logout_btn;
    GtkWidget *repair_btn;
    GtkWidget *close_patch_btn;
    GtkWidget *update_repair_progress_bar;
    GtkWidget *update_repair_download_bar;

    // Data from do_login
    LoginData login_data;

    // Gesture controllers
    GtkEventController *login_controller;
    GtkEventController *patch_controller;
    DragData drag_data;
} LauncherData;
#endif //SHARED_STRUCT_DEFS_H

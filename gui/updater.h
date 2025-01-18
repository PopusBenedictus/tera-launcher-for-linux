/** This program is free software. It comes without any warranty, to
* the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef UPDATER_H
#define UPDATER_H
#include <gtk/gtk.h>

// Structure to hold file information
typedef struct {
    char *path;
    char *hash;
    unsigned long size;
    unsigned long decompressed_size;
    char *url;
} FileInfo;

// Structure to pass data to updater functions
typedef struct {
    GtkProgressBar *progress_bar;
    GtkProgressBar *download_progress_bar;
    gchar *game_path;
    const char *public_patch_url;
} UpdateData;

// Callback type for progress updates
typedef void (*ProgressCallback)(double progress, const char *message, gpointer user_data);

// Initialize globals for update routines
void updater_init();

// Clean up any globals that require it, don't do this except for during shutdown of the application
void updater_shutdown();

// Function to determine files that need to be updated
GList* get_files_to_update(UpdateData *data, ProgressCallback callback, gpointer user_data);

// Function to determine files that are missing or damaged and initiate download/replacement of them
GList* get_files_to_repair(UpdateData *data, ProgressCallback callback, gpointer user_data);

// Function to download all files that need to be updated
gboolean download_all_files(UpdateData *data, GList *files_to_update, ProgressCallback callback, ProgressCallback download_callback, gpointer user_data);

// Utility function to free FileInfo
void free_file_info(void *info);

#endif // UPDATER_H

/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef OPTIONS_DIALOG_H
#define OPTIONS_DIALOG_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validate a wineprefix name.
 *
 * Checks that the wineprefix name is non-empty and does not contain '/' or '\'
 * characters.
 *
 * @param name The wineprefix name to validate.
 * @return true if the name is valid, false otherwise.
 */
bool validate_wineprefix_name(const char *name);

/**
 * @brief Validate a Wine directory.
 *
 * Checks that the directory exists, contains the required executables in its
 * "bin" subdirectory, and has a "lib" subdirectory.
 *
 * @param path The directory path to validate.
 * @return true if the directory is valid, false otherwise.
 */
bool validate_wine_dir(const char *path);

/**
 * @brief Check if gamemoderun is available.
 *
 * @return true if gamemoderun is found in the system path, false otherwise.
 */
bool check_gamemode_available(void);

/**
 * @brief Check if gamescope is available.
 *
 * @return true if gamescope is found in the system path, false otherwise.
 */
bool check_gamescope_available(void);

/**
 * @brief Validate the TERA Toolbox directory.
 *
 * Checks that the directory contains "TeraToolbox.exe".
 *
 * @param path The directory path to validate.
 * @return true if the toolbox path is valid, false otherwise.
 */
bool validate_toolbox_path(const char *path);

/**
 * @brief Create the options dialog.
 *
 * Constructs and returns a dialog that allows users to configure application
 * settings. The dialog includes input fields for the wineprefix name, Wine base
 * directory, and toggles for gamemode, gamescope, and TERA Toolbox. A Repair
 * button is also provided.
 *
 * @param ld Pointer to the LauncherData instance.
 * @param update_callback Callback function to initiate the update/repair
 * process. This function must have the signature: void
 * (*update_callback)(LauncherData *ld, bool do_repair);
 * @return GtkWidget* The created options dialog.
 */
GtkWidget *create_options_dialog(LauncherData *ld,
                                 void (*update_callback)(LauncherData *ld,
                                                         bool do_repair));

#ifdef __cplusplus
}
#endif

#endif /* OPTIONS_DIALOG_H */

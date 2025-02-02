/**
 * @file options_dialog.c
 * @brief Implementation of the options dialog for the launcher.
 *
 * This file contains functions for creating and handling the options dialog,
 * including file browsing, input validation, and repair functionality.
 */

#include "options_dialog.h"
#include "globals.h"
#include "shared_struct_defs.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

/* Structure for passing repair callback data */
typedef struct {
  LauncherData *ld;
  void (*update_callback)(LauncherData *ld, bool do_repair);
} RepairCallbackData;

/**
 * @brief Validate a wineprefix name.
 *
 * Checks that the wineprefix name is non-empty and does not contain '/' or '\'
 * characters.
 *
 * @param name The wineprefix name to validate.
 * @return true if the name is valid, false otherwise.
 */
bool validate_wineprefix_name(const char *name) {
  if (!name || *name == '\0')
    return false;
  return strpbrk(name, "/\\") == nullptr;
}

/**
 * @brief Validate a Wine directory.
 *
 * Checks that the directory exists, contains an executable "wine" and
 * "wineserver" in its "bin" subdirectory, and has a "lib" subdirectory.
 *
 * @param path The directory path to validate.
 * @return true if the directory is valid, false otherwise.
 */
bool validate_wine_dir(const char *path) {
  if (!path || !g_file_test(path, G_FILE_TEST_IS_DIR))
    return false;
  g_autofree char *wine = g_build_filename(path, "bin", "wine", nullptr);
  g_autofree char *wineserver =
      g_build_filename(path, "bin", "wineserver", nullptr);
  g_autofree char *lib = g_build_filename(path, "lib", nullptr);
  return g_file_test(wine, G_FILE_TEST_IS_EXECUTABLE) &&
         g_file_test(wineserver, G_FILE_TEST_IS_EXECUTABLE) &&
         g_file_test(lib, G_FILE_TEST_IS_DIR);
}

/**
 * @brief Check if gamemoderun is available.
 *
 * @return true if gamemoderun is found in the system path, false otherwise.
 */
bool check_gamemode_available(void) {
  return g_find_program_in_path("gamemoderun") != nullptr;
}

/**
 * @brief Check if gamescope is available.
 *
 * @return true if gamescope is found in the system path, false otherwise.
 */
bool check_gamescope_available(void) {
  return g_find_program_in_path("gamescope") != nullptr;
}

/**
 * @brief Validate the TERA Toolbox directory.
 *
 * Checks that the directory contains "TeraToolbox.exe".
 *
 * @param path The directory path to validate.
 * @return true if the toolbox path is valid, false otherwise.
 */
bool validate_toolbox_path(const char *path) {
  if (!path)
    return false;
  g_autofree char *exe = g_build_filename(path, "TeraToolbox.exe", nullptr);
  return g_file_test(exe, G_FILE_TEST_EXISTS);
}

/**
 * @brief Callback for when a wineprefix directory is selected.
 *
 * Retrieves the selected file path and sets the entry's text to the basename of
 * the path.
 *
 * @param source The file dialog.
 * @param result The asynchronous result.
 * @param user_data Pointer to the GtkEntry widget.
 */
static void on_wineprefix_selected(GObject *source, GAsyncResult *result,
                                   gpointer user_data) {
  GtkEntry *entry = GTK_ENTRY(user_data);
  GError *error = nullptr;
  GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source),
                                                     result, &error);
  if (file) {
    g_autofree const char *path = g_file_get_path(file);
    if (path) {
      const char *name = g_path_get_basename(path);
      gtk_entry_buffer_set_text(gtk_entry_get_buffer(entry), name, -1);
    }
    g_object_unref(file);
  }
  if (error)
    g_error_free(error);
}

/**
 * @brief Callback for the wineprefix browse button.
 *
 * Opens a file chooser dialog to select a wineprefix directory.
 *
 * @param button The clicked button.
 * @param user_data Pointer to the parent GtkWindow.
 */
static void on_wineprefix_browse_clicked(GtkButton *button,
                                         gpointer user_data) {
  const auto parent = GTK_WINDOW(user_data);
  GtkEntry *entry =
      GTK_ENTRY(g_object_get_data(G_OBJECT(parent), "wineprefix-entry"));
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Select Wineprefix Directory");
  gtk_file_dialog_set_modal(dialog, TRUE);
  gtk_file_dialog_select_folder(dialog, parent, nullptr,
                                (GAsyncReadyCallback)on_wineprefix_selected,
                                entry);
}

/**
 * @brief Callback for when a Wine base directory is selected.
 *
 * Retrieves the selected file path and sets it in the associated entry.
 *
 * @param source The file dialog.
 * @param result The asynchronous result.
 * @param user_data Pointer to the GtkEntry widget.
 */
static void on_winebase_selected(GObject *source, GAsyncResult *result,
                                 gpointer user_data) {
  GtkEntry *entry = GTK_ENTRY(user_data);
  GError *error = nullptr;
  GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source),
                                                     result, &error);
  if (file) {
    g_autofree char *path = g_file_get_path(file);
    if (path)
      gtk_entry_buffer_set_text(gtk_entry_get_buffer(entry), path, -1);
    g_object_unref(file);
  }
  if (error)
    g_error_free(error);
}

/**
 * @brief Callback for the Wine base browse button.
 *
 * Opens a file chooser dialog to select a Wine base directory.
 *
 * @param button The clicked button.
 * @param user_data Pointer to the parent GtkWindow.
 */
static void on_winebase_browse_clicked(GtkButton *button, gpointer user_data) {
  GtkWindow *parent = GTK_WINDOW(user_data);
  GtkEntry *entry =
      GTK_ENTRY(g_object_get_data(G_OBJECT(parent), "winebase-entry"));
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Select Wine Base Directory");
  gtk_file_dialog_set_modal(dialog, TRUE);
  gtk_file_dialog_select_folder(dialog, parent, nullptr,
                                (GAsyncReadyCallback)on_winebase_selected,
                                entry);
}

/**
 * @brief Callback for when a TERA Toolbox directory is selected.
 *
 * Retrieves the selected file path and sets it in the associated entry.
 *
 * @param source The file dialog.
 * @param result The asynchronous result.
 * @param user_data Pointer to the GtkEntry widget.
 */
static void on_toolbox_selected(GObject *source, GAsyncResult *result,
                                gpointer user_data) {
  const auto entry = GTK_ENTRY(user_data);
  GError *error = nullptr;
  GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source),
                                                     result, &error);
  if (file) {
    g_autofree const char *path = g_file_get_path(file);
    if (path)
      gtk_entry_buffer_set_text(gtk_entry_get_buffer(entry), path, -1);
    g_object_unref(file);
  }
  if (error)
    g_error_free(error);
}

/**
 * @brief Callback for the TERA Toolbox browse button.
 *
 * Opens a file chooser dialog to select a TERA Toolbox directory.
 *
 * @param button The clicked button.
 * @param user_data Pointer to the parent GtkWindow.
 */
static void on_toolbox_browse_clicked(GtkButton *button, gpointer user_data) {
  (void)button; // Unused
  const auto parent = GTK_WINDOW(user_data);
  const auto entry =
      GTK_ENTRY(g_object_get_data(G_OBJECT(parent), "toolbox-entry"));
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Select TERA Toolbox Folder");
  gtk_file_dialog_set_modal(dialog, TRUE);
  gtk_file_dialog_select_folder(
      dialog, parent, nullptr, (GAsyncReadyCallback)on_toolbox_selected, entry);
}

/**
 * @brief Callback for toggling the TERA Toolbox option.
 *
 * Enables or disables the associated browse button based on the toggle state.
 *
 * @param toggle The check button widget.
 * @param user_data Pointer to the browse button.
 */
static void on_toolbox_toggled(GtkCheckButton *toggle, gpointer user_data) {
  const gboolean active = gtk_check_button_get_active(toggle);
  gtk_widget_set_sensitive(GTK_WIDGET(user_data), active);
}

/**
 * @brief Callback for toggling the Gamescope option.
 *
 * Enables or disables the associated args entry textbox based on the toggle
 * state.
 *
 * @param toggle The check button widget.
 * @param user_data Pointer to the entry widget.
 */
static void on_gamescope_toggled(GtkCheckButton *toggle, gpointer user_data) {
  const gboolean active = gtk_check_button_get_active(toggle);
  gtk_widget_set_sensitive(GTK_WIDGET(user_data), active);
}

/**
 * @brief Display an error dialog.
 *
 * Shows a modal error dialog with the specified message.
 *
 * @param parent The parent window.
 * @param message The error message.
 */
static void show_error_dialog(GtkWindow *parent, const char *message) {
  GtkAlertDialog *alert = gtk_alert_dialog_new(message);
  gtk_alert_dialog_set_modal(alert, TRUE);
  gtk_alert_dialog_show(alert, parent);
}

/**
 * @brief Process the OK response for the options dialog.
 *
 * Retrieves user input from the dialog, validates it, and updates global
 * settings.
 *
 * @param dialog The options dialog.
 */
static void handle_ok_response(GtkDialog *dialog) {
  const auto wineprefix_entry =
      GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "wineprefix-entry"));
  const auto winebase_entry =
      GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "winebase-entry"));
  const auto gamescope_entry =
      GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "gamescope-entry"));
  const auto gamemode_toggle =
      GTK_CHECK_BUTTON(g_object_get_data(G_OBJECT(dialog), "gamemode-toggle"));
  const auto gamescope_toggle =
      GTK_CHECK_BUTTON(g_object_get_data(G_OBJECT(dialog), "gamescope-toggle"));
  const auto toolbox_toggle =
      GTK_CHECK_BUTTON(g_object_get_data(G_OBJECT(dialog), "toolbox-toggle"));
  const auto toolbox_entry =
      GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), "toolbox-entry"));

  const char *wineprefix =
      gtk_entry_buffer_get_text(gtk_entry_get_buffer(wineprefix_entry));
  if (!validate_wineprefix_name(wineprefix)) {
    show_error_dialog(GTK_WINDOW(dialog), "Invalid wineprefix specified");
    return;
  }

  const char *winebase =
      gtk_entry_buffer_get_text(gtk_entry_get_buffer(winebase_entry));
  if (!validate_wine_dir(winebase)) {
    show_error_dialog(GTK_WINDOW(dialog),
                      "Invalid wine base directory specified");
    return;
  }

  const char *gamescope_args =
      gtk_entry_buffer_get_text(gtk_entry_get_buffer(gamescope_entry));
  if (strlen(gamescope_args) == 0) {
    show_error_dialog(GTK_WINDOW(dialog),
                      "Cannot enable gamescope without arguments");
    return;
  }

  const bool new_gamemode = gtk_check_button_get_active(gamemode_toggle);
  if (new_gamemode && !check_gamemode_available()) {
    show_error_dialog(GTK_WINDOW(dialog),
                      "Gamemode not found, will not be enabled");
    return;
  }

  const bool new_gamescope = gtk_check_button_get_active(gamescope_toggle);
  if (new_gamescope && !check_gamescope_available()) {
    show_error_dialog(GTK_WINDOW(dialog),
                      "Gamescope not found, will not be enabled");
    return;
  }

  const bool new_toolbox = gtk_check_button_get_active(toolbox_toggle);
  const char *toolbox_path =
      gtk_entry_buffer_get_text(gtk_entry_get_buffer(toolbox_entry));
  if (new_toolbox && !validate_toolbox_path(toolbox_path)) {
    show_error_dialog(GTK_WINDOW(dialog), "Invalid TERA Toolbox path");
    return;
  }

  size_t required;
  bool success = str_copy_formatted(wineprefix_global, &required,
                                    FIXED_STRING_FIELD_SZ, "%s", wineprefix);
  if (!success) {
    show_error_dialog(GTK_WINDOW(dialog),
                      "Invalid wineprefix specified, changes will be ignored.");
  }

  success = str_copy_formatted(wine_base_dir_global, &required,
                               FIXED_STRING_FIELD_SZ, "%s", winebase);
  if (!success) {
    show_error_dialog(
        GTK_WINDOW(dialog),
        "Invalid wine base directory specified, changes will be ignored.");
  }

  success = str_copy_formatted(gamescope_args_global, &required,
                               FIXED_STRING_FIELD_SZ, "%s", gamescope_args);
  if (!success) {
    show_error_dialog(GTK_WINDOW(dialog),
                      "Gamescope arguments too large for buffer or invalid, "
                      "changes will be ignored.");
  }

  use_gamemoderun = new_gamemode;
  use_gamescope = new_gamescope;
  use_tera_toolbox = new_toolbox;

  if (new_toolbox) {
    success = str_copy_formatted(tera_toolbox_path_global, &required,
                                 FIXED_STRING_FIELD_SZ, "%s", toolbox_path);
    if (!success) {
      show_error_dialog(
          GTK_WINDOW(dialog),
          "Invalid TERA Toolbox directory specified, changes will be ignored.");
    }
  } else {
    memset(tera_toolbox_path_global, 0, FIXED_STRING_FIELD_SZ);
  }
}

/**
 * @brief Callback for the OK button.
 *
 * Processes the OK response and closes the dialog.
 *
 * @param button The clicked OK button.
 * @param user_data Pointer to the options dialog.
 */
static void on_ok_clicked(GtkButton *button, gpointer user_data) {
  const auto dialog = GTK_DIALOG(user_data);
  handle_ok_response(dialog);
  config_write_to_ini();
  gtk_window_close(GTK_WINDOW(dialog));
}

/**
 * @brief Callback for the Cancel button.
 *
 * Closes the dialog without processing input.
 *
 * @param button The clicked Cancel button.
 * @param user_data Pointer to the options dialog.
 */
static void on_cancel_clicked(GtkButton *button, gpointer user_data) {
  GtkDialog *dialog = GTK_DIALOG(user_data);
  gtk_window_close(GTK_WINDOW(dialog));
}

/**
 * @brief Callback for handling repair dialog responses.
 *
 * Processes the response from the repair alert dialog and initiates file repair
 * if confirmed.
 *
 * @param dialog The alert dialog.
 * @param result The asynchronous result.
 * @param user_data Pointer to a RepairCallbackData structure.
 */
static void on_repair_dialog_response(GtkAlertDialog *dialog,
                                      GAsyncResult *result,
                                      gpointer user_data) {
  RepairCallbackData *rcd = user_data;
  LauncherData *ld = rcd->ld;
  void (*update_callback)(LauncherData *ld, bool do_repair) =
      rcd->update_callback;
  GError *error = nullptr;
  const gint response =
      gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(dialog), result, &error);
  if (error) {
    g_warning("Dialog error: %s", error->message);
    g_error_free(error);
    g_free(rcd);
    return;
  }
  if (response == 1) {
    g_message("Initiating file repair");
    update_callback(ld, true);
  } else {
    g_message("Repair canceled");
  }
  g_free(rcd);
}

/**
 * @brief Callback for the Repair button.
 *
 * Creates an alert dialog to confirm repair and initiates the repair process
 * based on the response.
 *
 * @param btn The clicked Repair button.
 * @param user_data Pointer to the options dialog.
 */
static void on_repair_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  const auto dialog = GTK_DIALOG(user_data);
  LauncherData *ld = g_object_get_data(G_OBJECT(dialog), "launcher-data");
  void (*update_callback)(LauncherData *ld, bool do_repair) =
      g_object_get_data(G_OBJECT(dialog), "update-callback");

  const auto rcd = g_new(RepairCallbackData, 1);
  rcd->ld = ld;
  rcd->update_callback = update_callback;

  GtkAlertDialog *alert =
      gtk_alert_dialog_new("Are you sure you want to initiate repair?");
  gtk_alert_dialog_set_detail(
      alert,
      "This will verify and repair game files. It may take a long time.");
  gtk_alert_dialog_set_buttons(alert,
                               (const char *[]){"_Cancel", "_Repair", nullptr});
  gtk_alert_dialog_choose(alert, GTK_WINDOW(ld->window), nullptr,
                          (GAsyncReadyCallback)on_repair_dialog_response, rcd);
  g_object_unref(alert);
}

/**
 * @brief Create the options dialog.
 *
 * Constructs and returns a dialog that allows users to configure application
 * settings. The dialog includes input fields, browsing functionality, and a
 * repair option.
 *
 * @param ld Pointer to the LauncherData instance.
 * @param update_callback Callback function to initiate the update/repair
 * process.
 * @return GtkWidget* The created options dialog.
 */
GtkWidget *create_options_dialog(LauncherData *ld,
                                 void (*update_callback)(LauncherData *ld,
                                                         bool do_repair)) {
  GtkWidget *dialog =
      g_object_new(GTK_TYPE_DIALOG, "title", "Options", "transient-for",
                   ld->window, "modal", true, nullptr);

  /* Store launcher data and update callback for later retrieval */
  g_object_set_data(G_OBJECT(dialog), "launcher-data", ld);
  g_object_set_data(G_OBJECT(dialog), "update-callback", update_callback);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
  gtk_widget_set_margin_start(grid, 10);
  gtk_widget_set_margin_end(grid, 10);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_margin_bottom(grid, 10);

  GtkWidget *wineprefix_label = gtk_label_new("Wineprefix Name:");
  GtkWidget *wineprefix_entry = gtk_entry_new();
  GtkWidget *wineprefix_button = gtk_button_new_with_label("Browse...");
  gtk_grid_attach(GTK_GRID(grid), wineprefix_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), wineprefix_entry, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), wineprefix_button, 2, 0, 1, 1);
  g_object_set_data(G_OBJECT(dialog), "wineprefix-entry", wineprefix_entry);
  g_signal_connect(wineprefix_button, "clicked",
                   G_CALLBACK(on_wineprefix_browse_clicked), dialog);

  GtkWidget *winebase_label = gtk_label_new("Custom Wine Path:");
  GtkWidget *winebase_entry = gtk_entry_new();
  GtkWidget *winebase_button = gtk_button_new_with_label("Browse...");
  gtk_grid_attach(GTK_GRID(grid), winebase_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), winebase_entry, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), winebase_button, 2, 1, 1, 1);
  g_object_set_data(G_OBJECT(dialog), "winebase-entry", winebase_entry);
  g_signal_connect(winebase_button, "clicked",
                   G_CALLBACK(on_winebase_browse_clicked), dialog);

  GtkWidget *gamemode_toggle = gtk_check_button_new_with_label(
      "Use Feral Gamemode (only selectable if installed)");
  gtk_grid_attach(GTK_GRID(grid), gamemode_toggle, 0, 2, 3, 1);
  g_object_set_data(G_OBJECT(dialog), "gamemode-toggle", gamemode_toggle);

  GtkWidget *gamescope_toggle = gtk_check_button_new_with_label(
      "Use Gamescope (only selectable if installed)");
  GtkWidget *gamescope_args_label = gtk_label_new("Gamescope Arguments:");
  GtkWidget *gamescope_entry = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid), gamescope_toggle, 0, 3, 3, 1);
  gtk_grid_attach(GTK_GRID(grid), gamescope_args_label, 0, 4, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), gamescope_entry, 1, 4, 2, 1);
  g_object_set_data(G_OBJECT(dialog), "gamescope-toggle", gamescope_toggle);
  g_object_set_data(G_OBJECT(dialog), "gamescope-entry", gamescope_entry);
  gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(gamescope_entry)),
                            gamescope_args_global, -1);
  g_signal_connect(gamescope_toggle, "toggled",
                   G_CALLBACK(on_gamescope_toggled), gamescope_entry);

  GtkWidget *toolbox_toggle = gtk_check_button_new_with_label(
      "Launch TERA Toolbox (ignored if no path is provided)");
  GtkWidget *toolbox_entry = gtk_entry_new();
  GtkWidget *toolbox_button = gtk_button_new_with_label("Browse...");
  gtk_grid_attach(GTK_GRID(grid), toolbox_toggle, 0, 5, 3, 1);
  gtk_grid_attach(GTK_GRID(grid), toolbox_entry, 0, 6, 2, 1);
  gtk_grid_attach(GTK_GRID(grid), toolbox_button, 2, 6, 1, 1);
  g_object_set_data(G_OBJECT(dialog), "toolbox-toggle", toolbox_toggle);
  g_object_set_data(G_OBJECT(dialog), "toolbox-entry", toolbox_entry);
  g_signal_connect(toolbox_toggle, "toggled", G_CALLBACK(on_toolbox_toggled),
                   toolbox_button);
  g_signal_connect(toolbox_button, "clicked",
                   G_CALLBACK(on_toolbox_browse_clicked), dialog);

  gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(wineprefix_entry)),
                            wineprefix_global, -1);
  gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(winebase_entry)),
                            wine_base_dir_global, -1);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(gamemode_toggle),
                              use_gamemoderun);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(gamescope_toggle),
                              use_gamescope);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(toolbox_toggle),
                              use_tera_toolbox);
  gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(toolbox_entry)),
                            tera_toolbox_path_global, -1);
  gtk_widget_set_sensitive(toolbox_button, use_tera_toolbox);

  const bool gamemode_available = check_gamemode_available();
  if (!gamemode_available && use_gamemoderun)
    use_gamemoderun = false;
  gtk_widget_set_sensitive(gamemode_toggle, gamemode_available);

  const bool gamescope_available = check_gamescope_available();
  if (!gamescope_available && use_gamescope)
    use_gamescope = false;
  gtk_widget_set_sensitive(gamescope_toggle, gamescope_available);
  gtk_widget_set_sensitive(gamescope_entry, use_gamescope);

  if (use_tera_toolbox && !validate_toolbox_path(tera_toolbox_path_global)) {
    use_tera_toolbox = false;
    memset(tera_toolbox_path_global, 0, FIXED_STRING_FIELD_SZ);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(toolbox_toggle), FALSE);
  }

  GtkWidget *action_area = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_start(action_area, 10);
  gtk_widget_set_margin_bottom(action_area, 10);
  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget *ok_button = gtk_button_new_with_label("OK");
  GtkWidget *repair_button = gtk_button_new_with_label("Repair");
  gtk_box_append(GTK_BOX(action_area), cancel_button);
  gtk_box_append(GTK_BOX(action_area), ok_button);
  gtk_box_append(GTK_BOX(action_area), repair_button);
  g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_cancel_clicked),
                   dialog);
  g_signal_connect(ok_button, "clicked", G_CALLBACK(on_ok_clicked), dialog);
  g_signal_connect(repair_button, "clicked", G_CALLBACK(on_repair_clicked),
                   dialog);
  gtk_box_append(GTK_BOX(vbox), grid);
  gtk_box_append(GTK_BOX(vbox), action_area);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
  gtk_window_set_child(GTK_WINDOW(dialog), vbox);

  return dialog;
}

/**
 * @brief Reads configuration values from "tera-launcher-config.ini" into global
 * variables.
 *
 * @note String values that are too long for their destination buffers will be
 * truncated via str_copy_formatted(), preserving the existing default value.
 * @note Errors during file loading are ignored (treated as non-existent config
 * file).
 */
void config_read_from_ini(void) {
  GKeyFile *keyfile = g_key_file_new();
  GError *error = nullptr;

  // Attempt to load the INI file; silently ignore if not found
  if (!g_key_file_load_from_file(keyfile, "tera-launcher-config.ini",
                                 G_KEY_FILE_NONE, &error)) {
    g_clear_error(&error);
    g_key_file_free(keyfile);
    return;
  }

// Helper macro to read and copy string values if they exist
#define READ_STRING_KEY(key_name, global_var)                                  \
  do {                                                                         \
    char *value =                                                              \
        g_key_file_get_string(keyfile, "Settings", key_name, nullptr);         \
    if (value) {                                                               \
      size_t needed;                                                           \
      if (!str_copy_formatted(global_var, &needed, FIXED_STRING_FIELD_SZ,      \
                              "%s", value)) {                                  \
        g_warning("Unable to load config value '%s': too big for buffer.",     \
                  key_name);                                                   \
      }                                                                        \
      g_free(value);                                                           \
    }                                                                          \
  } while (0)

  READ_STRING_KEY("wineprefix", wineprefix_global);
  READ_STRING_KEY("wine_base_dir", wine_base_dir_global);
  READ_STRING_KEY("tera_toolbox_path", tera_toolbox_path_global);
  READ_STRING_KEY("gamescope_args", gamescope_args_global);

#undef READ_STRING_KEY

  /* Check for TERA_CUSTOM_WINE_DIR ENV (overrides INI if present) */
  const char *env_wine_dir = g_getenv("TERA_CUSTOM_WINE_DIR");
  if (env_wine_dir != NULL) {
    size_t needed;
    if (!str_copy_formatted(wine_base_dir_global, &needed,
                            FIXED_STRING_FIELD_SZ, "%s", env_wine_dir)) {
      g_warning("Unable to use TERA_CUSTOM_WINE_DIR environment variable "
                "value: too large for buffer.");
    }
  }

// Read boolean values, only update if key exists
#define READ_BOOL_KEY(key_name, global_var)                                    \
  do {                                                                         \
    error = NULL;                                                              \
    gboolean value =                                                           \
        g_key_file_get_boolean(keyfile, "Settings", key_name, &error);         \
    if (!error) {                                                              \
      global_var = value;                                                      \
    } else {                                                                   \
      g_clear_error(&error);                                                   \
    }                                                                          \
  } while (0)

  READ_BOOL_KEY("use_gamemoderun", use_gamemoderun);
  READ_BOOL_KEY("use_gamescope", use_gamescope);
  READ_BOOL_KEY("use_tera_toolbox", use_tera_toolbox);

#undef READ_BOOL_KEY

  g_key_file_free(keyfile);
}

/**
 * @brief Writes current configuration values to "tera-launcher-config.ini".
 *
 * @note Empty string values (first character is '\\0') will be omitted from the
 * output file.
 * @note If writing fails, the error is silently ignored (existing file remains
 * unchanged).
 */
void config_write_to_ini(void) {
  GKeyFile *keyfile = g_key_file_new();

// Helper macro to write string keys if they are non-empty
#define WRITE_STRING_KEY(key_name, global_var)                                 \
  do {                                                                         \
    if (global_var[0] != '\0') {                                               \
      g_key_file_set_string(keyfile, "Settings", key_name, global_var);        \
    }                                                                          \
  } while (0)

  WRITE_STRING_KEY("wineprefix", wineprefix_global);
  WRITE_STRING_KEY("wine_base_dir", wine_base_dir_global);
  WRITE_STRING_KEY("tera_toolbox_path", tera_toolbox_path_global);
  WRITE_STRING_KEY("gamescope_args", gamescope_args_global);

#undef WRITE_STRING_KEY

  // Write boolean values (always written)
  g_key_file_set_boolean(keyfile, "Settings", "use_gamemoderun",
                         use_gamemoderun);
  g_key_file_set_boolean(keyfile, "Settings", "use_gamescope", use_gamescope);
  g_key_file_set_boolean(keyfile, "Settings", "use_tera_toolbox",
                         use_tera_toolbox);

  // Save to file
  gsize length = 0;
  gchar *data = g_key_file_to_data(keyfile, &length, nullptr);
  GError *error = nullptr;
  if (!g_file_set_contents("tera-launcher-config.ini", data, (gssize)length,
                           &error)) {
    g_warning("Unable to write config to disk: %s", error->message);
    g_clear_error(&error);
  }
  g_free(data);
  g_key_file_free(keyfile);
}
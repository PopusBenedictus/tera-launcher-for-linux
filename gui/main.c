/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#include "updater.h"
#include <curl/curl.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Used to capture auth info when a user logs into the TERA server. TODO:
 * implement bounds checks here because we could easily exceed these limits and
 * cause a crash.
 */
typedef struct {
  char user_no[256];
  char auth_key[256];
  char character_count[256];
  char welcome_label_msg[256];
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

/**
 * @brief Structure to hold data for launching the game.
 */
typedef struct {
  GtkWindow *window;
  LauncherData *ld;
} GameLaunchData;

/**
 * @brief Structure to hold callback data when the game exits.
 */
typedef struct {
  const LauncherData *ld;
  int exit_code;
} GameExitCallbackData;

/**
 * @brief Structure to hold data for the update process.
 */
typedef struct {
  gint refcount;
  LauncherData *ld;
  UpdateData update_data;
  double current_progress;
  double current_download_progress;
  const char *current_message;
  const char *current_download_message;
  gboolean play_button_enabled;
  gboolean repair_button_enabled;
  gboolean repair_requested;
} UpdateThreadData;

/**
 * @brief Structure to hold data for cURL responses.
 */
struct CurlResponse {
  char *data;
  size_t size;
};

/**
 * @brief Game language string from the embedded sjon resource.
 */
static char game_lang_global[1024] = {0};

/**
 * @brief Wineprefix folder name from the embedded json resource.
 */
static char wineprefix_global[1024] = {0};

/**
 * @brief Holds a copy of the patch url root.
 */
static char patch_url_global[1024] = {0};

/**
 * @brief Holds a copy of the auth url root.
 */
static char auth_url_global[1024] = {0};

/**
 * @brief Holds a copy of the server list url.
 */
static char server_list_url_global[1024] = {0};

/**
 * @brief Holds a copy of the service name that is displayed in the patch window
 * footer.
 */
static char service_name_global[1024] = {0};

/**
 * @brief Used to store the final update thread message, if any, to update
 * progress bar label when the update resources are being thrown out.
 */
static char update_finish_message[1024] = {0};

/**
 * @brief Holds a reference to the GUI stylesheet from the embedded resources.
 * Cannot be const due to loss of qualifiers on assignment, but should not be
 * free'd.
 */
static GBytes *style_data_gbytes = nullptr;

/**
 * @brief Data extracted from the embedded stylesheet resource for the GUI.
 */
const static gchar *style_data = nullptr;

/**
 * @brief Increment reference count on an instance of update thread data.
 * @param td The thread data to increment.
 * @return Incremented thread data reference.
 */
static UpdateThreadData *ut_data_ref(UpdateThreadData *td) {
  g_atomic_int_inc(&td->refcount);
  return td;
}

/**
 * @brief Decrement reference count on an instance of update thread data.
 * @param td The thread data to decrement.
 */
static void ut_data_unref(UpdateThreadData *td) {
  if (g_atomic_int_dec_and_test(&td->refcount)) {
    g_free(td->update_data.game_path);
    free(td);
  }
}

/**
 * @brief Callback function for handling data received by cURL.
 *
 * @param contents Pointer to the data.
 * @param size Size of each data chunk.
 * @param nmemb Number of data chunks.
 * @param userp Pointer to the CurlResponse structure.
 * @return The number of bytes handled.
 */
static size_t write_callback(const void *contents, size_t size, size_t nmemb,
                             void *userp) {
  size_t realsize = size * nmemb;
  struct CurlResponse *mem = userp;

  char *ptr = realloc(mem->data, mem->size + realsize + 1);
  if (!ptr) {
    // Out of memory
    return 0;
  }

  mem->data = ptr;
  memcpy(&(mem->data[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->data[mem->size] = 0;
  return realsize;
}

/**
 * @brief Performs the login operation by sending credentials to the server.
 *
 * @param username The user's login name.
 * @param password The user's password.
 * @param out Pointer to the LoginData structure to store results.
 * @return true if login is successful, false otherwise.
 */
static bool do_login(const char *username, const char *password,
                     LoginData *out) {
  CURL *curl = curl_easy_init();
  if (!curl) {
    g_warning("Failed to initialize cURL");
    return false;
  }

  struct CurlResponse chunk;
  chunk.data = malloc(1);
  chunk.size = 0;
  chunk.data[0] = '\0';

  // Set HTTP headers
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0");
  headers = curl_slist_append(headers, "Accept: */*");
  headers = curl_slist_append(
      headers,
      "Content-Type: application/x-www-form-urlencoded; charset=UTF-8");

  // Prepare POST data
  char postfields[512];
  snprintf(postfields, sizeof(postfields), "login=%s&password=%s", username,
           password);
  g_message("Fields being sent: %s", postfields);

  // Set cURL options
  curl_easy_setopt(curl, CURLOPT_URL, auth_url_global);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

  // Perform the request
  CURLcode res = curl_easy_perform(curl);
  bool success = false;
  if (res == CURLE_OK) {
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code == 200) {
      // Parse JSON response
      json_error_t jerr;
      json_t *root = json_loads(chunk.data, 0, &jerr);
      if (root) {
        json_t *jReturn = json_object_get(root, "Return");
        json_t *jMsg = json_object_get(root, "Msg");
        if (json_is_boolean(jReturn) && json_is_string(jMsg)) {
          const bool retVal = json_boolean_value(jReturn);
          const char *msg = json_string_value(jMsg);
          if (retVal && strcmp(msg, "success") == 0) {
            // Extract additional data
            json_t *jUserNo = json_object_get(root, "UserNo");
            json_t *jAuthKey = json_object_get(root, "AuthKey");
            json_t *jCharCnt = json_object_get(root, "CharacterCount");

            if (json_is_number(jUserNo) && json_is_string(jAuthKey)) {
              const double userNoVal = json_number_value(jUserNo);
              const char *authKey = json_string_value(jAuthKey);
              const char *charCnt =
                  json_is_string(jCharCnt) ? json_string_value(jCharCnt) : "0";

              snprintf(out->user_no, sizeof(out->user_no), "%.0f", userNoVal);
              strncpy(out->auth_key, authKey, sizeof(out->auth_key) - 1);
              strncpy(out->character_count, charCnt,
                      sizeof(out->character_count) - 1);
              out->user_no[sizeof(out->user_no) - 1] = '\0';
              out->auth_key[sizeof(out->auth_key) - 1] = '\0';
              out->character_count[sizeof(out->character_count) - 1] = '\0';

              g_message("Login success: user_no=%s, AuthKey=%s, CharCount=%s",
                        out->user_no, out->auth_key, out->character_count);
              success = true;
            } else {
              g_warning("Invalid JSON structure for login data.");
            }
          } else {
            const char *reason =
                json_string_value(json_object_get(root, "Msg"));
            g_warning("Login failure: %s", reason ? reason : "Unknown");
          }
        }
        json_decref(root);
      } else {
        g_warning("JSON parse error: %s", jerr.text);
      }
    } else {
      g_warning("HTTP response code: %ld", http_code);
    }
  } else {
    g_warning("cURL perform failed: %s", curl_easy_strerror(res));
  }

  // Clean up
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  free(chunk.data);

  return success;
}

/**
 * @brief Loads a texture from a resource path.
 *
 * @param resource_path The path to the resource.
 * @return Pointer to the GdkTexture, or NULL on failure.
 */
static GdkTexture *load_texture(const char *resource_path) {
  GdkTexture *texture = gdk_texture_new_from_resource(resource_path);
  if (!texture) {
    g_warning("Could not load texture from %s", resource_path);
  }
  return texture;
}

/**
 * @brief Loads a sub-image from a resource path.
 *
 * @param resource_path The path to the resource.
 * @param sub_x The x-coordinate of the sub-image.
 * @param sub_y The y-coordinate of the sub-image.
 * @param sub_w The width of the sub-image.
 * @param sub_h The height of the sub-image.
 * @return Pointer to the GdkTexture, or NULL on failure.
 */
static GdkTexture *load_subimage(const char *resource_path, int sub_x,
                                 int sub_y, int sub_w, int sub_h) {
  GdkPixbuf *full_pixbuf = gdk_pixbuf_new_from_resource(resource_path, nullptr);
  if (!full_pixbuf) {
    g_warning("Could not load pixbuf from %s", resource_path);
    return nullptr;
  }

  GdkPixbuf *sub_pixbuf =
      gdk_pixbuf_new_subpixbuf(full_pixbuf, sub_x, sub_y, sub_w, sub_h);
  g_object_unref(full_pixbuf);

  if (!sub_pixbuf) {
    g_warning("Could not create subpixbuf from: %s", resource_path);
    return nullptr;
  }

  GdkTexture *texture = gdk_texture_new_for_pixbuf(sub_pixbuf);
  g_object_unref(sub_pixbuf);
  return texture;
}

/**
 * @brief Creates the login overlay containing login fields and buttons.
 *
 * @param ld Pointer to the LauncherData structure.
 * @return Pointer to the created GtkWidget overlay.
 */
static GtkWidget *create_login_overlay(LauncherData *ld) {
  GtkWidget *overlay = gtk_overlay_new();

  // Load CSS for specific widgets
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, style_data);
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(overlay), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  // Background
  GdkTexture *bg = load_texture("/com/tera/launcher/bg.jpg");
  if (bg) {
    GtkWidget *bg_pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(bg));
    gtk_picture_set_content_fit(GTK_PICTURE(bg_pic), GTK_CONTENT_FIT_FILL);
    gtk_widget_set_can_target(bg_pic, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), bg_pic);
    g_object_unref(bg);
  } else {
    g_warning("Background texture not loaded for login pane.");
  }

  // Form (Username Field)
  GdkTexture *form_uname_tex =
      load_subimage("/com/tera/launcher/form.png", 0, 0, 220, 50);
  if (form_uname_tex) {
    GtkWidget *form_uname_pic =
        gtk_picture_new_for_paintable(GDK_PAINTABLE(form_uname_tex));
    gtk_widget_set_valign(form_uname_pic, GTK_ALIGN_START);
    gtk_widget_set_halign(form_uname_pic, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(form_uname_pic, 105);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), form_uname_pic);
    g_object_unref(form_uname_tex);
  } else {
    g_warning("Form username texture not loaded for login pane.");
  }

  // Form (Password Field)
  GdkTexture *form_passwd_tex =
      load_subimage("/com/tera/launcher/form.png", 0, 50, 220, 50);
  if (form_passwd_tex) {
    GtkWidget *form_passwd_pic =
        gtk_picture_new_for_paintable(GDK_PAINTABLE(form_passwd_tex));
    gtk_widget_set_valign(form_passwd_pic, GTK_ALIGN_START);
    gtk_widget_set_halign(form_passwd_pic, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(form_passwd_pic, 155);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), form_passwd_pic);
    g_object_unref(form_passwd_tex);
  } else {
    g_warning("Form password texture not loaded for login pane.");
  }

  // User Entry
  ld->user_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(ld->user_entry), "Login");
  gtk_widget_set_margin_top(ld->user_entry, 105);
  gtk_widget_set_margin_start(ld->user_entry, 100);
  gtk_widget_set_margin_end(ld->user_entry, 60);
  gtk_widget_set_margin_bottom(ld->user_entry, 352);
  gtk_widget_add_css_class(ld->user_entry, "img_textbox");
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->user_entry);

  // Password Entry
  ld->pass_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(ld->pass_entry), "Password");
  gtk_entry_set_visibility(GTK_ENTRY(ld->pass_entry), FALSE);
  gtk_widget_set_margin_top(ld->pass_entry, 155);
  gtk_widget_set_margin_start(ld->pass_entry, 100);
  gtk_widget_set_margin_end(ld->pass_entry, 60);
  gtk_widget_set_margin_bottom(ld->pass_entry, 302);
  gtk_widget_add_css_class(ld->pass_entry, "img_textbox");
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->pass_entry);

  // Login Button
  GdkTexture *login_sub =
      load_subimage("/com/tera/launcher/btn-auth.png", 0, 0, 224, 69);
  ld->login_btn = gtk_button_new();
  if (login_sub) {
    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(login_sub));
    gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_FILL);
    gtk_widget_set_valign(pic, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(pic, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(pic, "img_button_icons");
    gtk_button_set_child(GTK_BUTTON(ld->login_btn), pic);
    g_object_unref(login_sub);
  } else {
    gtk_button_set_label(GTK_BUTTON(ld->login_btn), "Login");
  }
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->login_btn);
  gtk_widget_set_margin_top(ld->login_btn, 240);
  gtk_widget_set_margin_bottom(ld->login_btn, 191);
  gtk_widget_set_margin_start(ld->login_btn, 58);
  gtk_widget_set_margin_end(ld->login_btn, 58);
  gtk_widget_add_css_class(ld->login_btn, "img_buttons");

  // Close Button
  GdkTexture *close_tex =
      load_subimage("/com/tera/launcher/btn-close1.png", 0, 0, 22, 22);
  ld->close_login_btn = gtk_button_new();
  if (close_tex) {
    GtkWidget *cpic = gtk_picture_new_for_paintable(GDK_PAINTABLE(close_tex));
    gtk_picture_set_content_fit(GTK_PICTURE(cpic), GTK_CONTENT_FIT_FILL);
    gtk_widget_set_valign(cpic, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(cpic, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(cpic, "img_exit_icons");
    gtk_button_set_child(GTK_BUTTON(ld->close_login_btn), cpic);
    g_object_unref(close_tex);
  } else {
    gtk_button_set_label(GTK_BUTTON(ld->close_login_btn), "Close");
  }
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->close_login_btn);
  gtk_widget_set_margin_start(ld->close_login_btn, 313);
  gtk_widget_set_margin_end(ld->close_login_btn, 5);
  gtk_widget_set_margin_top(ld->close_login_btn, 5);
  gtk_widget_set_margin_bottom(ld->close_login_btn, 473);
  gtk_widget_add_css_class(ld->close_login_btn, "img_buttons");

  return overlay;
}

/**
 * @brief Creates the patch overlay containing play, logout, and repair buttons,
 * along with progress bars.
 *
 * @param ld Pointer to the LauncherData structure.
 * @return Pointer to the created GtkWidget overlay.
 */
static GtkWidget *create_patch_overlay(LauncherData *ld) {
  GtkWidget *overlay = gtk_overlay_new();

  // Load CSS for specific widgets
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, style_data);
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(overlay), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_widget_add_css_class(overlay, "transparent_bg");

  // Background
  GdkTexture *bg = load_texture("/com/tera/launcher/bg.png");
  if (bg) {
    GtkWidget *bg_pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(bg));
    gtk_picture_set_content_fit(GTK_PICTURE(bg_pic), GTK_CONTENT_FIT_FILL);
    gtk_widget_set_can_target(bg_pic, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), bg_pic);
    g_object_unref(bg);
  } else {
    g_warning("Background texture not loaded for patch pane.");
  }

  // Launcher Logo
  GdkTexture *logo = load_texture("/com/tera/launcher/logo.png");
  if (logo) {
    GtkWidget *logo_pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(logo));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), logo_pic);
    gtk_widget_set_halign(logo_pic, GTK_ALIGN_START);
    gtk_widget_set_valign(logo_pic, GTK_ALIGN_START);
    gtk_widget_set_margin_start(logo_pic, 45);
    gtk_widget_set_margin_top(logo_pic, 70);
    g_object_unref(logo);
  } else {
    g_warning("Logo texture not loaded for patch pane.");
  }

  // Footer Text
  const time_t t = time(nullptr);
  const struct tm *tm_info = localtime(&t);
  const int year = 1900 + tm_info->tm_year;
  char footer_str[1024] = {0};
  const size_t footer_sz =
      snprintf(footer_str, sizeof(footer_str), "© %i %s. All Rights Reserved.",
               year, service_name_global);
  if (footer_sz <= 0)
    g_error("Service name input renders invalid footer string.");

  ld->footer_label = gtk_label_new(footer_str);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->footer_label);
  gtk_widget_set_halign(ld->footer_label, GTK_ALIGN_START);
  gtk_widget_set_valign(ld->footer_label, GTK_ALIGN_END);
  gtk_widget_set_margin_start(ld->footer_label, 30);
  gtk_widget_set_margin_bottom(ld->footer_label, 45);
  gtk_widget_add_css_class(ld->footer_label, "footer_text");

  // User Welcome Icon
  GdkTexture *ico = load_texture("/com/tera/launcher/ico.png");
  if (ico) {
    GtkWidget *ico_pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(ico));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ico_pic);
    gtk_widget_set_valign(ico_pic, GTK_ALIGN_START);
    gtk_widget_set_halign(ico_pic, GTK_ALIGN_START);
    gtk_widget_set_margin_start(ico_pic, 45);
    gtk_widget_set_margin_top(ico_pic, 161);
    g_object_unref(ico);
  } else {
    g_warning("Icon texture not loaded for patch pane.");
  }

  // HBox for the welcome label and logout button
  ld->welcome_label_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

  // User Welcome Label
  ld->welcome_label = gtk_label_new(nullptr);
  gtk_widget_set_halign(ld->welcome_label, GTK_ALIGN_START);
  gtk_widget_set_valign(ld->welcome_label, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(ld->welcome_label_hbox), ld->welcome_label);

  // Logout Button
  GdkTexture *logout_tex =
      load_subimage("/com/tera/launcher/btn-logout.png", 0, 0, 18, 18);
  ld->logout_btn = gtk_button_new();
  if (logout_tex) {
    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(logout_tex));
    gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_FILL);
    gtk_button_set_child(GTK_BUTTON(ld->logout_btn), pic);
    gtk_widget_add_css_class(ld->logout_btn, "logout_button");
    g_object_unref(logout_tex);
  } else {
    gtk_button_set_label(GTK_BUTTON(ld->logout_btn), "Logout");
  }

  // Add the logout button to the hbox
  gtk_box_append(GTK_BOX(ld->welcome_label_hbox), ld->logout_btn);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->welcome_label_hbox);
  gtk_widget_add_css_class(ld->welcome_label, "welcome_text");
  gtk_widget_set_halign(ld->welcome_label_hbox, GTK_ALIGN_START);
  gtk_widget_set_valign(ld->welcome_label_hbox, GTK_ALIGN_START);
  gtk_widget_set_margin_top(ld->welcome_label_hbox, 155);
  gtk_widget_set_margin_start(ld->welcome_label_hbox, 75);

  // Play Button
  GdkTexture *play_tex =
      load_subimage("/com/tera/launcher/btn-game-start.png", 0, 0, 240, 90);
  ld->play_btn = gtk_button_new();
  if (play_tex) {
    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(play_tex));
    gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_FILL);
    gtk_button_set_child(GTK_BUTTON(ld->play_btn), pic);
    gtk_widget_add_css_class(ld->play_btn, "img_button_icons");
    g_object_unref(play_tex);
  } else {
    gtk_button_set_label(GTK_BUTTON(ld->play_btn), "Play");
  }
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->play_btn);
  gtk_widget_add_css_class(ld->play_btn, "img_buttons");
  gtk_widget_set_halign(ld->play_btn, GTK_ALIGN_START);
  gtk_widget_set_valign(ld->play_btn, GTK_ALIGN_START);
  gtk_widget_set_margin_start(ld->play_btn, 580);
  gtk_widget_set_margin_top(ld->play_btn, 458);

  // Update / Repair Progress Bar
  ld->update_repair_progress_bar = gtk_progress_bar_new();
  gtk_widget_set_halign(ld->update_repair_progress_bar, GTK_ALIGN_START);
  gtk_widget_set_valign(ld->update_repair_progress_bar, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom(ld->update_repair_progress_bar, 110);
  gtk_widget_set_margin_start(ld->update_repair_progress_bar, 45);
  gtk_progress_bar_set_show_text(
      GTK_PROGRESS_BAR(ld->update_repair_progress_bar), TRUE);
  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ld->update_repair_progress_bar),
                            "Ready");
  gtk_widget_add_css_class(ld->update_repair_progress_bar,
                           "repair-progress-bar");
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->update_repair_progress_bar);
  gtk_progress_bar_set_ellipsize(
      GTK_PROGRESS_BAR(ld->update_repair_progress_bar), PANGO_ELLIPSIZE_END);
  gtk_widget_set_size_request(ld->update_repair_progress_bar, 450, 40);
  gtk_widget_set_hexpand(ld->update_repair_download_bar, FALSE);

  // Download Progress Bar
  ld->update_repair_download_bar = gtk_progress_bar_new();
  gtk_widget_set_halign(ld->update_repair_download_bar, GTK_ALIGN_START);
  gtk_widget_set_valign(ld->update_repair_download_bar, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom(ld->update_repair_download_bar, 70);
  gtk_widget_set_margin_start(ld->update_repair_download_bar, 45);
  gtk_progress_bar_set_show_text(
      GTK_PROGRESS_BAR(ld->update_repair_download_bar), TRUE);
  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ld->update_repair_download_bar),
                            "");
  gtk_widget_add_css_class(ld->update_repair_download_bar,
                           "repair-progress-bar");
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->update_repair_download_bar);
  gtk_progress_bar_set_ellipsize(
      GTK_PROGRESS_BAR(ld->update_repair_download_bar), PANGO_ELLIPSIZE_END);
  gtk_widget_set_size_request(ld->update_repair_download_bar, 450, 40);
  gtk_widget_set_hexpand(ld->update_repair_download_bar, FALSE);

  // Repair Button
  GdkTexture *rep_tex =
      load_subimage("/com/tera/launcher/repair-btn.png", 0, 0, 50, 50);
  ld->repair_btn = gtk_button_new();
  if (rep_tex) {
    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(rep_tex));
    gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_FILL);
    gtk_button_set_child(GTK_BUTTON(ld->repair_btn), pic);
    gtk_widget_add_css_class(ld->repair_btn, "img_button_icons");
    g_object_unref(rep_tex);
  } else {
    gtk_button_set_label(GTK_BUTTON(ld->repair_btn), "Repair");
  }
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->repair_btn);
  gtk_widget_set_size_request(ld->repair_btn, 50, 50);
  gtk_widget_set_halign(ld->repair_btn, GTK_ALIGN_START);
  gtk_widget_set_valign(ld->repair_btn, GTK_ALIGN_START);
  gtk_widget_set_margin_start(ld->repair_btn, 515);
  gtk_widget_set_margin_top(ld->repair_btn, 468);
  gtk_widget_add_css_class(ld->repair_btn, "img_buttons");

  // Close Button
  GdkTexture *close_patch_tex =
      load_subimage("/com/tera/launcher/btn-close.png", 0, 0, 22, 22);
  ld->close_patch_btn = gtk_button_new();
  if (close_patch_tex) {
    GtkWidget *cpic =
        gtk_picture_new_for_paintable(GDK_PAINTABLE(close_patch_tex));
    gtk_picture_set_content_fit(GTK_PICTURE(cpic), GTK_CONTENT_FIT_FILL);
    gtk_button_set_child(GTK_BUTTON(ld->close_patch_btn), cpic);
    gtk_widget_add_css_class(cpic, "img_exit_icons");
    g_object_unref(close_patch_tex);
  } else {
    gtk_button_set_label(GTK_BUTTON(ld->close_patch_btn), "Close");
  }
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ld->close_patch_btn);
  gtk_widget_set_halign(ld->close_patch_btn, GTK_ALIGN_END);
  gtk_widget_set_valign(ld->close_patch_btn, GTK_ALIGN_START);
  gtk_widget_set_margin_end(ld->close_patch_btn, 112);
  gtk_widget_set_margin_top(ld->close_patch_btn, 60);
  gtk_widget_add_css_class(ld->close_patch_btn, "img_buttons");

  return overlay;
}

/**
 * @brief Configures the window to be borderless and supports transparency.
 *
 * @param window The GTK window to configure.
 */
static void setup_transparent_window(GtkWidget *window) {
  // No decorations => borderless
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

  // Load CSS for transparency
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, style_data);
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(window), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_widget_add_css_class(GTK_WIDGET(window), "transparent_bg");
}

/**
 * @brief Callback for the dialog's response signal. Destroys the dialog
 *        and quits the application after the user dismisses the error.
 *
 * @param dialog      The GtkDialog pointer.
 * @param response_id The response chosen by the user (close, delete-event,
 * etc.).
 * @param user_data   Pointer to the GtkApplication to be quit.
 */
static void on_fatal_error_dialog_response(GtkDialog *dialog, gint response_id,
                                           gpointer user_data) {
  /* Destroy the dialog */
  gtk_window_destroy(GTK_WINDOW(dialog));

  /* Quit the application (user_data is our GtkApplication) */
  if (user_data) {
    g_application_quit(G_APPLICATION(user_data));
  }
}

/**
 * @brief Show a fatal error dialog (for GTK4) with the given message, then quit
 * the app.
 *
 * Once the user dismisses the dialog, the application immediately exits.
 *
 * @param app     The GtkApplication pointer to quit.
 * @param message The error message to display in the dialog.
 */
static void show_fatal_error_dialog_and_exit(GtkApplication *app,
                                             const char *message) {
  /*
   * In GTK4, you can still create a GtkMessageDialog, but you may need
   * transitional APIs. Alternatively, consider GtkAlertDialog for pure GTK4.
   * Here, we demonstrate a traditional modal error message.
   */
  GtkWidget *dialog = gtk_message_dialog_new(
      NULL, /* parent window if you have one, or NULL */
      GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);

  /* Connect a callback so that when the dialog closes, the app quits. */
  g_signal_connect(dialog, "response",
                   G_CALLBACK(on_fatal_error_dialog_response), app);

  gtk_window_present(GTK_WINDOW(dialog));
}

/**
 * @brief Load and parse JSON configuration from an embedded resource.
 *
 * Looks up the specified resource, reads it as JSON, and returns the jansson
 * JSON root object. On any error, it displays a fatal error dialog (using the
 * provided GtkApplication) and returns NULL.
 *
 * @param app           The GtkApplication to use for showing fatal errors.
 * @param resource_path The GResource path to the embedded JSON file.
 * @return json_t*      The parsed JSON object, or NULL on failure.
 */
static json_t *load_launcher_config_json(GtkApplication *app,
                                         const char *resource_path) {
  GError *error = nullptr;
  GBytes *launcher_config_gbytes =
      g_resources_lookup_data(resource_path, 0, &error);

  if (!launcher_config_gbytes) {
    show_fatal_error_dialog_and_exit(
        app, g_strdup_printf("Failed to load config file: %s",
                             error ? error->message : "Unknown error"));
    if (error) {
      g_error_free(error);
    }
    return nullptr;
  }

  gsize size = 0;
  const gchar *data = g_bytes_get_data(launcher_config_gbytes, &size);
  if (!data) {
    show_fatal_error_dialog_and_exit(
        app, g_strdup_printf("Failed to load embedded configuration data: %s",
                             error ? error->message : "Unknown error"));
    if (error) {
      g_error_free(error);
    }
    return nullptr;
  }

  json_error_t json_error;
  json_t *config_json = json_loads(data, JSON_STRING, &json_error);
  if (!config_json) {
    show_fatal_error_dialog_and_exit(
        app, "Could not parse launcher configuration JSON.");
    if (error) {
      g_error_free(error);
    }
    return nullptr;
  }

  if (error) {
    g_error_free(error);
  }
  return config_json;
}

/**
 * @brief Retrieve a string value by key from the JSON object and copy it into
 * the given buffer.
 *
 * If the key is missing or not a string, a fatal error dialog is shown and the
 * application quits.
 *
 * @param app                  The GtkApplication to use for showing fatal
 * errors.
 * @param launcher_config_json The JSON object to read from.
 * @param key                  The JSON key to look up.
 * @param destination          The buffer to copy the resulting string into.
 */
static void parse_and_copy_string(GtkApplication *app,
                                  const json_t *launcher_config_json,
                                  const char *key, char *destination) {
  json_t *field = json_object_get(launcher_config_json, key);
  if (!field) {
    show_fatal_error_dialog_and_exit(
        app, g_strdup_printf("Could not parse key: %s", key));
  }

  if (!json_is_string(field)) {
    show_fatal_error_dialog_and_exit(
        app, g_strdup_printf("Key '%s' is not a valid string.", key));
  }

  const char *str_value = json_string_value(field);
  strcpy(destination, str_value);
}

/**
 * @brief Load and validate the launcher's configuration from embedded JSON.
 *
 * Reads JSON from /com/tera/launcher/launcher-config.json, populates the global
 * config variables, and checks them for correctness. On failure, it shows an
 * error dialog (via @p app) and quits the application.
 *
 * @param app The GtkApplication instance used for showing fatal error dialogs.
 * @return gboolean TRUE on success (the function returns normally). In the
 * event of an error, a dialog is shown and the app quits, so effectively this
 * function only returns FALSE if something interrupts the flow before the
 * dialog.
 */
static gboolean launcher_init_config(GtkApplication *app) {
  json_t *launcher_config_json =
      load_launcher_config_json(app, "/com/tera/launcher/launcher-config.json");
  if (!launcher_config_json) {
    return FALSE;
  }

  parse_and_copy_string(app, launcher_config_json, "public_patch_url",
                        patch_url_global);
  parse_and_copy_string(app, launcher_config_json, "auth_url", auth_url_global);
  parse_and_copy_string(app, launcher_config_json, "server_list_url",
                        server_list_url_global);
  parse_and_copy_string(app, launcher_config_json, "wine_prefix_name",
                        wineprefix_global);

  const char *p = wineprefix_global;
  while (*p) {
    if (*p == '/' || *p == '\\') {
      show_fatal_error_dialog_and_exit(
          app, "Wine prefix contains path separators, which is not valid.");
      /* The application will quit upon dialog dismissal. */
    }
    p++;
  }

  parse_and_copy_string(app, launcher_config_json, "game_lang",
                        game_lang_global);
  parse_and_copy_string(app, launcher_config_json, "service_name",
                        service_name_global);

  json_decref(launcher_config_json);
  return TRUE;
}

/**
 * @brief Callback function for marshalling progress bar updates on the GUI
 * @param data A pointer to an instance of update thread data.
 */
static gboolean progress_bar_callback(gpointer data) {
  const UpdateThreadData *td = data;
  GtkProgressBar *pb = td->update_data.progress_bar;
  gtk_progress_bar_set_fraction(pb, td->current_progress);
  gtk_progress_bar_set_text(pb, td->current_message);
  return FALSE;
}

/**
 * @brief Callback function for marshalling progress bar updates on the GUI
 * @param data A pointer to an instance of update thread data.
 * @return Whether to kill the signal that fired this callback off or not.
 */
static gboolean download_progress_bar_callback(gpointer data) {
  const UpdateThreadData *td = data;
  GtkProgressBar *pb = td->update_data.download_progress_bar;
  gtk_progress_bar_set_fraction(pb, td->current_download_progress);
  gtk_progress_bar_set_text(pb, td->current_download_message);
  return FALSE;
}

/**
 * @brief Used when we want to reset progress bar state after work is complete.
 * @param data A pointer to an instance of update thread data.
 * @return Whether to kill the signal that fired this callback off or not.
 */
static gboolean progress_bar_final_callback(gpointer data) {
  const UpdateThreadData *td = data;
  if (strlen(update_finish_message) != 0) {
    gtk_progress_bar_set_fraction(td->update_data.progress_bar, 1.0);
    gtk_progress_bar_set_text(td->update_data.progress_bar,
                              update_finish_message);
    gtk_progress_bar_set_fraction(td->update_data.download_progress_bar, 0.0);
    gtk_progress_bar_set_text(td->update_data.download_progress_bar, "");
    memset(update_finish_message, 0, sizeof(update_finish_message));
  }

  return FALSE;
}

/**
 * @brief Callback function for marshalling button status updates on the GUI
 * @param data A pointer to an instance of update thread data.
 */
static gboolean button_status_callback(gpointer data) {
  const UpdateThreadData *td = data;
  gtk_widget_set_sensitive(td->ld->play_btn, td->play_button_enabled);
  gtk_widget_set_sensitive(td->ld->repair_btn, td->repair_button_enabled);
  return FALSE;
}

/**
 * @brief Callback function for updating the progress bar.
 *
 * @param progress The progress fraction (0.0 to 1.0).
 * @param message The message to display.
 * @param user_data Pointer to LauncherData.
 */
static void update_progress_callback(double progress, const char *message,
                                     gpointer user_data) {
  UpdateThreadData *td = user_data;
  td->current_progress = progress;
  td->current_message = message;

  // Schedule the update in the main thread
  g_idle_add_full(G_PRIORITY_HIGH_IDLE, progress_bar_callback, ut_data_ref(td),
                  (GDestroyNotify)ut_data_unref);
}

/**
 * @brief Callback function for updating the download progress bar.
 *
 * @param progress The progress fraction (0.0 to 1.0).
 * @param message The message to display.
 * @param user_data Pointer to LauncherData.
 */
static void update_download_progress_callback(double progress,
                                              const char *message,
                                              gpointer user_data) {
  UpdateThreadData *td = user_data;
  td->current_download_progress = progress;
  td->current_download_message = message;

  // Schedule the update in the main thread
  g_idle_add_full(G_PRIORITY_HIGH_IDLE, download_progress_bar_callback,
                  ut_data_ref(td), (GDestroyNotify)ut_data_unref);
}

// Thread function
static gpointer update_thread_func(gpointer data) {
  UpdateThreadData *ut_data = data;
  if (!ut_data) {
    return nullptr;
  }

  UpdateData *update_data = &ut_data->update_data;

  // Step 2: Get files to update (may become repair task if conditions align to
  // require it or is requested)
  GList *files_to_update;
  if (ut_data->repair_requested)
    files_to_update =
        get_files_to_repair(update_data, update_progress_callback, ut_data);
  else
    files_to_update =
        get_files_to_update(update_data, update_progress_callback, ut_data);

  if (!files_to_update) {
    ut_data->play_button_enabled = TRUE;
    ut_data->repair_button_enabled = TRUE;
    strcpy(update_finish_message, "Game is up to date.");
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, progress_bar_final_callback,
                    ut_data_ref(ut_data), (GDestroyNotify)ut_data_unref);
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, button_status_callback,
                    ut_data_ref(ut_data), (GDestroyNotify)ut_data_unref);
    return nullptr;
  }

  // Step 3: Download all files
  if (!download_all_files(update_data, files_to_update,
                          update_progress_callback,
                          update_download_progress_callback, ut_data)) {
    ut_data->play_button_enabled = TRUE;
    ut_data->repair_button_enabled = TRUE;
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, button_status_callback,
                    ut_data_ref(ut_data), (GDestroyNotify)ut_data_unref);
    g_list_free_full(files_to_update, free_file_info);
    return nullptr;
  }

  // Cleanup
  g_list_free_full(files_to_update, free_file_info);

  // Re-enable play/repair buttons and cleanup the update thread data struct
  ut_data->play_button_enabled = TRUE;
  ut_data->repair_button_enabled = TRUE;
  g_idle_add_full(G_PRIORITY_HIGH_IDLE, button_status_callback,
                  ut_data_ref(ut_data), (GDestroyNotify)ut_data_unref);
  return nullptr;
}

/**
 * @brief Initiates the update process by checking for updates and downloading
 * them.
 *
 * @param ld Pointer to the LauncherData structure.
 * @param do_repair If set to TRUE, a full repair operation is carried out
 * rather than an update.
 */
static void start_update_process(LauncherData *ld, bool do_repair) {
  // Disable the repair and play buttons to prevent multiple updates
  gtk_widget_set_sensitive(ld->repair_btn, FALSE);
  gtk_widget_set_sensitive(ld->play_btn, FALSE);

  // Reset the progress bar
  gtk_progress_bar_set_fraction(
      GTK_PROGRESS_BAR(ld->update_repair_progress_bar), 0.0);
  if (do_repair)
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ld->update_repair_progress_bar),
                              "Starting repair...");
  else
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ld->update_repair_progress_bar),
                              "Checking for updates...");

  // Start the update process in a separate thread
  UpdateThreadData *thread_data = malloc(sizeof(UpdateThreadData));
  if (!thread_data) {
    g_warning("Failed to allocate memory for UpdateThreadData");
    if (do_repair)
      gtk_progress_bar_set_text(
          GTK_PROGRESS_BAR(ld->update_repair_progress_bar), "Repair failed.");
    else
      gtk_progress_bar_set_text(
          GTK_PROGRESS_BAR(ld->update_repair_progress_bar), "Update failed.");

    gtk_widget_set_sensitive(ld->repair_btn, TRUE);
    gtk_widget_set_sensitive(ld->play_btn, TRUE);
    return;
  }

  // Share patch URL and game path in UpdateData for update functions to do
  // their thing.
  thread_data->update_data.public_patch_url = patch_url_global;
  thread_data->update_data.game_path = g_get_current_dir();

  // Populate UpdateThreadData
  thread_data->ld = ld;
  thread_data->update_data.progress_bar =
      GTK_PROGRESS_BAR(ld->update_repair_progress_bar);
  thread_data->update_data.download_progress_bar =
      GTK_PROGRESS_BAR(ld->update_repair_download_bar);
  thread_data->repair_requested = do_repair;

  // Create and start the thread
  GThread *thread =
      g_thread_new("update_thread", update_thread_func, thread_data);
  if (!thread) {
    g_warning("Failed to create update thread");
    if (do_repair)
      gtk_progress_bar_set_text(
          GTK_PROGRESS_BAR(ld->update_repair_progress_bar), "Repair failed.");
    else
      gtk_progress_bar_set_text(
          GTK_PROGRESS_BAR(ld->update_repair_progress_bar), "Update failed.");

    gtk_widget_set_sensitive(ld->repair_btn, TRUE);
    gtk_widget_set_sensitive(ld->play_btn, TRUE);
    g_free(thread_data->update_data.game_path);
    free(thread_data);
  } else {
    // Detach the thread as we don't need to join it
    g_thread_unref(thread);
  }
}

/**
 * @brief Callback function for handling the update process after game exits.
 *
 * @param user_data Pointer to GameExitCallbackData.
 * @return FALSE to remove the source.
 */
static gboolean restore_launcher_callback(gpointer user_data) {
  GameExitCallbackData *cb_data = user_data;
  if (!cb_data)
    return FALSE;

  gtk_widget_set_sensitive(cb_data->ld->window, TRUE);
  gtk_widget_set_sensitive(cb_data->ld->play_btn, TRUE);
  gtk_widget_set_sensitive(cb_data->ld->repair_btn, TRUE);
  gtk_window_present(GTK_WINDOW(cb_data->ld->window));

  free(cb_data);
  return FALSE;
}

/**
 * @brief Callback function invoked by the thread after the game exits.
 *
 * @param exit_code The exit code of the game.
 * @param user_data Pointer to the GTK window.
 */
void game_exit_callback(const int exit_code, const void *user_data) {
  // Allocate and populate callback data
  GameExitCallbackData *cb_data = malloc(sizeof(GameExitCallbackData));
  if (!cb_data) {
    g_message("Failed to allocate memory for GameExitCallbackData");
    return;
  }

  const LauncherData *ld = user_data;
  cb_data->ld = ld;
  cb_data->exit_code = exit_code;

  // Schedule the restore_launcher_callback to run on the GTK main thread
  g_idle_add_full(G_PRIORITY_HIGH_IDLE, restore_launcher_callback, cb_data,
                  nullptr);
}

/**
 * @brief Thread function to launch stub_launcher and capture its exit code.
 *
 * @param data Pointer to GameLaunchData.
 * @return NULL.
 */
static gpointer game_launcher_thread(gpointer data) {
  GameLaunchData *launch_data = data;
  if (!launch_data) {
    return nullptr;
  }

  // Assemble the launcher path: "Z:" + current working directory with
  // backslashes + "Binaries\\TERA.exe"
  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof(cwd))) {
    g_warning("Failed to get current working directory.");
    gtk_widget_set_sensitive(GTK_WIDGET(launch_data->ld->play_btn), TRUE);
    free(launch_data);
    return nullptr;
  }

  // Replace '/' with '\\' in cwd
  for (int i = 0; cwd[i]; i++) {
    if (cwd[i] == '/')
      cwd[i] = '\\';
  }

  // Assemble the path. The stub launcher is a winelib app and wants a
  // Windows-like file path.
  char launcher_path[PATH_MAX];
  snprintf(launcher_path, sizeof(launcher_path), "Z:%s\\Binaries\\TERA.exe",
           cwd);

  // Prepare command-line arguments for stub_launcher
  const char *account_name = launch_data->ld->login_data.user_no;
  const char *characters_count =
      (launch_data->ld->login_data.character_count[0]
           ? launch_data->ld->login_data.character_count
           : "0");
  const char *ticket = launch_data->ld->login_data.auth_key;
  const char *game_lang = game_lang_global;
  const char *game_path = launcher_path; // Use the dynamically assembled path

  // Path to stub_launcher executable
  // Assuming stub_launcher.exe is in the same directory as the main executable
  gchar *current_dir = g_get_current_dir();
  gchar *stub_launcher_path =
      g_build_filename(current_dir, "stub_launcher.exe", nullptr);
  if (!g_file_test(stub_launcher_path, G_FILE_TEST_EXISTS)) {
    g_message("stub_launcher.exe not found at path: %hs", stub_launcher_path);
    g_free(stub_launcher_path);
    // Re-enable the play button
    game_exit_callback(1, launch_data->ld);
    free(launch_data);
    return nullptr;
  }

  // Launch stub_launcher and wait for it to exit
  GError *error = nullptr;
  gint exit_status = 0;
  gchar **envp = g_get_environ();

  const gchar *custom_wine_dir = g_environ_getenv(envp, "TERA_CUSTOM_WINE_DIR");
  gchar *wine_path = nullptr;
  const gchar *old_path = g_environ_getenv(envp, "PATH");

  if (custom_wine_dir && *custom_wine_dir != '\0') {
    wine_path = g_build_filename(custom_wine_dir, "bin", "wine", nullptr);
    if (!g_file_test(wine_path, G_FILE_TEST_IS_EXECUTABLE)) {
      g_warning("Custom wine not found or not executable: %s", wine_path);
      g_free(wine_path);
      g_strfreev(envp);
      return FALSE;
    }

    const gchar *old_ld_library_path =
        g_environ_getenv(envp, "LD_LIBRARY_PATH");
    GString *new_ld_library_path = g_string_new("");

    g_string_append_printf(new_ld_library_path, "%s/lib:%s/lib64",
                           custom_wine_dir, custom_wine_dir);

    if (old_ld_library_path && *old_ld_library_path != '\0') {
      g_string_append_c(new_ld_library_path, ':');
      g_string_append(new_ld_library_path, old_ld_library_path);
    }

    envp = g_environ_setenv(envp, "LD_LIBRARY_PATH", new_ld_library_path->str,
                            TRUE);
    g_string_free(new_ld_library_path, TRUE);

    gchar *wine_loader_path =
        g_build_filename(custom_wine_dir, "bin", "wine", nullptr);
    envp = g_environ_setenv(envp, "WINELOADER", wine_loader_path, TRUE);
    g_free(wine_loader_path);

    gchar *wine_server_path =
        g_build_filename(custom_wine_dir, "bin", "wineserver", nullptr);
    envp = g_environ_setenv(envp, "WINESERVER", wine_server_path, TRUE);
    g_free(wine_server_path);

    const gchar *home_dir = g_get_home_dir();
    if (home_dir) {
      gchar *wine_prefix_path =
          g_build_filename(home_dir, wineprefix_global, nullptr);
      envp = g_environ_setenv(envp, "WINEPREFIX", wine_prefix_path, TRUE);
      g_free(wine_prefix_path);
    }
  } else {
    wine_path = g_find_program_in_path("wine");
    if (!wine_path) {
      g_warning(
          "System Wine not found on PATH, and TERA_CUSTOM_WINE_DIR not set.");
      g_strfreev(envp);
      return FALSE;
    }
  }

  if (!old_path) {
    old_path = "/usr/local/bin:/usr/bin:/bin";
  } else {
    g_message("Old PATH: %s", old_path);
  }

  if (custom_wine_dir) {
    gchar *wine_bin_path = g_build_filename(custom_wine_dir, "bin", nullptr);
    GString *new_path = g_string_new(wine_bin_path);
    g_string_append_c(new_path, G_SEARCHPATH_SEPARATOR);
    g_string_append(new_path, old_path);

    envp = g_environ_setenv(envp, "PATH", new_path->str, TRUE);
    g_string_free(new_path, TRUE);
  } else {
    envp = g_environ_setenv(envp, "PATH", old_path, TRUE);
  }

  envp = g_environ_setenv(envp, "WINEDEBUG", "-all", TRUE);
  envp = g_environ_setenv(envp, "DXVK_LOG_LEVEL", "none", TRUE);

  GPtrArray *argv_array = g_ptr_array_new();
  g_ptr_array_add(argv_array, g_strdup(stub_launcher_path));
  g_ptr_array_add(argv_array, g_strdup(account_name));
  g_ptr_array_add(argv_array, g_strdup(characters_count));
  g_ptr_array_add(argv_array, g_strdup(ticket));
  g_ptr_array_add(argv_array, g_strdup(game_lang));
  g_ptr_array_add(argv_array, g_strdup(game_path));
  g_ptr_array_add(argv_array, g_strdup(server_list_url_global));
  g_ptr_array_add(argv_array, nullptr);
  const auto argv_final = (gchar **)g_ptr_array_free(argv_array, FALSE);

  // Use g_spawn_sync to launch the process and wait for it to complete
  const gboolean success = g_spawn_sync(current_dir,     // working directory
                                        argv_final,      // argv
                                        envp,            // envp
                                        G_SPAWN_DEFAULT, // flags
                                        nullptr,         // child_setup
                                        nullptr,         // user_data
                                        nullptr,         // standard output
                                        nullptr,         // standard error
                                        &exit_status,    // exit status
                                        &error           // error
  );

  if (!success) {
    g_error_free(error);
    error = nullptr;
  }

  g_message("Restore Game Launcher controls after game exit. Exit status: %i",
            exit_status);
  game_exit_callback(exit_status, launch_data->ld);

  // Clean up
  g_strfreev(argv_final);
  g_free(wine_path);
  g_free(envp);
  g_free(stub_launcher_path);
  g_free(current_dir);
  free(launch_data);

  return nullptr;
}

/**
 * @brief Callback function for handling the play button clicks.
 *
 * @param btn The play button widget.
 * @param user_data Pointer to LauncherData.
 */
static void on_play_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn; // Unused here.
  LauncherData *ld = user_data;
  g_message("Play button clicked. Initiating run_game.");

  // Disable the play button to prevent multiple clicks
  gtk_widget_set_sensitive(ld->play_btn, FALSE);

  // Minimize the launcher window
  gtk_window_minimize(GTK_WINDOW(ld->window));
  gtk_widget_set_sensitive(GTK_WIDGET(ld->window), FALSE);

  // Allocate and populate GameLaunchData
  GameLaunchData *launch_data = malloc(sizeof(GameLaunchData));
  if (!launch_data) {
    g_message("Failed to allocate memory for GameLaunchData");
    // Re-enable the play button
    gtk_widget_set_sensitive(ld->play_btn, TRUE);
    return;
  }

  launch_data->window = GTK_WINDOW(ld->window);
  launch_data->ld = ld;

  // Start the thread
  GThread *thread = g_thread_new("game_launcher_thread", game_launcher_thread,
                                 (gpointer)launch_data);
  if (!thread) {
    g_message("Failed to create game_launcher_thread");
    free(launch_data);
    // Re-enable the launcher
    gtk_window_present(GTK_WINDOW(ld->window));
    gtk_widget_set_sensitive(GTK_WIDGET(ld->window), TRUE);
    return;
  }

  // Detach the thread as we don't need to join it
  g_thread_unref(thread);
}

//----------------------------------------------------
// Forward declarations
//----------------------------------------------------
static void switch_to_patch(LauncherData *ld);

/**
 * @brief Callback function for handling login button clicks.
 *
 * @param btn The login button widget.
 * @param user_data Pointer to LauncherData.
 */
static void on_login_clicked(GtkButton *btn, gpointer user_data) {
  LauncherData *ld = user_data;

  const char *username = gtk_editable_get_text(GTK_EDITABLE(ld->user_entry));
  const char *password = gtk_editable_get_text(GTK_EDITABLE(ld->pass_entry));
  g_message("Attempting login for user=%s", username);

  LoginData temp = {0};
  if (do_login(username, password, &temp)) {
    // Store login data
    strncpy(ld->login_data.user_no, temp.user_no,
            sizeof(ld->login_data.user_no) - 1);
    strncpy(ld->login_data.auth_key, temp.auth_key,
            sizeof(ld->login_data.auth_key) - 1);
    strncpy(ld->login_data.character_count, temp.character_count,
            sizeof(ld->login_data.character_count) - 1);

    // Prepare user welcome label on patch screen.
    snprintf(ld->login_data.welcome_label_msg,
             sizeof(ld->login_data.welcome_label_msg), "Welcome, <b>%s!</b>",
             username);

    gtk_label_set_markup(GTK_LABEL(ld->welcome_label),
                         ld->login_data.welcome_label_msg);

    g_message("Login success => switch to patch");
    switch_to_patch(ld);
  } else {
    // Display error dialog
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(ld->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE, "Login failed. Please check your credentials.");
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_window_destroy(GTK_WINDOW(dialog));
  }
}

/**
 * @brief Callback function for handling the logout button clicks.
 *
 * @param btn The logout button widget.
 * @param user_data Pointer to LauncherData.
 */
static void on_logout_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn; // Unused parameter
  const LauncherData *ld = user_data;

  g_message("Logout => back to login");

  // Show the login overlay and hide the patch overlay
  gtk_widget_set_visible(ld->login_overlay, TRUE);
  gtk_widget_set_visible(ld->patch_overlay, FALSE);

  // Set the window size back to login dimensions
  gtk_window_set_default_size(GTK_WINDOW(ld->window), 340, 500);
  gtk_widget_set_size_request(GTK_WIDGET(ld->login_overlay), 340, 500);
  gtk_widget_set_size_request(GTK_WIDGET(ld->patch_overlay), 340, 500);

  g_message("Successfully returned to login screen");
}

/**
 * @brief Callback function for handling repair dialog confirmation.
 *
 * @param dialog The repair confirmation dialog the user was prompted with.
 * @param response_id The type of response the user provided.
 * @param user_data Pointer to LauncherData.
 */
static void on_repair_dialog_popped(GtkDialog *dialog, gint response_id,
                                    gpointer user_data) {
  LauncherData *ld_inner = user_data;
  if (response_id == GTK_RESPONSE_YES) {
    g_message("Repair confirmed. Initiating repair process.");

    // Start the update process
    start_update_process(ld_inner, TRUE);
  } else {
    g_message("Repair canceled by user.");
  }

  // Destroy the dialog
  gtk_window_destroy(GTK_WINDOW(dialog));
}

/**
 * @brief Callback function for handling the repair button clicks.
 *
 * @param btn The repair button widget.
 * @param user_data Pointer to LauncherData.
 */
static void on_repair_clicked(GtkButton *btn, gpointer user_data) {
  LauncherData *ld = user_data;

  // Create a confirmation dialog
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Confirm Repair", GTK_WINDOW(ld->window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Yes",
      GTK_RESPONSE_YES, "_No", GTK_RESPONSE_NO, nullptr);

  GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *label = gtk_label_new("Are you sure you want to initiate repair?");
  gtk_box_append(GTK_BOX(content_area), label);
  gtk_widget_set_visible(label, TRUE);

  // Connect to the 'response' signal to handle user input
  g_signal_connect(dialog, "response", G_CALLBACK(on_repair_dialog_popped), ld);

  // Present the dialog
  gtk_window_present(GTK_WINDOW(dialog));
}

/**
 * @brief Callback function for handling the close button on the login pane.
 *
 * @param btn The close button widget.
 * @param user_data Pointer to LauncherData.
 */
static void on_close_login_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  const LauncherData *ld = user_data;
  g_message("Close from login pane");
  updater_shutdown();
  gtk_window_destroy(GTK_WINDOW(ld->window));
}

/**
 * @brief Callback function for handling the close button on the patch pane.
 *
 * @param btn The close button widget.
 * @param user_data Pointer to LauncherData.
 */
static void on_close_patch_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  const LauncherData *ld = user_data;
  g_message("Close from patch pane => destroy");
  updater_shutdown();
  gtk_window_destroy(GTK_WINDOW(ld->window));
}

/**
 * @brief Callback function for handling mouse motion events for window
 * dragging.
 *
 * @param controller The motion event controller.
 * @param x The x-coordinate of the mouse.
 * @param y The y-coordinate of the mouse.
 * @param user_data Pointer to LauncherData.
 * @return FALSE to propagate the event further.
 */
static gboolean on_motion(GtkEventControllerMotion *controller, double x,
                          double y, gpointer user_data) {
  LauncherData *ld = user_data;
  DragData *data = &ld->drag_data;

  // Check if left mouse button is pressed
  const GdkModifierType state = gtk_event_controller_get_current_event_state(
      GTK_EVENT_CONTROLLER(controller));

  if ((state & GDK_BUTTON1_MASK) && !data->dragging) {
    data->dragging = TRUE;

    constexpr gint button = 1; // Left mouse button
    const gint x_root = (int)x;
    const gint y_root = (int)y;

    // Obtain the current time for the event
    guint32 timestamp =
        gdk_event_get_time(gtk_event_controller_get_current_event(
            GTK_EVENT_CONTROLLER(controller)));
    if (!timestamp) {
      timestamp = g_get_monotonic_time() / 1000;
    }

    // Initiate the window move drag
    GtkWidget *widget =
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    GdkSurface *surface = gtk_native_get_surface(gtk_widget_get_native(widget));
    GdkEvent *event = gtk_event_controller_get_current_event(
        GTK_EVENT_CONTROLLER(controller));
    const auto source_point = GRAPHENE_POINT_INIT(x, y);
    graphene_point_t result_point;
    if (!gtk_widget_compute_point(widget,
                                  GTK_WIDGET(gtk_widget_get_root(widget)),
                                  &source_point, &result_point)) {
      // Unable to compute point
      data->dragging = FALSE;
      gtk_event_controller_reset(GTK_EVENT_CONTROLLER(controller));
      return FALSE;
    }
    gdk_toplevel_begin_move(GDK_TOPLEVEL(surface), gdk_event_get_device(event),
                            button, x_root, y_root, timestamp);
    gtk_event_controller_reset(GTK_EVENT_CONTROLLER(controller));
  } else {
    data->dragging = FALSE;
  }

  return FALSE; // Propagate the event further
}

/**
 * @brief Switches the UI from the login overlay to the patch overlay and
 * initiates the update process.
 *
 * @param ld Pointer to the LauncherData structure.
 */
static void switch_to_patch(LauncherData *ld) {
  gtk_window_set_default_size(GTK_WINDOW(ld->window), 960, 610);
  gtk_widget_set_size_request(GTK_WIDGET(ld->login_overlay), 960, 610);
  gtk_widget_set_size_request(GTK_WIDGET(ld->patch_overlay), 960, 610);
  gtk_widget_set_visible(ld->login_overlay, FALSE);
  gtk_widget_set_visible(ld->patch_overlay, TRUE);

  // Start the update process
  start_update_process(ld, FALSE);
}

/**
 * @brief Activation callback for GtkApplication.
 *
 * @param app The GtkApplication.
 * @param user_data Pointer to LauncherData.
 */
static void activate(GtkApplication *app, gpointer user_data) {
  LauncherData *ld = user_data;
  if (!launcher_init_config(app)) {
    g_error("Could not initialize launcher from embedded configuration");
  }

  GError *error = nullptr;
  style_data_gbytes =
      g_resources_lookup_data("/com/tera/launcher/styles.css", 0, &error);
  if (error) {
    g_error("Could not load styles from css: %s", error->message);
  }

  gsize size;
  style_data = g_bytes_get_data(style_data_gbytes, &size);
  if (!style_data) {
    g_error("Could not get style data from css");
  }

  updater_init();

  // Create top-level window
  ld->window = gtk_application_window_new(app);
  setup_transparent_window(ld->window);

  // Build login and patch overlays
  ld->login_overlay = create_login_overlay(ld);
  ld->patch_overlay = create_patch_overlay(ld);
  ld->base_overlay = gtk_overlay_new();
  gtk_window_set_child(GTK_WINDOW(ld->window), ld->base_overlay);
  gtk_overlay_add_overlay(GTK_OVERLAY(ld->base_overlay), ld->login_overlay);
  gtk_overlay_add_overlay(GTK_OVERLAY(ld->base_overlay), ld->patch_overlay);
  gtk_window_set_default_size(GTK_WINDOW(ld->window), 340, 500);
  gtk_widget_set_size_request(GTK_WIDGET(ld->login_overlay), 340, 500);
  gtk_widget_set_visible(ld->login_overlay, TRUE);
  gtk_widget_set_visible(ld->patch_overlay, FALSE);

  // Add gesture controllers.
  ld->login_controller = gtk_event_controller_motion_new();
  ld->patch_controller = gtk_event_controller_motion_new();

  // Connect signals for login
  g_signal_connect(ld->login_btn, "clicked", G_CALLBACK(on_login_clicked), ld);
  g_signal_connect(ld->close_login_btn, "clicked",
                   G_CALLBACK(on_close_login_clicked), ld);
  g_signal_connect(ld->login_controller, "motion", G_CALLBACK(on_motion), ld);
  g_signal_connect(ld->patch_controller, "motion", G_CALLBACK(on_motion), ld);
  // Connect signals for patch
  g_signal_connect(ld->play_btn, "clicked", G_CALLBACK(on_play_clicked), ld);
  g_signal_connect(ld->logout_btn, "clicked", G_CALLBACK(on_logout_clicked),
                   ld);
  g_signal_connect(ld->repair_btn, "clicked", G_CALLBACK(on_repair_clicked),
                   ld);
  g_signal_connect(ld->close_patch_btn, "clicked",
                   G_CALLBACK(on_close_patch_clicked), ld);

  // Track motion on our window overlays
  gtk_widget_add_controller(ld->login_overlay,
                            GTK_EVENT_CONTROLLER(ld->login_controller));
  gtk_widget_add_controller(ld->patch_overlay,
                            GTK_EVENT_CONTROLLER(ld->patch_controller));

  // Show the main window
  gtk_widget_set_visible(ld->window, TRUE);
}

GResource *mylauncher_get_resource(void);

/**
 * @brief Main function initializing the GTK application.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status.
 */
int main(const int argc, char **argv) {
  // Initialize GTK application
  GtkApplication *app =
      gtk_application_new("com.tera.launcher", G_APPLICATION_DEFAULT_FLAGS);

  // Initialize LauncherData
  LauncherData ld = {nullptr};

  // Register the resources
  g_resources_register(mylauncher_get_resource());

  // Connect the activate signal
  g_signal_connect(app, "activate", G_CALLBACK(activate), &ld);

  // Run the application
  int status = g_application_run(G_APPLICATION(app), argc, argv);

  // Clean up
  g_object_unref(app);

  return status;
}

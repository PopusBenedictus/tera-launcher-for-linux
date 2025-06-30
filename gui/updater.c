/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#include "updater.h"
#include "globals.h"
#include "util.h"
#include <curl/curl.h>
#include <gio/gio.h>
#include <glib.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

/* --- CONSTANTS --- */

/* We use our own handle to avoid competing with other threads for access to it
 */
static CURL *curl = nullptr;

/* Current game version parsed from version.ini */
static gint current_version = 0;

/* The requested max retries for file downloads per file from the server. */
static gint max_retries = 3;

/* The amount of time to wait between retries, in milliseconds */
static gint retry_delay_ms = 1000;

static gchar *db_url_path = nullptr;

static gchar *db_name = nullptr;

static gchar *patch_path = nullptr;

/* SQL query to generate the update manifest.
   Note: The @current_version placeholder is bound in the code. */
static const gchar *sql_generate_update_manifest = nullptr;
static GBytes *generate_update_manifest_gbytes = nullptr;

/* SQL query to generate a full file manifest (used for repair operations) */
static const gchar *sql_generate_full_manifest = nullptr;
static GBytes *generate_full_file_manifest_gbytes = nullptr;

/* SQL query to get a count of records for the query above (used to report
 * progress during repair operations) */
static const gchar *sql_generate_full_manifest_count = nullptr;
static GBytes *generate_full_file_manifest_count_gbytes = nullptr;

/* SQL query to generate filesystem directory tree (used for repair operations)
 */
static const gchar *sql_generate_file_paths = nullptr;
static GBytes *generate_file_paths_gbytes = nullptr;

/* SQL query to get a count of records for the query above (used to report
 * progress during directory tree build operations) */
static const gchar *sql_generate_file_paths_count = nullptr;
static GBytes *generate_file_paths_count_gbytes = nullptr;

/* Used by curl to report download data rates to the progress bar label */
typedef struct {
  ProgressCallback callback;
  ProgressCallback download_callback;
  gpointer user_data;
  const char *prefix_string;
  double progress;
  double last_update_time;
  char pbar_label[FIXED_STRING_FIELD_SZ * 4];
  char download_now[FIXED_STRING_FIELD_SZ];
  char download_total[FIXED_STRING_FIELD_SZ];
  char download_speed[FIXED_STRING_FIELD_SZ];
} ProgressData;

/* --- HELPER FUNCTIONS --- */

/*
 * update_progress:
 *
 * Calls the user-supplied progress callback if provided.
 */
static void update_progress(ProgressCallback callback, double progress,
                            const char *message, gpointer user_data) {
  if (callback)
    callback(progress, message, user_data);
}

// Helper to print size in KB or MB
static void print_size(const double bytes, char *out, const size_t sz) {
  // 1 MB = 1024 * 1024 bytes
  bool success;
  size_t required;
  if (bytes < 1024.0 * 1024.0) {
    // Print in KB
    success = str_copy_formatted(out, &required, sz, "%.2f KB", bytes / 1024.0);
  } else {
    // Print in MB
    success = str_copy_formatted(out, &required, sz, "%.2f MB",
                                 bytes / (1024.0 * 1024.0));
  }

  if (!success) {
    g_error("Failed to allocate %zu bytes for file size update into buffer of "
            "%zu bytes.",
            required, sz);
  }
}

// Helper to print speed in kilobits or megabits
static void print_speed(curl_off_t bytes_per_second, char *out,
                        const size_t sz) {
  // Convert bytes/sec -> bits/sec
  double bits_per_second = (double)bytes_per_second * 8.0;
  bool success;
  size_t required;
  // 1 Mbit/s = 1,048,576 bits/s = 1024 * 1024
  if (bits_per_second < 1024.0 * 1024.0) {
    // Print in kb/s
    success = str_copy_formatted(out, &required, sz, "%.2f kb/s",
                                 bits_per_second / 1024.0);
  } else {
    // Print in Mb/s
    success = str_copy_formatted(out, &required, sz, "%.2f Mb/s",
                                 bits_per_second / (1024.0 * 1024.0));
  }

  if (!success) {
    g_error("Failed to allocate %zu bytes for data rate update into buffer of "
            "%zu bytes.",
            required, sz);
  }
}

static double get_time_in_seconds(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
}

/*
 * xfer_progress:
 *
 * A callback for libcurl to report data rate progress
 */
static int xfer_progress(void *p, curl_off_t dltotal, curl_off_t dlnow,
                         curl_off_t ultotal, curl_off_t ulnow) {
  ProgressData *data = p;
  curl_off_t speed;
  const double now = get_time_in_seconds();

  if (now - data->last_update_time >= 0.15) {
    data->last_update_time = now;
    curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD_T, &speed);

    // Clamp inputs to zero if we receive negative values to avoid displaying
    // invalid data.
    print_size((double)dlnow >= 0.0 ? (double)dlnow : 0.0, data->download_now,
               sizeof(data->download_now));
    print_size((double)dltotal >= 0.0 ? (double)dltotal : 0.0,
               data->download_total, sizeof(data->download_total));
    print_speed(speed, data->download_speed, sizeof(data->download_speed));
    size_t required;
    constexpr size_t pbar_sz = sizeof(data->pbar_label);
    const bool success = str_copy_formatted(
        data->pbar_label, &required, pbar_sz, "Progress: ( %s / %s ) %s",
        data->download_now, data->download_total, data->download_speed);
    if (!success) {
      g_error("Failed to allocate %zu bytes for progress bar update into "
              "buffer of %zu bytes.",
              required, pbar_sz);
    }

    // Like earlier, clamp result to zero if negative value is produced (invalid
    // data, divide by zero result, etc.)
    double progress_now = (double)dlnow / (double)dltotal;
    if (progress_now < 0.0)
      progress_now = 0.0;

    update_progress(data->download_callback, progress_now, data->pbar_label,
                    data->user_data);
  }
  return 0;
}

/*
 * compute_file_md5:
 *
 * Computes the MD5 checksum for the file at 'filepath'.
 * Returns a newly allocated hexadecimal string (which should be freed by the
 * caller), or NULL on error.
 */
static char *compute_file_md5(const char *filepath) {
  GChecksum *checksum = g_checksum_new(G_CHECKSUM_MD5);
  gchar *contents = nullptr;
  gssize length = 0;

  if (!g_file_get_contents(filepath, &contents, (gsize *)&length, nullptr)) {
    g_checksum_free(checksum);
    return nullptr;
  }
  g_checksum_update(checksum, (const guchar *)contents, length);
  g_free(contents);
  return g_strdup(g_checksum_get_string(checksum));
}

/*
 * get_file_size:
 *
 * Returns the size (in bytes) of the file at 'filepath', or 0 if it cannot be
 * read.
 */
static unsigned long get_file_size(const char *filepath) {
  struct stat st;
  if (stat(filepath, &st) == 0)
    return st.st_size;
  return 0;
}

/*
 * download_file:
 *
 * Downloads the file at 'url' to a temporary file using libcurl.
 * If expected_size is greater than 0, verifies that the downloaded file size
 * matches. On success, returns a newly allocated string with the temporary file
 * path. On failure, returns NULL.
 */
static char *download_file(const char *url, const unsigned long expected_size,
                           ProgressData *p_data) {
  char template[] = "/tmp/updaterXXXXXX";
  const int fd = mkstemp(template);
  if (fd == -1)
    return nullptr;
  FILE *fp = fdopen(fd, "wb");
  if (!fp) {
    close(fd);
    unlink(template);
    return nullptr;
  }

  if (!curl) {
    fclose(fp);
    unlink(template);
    return nullptr;
  }

  curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (128 * 1024));
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

  // Hook up progress updates if we received a prefix string.
  if (p_data) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, p_data);
  }

  CURLcode res;
  gint retry_count = 0;
  do {
    res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
      break;
    }

    g_warning("curl_easy_perform() failed: %s", curl_easy_strerror(res));

    retry_count++;

    if (retry_count < max_retries) {
      g_warning("Retrying in %d seconds... (retry %d of %d)",
                retry_delay_ms / 1000, retry_count, max_retries);
      sleep(retry_delay_ms / 1000);
    } else {
      g_warning("Max retries reached. Exiting.");
    }
  } while (retry_count < max_retries);
  fclose(fp);

  if (res != CURLE_OK) {
    unlink(template);
    return nullptr;
  }

  /* If an expected size is specified (> 0) then validate the download */
  if (expected_size > 0) {
    const unsigned long actual_size = get_file_size(template);
    if (actual_size != expected_size) {
      unlink(template);
      return nullptr;
    }
  }

  return g_strdup(template);
}

/*
 * extract_cabinet:
 *
 * Uses the external 'unelzma' tool to extract the compressed file.
 * 'cabinet_path' is the downloaded compressed file.
 * 'dest_path' is where the extracted file will be placed.
 * 'expected_size' is the expected size of the decompressed file (if > 0 it is
 * checked).
 *
 * Returns TRUE on success, FALSE on failure.
 * Note: unelzma is expected to remove the cabinet file on successful
 * extraction.
 */
static gboolean extract_cabinet(const char *cabinet_path, const char *dest_path,
                                unsigned long expected_size) {
  char command[FIXED_STRING_FIELD_SZ];
  /* Since unelzma is a custom app we build for use with the launcher it is
   * expected to be bundled with the launcher */
  size_t required;
  bool success = false;
  if (appimage_mode)
    success = str_copy_formatted(command, &required, FIXED_STRING_FIELD_SZ,
                         "%s/usr/bin/unelzma \"%s\" \"%s\"", appdir_global, cabinet_path, dest_path);
  else
    success = str_copy_formatted(command, &required, FIXED_STRING_FIELD_SZ,
                         "./unelzma \"%s\" \"%s\"", cabinet_path, dest_path);

  if (!success) {
    g_error("Failed to allocate %zu bytes for command path string in buffer of "
            "size %zu bytes.",
            required, FIXED_STRING_FIELD_SZ);
  }
  if (system(command) != 0)
    return FALSE;

  const unsigned long actual_size = get_file_size(dest_path);
  if (expected_size > 0 && actual_size != expected_size)
    return FALSE;
  return TRUE;
}

static gboolean download_version_ini(UpdateData *data) {
  /* Construct the URL to download the version.ini file. */
  gchar *version_ini_url =
      g_strdup_printf("%s/%s", data->public_patch_url, "version.ini");

  char *version_ini_path = download_file(version_ini_url, 0, nullptr);
  if (!version_ini_path) {
    g_printerr("Failed to download version.ini\n");
    return FALSE;
  }

  GError *error = nullptr;
  gchar *current_dir = g_get_current_dir();
  gchar *dest_path;

  if (appimage_mode) {
    dest_path = g_build_filename(configprefix_global, "version.ini", nullptr);
  } else {
    dest_path = g_build_filename(current_dir, "version.ini", nullptr);
  }

  GFile *src_file = g_file_new_for_path(version_ini_path);
  GFile *dest_file = g_file_new_for_path(dest_path);
  g_free(current_dir);

  if (g_file_test(dest_path, G_FILE_TEST_EXISTS)) {
    if (!g_file_delete(dest_file, nullptr, &error)) {
      g_printerr(
          "Unable to delete the old version.ini while fetching the new one.");
      g_error_free(error);
      g_object_unref(src_file);
      g_object_unref(dest_file);
      g_free(dest_path);
      return FALSE;
    }
  }

  g_free(dest_path);

  if (!g_file_move(src_file, dest_file, G_FILE_COPY_NONE, nullptr, nullptr,
                   nullptr, &error)) {
    g_printerr("Failed to move version.ini to game path: %s\n", error->message);
    g_error_free(error);
    g_object_unref(src_file);
    g_object_unref(dest_file);
    return FALSE;
  }

  g_object_unref(src_file);
  g_object_unref(dest_file);
  return TRUE;
}

/*
 * load_server_db:
 *
 * Downloads the latest database cabinet using the "DB file" value
 * (db_cab_filename) combined with data->public_patch_url, extracts it to a
 * temporary file (with the .cab removed) and opens the resulting SQLite
 * database.
 *
 * Returns a pointer to an open sqlite3 database or NULL on error.
 *
 * NOTE: In a production environment the expected compressed file size should be
 * taken from version.ini.
 */
static sqlite3 *load_server_db(UpdateData *data, gboolean skip_download) {
  sqlite3 *db = nullptr;

  gchar *db_full_path;
  if (appimage_mode)
    db_full_path = g_build_filename(configprefix_global, db_name, nullptr);
  else
    db_full_path = g_strdup(db_name);

  /* Here we use 0 for expected_size to disable the size check.
     There is no way to know what the size of this file is in advance and no
     verification methods are provided AFAIK. */
  if (!skip_download) {
    /* Construct the URL to download the DB cabinet. */
    gchar *db_url =
        g_strdup_printf("%s/%s", data->public_patch_url, db_url_path);
    char *db_cab_path = download_file(db_url, 0, nullptr);
    g_free(db_url);

    if (!db_cab_path) {
      g_printerr("Failed to download database cab file.\n");
      return nullptr;
    }

    if (!extract_cabinet(db_cab_path, db_full_path, 0)) {
      g_printerr("Failed to extract the database cabinet file.\n");
      unlink(db_cab_path);
      g_free(db_cab_path);
      return nullptr;
    }
    /* The cabinet is expected to be removed by unelzma on success */
    g_free(db_cab_path);
  }

  /* Open the SQLite database */
  if (sqlite3_open(db_full_path, &db) != SQLITE_OK) {
    g_printerr("Error opening database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    db = nullptr;
  }

  g_free(db_full_path);
  return db;
}

/* --- PUBLIC FUNCTIONS --- */

/*
 * updater_init:
 *
 * Initialize globals the updater routines use.
 */
void updater_init() {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  curl = curl_easy_init();

  GError *error = nullptr;
  generate_file_paths_gbytes = g_resources_lookup_data(
      "/com/tera/launcher/generate-file-paths.sql", 0, &error);
  if (!generate_file_paths_gbytes) {
    g_error("Error loading SQL resource: %s", error->message);
  }

  generate_full_file_manifest_gbytes = g_resources_lookup_data(
      "/com/tera/launcher/generate-full-file-manifest.sql", 0, &error);
  if (!generate_full_file_manifest_gbytes) {
    g_error("Error loading SQL resource: %s", error->message);
  }

  generate_update_manifest_gbytes = g_resources_lookup_data(
      "/com/tera/launcher/generate-update-manifest.sql", 0, &error);
  if (!generate_update_manifest_gbytes) {
    g_error("Error loading SQL resource: %s", error->message);
  }

  generate_full_file_manifest_count_gbytes = g_resources_lookup_data(
      "/com/tera/launcher/generate-full-file-manifest-count.sql", 0, &error);
  if (!generate_full_file_manifest_count_gbytes) {
    g_error("Error loading SQL resource: %s", error->message);
  }

  generate_file_paths_count_gbytes = g_resources_lookup_data(
      "/com/tera/launcher/generate-file-paths-count.sql", 0, &error);
  if (!generate_file_paths_count_gbytes) {
    g_error("Error loading SQL resource: %s", error->message);
  }

  gsize size;
  sql_generate_file_paths = g_bytes_get_data(generate_file_paths_gbytes, &size);
  if (!sql_generate_file_paths) {
    g_error("Could not get file paths query data from resource.");
  }

  sql_generate_update_manifest =
      g_bytes_get_data(generate_update_manifest_gbytes, &size);
  if (!sql_generate_update_manifest) {
    g_error("Could not get update manifest query data from resource.");
  }

  sql_generate_full_manifest =
      g_bytes_get_data(generate_full_file_manifest_gbytes, &size);
  if (!sql_generate_full_manifest) {
    g_error("Could not get full manifest query data from resource.");
  }

  sql_generate_full_manifest_count =
      g_bytes_get_data(generate_full_file_manifest_count_gbytes, &size);
  if (!sql_generate_full_manifest_count) {
    g_error("Could not get full manifest query count data from resource.");
  }

  sql_generate_file_paths_count =
      g_bytes_get_data(generate_file_paths_gbytes, &size);
  if (!sql_generate_file_paths_count) {
    g_error("Could not get file paths query count data from resource.");
  }
}

/*
 * updater_shutdown:
 *
 * Dispose of globals the updater routines use.
 */
void updater_shutdown() {
  curl_easy_cleanup(curl);
  curl_global_cleanup();
}

static gboolean parse_version_ini() {
  GKeyFile *key_file = g_key_file_new();
  GError *error = nullptr;

  char ini_path[FIXED_STRING_FIELD_SZ] = {0};
  if (appimage_mode) {
    size_t required;
    if (!str_copy_formatted(ini_path, &required, FIXED_STRING_FIELD_SZ, "%s/%s",
                            configprefix_global, "version.ini")) {
      g_error("Unable to construct config path: too big for buffer.");
    }
  } else {
    strcpy(ini_path, "version.ini");
  }

  if (!g_key_file_load_from_file(key_file, ini_path, G_KEY_FILE_NONE, &error)) {
    g_object_unref(key_file);
    return FALSE;
  }

  max_retries = g_key_file_get_integer(key_file, "Download", "Retry", &error);
  if (error) {
    g_object_unref(key_file);
    return FALSE;
  }

  retry_delay_ms = g_key_file_get_integer(key_file, "Download", "Wait", &error);
  if (error) {
    g_object_unref(key_file);
    return FALSE;
  }

  current_version =
      g_key_file_get_integer(key_file, "Download", "Version", &error);
  if (error) {
    g_object_unref(key_file);
    return FALSE;
  }

  // If there are existing instances of these strings, we are clearing them
  // before setting new values.
  if (db_url_path) {
    g_free(db_url_path);
    db_url_path = nullptr;
  }

  db_url_path = g_key_file_get_string(key_file, "Download", "DB file", &error);
  if (error) {
    g_object_unref(key_file);
    return FALSE;
  }

  if (patch_path) {
    g_free(patch_path);
    patch_path = nullptr;
  }

  patch_path = g_key_file_get_string(key_file, "Download", "DL root", &error);
  if (error) {
    g_object_unref(key_file);
    g_free(db_url_path);
    db_url_path = nullptr;
    return FALSE;
  }

  if (db_name) {
    g_free(db_name);
    db_name = nullptr;
  }

  // Its possible the database will not end in cab, but let's handle the use
  // case where it does for clarity. If this does not occur, it does not
  // materially impact the function of the launcher. Just the database will be
  // stored, uncompressed, in its unarchived form with possibly an archive
  // looking name.
  db_name = g_path_get_basename(db_url_path);
  char suffix_strip[FIXED_STRING_FIELD_SZ] = {0};
  size_t required;
  const bool success =
      str_copy_formatted(suffix_strip, &required, FIXED_STRING_FIELD_SZ,
                         ".%i.cab", current_version);
  if (!success) {
    g_error("Unable to allocate %zu bytes for suffix into buffer of %zu bytes",
            required, FIXED_STRING_FIELD_SZ);
  }

  // Honestly, if someone returns a suffix longer than the above for this, and
  // we can't strip the suffix off because of it, it's on them. This won't
  // crash, but the db file gets to end in cab forever on the users system :^).
  if (g_str_has_suffix(db_name, suffix_strip))
    db_name[strlen(db_name) - strlen(suffix_strip)] = '\0';
  return TRUE;
}

/*
 * get_files_to_update:
 *
 * Determines which game files need updating by comparing the local version with
 * the latest information in the server database. This function uses the
 * generate-update-manifest SQL.
 *
 * Returns a GList of FileInfo structures (which must later be freed with
 * free_file_info()).
 */
GList *get_files_to_update(UpdateData *data, ProgressCallback callback,
                           gpointer user_data) {
  update_progress(callback, 0.0, "Checking for updates...", user_data);

  // If version.ini or server.db are missing, this becomes a repair operation.
  if (!parse_version_ini()) {
    update_progress(callback, 0.0,
                    "Missing or invalid version.ini: Beginning repair...",
                    user_data);
    g_usleep(3000000);
    return get_files_to_repair(data, callback, user_data);
  }

  /* Fetch latest, continue with updates. We just check version.ini as a
   * shortcut to know if things have been messed with or otherwise failed to
   * clear normally the last time the launcher ran.
   */
  if (!download_version_ini(data)) {
    update_progress(callback, 0.0, "Unable to fetch latest version.ini",
                    user_data);
    return nullptr;
  }

  if (!parse_version_ini()) {
    update_progress(callback, 0.0, "Unable to parse latest version.ini",
                    user_data);
    return nullptr;
  }

  GList *update_list = nullptr;
  sqlite3 *db = load_server_db(data, FALSE);
  if (!db) {
    update_progress(callback, 1.0, "Failed to download latest update database.",
                    user_data);
    return update_list;
  }

  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql_generate_update_manifest, -1, &stmt,
                         nullptr) != SQLITE_OK) {
    g_printerr("SQL error: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return update_list;
  }

  /* Bind the current_version parameter (the first parameter index is 1) */
  if (sqlite3_bind_int(stmt, 1, current_version) != SQLITE_OK) {
    g_printerr("Error binding current version: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return update_list;
  }

  /* Iterate through results and add each file to the list. */
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const int id = sqlite3_column_int(stmt, 0);
    const unsigned char *path_text = sqlite3_column_text(stmt, 1);
    const int new_ver = sqlite3_column_int(stmt, 2);
    const unsigned long compressed_size = sqlite3_column_int(stmt, 3);
    const unsigned long decompressed_size = sqlite3_column_int(stmt, 4);
    const unsigned char *hash_text = sqlite3_column_text(stmt, 5);

    auto info = g_new0(FileInfo, 1);
    info->path = g_strdup((const char *)path_text);
    info->hash = g_strdup((const char *)hash_text);
    info->size = compressed_size;
    info->decompressed_size = decompressed_size;
    /* Construct the download URL using data->public_patch_url and naming
       convention: {public_patch_url}/{patch_path}/IDNUM-VERIDNUM.cab */
    info->url = g_strdup_printf("%s/%s/%d-%d.cab", data->public_patch_url,
                                patch_path, id, new_ver);
    update_list = g_list_append(update_list, info);
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  update_progress(callback, 1.0, "Update manifest retrieved.", user_data);
  return update_list;
}

/*
 * get_files_to_repair:
 *
 * For repair operations (e.g. missing or damaged local files), this function
 * queries the full file manifest from the server database.
 *
 * Returns a GList of FileInfo structures for files that need to be repaired.
 */
GList *get_files_to_repair(UpdateData *data, ProgressCallback callback,
                           gpointer user_data) {
  update_progress(callback, 0.0, "Checking for missing or damaged files...",
                  user_data);

  if (!download_version_ini(data)) {
    update_progress(callback, 0.0, "Unable to fetch latest version.ini",
                    user_data);
    return nullptr;
  }

  if (!parse_version_ini()) {
    update_progress(callback, 0.0, "Unable to parse downloaded version.ini",
                    user_data);
    return nullptr;
  }

  GList *repair_list = nullptr;
  sqlite3 *db = load_server_db(data, FALSE);
  if (!db) {
    update_progress(callback, 1.0, "Failed to load server database.",
                    user_data);
    return repair_list;
  }

  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql_generate_full_manifest_count, -1, &stmt,
                         nullptr) != SQLITE_OK) {
    g_printerr("SQL error: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return repair_list;
  }

  int record_count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    record_count = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  stmt = nullptr;

  if (sqlite3_prepare_v2(db, sql_generate_full_manifest, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    g_printerr("SQL error: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return repair_list;
  }

  guint processed = 0;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const int id = sqlite3_column_int(stmt, 0);
    const unsigned char *path_text = sqlite3_column_text(stmt, 1);
    const int new_ver = sqlite3_column_int(stmt, 2);
    const unsigned long compressed_size = sqlite3_column_int(stmt, 3);
    const unsigned long decompressed_size = sqlite3_column_int(stmt, 4);
    const unsigned char *hash_text = sqlite3_column_text(stmt, 5);

    char progress_msg[FIXED_STRING_FIELD_SZ];
    processed++;
    gchar *file_name = g_path_get_basename((const char *)path_text);
    size_t required;
    const bool success =
        str_copy_formatted(progress_msg, &required, FIXED_STRING_FIELD_SZ,
                           "Scanning file %u of %i: %s", processed,
                           record_count, (const char *)file_name);
    if (!success) {
      g_error("Unable to allocate %zu bytes for progress message into buffer "
              "of %zu bytes.",
              required, FIXED_STRING_FIELD_SZ);
    }
    g_free(file_name);
    update_progress(callback, (double)processed / (double)record_count,
                    progress_msg, user_data);

    gchar *processed_path =
        g_build_filename(data->game_path, path_text, nullptr);
    if (g_file_test(processed_path, G_FILE_TEST_EXISTS)) {
      char *md5_result = compute_file_md5(processed_path);
      if (strcmp((const char *)hash_text, md5_result) == 0) {
        if (get_file_size(processed_path) == decompressed_size) {
          /* File exists, hash matches, size matches -- nothing to do here. */
          free(md5_result);
          continue;
        }
      } else {
        GFile *busted_file = g_file_new_for_path(processed_path);
        if (!busted_file) {
          // TODO: Handle this better because ya know, if we can't replace this
          // bad file then the repair is not going to succeed :^)
          g_printerr("Unable to delete '%s'", processed_path);
          continue;
        }

        GError *error = nullptr;
        if (!g_file_delete(busted_file, nullptr, &error)) {
          // TODO: See earlier TODO.
          g_printerr("Unable to delete '%s': %s", processed_path,
                     error->message);
          g_clear_error(&error);
        }
      }
      free(md5_result);
    }

    auto info = g_new0(FileInfo, 1);

    info->path = g_strdup(processed_path);
    info->hash = g_strdup((const char *)hash_text);
    info->size = compressed_size;
    info->decompressed_size = decompressed_size;
    /* Construct the URL using the same naming convention: IDNUM-VERIDNUM.cab */
    info->url = g_strdup_printf("%s/%s/%d-%d.cab", data->public_patch_url,
                                patch_path, id, new_ver);
    repair_list = g_list_append(repair_list, info);
    g_free(processed_path);
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  update_progress(callback, 1.0, "Repair manifest retrieved.", user_data);
  return repair_list;
}

/*
 * download_all_files:
 *
 * Given a list of FileInfo structures representing files to update, this
 * function downloads, extracts, validates, and writes each updated file.
 */
gboolean download_all_files(UpdateData *data, GList *files_to_update,
                            ProgressCallback callback,
                            ProgressCallback download_callback,
                            gpointer user_data) {
  gboolean overall_success = TRUE;
  const guint total_files = g_list_length(files_to_update);
  guint processed = 0;

  // We need to build the directory tree before we do anything else.
  // There will be cascading failures if this is not performed so we will simply
  // return FALSE for success if the directory tree check fails.
  update_progress(callback, 0.0, "Building game directory tree...", user_data);

  sqlite3 *db = load_server_db(data, TRUE);
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql_generate_file_paths_count, -1, &stmt,
                         nullptr) != SQLITE_OK) {
    g_printerr("SQL error: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return FALSE;
  }

  int directories_count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    directories_count = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  stmt = nullptr;

  if (sqlite3_prepare_v2(db, sql_generate_file_paths, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    g_printerr("SQL error: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return FALSE;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char *dir_path = sqlite3_column_text(stmt, 0);
    char progress_msg[FIXED_STRING_FIELD_SZ];
    size_t required;
    processed++;
    const bool success =
        str_copy_formatted(progress_msg, &required, FIXED_STRING_FIELD_SZ,
                           "Checking directory %u of %i: %s", processed,
                           directories_count, (const char *)dir_path);
    if (!success) {
      g_error("Unable to allocate %zu bytes for progress bar message into "
              "buffer of %zu bytes.",
              required, FIXED_STRING_FIELD_SZ);
    }
    update_progress(callback, (double)processed / directories_count,
                    progress_msg, user_data);

    gchar *processed_path =
        g_build_filename(data->game_path, dir_path, nullptr);

    if (g_file_test(processed_path, G_FILE_TEST_EXISTS)) {
      if (g_file_test(processed_path, G_FILE_TEST_IS_DIR))
        continue;
      if (remove(processed_path) != 0) {
        update_progress(callback, 1.0,
                        "Failed to remove file where directory should be",
                        user_data);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return FALSE;
      }
    }

    if (g_mkdir_with_parents(processed_path, 0755) != 0) {
      update_progress(callback, 1.0, "Failed to create directory", user_data);
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return FALSE;
    }

    g_free(processed_path);
  }

  // Move on to the file download step.
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  update_progress(callback, 0.0, "Downloading files...", user_data);
  processed = 0;

  for (const GList *l = files_to_update; l != NULL; l = l->next) {
    const FileInfo *info = l->data;
    char progress_msg[FIXED_STRING_FIELD_SZ];
    size_t required;
    gchar *file_name = g_path_get_basename(info->path);
    processed++;
    bool success = str_copy_formatted(
        progress_msg, &required, FIXED_STRING_FIELD_SZ,
        "Downloading file %u of %u: %s", processed, total_files, file_name);
    if (!success) {
      g_error("Unable to allocate %zu bytes for progress message into buffer "
              "of %zu bytes.",
              required, FIXED_STRING_FIELD_SZ);
    }
    const double current_progress = (double)processed / total_files;
    update_progress(callback, current_progress, progress_msg, user_data);

    /* Download the cabinet file for the update.
       Validate that the downloaded file size matches the expected compressed
       size. */
    ProgressData p_data;
    p_data.callback = callback;
    p_data.download_callback = download_callback;
    p_data.prefix_string = progress_msg;
    p_data.user_data = user_data;
    p_data.progress = current_progress;

    char *cabinet_path = download_file(info->url, info->size, &p_data);

    if (!cabinet_path) {
      g_printerr("Error downloading %s\n", info->url);
      overall_success = FALSE;
      continue;
    }

    /* Extract the cabinet file.
     * Create a temporary file for the extracted content.
     */
    char temp_extract[] = "/tmp/extractedXXXXXX";
    const int fd = mkstemp(temp_extract);
    if (fd == -1) {
      g_printerr("Error creating temporary file for extraction.\n");
      unlink(cabinet_path);
      g_free(cabinet_path);
      overall_success = FALSE;
      continue;
    }
    close(fd);

    success = str_copy_formatted(progress_msg, &required, FIXED_STRING_FIELD_SZ,
                                 "Extracting file %u of %u: %s", processed,
                                 total_files, file_name);
    if (!success) {
      g_error("Unable to allocate %zu bytes for progress message into buffer "
              "of %zu bytes.",
              required, FIXED_STRING_FIELD_SZ);
    }
    g_free(file_name);
    update_progress(callback, current_progress, progress_msg, user_data);
    update_progress(download_callback, 1.0, "Progress: Done!", user_data);
    if (!extract_cabinet(cabinet_path, temp_extract, info->decompressed_size)) {
      g_printerr("Extraction failed for %s\n", cabinet_path);
      unlink(temp_extract);
      overall_success = FALSE;
      continue;
    }
    /* cabinet file is expected to be removed by unelzma on success */
    g_free(cabinet_path);

    /* Validate the extracted file by checking MD5 hash. */
    char *extracted_md5 = compute_file_md5(temp_extract);
    if (!extracted_md5 || g_strcmp0(extracted_md5, info->hash) != 0) {
      g_printerr("Hash mismatch for %s\n", info->path);
      g_free(extracted_md5);
      unlink(temp_extract);
      overall_success = FALSE;
      continue;
    }
    g_free(extracted_md5);

    /* Create GFile objects to use g_file_move */
    GFile *src_file = g_file_new_for_path(temp_extract);
    GFile *dest_file = g_file_new_for_path(info->path);
    GError *error = nullptr;

    if (!g_file_move(src_file, dest_file, G_FILE_COPY_NONE, nullptr, nullptr,
                     nullptr, &error)) {
      g_printerr("Failed to move file to destination: %s\n", info->path);
      g_error_free(error);
      unlink(temp_extract);
      overall_success = FALSE;
    }
    g_object_unref(src_file);
    g_object_unref(dest_file);
  }

  update_progress(callback, 1.0, "All downloads processed.", user_data);
  update_progress(download_callback, 1.0, "", user_data);
  return overall_success;
}

/*
 * free_file_info:
 *
 * Frees the memory allocated for a FileInfo structure.
 */
void free_file_info(void *info) {
  FileInfo *f_info = info;
  if (f_info) {
    g_free(f_info->path);
    g_free(f_info->hash);
    g_free(f_info->url);
    g_free(f_info);
  }
}

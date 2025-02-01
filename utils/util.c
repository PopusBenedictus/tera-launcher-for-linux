/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#include "util.h"
#include <time.h>
#if !defined(_WIN32) && defined(HAVE_GLIB)
#include <glib.h>
#endif
#ifndef _WIN32
#include <iconv.h>
#endif
/* -------------------------------------------------------------------------
 * Global Log State
 * ------------------------------------------------------------------------- */

#ifdef _WIN32
static CRITICAL_SECTION g_log_mutex;
#endif

static bool g_initialized = false;
static FILE *g_log_file = NULL;
static log_level_t g_max_level = LOG_LEVEL_TRACE;

static bool g_console_path_failed = false;
static bool g_file_path_failed = false;
static bool g_console_path_failure_reported = false;
static bool g_file_path_failure_reported = false;

/**
 * @brief Temporary buffer size for string operations on ascii/utf8 strings
 */
#define TMP_BUFFER_SZ (FIXED_STRING_FIELD_SZ * 8)

bool str_copy_formatted(char *buffer, size_t *size_out, size_t size_in,
                        char const *format, ...) {
  if (buffer == nullptr || size_out == nullptr || format == nullptr) {
    if (size_out != nullptr) {
      *size_out = 0;
    }
    return false;
  }

  // First pass: measure how many characters are needed (excluding null
  // terminator).
  {
    va_list args;
    va_start(args, format);
    // With size=0 and a nullptr buffer, vsnprintf returns the needed size
    // (excluding '\0').
    const int needed = vsnprintf(nullptr, 0, format, args);
    va_end(args);

    if (needed < 0) {
      // vsnprintf failed and cannot compute size
      *size_out = 0;
      return false;
    }

    // 'needed' does not include the null terminator.
    *size_out = (size_t)needed;

    // Check against user buffer capacity.
    // If needed + 1 (for '\0') > size_in, it won't fit -> fail.
    if ((size_t)needed + 1 > size_in) {
      return false;
    }

    // Check against our fixed stack buffer size as well:
    if ((size_t)needed + 1 > TMP_BUFFER_SZ) {
      // The formatted string doesn't fit into our on-stack temp array.
      return false;
    }

    // Now do a second pass into the fixed stack buffer.
    char temp_buf[TMP_BUFFER_SZ];

    va_list args2;
    va_start(args2, format);
    const int written = vsnprintf(temp_buf, (size_t)needed + 1, format, args2);
    va_end(args2);

    // If something went wrong this time, fail safely.
    // (written should match 'needed' if it succeeded.)
    if (written < 0 || written != needed) {
      return false;
    }

    // Success: copy from temp_buf into user buffer.
    // Copy needed chars plus the null terminator.
    for (int i = 0; i <= needed; i++) {
      buffer[i] = temp_buf[i];
    }
  }

  return true;
}

/**
 * @brief Maps a log_level_t to a string label.
 */
static char const *level_to_str(const log_level_t lvl) {
  switch (lvl) {
  case LOG_LEVEL_CRITICAL:
    return "CRITICAL";
  case LOG_LEVEL_INFO:
    return "INFO";
  case LOG_LEVEL_WARNING:
    return "WARNING";
  case LOG_LEVEL_ERROR:
    return "ERROR";
  case LOG_LEVEL_DEBUG:
    return "DEBUG";
  case LOG_LEVEL_TRACE:
    return "TRACE";
  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Output a timestamped message to a given stream.
 *
 * Format: `[LEVEL] YYYY-MM-DD HH:MM:SS: message\n`
 *
 * @return true on success, false on any I/O error.
 */
static bool output_timestamped_message(FILE *stream, const log_level_t lvl,
                                       char const *msg) {
  if (!stream) {
    return false;
  }

  // Time stamp
  time_t now = time(nullptr);
  struct tm *tm_p = nullptr;
#if defined(_WIN32)
  struct tm tbuf;
  localtime_s(&tbuf, &now); // Windows secure version
  tm_p = &tbuf;
#else
  tm_p = localtime(&now); // Elsewhere
#endif

  if (!tm_p) {
    return false;
  }

  char time_str[32];
  if (strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_p) == 0) {
    // Fallback if time can't be formatted
    time_str[0] = '\0';
  }

  // Print to stream
  if (fprintf(stream, "[%s] %s: %s\n", level_to_str(lvl), time_str, msg) < 0) {
    return false;
  }
  if (fflush(stream) == EOF) {
    return false;
  }

  return true;
}

bool log_init(const log_level_t max_level, char const *prefix) {
  // Guard from re-init
  if (g_initialized) {
    return true; // or decide to re-init
  }

  if (!prefix) {
    // fallback
    prefix = "log";
  }

  // Initialize critical section (mutex)
#ifdef _WIN32
  InitializeCriticalSection(&g_log_mutex);
#endif

  g_max_level = max_level;
  g_log_file = nullptr;
  g_initialized = true;
  g_console_path_failed = false;
  g_file_path_failed = false;
  g_console_path_failure_reported = false;
  g_file_path_failure_reported = false;

  // Build the filename "prefix-launcher.log" safely with str_copy_formatted
  char filename[1024];
  size_t needed;
  const bool ok = str_copy_formatted(filename, &needed, sizeof(filename),
                                     "%s-launcher.log", prefix);
  if (!ok) {
    // Could not build the filename => we can still log to console
    // Return false to indicate file won't be used
    g_file_path_failed = true;
    return false;
  }

  // Open the file in append mode
  g_log_file = fopen(filename, "a");
  if (!g_log_file) {
    // Mark file path as failed, but we can still log to console
    g_file_path_failed = true;
    return false;
  }

#if !defined(_WIN32) && defined(HAVE_GLIB)
  init_glib_logging();
#endif
  return true;
}

void log_shutdown(void) {
#ifdef _WIN32
  // Lock to avoid race with any in-progress calls
  EnterCriticalSection(&g_log_mutex);
#endif

  if (g_log_file) {
    fclose(g_log_file);
    g_log_file = nullptr;
  }
  g_initialized = false;

#ifdef _WIN32
  LeaveCriticalSection(&g_log_mutex);
  DeleteCriticalSection(&g_log_mutex);
#endif
}

bool log_message(const log_level_t level, char const *fmt, ...) {
  // If not initialized or invalid fmt, we can't proceed
  if (!g_initialized || !fmt) {
    return false;
  }

  // If level is above max, ignore quietly
  if (level > g_max_level) {
    return true;
  }

  // Build user message
  char user_msg[FIXED_STRING_FIELD_SZ];
  {
    va_list args;
    va_start(args, fmt);
    const int written = vsnprintf(user_msg, sizeof(user_msg), fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= sizeof(user_msg)) {
      // truncated or error
      user_msg[sizeof(user_msg) - 1] = '\0';
    }
  }

  // Thread-safe region
#ifdef _WIN32
  EnterCriticalSection(&g_log_mutex);
#endif

  bool console_ok = true;
  bool file_ok = true;

  // Console path
  if (!g_console_path_failed) {
    if (!output_timestamped_message(stderr, level, user_msg)) {
      // We have a console path failure
      g_console_path_failed = true;
      if (!g_console_path_failure_reported) {
        g_console_path_failure_reported = true;
        // If file is still valid, log a CRITICAL error to file
        if (!g_file_path_failed && g_log_file) {
          (void)output_timestamped_message(
              g_log_file, LOG_LEVEL_CRITICAL,
              "Console path has failed! Future console logs will be skipped.");
        }
      }
      console_ok = false;
    }
  }

  // File path
  if (!g_file_path_failed && g_log_file) {
    if (!output_timestamped_message(g_log_file, level, user_msg)) {
      // We have a file path failure
      g_file_path_failed = true;
      if (!g_file_path_failure_reported) {
        g_file_path_failure_reported = true;
        // If console is still valid, log a CRITICAL error to console
        if (!g_console_path_failed) {
          (void)output_timestamped_message(
              stderr, LOG_LEVEL_CRITICAL,
              "File path has failed! Future file logs will be skipped.");
        }
      }
      file_ok = false;
    }
  }

#ifdef _WIN32
  LeaveCriticalSection(&g_log_mutex);
#endif

  // Return true only if both paths succeeded
  return (console_ok && file_ok);
}

#if !defined(_WIN32) && defined(HAVE_GLIB)
/**
 * @brief Maps GLib log levels to our log_level_t.
 */
static log_level_t map_glib_to_log_level(GLogLevelFlags glib_level) {
  switch (glib_level) {
  case G_LOG_LEVEL_CRITICAL:
    return LOG_LEVEL_CRITICAL;
  case G_LOG_LEVEL_ERROR:
    return LOG_LEVEL_ERROR;
  case G_LOG_LEVEL_WARNING:
    return LOG_LEVEL_WARNING;
  case G_LOG_LEVEL_MESSAGE:
  case G_LOG_LEVEL_INFO:
    return LOG_LEVEL_INFO;
  case G_LOG_LEVEL_DEBUG:
    return LOG_LEVEL_DEBUG;
  default:
    return LOG_LEVEL_INFO;
  }
}

/**
 * @brief Custom GLib log handler that routes messages to our log_message().
 */
static void my_glib_log_handler(const gchar *log_domain,
                                GLogLevelFlags log_level, const gchar *message,
                                gpointer user_data) {
  log_level_t lvl = map_glib_to_log_level(log_level);

  if (log_domain) {
    log_message(lvl, "[%s] %s", log_domain, message);
  } else {
    log_message(lvl, "%s", message);
  }
}

void init_glib_logging(void) {
  // Install our handler as the default for any GLib domain.
  g_log_set_default_handler(my_glib_log_handler, NULL);
}
#endif
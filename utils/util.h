/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef UTIL_H
#define UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * @enum log_level_t
 * @brief Enumeration of log levels (numeric values dictate cutoff).
 *
 * If the max log level is LOG_LEVEL_WARNING (2), then CRITICAL (0),
 * INFO (1), and WARNING (2) messages pass through. ERROR (3), DEBUG (4),
 * and TRACE (5) do not.
 */
typedef enum {
  LOG_LEVEL_CRITICAL = 0,
  LOG_LEVEL_INFO = 1,
  LOG_LEVEL_WARNING = 2,
  LOG_LEVEL_ERROR = 3,
  LOG_LEVEL_DEBUG = 4,
  LOG_LEVEL_TRACE = 5
} log_level_t;

#ifndef FIXED_STRING_FIELD_SZ
#define FIXED_STRING_FIELD_SZ 16384
#endif

/**
 * @brief Copies formatted text into @p buffer (like snprintf) but:
 *        - Returns `true` only if it fully fits (including the null terminator)
 *        - Returns `false` otherwise
 *        - On any failure or insufficient space, does NOT modify @p buffer
 *        - Always writes into @p size_out the number of characters that
 *          would be required (excluding the null terminator).
 *
 * Uses a fixed-size stack buffer of size `TMP_BUFFER_SIZE` for the actual
 * formatting pass to avoid partial writes to the user buffer.
 *
 * @param[in,out] buffer   Destination buffer (only written on success).
 * @param[out]    size_out Number of characters needed to store the result
 *                         (excluding the null terminator).
 * @param[in]     size_in  Capacity of @p buffer (in bytes).
 * @param[in]     format   `printf`-style format string.
 * @param[in]     ...      Variadic arguments for @p format.
 *
 * @return `true` if the formatted string fits completely (including '\0'),
 *         `false` otherwise.
 *
 * **Example usage**:
 * \code{.c}
 *   char mybuf[100];
 *   size_t needed;
 *   bool ok = str_copy_formatted(mybuf, &needed, sizeof(mybuf),
 *                                "Hello %s", "World");
 *   // ok == true if "Hello World" (plus null terminator) fits in mybuf.
 *   // needed == 11 (the number of characters before the '\0').
 * \endcode
 */
bool str_copy_formatted(char *buffer, size_t *size_out, size_t size_in,
                        const char *format, ...);

#ifdef _WIN32
/**
 * @brief Converts a UTF-8 string into a wide-character (UTF-16) string but:
 *        - Returns `true` only if it fully fits (including the wide null
 * terminator)
 *        - Returns `false` otherwise
 *        - On any failure or insufficient space, does NOT modify @p buffer
 *        - Always writes into @p size_out the number of bytes required to
 *          store the resulting wide string (excluding the null terminator).
 *
 * Uses a fixed-size stack buffer of size `WTMP_BUFFER_SIZE` (in wide
 * characters) for the actual conversion pass to avoid partial writes to the
 * user buffer.
 *
 * @param[in,out] buffer   Wide-character destination buffer (only written on
 * success).
 * @param[out]    size_out Number of bytes required (excluding the wide null
 * terminator).
 * @param[in]     size_in  Capacity of @p buffer in **wide characters**
 * (including space for the null terminator).
 * @param[in]     source   UTF-8 source string (null-terminated).
 *
 * @return `true` if the converted wide string fits fully (including `L'\0'`),
 *         `false` otherwise.
 *
 * **Notes**:
 * - `size_in` is the number of `wchar_t` elements available in @p buffer.
 * - `*size_out` is the number of **bytes** needed for the resulting wide string
 *   (excluding the wide null terminator).
 * - This function uses `MultiByteToWideChar` with `CP_UTF8`.
 */
bool wstr_copy_from_utf8(wchar_t *buffer, size_t *size_out, size_t size_in,
                         char const *source);
#endif

/**
 * @brief Initialize the logging system.
 *
 * Opens (or creates) a file named "<prefix>-launcher.log" in the current
 * working directory, **appending** if it already exists.
 *
 * @param max_level  The maximum log level to allow (0..5).
 * @param prefix     The prefix for the log file name (e.g., "myapp").
 * @return           true if initialization succeeded (file opened or at least
 *                   console logging is possible), false otherwise.
 */
bool log_init(log_level_t max_level, char const *prefix);

/**
 * @brief Cleanly shut down the logging system.
 *
 * Closes the log file, frees resources, etc.
 */
void log_shutdown(void);

/**
 * @brief Logs a message at a given level, printing to console (stderr)
 *        and appending to the log file (unless either path has previously
 * failed).
 *
 * If @p level exceeds the configured max, the call does nothing (returns true).
 * If a path fails for the first time, it logs a CRITICAL message to the other
 * path.
 *
 * The format is "[LEVEL] YYYY-MM-DD HH:MM:SS: user_msg\n".
 *
 * @param level The log level.
 * @param fmt   `printf`-style format string.
 * @param ...   Arguments for the format string.
 * @return      true if both console & file logging succeeded for this message;
 *              false if at least one path fails.
 */
bool log_message(log_level_t level, char const *fmt, ...);

#include "util.h" // for log_message()
#include <assert.h>

#ifdef NDEBUG
// Release build
#define log_message_safe(level, fmt, ...)                                      \
  (void)log_message((level), (fmt), __VA_ARGS__)
#else
// Debug build
#define log_message_safe(level, fmt, ...)                                      \
  assert(log_message((level), (fmt), __VA_ARGS__))
#endif

/**
 * @brief If GLib is available and this is not a Windows build, we provide
 *        a function to redirect GLib logging (`g_message`, `g_warning`, etc.)
 *        into our own logging system.
 */
#if !defined(_WIN32) && defined(HAVE_GLIB)
void init_glib_logging(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // UTIL_H

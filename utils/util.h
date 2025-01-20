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

#ifdef __cplusplus
}
#endif

#endif // UTIL_H

/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#include "util.h"

/**
 * @brief Temporary buffer size for string operations on ascii/utf8 strings
 */
#define TMP_BUFFER_SZ (FIXED_STRING_FIELD_SZ * 8)

/**
 * @brief Temporary buffer size for string operations on utf16le strings (e.g.
 * WCHAR, wchar_t)
 */
#define WTMP_BUFFER_SZ (FIXED_STRING_FIELD_SZ * 16)

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

#ifdef _WIN32
bool wstr_copy_from_utf8(wchar_t *buffer, size_t *size_out, size_t size_in,
                         char const *source) {
  if (buffer == nullptr || size_out == nullptr || source == nullptr) {
    if (size_out != nullptr) {
      *size_out = 0;
    }
    return false;
  }

  // First pass: how many wide chars are needed (including the null terminator)?
  int needed_wchars = MultiByteToWideChar(CP_UTF8, 0, source,
                                          -1, // process until null terminator
                                          nullptr, 0);

  if (needed_wchars <= 0) {
    *size_out = 0;
    return false;
  }

  // needed_wchars includes the null terminator in wide chars.
  // The number of bytes (excluding the null terminator) is:
  //   (needed_wchars - 1) * sizeof(wchar_t).
  *size_out = (size_t)(needed_wchars - 1) * sizeof(wchar_t);

  // Check if user buffer has enough space (in wide chars).
  if ((size_t)needed_wchars > size_in) {
    return false;
  }

  // Check if it fits in our fixed on-stack buffer:
  if ((size_t)needed_wchars > WTMP_BUFFER_SZ) {
    // The converted string won't fit into our on-stack temp array.
    return false;
  }

  // Second pass: convert into our on-stack temporary buffer
  wchar_t temp_buf[WTMP_BUFFER_SZ];

  int converted =
      MultiByteToWideChar(CP_UTF8, 0, source, -1, temp_buf, needed_wchars);

  if (converted <= 0) {
    // Conversion failed
    return false;
  }

  // Copy from temp_buf to user buffer (including null terminator).
  for (int i = 0; i < needed_wchars; i++) {
    buffer[i] = temp_buf[i];
  }

  return true;
}
#endif
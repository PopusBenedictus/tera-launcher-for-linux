/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef SERVERLIST_FETCH_H
#define SERVERLIST_FETCH_H

#include <stddef.h>
#include <unicode/utypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetches a server list via XML from a given URL, parses it into a
 * Protobuf `ServerList`, and returns the serialized bytes. The caller must
 * free() the returned buffer.
 *
 * @param[out] out_size         The number of bytes in the returned buffer.
 * @param[in]  server_list_url  The URL to fetch; must not be NULL.
 * @return Pointer to a newly allocated buffer of Protobuf data, or NULL on
 * failure.
 */
uint8_t *get_server_list(size_t *out_size, const char *server_list_url,
                         const char *characters_count);

#ifdef __cplusplus
}
#endif

#endif /* SERVERLIST_FETCH_H */

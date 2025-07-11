/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef TORRENT_WRAPPER_H
#define TORRENT_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @struct TorrentSession
 * @brief Opaque handle to a libtorrent session and download context.
 */
typedef struct TorrentSession TorrentSession;

/**
 * @typedef TorrentProgressCallback
 * @brief Callback function to receive download progress updates.
 *
 * @param progress       Download progress as a percentage (0.0 to 100.0).
 *                       A value of -1.0 indicates an error occurred.
 * @param downloaded     Total bytes downloaded so far.
 * @param total          Total bytes to download.
 * @param download_rate  Current download rate in bytes per second.
 * @param userdata       User-provided context pointer passed back in each
 * callback.
 */
typedef void (*TorrentProgressCallback)(float progress, uint64_t downloaded,
                                        uint64_t total, uint32_t download_rate,
                                        void *userdata);

/**
 * @brief Create and configure a new torrent session.
 *
 * Allocates and initializes a libtorrent session listening on random ports.
 * Sets up internal alert handling for progress updates and errors.
 *
 * @param progress_cb Function pointer to receive progress updates.
 * @param userdata    Pointer to user-defined context for the callback.
 * @return Pointer to a new TorrentSession on success, or NULL on failure.
 */
TorrentSession *torrent_session_create(TorrentProgressCallback progress_cb,
                                       void *userdata);

/**
 * @brief Start downloading a torrent from a magnet link.
 *
 * Parses the provided magnet URI and begins downloading files to the
 * specified save directory. Progress is reported asynchronously via the
 * callback provided in torrent_session_create().
 *
 * @param session     Pointer to a valid TorrentSession.
 * @param magnet_link Null-terminated string containing the magnet URI.
 * @param save_path   Null-terminated string specifying the download folder.
 * @return 0 on success (download started), or -1 on error. Check
 *         torrent_session_get_error() for details on failure.
 */
int torrent_session_start_download(TorrentSession *session,
                                   const char *magnet_link,
                                   const char *save_path);

/**
 * @brief Retrieve the total size of the torrent contents (in bytes) by fetching
 * metadata.
 *
 * Parses the magnet URI, fetches metadata (blocking), and returns the total
 * content size.
 *
 * @param session     Pointer to a valid TorrentSession.
 * @param magnet_link Null-terminated string containing the magnet URI.
 * @param size_out    Pointer to uint64_t to receive the total size in bytes.
 * @return 0 on success, or -1 on error. Check torrent_session_get_error() for
 * details on failure.
 */
int torrent_session_get_total_size(TorrentSession *session,
                                   const char *magnet_link, uint64_t *size_out);

/**
 * @brief Close and clean up a torrent session.
 *
 * Signals the download thread to stop, waits for it to finish,
 * removes any active torrent, and destroys the libtorrent session.
 *
 * @param session Pointer to the TorrentSession to close. After this call,
 *                the session handle is no longer valid.
 */
void torrent_session_close(TorrentSession *session);

/**
 * @brief Retrieve the last error message for a session.
 *
 * Returns a C-string describing the most recent error. Valid until
 * torrent_session_close() is called on the same session.
 *
 * @param session Pointer to the TorrentSession instance.
 * @return Null-terminated string with error description, or an empty
 *         string if no error has occurred.
 */
const char *torrent_session_get_error(const TorrentSession *session);

#ifdef __cplusplus
}
#endif

#endif // TORRENT_WRAPPER_H
/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef TERALIB_H
#define TERALIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <util.h>
#include <stdbool.h>

/**
 * @brief Initializes the library, creating necessary synchronization
 * primitives.
 */
bool teralib_init(void);

/**
 * @brief Shuts down the library, destroying synchronization primitives.
 */
void teralib_shutdown(void);

/**
 * @brief Sets global credentials (account name, ticket, etc.).
 *
 * @param account_name      User's account name
 * @param characters_count  String containing character count info
 * @param ticket            Session ticket
 * @param game_lang         Game language code (e.g., "EUR")
 * @param game_path         Path to the game executable
 */
void set_credentials(const char *account_name, const char *characters_count,
                     const char *ticket, const char *game_lang,
                     const char *game_path);

/**
 * @brief Returns the stored account name.
 *
 * @return Pointer to a string containing the account name.
 */
const char *get_account_name(void);

/**
 * @brief Returns the stored characters_count string.
 *
 * @return Pointer to a string containing the characters_count.
 */
const char *get_characters_count(void);

/**
 * @brief Returns the stored session ticket.
 *
 * @return Pointer to a string containing the ticket.
 */
const char *get_ticket(void);

/**
 * @brief Returns the stored game language (e.g. "en").
 *
 * @return Pointer to a string containing the language code.
 */
const char *get_game_lang(void);

/**
 * @brief Returns the stored game path (e.g. "Z:\\path\\to\\TeraGame.exe").
 *
 * @return Pointer to a string containing the game path.
 */
const char *get_game_path(void);

/**
 * @brief Returns whether the game is currently marked as running (an atomic
 * bool).
 *
 * @return True if the game is considered running, false otherwise.
 */
bool is_game_running(void);

/**
 * @brief Runs the game using the provided credentials, creating a window thread
 *        and launching the game process, then waiting for the process to exit.
 *
 * @param account_name      The user's account name
 * @param characters_count  String containing character count info
 * @param ticket            Session ticket
 * @param game_lang         Game language code (e.g. "EUR")
 * @param game_path         The path to the game executable
 * @param server_list_url   URL for the server list fetch
 * @return                  The exit code of the game process, or -1 on error
 */
int run_game(const char *account_name, const char *characters_count,
             const char *ticket, const char *game_lang, const char *game_path,
             const char *server_list_url);

/**
 * @brief A callback type definition for handling game exit events.
 *
 * @param exit_code The exit code from the game process.
 * @param user_data User-defined data pointer.
 */
typedef void (*teralib_game_exit_callback_t)(int exit_code, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TERALIB_H */

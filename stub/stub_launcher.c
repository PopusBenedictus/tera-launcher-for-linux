/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#include "teralib.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Entry point for stub_launcher.
 *
 * Usage:
 *   stub_launcher <account_name> <characters_count> <ticket> <game_lang> <game_path>
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit code from run_game, or -1 on failure
 */
int main(const int argc, char* argv[]) {
    if (argc != 7) {
        fprintf(stderr, "Usage: stub_launcher <account_name> <characters_count> <ticket> <game_lang> <game_path>\n");
        return -1;
    }

    const char* account_name = argv[1];
    const char* characters_count = argv[2];
    const char* ticket = argv[3];
    const char* game_lang = argv[4];
    const char* game_path = argv[5];
    const char* server_list_in = argv[6];

    // Initialize teralib
    teralib_init();

    // Run the game synchronously
    const int exit_code = run_game(account_name, characters_count, ticket, game_lang, game_path, server_list_in);

    // Shutdown teralib
    teralib_shutdown();

    return exit_code;
}

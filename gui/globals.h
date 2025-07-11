/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char last_successful_login_username_global[FIXED_STRING_FIELD_SZ];
extern char last_successful_login_password_global[FIXED_STRING_FIELD_SZ];
extern char appdir_global[FIXED_STRING_FIELD_SZ];
extern char game_lang_global[FIXED_STRING_FIELD_SZ];
extern char wineprefix_global[FIXED_STRING_FIELD_SZ];
extern char wineprefix_default_global[FIXED_STRING_FIELD_SZ];
extern char gameprefix_global[FIXED_STRING_FIELD_SZ];
extern char gameprefix_default_global[FIXED_STRING_FIELD_SZ];
extern char configprefix_global[FIXED_STRING_FIELD_SZ];
extern char wine_base_dir_global[FIXED_STRING_FIELD_SZ];
extern char torrentprefix_global[FIXED_STRING_FIELD_SZ];
extern char torrent_file_name[FIXED_STRING_FIELD_SZ];
extern char torrent_magnet_link[FIXED_STRING_FIELD_SZ];
extern char patch_url_global[FIXED_STRING_FIELD_SZ];
extern char auth_url_global[FIXED_STRING_FIELD_SZ];
extern char server_list_url_global[FIXED_STRING_FIELD_SZ];
extern char service_name_global[FIXED_STRING_FIELD_SZ];
extern char tera_toolbox_path_global[FIXED_STRING_FIELD_SZ];
extern char gamescope_args_global[FIXED_STRING_FIELD_SZ];
extern bool appimage_mode;
extern bool use_gamemoderun;
extern bool use_gamescope;
extern bool use_tera_toolbox;
extern bool save_login_info;
extern bool plaintext_login_info_storage;
extern bool torrent_download_enabled;

#ifdef __cplusplus
}
#endif

#endif // GLOBALS_H

/** This program is free software. It comes without any warranty, to
* the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef AUTH_H
#define AUTH_H
#include "globals.h"
#include "shared_struct_defs.h"

gboolean tl4l_store_account_password(const gchar *account, const char *password);
gchar *tl4l_lookup_account_password(const gchar *account);
void tl4l_clear_account_password(const gchar *account);
#endif //AUTH_H

/** This program is free software. It comes without any warranty, to
* the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#include "auth.h"
#include <libsecret/secret.h>

static const SecretSchema app_schema = {
 "org.tera.launcher", SECRET_SCHEMA_NONE,
 {
  { "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
  {"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
  {nullptr, 0}
 }
};

gboolean tl4l_store_account_password(const gchar *account, const char *password) {
 GError *error = nullptr;
 const gboolean ok = secret_password_store_sync(
  &app_schema,
  SECRET_COLLECTION_DEFAULT,
  "Stored by TERA Launcher for Linux",
  password,
  nullptr,
  &error,
  "account", account,
  "service", "TL4L",
  nullptr
  );

 if (!ok) {
  g_warning("Error storing Password: %s", error->message);
  g_error_free(error);
 }
 return ok;
}

gchar *tl4l_lookup_account_password(const gchar *account) {
 GError *error = nullptr;
 gchar *password = secret_password_lookup_sync(
  &app_schema,
  nullptr,
  &error,
  "account", account,
  "service", "TL4L",
  nullptr);

 if (error) {
  g_warning("Error looking up password: %s", error->message);
  g_error_free(error);
  return nullptr;
 }

 return password;
}

void tl4l_clear_account_password(const gchar *account) {
 GError *error = nullptr;;
 secret_password_clear_sync(
  &app_schema,
  nullptr,
  nullptr,
  "account", account,
  "service", "TL4L",
  nullptr);

 if (error) {
  g_warning("Problem clearing account password for '%s': %s", account, error->message);
  g_error_free(error);
 }
}
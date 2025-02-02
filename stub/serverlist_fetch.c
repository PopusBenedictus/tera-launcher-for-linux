/**
 * This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#define _GNU_SOURCE
#undef WIN32
#undef _WIN32
#undef _WIN32_
#undef __WIN32__

#include "serverlist_fetch.h"
#include "serverlist.pb-c.h"
#include <arpa/inet.h>
#include <curl/curl.h>
#include <errno.h>
#include <iconv.h>
#include <inttypes.h>
#include <protobuf-c/protobuf-c.h>
#include <stdint.h>
#include <stdlib.h>
#include <util.h>

/* Mini-XML */
#include <mxml.h>

/**
 * @brief Attempts to print a UTF-16 little-endian string in a human-readable
 * manner.
 */
static void debug_print_utf16(const char *field_name,
                              ProtobufCBinaryData data) {
  printf("  %s (UTF16) length=%zu bytes: ", field_name, data.len);
  size_t i = 0;
  printf("\"");
  while (i + 1 < data.len) {
    uint16_t code_unit = (uint16_t)(data.data[i] | (data.data[i + 1] << 8));
    if (code_unit == 0)
      break;
    if (code_unit >= 32 && code_unit < 127)
      printf("%c", (char)code_unit);
    else
      printf("?");
    i += 2;
  }
  printf("\"\n");
}

/**
 * @brief Prints the fields of a single ServerList__ServerInfo struct.
 */
static void debug_print_serverinfo(const ServerList__ServerInfo *info) {
  printf("== ServerInfo ==\n");
  printf("  id: %u\n", info->id);
  printf("  address (fixed32): 0x%08X\n", info->address);
  printf("  port: %u\n", info->port);
  printf("  available: %u\n", info->available);
  debug_print_utf16("name", info->name);
  debug_print_utf16("category", info->category);
  debug_print_utf16("title", info->title);
  debug_print_utf16("queue", info->queue);
  debug_print_utf16("population", info->population);
  debug_print_utf16("unavailable_message", info->unavailable_message);
  debug_print_utf16("host", info->host);
  printf("== End ServerInfo ==\n\n");
}

/**
 * @brief Prints the entire ServerList, then prints the packed bytes in hex.
 */
static void debug_print_serverlist(const ServerList *list,
                                   const uint8_t *packed_buf,
                                   size_t packed_len) {
  printf("========================================\n");
  printf("ServerList Debug Print\n");
  printf("  n_servers: %u\n", (unsigned)list->n_servers);
  printf("  last_server_id: %u\n", list->last_server_id);
  printf("  sort_criterion: %u\n", list->sort_criterion);

  for (size_t i = 0; i < list->n_servers; i++) {
    const ServerList__ServerInfo *info = list->servers[i];
    if (info)
      debug_print_serverinfo(info);
    else
      printf("  Server %zu is NULL\n", i);
  }

  printf("Packed ServerList (%zu bytes):\n", packed_len);
  for (size_t i = 0; i < packed_len; i++) {
    if (i % 16 == 0)
      printf("\n  ");
    printf("%02X ", packed_buf[i]);
  }
  printf("\n========================================\n");
}

/**
 * @brief Converts an ASCII C-string to UTF-16LE and stores it in a
 * ProtobufCBinaryData.
 */
static void assign_utf16_field(ProtobufCBinaryData *field, const char *cstr) {
  if (!cstr)
    return;
  iconv_t cd = iconv_open("UTF-16LE", "UTF-8");
  if (cd == (iconv_t)-1) {
    log_message_safe(LOG_LEVEL_ERROR, "Failed to open iconv descriptor: %s",
                     strerror(errno));
    return;
  }
  size_t ascii_len = strlen(cstr);
  size_t in_bytes_left = ascii_len;
  size_t out_bytes_left = (ascii_len + 1) * 2U; /* space for null terminator */
  uint8_t *buffer = (uint8_t *)malloc(out_bytes_left);
  if (!buffer) {
    log_message_safe(LOG_LEVEL_ERROR, "Out of memory in assign_utf16_field");
    iconv_close(cd);
    return;
  }
  char *in_buf = (char *)cstr;
  char *out_buf = (char *)buffer;
  size_t result = iconv(cd, &in_buf, &in_bytes_left, &out_buf, &out_bytes_left);
  if (result == (size_t)-1) {
    log_message_safe(LOG_LEVEL_ERROR, "iconv failed: %s", strerror(errno));
    free(buffer);
    iconv_close(cd);
    return;
  }
  size_t bytes_written = ((ascii_len + 1) * 2U) - out_bytes_left;
  field->data = buffer;
  field->len = bytes_written;
  iconv_close(cd);
}

/**
 * @brief cURL write callback for accumulating response data into a single
 * buffer.
 */
static size_t write_callback(const void *ptr, size_t size, size_t nmemb,
                             void *userdata) {
  size_t total = size * nmemb;
  char **response_ptr = (char **)userdata;
  size_t old_len = (*response_ptr) ? strlen(*response_ptr) : 0U;
  *response_ptr = (char *)realloc(*response_ptr, old_len + total + 1U);
  if (!(*response_ptr))
    return 0;
  memcpy((*response_ptr) + old_len, ptr, total);
  (*response_ptr)[old_len + total] = '\0';
  return total;
}

/**
 * @brief Converts a dotted IPv4 string (e.g. "1.2.3.4") to a 32-bit integer.
 */
static uint32_t ipv4_to_u32(const char *ip) {
  if (!ip)
    return 0U;
  struct in_addr addr;
  if (inet_pton(AF_INET, ip, &addr) == 1)
    return ntohl(addr.s_addr);
  return 0U;
}

/**
 * @brief Trims leading and trailing whitespace from a string.
 */
static char *trim_whitespace(const char *str) {
  if (!str)
    return NULL;
  while (*str &&
         ((*str == ' ') || (*str == '\t') || (*str == '\n') || (*str == '\r')))
    str++;
  if (*str == '\0')
    return strdup("");
  const char *end = str + strlen(str) - 1;
  while (end > str &&
         ((*end == ' ') || (*end == '\t') || (*end == '\n') || (*end == '\r')))
    end--;
  size_t len = end - str + 1;
  char *result = (char *)malloc(len + 1);
  if (result) {
    memcpy(result, str, len);
    result[len] = '\0';
  }
  return result;
}

/**
 * @brief Removes <![CDATA[ ... ]]> markers from a string, if present.
 *
 * The caller must free() the returned string.
 */
static char *remove_cdata(const char *str) {
  if (!str)
    return NULL;
  const char *start_tag = "<![CDATA[";
  const char *end_tag = "]]>";
  size_t start_len = strlen(start_tag);
  size_t end_len = strlen(end_tag);
  size_t str_len = strlen(str);
  // Check if the string begins with <![CDATA[ and ends with ]]>
  if (str_len >= start_len + end_len &&
      strncmp(str, start_tag, start_len) == 0 &&
      strcmp(str + str_len - end_len, end_tag) == 0) {
    size_t new_len = str_len - start_len - end_len;
    char *result = (char *)malloc(new_len + 1);
    if (result) {
      memcpy(result, str + start_len, new_len);
      result[new_len] = '\0';
    }
    return result;
  }
  return strdup(str);
}

/**
 * @brief Recursively builds a string containing the XML representation of all
 * child nodes of the given node.
 *
 * This function produces the "inner XML" of a node. For each child node,
 * we allocate a temporary buffer (16 KB) and call mxmlSaveString().
 * The resulting string (if any) is concatenated.
 *
 * The caller must free() the returned string.
 */
static char *get_inner_xml(mxml_node_t *node) {
  char *result = NULL;
  size_t total_len = 0;
  mxml_node_t *child = mxmlGetFirstChild(node);
  while (child) {
    /* Allocate a temporary buffer (16 KB) for saving the child's XML.
       Adjust the buffer size if needed for larger content. */
    char *buf = (char *)malloc(FIXED_STRING_FIELD_SZ);
    if (!buf) {
      child = mxmlGetNextSibling(child);
      continue;
    }
    int ret =
        mxmlSaveString(child, buf, FIXED_STRING_FIELD_SZ, MXML_NO_CALLBACK);
    if (ret < 0) {
      free(buf);
      child = mxmlGetNextSibling(child);
      continue;
    }
    size_t child_len = (size_t)ret;
    result = realloc(result, total_len + child_len + 1);
    if (!result) {
      free(buf);
      return NULL;
    }
    memcpy(result + total_len, buf, child_len);
    total_len += child_len;
    result[total_len] = '\0';
    free(buf);
    child = mxmlGetNextSibling(child);
  }
  if (!result)
    result = strdup("");
  return result;
}

/**
 * @brief Retrieves the inner XML content of the first child with the specified
 * name.
 *
 * The inner XML (i.e. all of the child nodes serialized to XML) is returned.
 * Leading and trailing whitespace is trimmed.
 * If the resulting string is wrapped in <![CDATA[ ]]> markers, they are
 * removed.
 *
 * The caller must free() the returned string.
 */
static char *get_xml_child_content(mxml_node_t *parent,
                                   const char *child_name) {
  if (!parent || !child_name)
    return NULL;
  mxml_node_t *child = mxmlFindElement(parent, parent, child_name, NULL, NULL,
                                       MXML_DESCEND_FIRST);
  if (!child)
    return NULL;
  char *inner = get_inner_xml(child);
  char *trimmed = trim_whitespace(inner);
  free(inner);
  if (!trimmed || !*trimmed) {
    free(trimmed);
    return NULL;
  }
  /* Remove CDATA wrapper if present */
  char *no_cdata = remove_cdata(trimmed);
  free(trimmed);
  return no_cdata;
}

/**
 * @brief Finds the character-count suffix for a given server ID by parsing the
 * pipe-delimited characters_count string.
 *
 * Format: SERVER_ID|SERVER_ID,CHAR_COUNT|SERVER_ID|SERVER_ID,CHAR_COUNT|...
 */
static char *find_character_count_for_id(const char *characters_count,
                                         const char *id_str) {
  if (!characters_count || !id_str) {
    log_message_safe(LOG_LEVEL_ERROR,
                     "Invalid arguments to find_character_count_for_id");
    return NULL;
  }
  char *cc_copy = strdup(characters_count);
  if (!cc_copy) {
    log_message_safe(LOG_LEVEL_ERROR,
                     "Memory allocation failed in find_character_count_for_id");
    return NULL;
  }
  char *result = NULL;
  char *token = strtok(cc_copy, "|");
  while (token) {
    char *server_id = token;
    char *combined = strtok(NULL, "|");
    if (!combined)
      break;
    if (strcmp(server_id, id_str) == 0) {
      char *comma_ptr = strchr(combined, ',');
      if (comma_ptr) {
        result = strdup(comma_ptr + 1);
        if (!result)
          log_message_safe(LOG_LEVEL_ERROR,
                           "Memory allocation failed for result in "
                           "find_character_count_for_id");
      }
      break;
    }
    token = strtok(NULL, "|");
  }
  free(cc_copy);
  return result;
}

/**
 * @brief Appends a suffix to the original string and assigns the UTF-16 value.
 */
static void append_suffix_and_assign_utf16(ProtobufCBinaryData *field,
                                           const char *original_str,
                                           const char *suffix) {
  if (!field || !original_str || !suffix) {
    log_message_safe(LOG_LEVEL_ERROR,
                     "Invalid arguments to append_suffix_and_assign_utf16");
    return;
  }
  size_t orig_len = strlen(original_str);
  size_t suffix_len = strlen(suffix);
  char *appended = (char *)malloc(orig_len + suffix_len + 1);
  if (!appended) {
    log_message_safe(
        LOG_LEVEL_ERROR,
        "Memory allocation failed in append_suffix_and_assign_utf16");
    return;
  }
  strcpy(appended, original_str);
  strcpy(appended + orig_len, suffix);
  assign_utf16_field(field, appended);
  free(appended);
}

/**
 * @brief Fetches an XML server list via cURL, parses it with Mini-XML,
 * builds a Protobuf ServerList, and returns the packed Protobuf bytes.
 */
uint8_t *get_server_list(size_t *out_size, const char *server_list_url,
                         const char *characters_count) {
  CURL *curl = curl_easy_init();
  if (!curl) {
    log_message_safe(LOG_LEVEL_ERROR, "Failed to init cURL");
    return NULL;
  }
  char *response = (char *)calloc(1, 1);
  if (!response) {
    log_message_safe(LOG_LEVEL_ERROR,
                     "Out of memory allocating response buffer");
    curl_easy_cleanup(curl);
    return NULL;
  }
  curl_easy_setopt(curl, CURLOPT_URL, server_list_url);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    log_message_safe(LOG_LEVEL_ERROR, "curl_easy_perform() failed: %s",
                     curl_easy_strerror(res));
    curl_easy_cleanup(curl);
    free(response);
    return NULL;
  }
  long http_code = 0L;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);
  if (http_code < 200L || http_code >= 300L) {
    log_message_safe(LOG_LEVEL_ERROR, "Unsuccessful HTTP response: %ld",
                     http_code);
    free(response);
    return NULL;
  }
  /* Parse the XML with Mini-XML */
  mxmlSetWrapMargin(0);
  mxml_node_t *doc = mxmlLoadString(NULL, response, MXML_NO_CALLBACK);
  if (!doc) {
    log_message_safe(LOG_LEVEL_ERROR, "Failed to parse XML data via Mini-XML");
    free(response);
    return NULL;
  }
  /* Find the <serverlist> element */
  mxml_node_t *root =
      mxmlFindElement(doc, doc, "serverlist", NULL, NULL, MXML_DESCEND);
  if (!root) {
    log_message_safe(LOG_LEVEL_ERROR, "Root element is not <serverlist>");
    mxmlDelete(doc);
    free(response);
    return NULL;
  }
  ServerList server_list = SERVER_LIST__INIT;
  server_list.n_servers = 0U;
  server_list.servers = NULL;
  server_list.last_server_id = 0U;
  server_list.sort_criterion = 3U;
  size_t allocated = 0U;
  mxml_node_t *srv =
      mxmlFindElement(root, root, "server", NULL, NULL, MXML_DESCEND);
  while (srv) {
    ServerList__ServerInfo *info =
        (ServerList__ServerInfo *)malloc(sizeof(*info));
    if (!info) {
      log_message_safe(LOG_LEVEL_CRITICAL,
                       "Out of memory allocating ServerList__ServerInfo");
      srv = mxmlFindElement(mxmlGetNextSibling(srv), root, "server", NULL, NULL,
                            MXML_DESCEND);
      continue;
    }
    server_list__server_info__init(info);
    if (server_list.n_servers >= allocated) {
      allocated = (allocated == 0U) ? 4U : allocated * 2U;
      ServerList__ServerInfo **tmp = (ServerList__ServerInfo **)realloc(
          server_list.servers, allocated * sizeof(*tmp));
      if (!tmp) {
        log_message_safe(LOG_LEVEL_CRITICAL,
                         "Out of memory resizing server array");
        free(info);
        srv = mxmlFindElement(mxmlGetNextSibling(srv), root, "server", NULL,
                              NULL, MXML_DESCEND);
        continue;
      }
      server_list.servers = tmp;
    }
    /* <id> -> info->id */
    char *id_str = get_xml_child_content(srv, "id");
    if (id_str)
      info->id = (uint32_t)atoi(id_str);
    else
      info->id = 0U;
    char *server_char_count = NULL;
    if (id_str) {
      server_char_count = find_character_count_for_id(characters_count, id_str);
      free(id_str);
    }
    if (!server_char_count)
      server_char_count = strdup("");
    char suffix[256] = {0};
    size_t required;
    bool success = str_copy_formatted(suffix, &required, sizeof(suffix), "(%s)",
                                      server_char_count);
    if (!success) {
      log_message_safe(
          LOG_LEVEL_CRITICAL,
          "Failed to allocate %zu bytes for suffix buffer of %zu bytes",
          required, sizeof(suffix));
      free(info);
      free(server_list.servers);
      mxmlDelete(doc);
      free(response);
      return NULL;
    }
    /* <ip> -> info->address */
    char *ip_str = get_xml_child_content(srv, "ip");
    if (ip_str) {
      info->address = ipv4_to_u32(ip_str);
      free(ip_str);
    } else
      info->address = 0U;
    /* <port> -> info->port */
    char *port_str = get_xml_child_content(srv, "port");
    if (port_str) {
      info->port = (uint32_t)atoi(port_str);
      free(port_str);
    } else
      info->port = 0U;
    /* <category> -> info->category (UTF-16) */
    char *cat_str = get_xml_child_content(srv, "category");
    assign_utf16_field(&info->category, cat_str);
    free(cat_str);
    /* <name> -> info->name (UTF-16) */
    char *name_str = get_xml_child_content(srv, "name");
    if (name_str) {
      append_suffix_and_assign_utf16(&info->name, name_str, suffix);
      free(name_str);
    }
    /* <title> -> info->title (UTF-16)
     * (Change "name" to "title" if your XML has a separate <title> tag.)
     */
    char *title_str = get_xml_child_content(srv, "name");
    if (title_str) {
      append_suffix_and_assign_utf16(&info->title, title_str, suffix);
      free(title_str);
    }
    /* <queue> -> info->queue (UTF-16) */
    char *queue_str = get_xml_child_content(srv, "queue");
    assign_utf16_field(&info->queue, queue_str);
    free(queue_str);
    /* <open> -> info->population (UTF-16) */
    char *open_str = get_xml_child_content(srv, "open");
    assign_utf16_field(&info->population, open_str);
    free(open_str);
    /* Mark as available (hard-coded) */
    info->available = 1U;
    /* <popup> -> info->unavailable_message (UTF-16) */
    char *popup_str = get_xml_child_content(srv, "popup");
    assign_utf16_field(&info->unavailable_message, popup_str);
    free(popup_str);
    /* <host> -> info->host (UTF-16); only if IP is missing */
    if (info->address == 0U) {
      char *host_str = get_xml_child_content(srv, "host");
      if (host_str) {
        assign_utf16_field(&info->host, host_str);
        free(host_str);
      } else
        log_message_safe(LOG_LEVEL_WARNING,
                         "No IP or <host> for server item (id=%u).", info->id);
    }
    free(server_char_count);
    server_list.servers[server_list.n_servers++] = info;
    srv = mxmlFindElement(mxmlGetNextSibling(srv), root, "server", NULL, NULL,
                          MXML_DESCEND);
  }
  size_t packed_size = server_list__get_packed_size(&server_list);
  uint8_t *buffer = (uint8_t *)malloc(packed_size);
  if (!buffer) {
    log_message_safe(LOG_LEVEL_CRITICAL,
                     "Out of memory allocating Protobuf buffer");
    for (size_t i = 0; i < server_list.n_servers; i++) {
      ServerList__ServerInfo *info = server_list.servers[i];
      if (!info)
        continue;
      free(info->name.data);
      free(info->category.data);
      free(info->title.data);
      free(info->queue.data);
      free(info->population.data);
      free(info->unavailable_message.data);
      free(info->host.data);
      free(info);
    }
    free(server_list.servers);
    mxmlDelete(doc);
    free(response);
    return NULL;
  }
  server_list__pack(&server_list, buffer);
  /*
  // Uncomment the following line to debug the server list:
  // debug_print_serverlist(&server_list, buffer, packed_size);
  */
  for (size_t i = 0; i < server_list.n_servers; i++) {
    ServerList__ServerInfo *info = server_list.servers[i];
    if (!info)
      continue;
    free(info->name.data);
    free(info->category.data);
    free(info->title.data);
    free(info->queue.data);
    free(info->population.data);
    free(info->unavailable_message.data);
    free(info->host.data);
    free(info);
  }
  free(server_list.servers);
  mxmlDelete(doc);
  free(response);
  *out_size = packed_size;
  return buffer;
}

/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#define _GNU_SOURCE
#undef WIN32
#undef _WIN32

#include <errno.h>
#include <iconv.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <arpa/inet.h>

#include <protobuf-c/protobuf-c.h>
#include "serverlist.pb-c.h"
#include "serverlist_fetch.h"

/**
 * @brief Logs fetch-related errors.
 *
 * @param fmt  Format string
 * @param ...  Additional arguments
 */
static void fetch_error_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[FETCH ERROR] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

/**
 * @brief Attempts to print a UTF-16 little-endian string in a human-readable manner.
 *        Interprets each pair of bytes as an ASCII code unit if possible,
 *        otherwise prints '?'.
 *
 * @param field_name  A label for the field (e.g. "name", "category").
 * @param data        The ProtobufCBinaryData containing UTF-16 little-endian bytes.
 */
static void debug_print_utf16(const char *field_name, ProtobufCBinaryData data)
{
    printf("  %s (UTF16) length=%zu bytes: ", field_name, data.len);

    size_t i = 0;
    printf("\"");
    while (i + 1 < data.len) {
        uint16_t code_unit = (uint16_t)(data.data[i] | (data.data[i + 1] << 8));
        if (code_unit == 0) {
            /* Reached null terminator */
            break;
        }
        /* If it's in ASCII range, print it, else print '?' */
        if (code_unit >= 32 && code_unit < 127) {
            printf("%c", (char)code_unit);
        } else {
            printf("?");
        }
        i += 2;
    }
    printf("\"\n");
}

/**
 * @brief Prints the fields of a single ServerList__ServerInfo struct.
 *
 * @param info  Pointer to the ServerList__ServerInfo object.
 */
static void debug_print_serverinfo(const ServerList__ServerInfo *info)
{
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
 *
 * @param list        Pointer to the constructed ServerList (unpacked).
 * @param packed_buf  The buffer returned by server_list__pack().
 * @param packed_len  The size of packed_buf in bytes.
 */
static void debug_print_serverlist(const ServerList *list,
                                   const uint8_t *packed_buf,
                                   size_t packed_len)
{
    printf("========================================\n");
    printf("ServerList Debug Print\n");
    printf("  n_servers: %u\n", (unsigned)list->n_servers);
    printf("  last_server_id: %u\n", list->last_server_id);
    printf("  sort_criterion: %u\n", list->sort_criterion);

    for (size_t i = 0; i < list->n_servers; i++) {
        const ServerList__ServerInfo *info = list->servers[i];
        if (info) {
            debug_print_serverinfo(info);
        } else {
            printf("  Server %zu is NULL\n", i);
        }
    }

    /* Now print the packed bytes in hex */
    printf("Packed ServerList (%zu bytes):\n", packed_len);
    for (size_t i = 0; i < packed_len; i++) {
        if (i % 16 == 0) {
            printf("\n  ");
        }
        printf("%02X ", packed_buf[i]);
    }
    printf("\n========================================\n");
}

/**
 * @brief Converts an ASCII C-string to UTF-16LE and stores it in a ProtobufCBinaryData.
 *
 * @param field  Pointer to the ProtobufCBinaryData to fill.
 * @param cstr   The source ASCII string (may be NULL).
 */
static void assign_utf16_field(ProtobufCBinaryData *field, const char *cstr)
{
    if (!cstr) {
        return;
    }

    iconv_t cd = iconv_open("UTF-16LE", "UTF-8");
    if (cd == (iconv_t)-1) {
        fetch_error_log("Failed to open iconv descriptor: %s\n", strerror(errno));
        return;
    }

    size_t ascii_len = strlen(cstr);
    size_t in_bytes_left = ascii_len;
    size_t out_bytes_left = (ascii_len + 1) * 2U; /* Each ASCII char -> 2 bytes in UTF-16LE */

    uint8_t *buffer = (uint8_t *)malloc(out_bytes_left);
    if (!buffer) {
        fetch_error_log("Out of memory in assign_utf16_field\n");
        iconv_close(cd);
        return;
    }

    char *in_buf = (char *)cstr;
    char *out_buf = (char *)buffer;
    size_t result = iconv(cd, &in_buf, &in_bytes_left, &out_buf, &out_bytes_left);

    if (result == (size_t)-1) {
        fetch_error_log("iconv failed: %s\n", strerror(errno));
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
 * @brief cURL write callback for accumulating response data into a single buffer.
 *
 * @param ptr      Pointer to the newly received data
 * @param size     Size of each data element
 * @param nmemb    Number of data elements
 * @param userdata User-provided pointer; here, it points to a `char*` holding the entire response
 * @return         Number of bytes actually handled
 */
static size_t write_callback(const void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    char **response_ptr = (char **)userdata;

    size_t old_len = (*response_ptr) ? strlen(*response_ptr) : 0U;
    *response_ptr = (char *)realloc(*response_ptr, old_len + total + 1U);
    if (!(*response_ptr)) {
        return 0; /* Allocation failed */
    }
    memcpy((*response_ptr) + old_len, ptr, total);
    (*response_ptr)[old_len + total] = '\0';
    return total;
}

/**
 * @brief Converts a dotted IPv4 string (e.g. "1.2.3.4") to a 32-bit integer (fixed32).
 *
 * @param ip  The dotted IPv4 string
 * @return    The IP in host byte order as a 32-bit integer. Returns 0 if parsing fails.
 */
static uint32_t ipv4_to_u32(const char *ip)
{
    if (!ip) {
        return 0U;
    }

    struct in_addr addr;
    if (inet_pton(AF_INET, ip, &addr) == 1) {
        return ntohl(addr.s_addr);
    }
    return 0U;
}

/**
 * @brief Retrieves the text content of <child_name> from an XML node.
 *
 * Caller must free() the returned string. Returns NULL if not found.
 *
 * @param parent     Pointer to the parent xmlNode.
 * @param child_name The name of the child element to search for.
 * @return           A newly allocated string containing the child text, or NULL on failure.
 */
static char *get_xml_child_content(xmlNode *parent, const char *child_name)
{
    for (xmlNode *node = parent->children; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE &&
            xmlStrcmp(node->name, (const xmlChar *)child_name) == 0)
        {
            xmlChar *content = xmlNodeGetContent(node);
            if (content) {
                char *result = strdup((const char *)content);
                xmlFree(content);
                return result;
            }
        }
    }
    return NULL;
}

/**
 * @brief Assigns a string-like field in a Protobuf struct.
 *        The string is directly copied (strdup) into the ProtobufCBinaryData.
 *
 * @param field Pointer to the ProtobufCBinaryData to fill.
 * @param cstr  Source C-string (ASCII/UTF-8).
 */
static void assign_bytes_field(ProtobufCBinaryData *field, const char *cstr)
{
    if (!cstr) {
        return;
    }
    field->data = (uint8_t *)strdup(cstr);
    field->len  = strlen(cstr);
}

/**
 * @brief Finds the character-count suffix for a given server ID by
 *        parsing the pipe-delimited `characters_count` string.
 *
 * The expected format for the `characters_count` string is:
 *
 * SERVER_ID|SERVER_ID,CHAR_COUNT|SERVER_ID|SERVER_ID,CHAR_COUNT|...
 *
 * Example: "SERVER_ID1|SERVER_ID1,2|SERVER_ID2|SERVER_ID2,10"
 *
 * @param[in]  characters_count  The pipe-delimited string describing server IDs and their character counts.
 * @param[in]  id_str            The server ID whose character count should be extracted.
 * @return A dynamically allocated string containing the character count for the given `id_str`.
 *         Returns NULL if no match is found or if allocation fails.
 *         The caller is responsible for freeing this string.
 */
static char* find_character_count_for_id(const char* characters_count, const char* id_str)
{
    if (!characters_count || !id_str) {
        fetch_error_log("Invalid arguments to find_character_count_for_id");
        return NULL;
    }

    /* Copy the input string, because strtok modifies its input. */
    char* cc_copy = strdup(characters_count);
    if (!cc_copy) {
        fetch_error_log("Memory allocation failed for cc_copy in find_character_count_for_id");
        return NULL;
    }

    char* result = NULL;
    char* token  = strtok(cc_copy, "|");

    /*
       The string is parsed in pairs:
       1) SERVER_ID
       2) SERVER_ID,CHAR_COUNT
       3) SERVER_ID
       4) SERVER_ID,CHAR_COUNT
       ... and so on.
    */
    while (token) {
        char* server_id = token;
        char* combined  = strtok(NULL, "|");
        if (!combined) {
            /* No more pairs. */
            break;
        }

        /* Check if this server_id matches what we're looking for. */
        if (strcmp(server_id, id_str) == 0) {
            /* combined should look like: "SERVER_ID,2" */
            char* comma_ptr = strchr(combined, ',');
            if (comma_ptr) {
                /* Everything after the comma is the actual character count. */
                result = strdup(comma_ptr + 1);
                if (!result) {
                    fetch_error_log("Memory allocation failed for result in find_character_count_for_id");
                }
            }
            break; /* Found a match; no need to continue. */
        }

        /* Move to the next pair. */
        token = strtok(NULL, "|");
    }

    free(cc_copy);
    return result;
}

/**
 * @brief Appends a suffix to the original string and then assigns the result
 *        to a UTF-16 field (via assign_utf16_field).
 *
 * @param[in,out] field_ptr     Pointer to the destination field (e.g., &info->name).
 * @param[in]     original_str  The original, unmodified string (e.g., "Jane").
 * @param[in]     suffix        The suffix to append (e.g., "2").
 */
static void append_suffix_and_assign_utf16(
    ProtobufCBinaryData *field,
    const char* original_str,
    const char* suffix
)
{
    if (!field || !original_str || !suffix) {
        fetch_error_log("Invalid arguments to append_suffix_and_assign_utf16");
        return;
    }

    size_t orig_len   = strlen(original_str);
    size_t suffix_len = strlen(suffix);

    /* +1 for null terminator. */
    char* appended = (char*)malloc(orig_len + suffix_len + 1);
    if (!appended) {
        fetch_error_log("Memory allocation failed for appended in append_suffix_and_assign_utf16");
        return;
    }

    /* Build the appended string: original + suffix. */
    strcpy(appended, original_str);
    strcpy(appended + orig_len, suffix);

    /* Assign to the destination field (UTF-16 conversion would happen in your actual function). */
    assign_utf16_field(field, appended);

    free(appended);
}

/**
 * @brief Fetches an XML server list via cURL, parses it with libxml2,
 *        builds a Protobuf ServerList, and returns the packed Protobuf bytes.
 *        Caller must free() the returned buffer.
 *
 * @param[out] out_size         The number of bytes in the returned buffer.
 * @param[in]  server_list_url  The URL to fetch; must not be NULL.
 * @return A newly allocated buffer containing serialized Protobuf data, or NULL on error.
 */
uint8_t *get_server_list(size_t *out_size, const char *server_list_url, const char *characters_count)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        fetch_error_log("Failed to init cURL");
        return NULL;
    }

    char *response = (char *)calloc(1, 1);
    if (!response) {
        fetch_error_log("Out of memory allocating response buffer");
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, server_list_url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fetch_error_log("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(response);
        return NULL;
    }

    long http_code = 0L;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code < 200L || http_code >= 300L) {
        fetch_error_log("Unsuccessful HTTP response: %ld", http_code);
        free(response);
        return NULL;
    }

    /* Parse the XML with libxml2 */
    xmlDoc *doc = xmlReadMemory(response, (int)strlen(response), "serverlist.xml", NULL, 0);
    if (!doc) {
        fetch_error_log("Failed to parse XML data");
        free(response);
        return NULL;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root || xmlStrcmp(root->name, (const xmlChar *)"serverlist") != 0) {
        fetch_error_log("Root element is not <serverlist>");
        xmlFreeDoc(doc);
        free(response);
        return NULL;
    }

    ServerList server_list = SERVER_LIST__INIT;
    server_list.n_servers = 0U;
    server_list.servers = NULL;
    server_list.last_server_id = 0U;
    server_list.sort_criterion = 3U;

    /* Count <server> elements */
    size_t count = 0U;
    for (xmlNode *srv = root->children; srv; srv = srv->next) {
        if (srv->type == XML_ELEMENT_NODE &&
            xmlStrcmp(srv->name, (const xmlChar *)"server") == 0)
        {
            count++;
        }
    }

    if (count == 0U) {
        fetch_error_log("No <server> elements found");
        xmlFreeDoc(doc);
        free(response);
        return NULL;
    }

    server_list.servers = (ServerList__ServerInfo **)
        calloc(count, sizeof(ServerList__ServerInfo *));
    if (!server_list.servers) {
        fetch_error_log("Out of memory allocating servers array");
        xmlFreeDoc(doc);
        free(response);
        return NULL;
    }

    for (xmlNode *srv = root->children; srv; srv = srv->next) {
        if (srv->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrcmp(srv->name, (const xmlChar *)"server") != 0) {
            continue;
        }

        ServerList__ServerInfo *info =
            (ServerList__ServerInfo *)malloc(sizeof(ServerList__ServerInfo));
        if (!info) {
            fetch_error_log("Out of memory allocating ServerList__ServerInfo");
            continue; /* Attempt to keep processing other servers */
        }
        server_list__server_info__init(info);

        /* <id> -> info->id */
        char *id_str = get_xml_child_content(srv, "id");
        if (id_str) {
            info->id = (uint32_t)atoi(id_str);
            free(id_str);
        } else {
            info->id = 0U;
        }

        char* server_char_count = find_character_count_for_id(characters_count, id_str);
    	if (!server_char_count) {
        	/* If not found, default to an empty string. */
        	server_char_count = strdup("");
    	}

        char suffix[256] = {0};
        if (strlen(server_char_count) <= 255)
          snprintf(suffix, sizeof(suffix) - 1, "(%s)", server_char_count);

        /* <ip> -> info->address */
        char *ip_str = get_xml_child_content(srv, "ip");
        if (ip_str) {
            info->address = ipv4_to_u32(ip_str);
            free(ip_str);
        } else {
            info->address = 0U;
        }

        /* <port> -> info->port */
        char *port_str = get_xml_child_content(srv, "port");
        if (port_str) {
            info->port = (uint32_t)atoi(port_str);
            free(port_str);
        } else {
            info->port = 0U;
        }

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

        /* <title> -> info->title (UTF-16) */
        char *title_str = get_xml_child_content(srv, "name");
        if (title_str) {
            append_suffix_and_assign_utf16(&info->title, title_str, suffix);
            free(title_str);
        }

        /* <open> -> info->population (UTF-16) */
        char *open_str = get_xml_child_content(srv, "open");
        assign_utf16_field(&info->population, open_str);
        free(open_str);

        /* Mark as available; the client requires this. */
        /* TODO: If there is another way to detect this up front we can stop hard coding this. */
        info->available = 1U;

        /* <popup> -> info->unavailable_message (UTF-16) */
        char *popup_str = get_xml_child_content(srv, "popup");
        assign_utf16_field(&info->unavailable_message, popup_str);
        free(popup_str);

        /* host field is left empty if not present in XML */
        /* info->host stays empty by default */
        if (info->address == 0U) {
          char *host_str = get_xml_child_content(srv, "host");
          if (host_str) {
            assign_utf16_field(&info->host, host_str);
            free(host_str);
          } else {
            fetch_error_log("WARNING: Unable to set host for server item and no IP address provided either.");
          }
        }

        server_list.servers[server_list.n_servers++] = info;
    }

    /* Pack into Protobuf bytes */
    size_t packed_size = server_list__get_packed_size(&server_list);
    uint8_t *buffer = (uint8_t *)malloc(packed_size);
    if (!buffer) {
        fetch_error_log("Out of memory allocating Protobuf buffer");
        /* Clean up partially filled server_list before returning */
        for (size_t i = 0; i < server_list.n_servers; i++) {
            ServerList__ServerInfo *info = server_list.servers[i];
            if (!info) {
                continue;
            }
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
        xmlFreeDoc(doc);
        free(response);
        return NULL;
    }

    server_list__pack(&server_list, buffer);

    /* Keep this around incase we fuck it big time and need to check the packed data payload */
    /* debug_print_serverlist(&server_list, buffer, packed_size); */

    /* Clean up allocated fields */
    for (size_t i = 0; i < server_list.n_servers; i++) {
        ServerList__ServerInfo *info = server_list.servers[i];
        if (!info) {
            continue;
        }
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

    xmlFreeDoc(doc);
    free(response);

    *out_size = packed_size;
    return buffer;
}

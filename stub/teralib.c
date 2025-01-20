/** This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#include <stdlib.h>
#include <util.h>
#include <wchar.h>

#include "serverlist.pb-c.h"
#include "serverlist_fetch.h"
#include "teralib.h"

/**
 * @brief Callback used by EnumWindows to list top-level windows.
 *
 * @param hwnd   Current window handle.
 * @param lParam Unused parameter.
 * @return TRUE to continue enumeration.
 */
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
  (void)lParam; /* Unused */

  char className[256];

  /* Get the class name of the window */
  if (GetClassNameA(hwnd, className, sizeof(className))) {
    char windowTitle[256];

    /* Get the window title (name) */
    if (GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle))) {
      printf("Window Handle: 0x%08p, Class Name: %s, Window Title: %s\n",
             (void *)hwnd, className, windowTitle);
    } else {
      /* If the window has no title */
      printf(
          "Window Handle: 0x%08p, Class Name: %s, Window Title: [No Title]\n",
          (void *)hwnd, className);
    }
  }

  return TRUE; /* Continue enumeration */
}

/*----------------------------------------------------
 Global credentials structure.
----------------------------------------------------*/
typedef struct {
  char account_name[256];
  char characters_count[512];
  char ticket[256];
  char game_lang[64];
  char game_path[512];
} GameCredentials;

static GameCredentials g_credentials = {0};

/*----------------------------------------------------
 Accessors/Mutators for the global credentials.
----------------------------------------------------*/

/**
 * @brief Sets the global credentials used for the game.
 *
 * @param account_name     The account name to use.
 * @param characters_count The number of characters on the account.
 * @param ticket           The session ticket.
 * @param game_lang        The language code (e.g., "EUR").
 * @param game_path        The local file system path to the game executable.
 */
void set_credentials(const char *account_name, const char *characters_count,
                     const char *ticket, const char *game_lang,
                     const char *game_path) {
  snprintf(g_credentials.account_name, sizeof(g_credentials.account_name), "%s",
           account_name);
  snprintf(g_credentials.characters_count,
           sizeof(g_credentials.characters_count), "%s", characters_count);
  snprintf(g_credentials.ticket, sizeof(g_credentials.ticket), "%s", ticket);
  snprintf(g_credentials.game_lang, sizeof(g_credentials.game_lang), "%s",
           game_lang);
  snprintf(g_credentials.game_path, sizeof(g_credentials.game_path), "%s",
           game_path);
}

/**
 * @brief Retrieves the currently set account name.
 *
 * @return Const pointer to the account name string.
 */
const char *get_account_name(void) { return g_credentials.account_name; }

/**
 * @brief Retrieves the currently set character count.
 *
 * @return Const pointer to the character count string.
 */
const char *get_characters_count(void) {
  return g_credentials.characters_count;
}

/**
 * @brief Retrieves the currently set session ticket.
 *
 * @return Const pointer to the session ticket string.
 */
const char *get_ticket(void) { return g_credentials.ticket; }

/**
 * @brief Retrieves the currently set language code for the game.
 *
 * @return Const pointer to the language code string.
 */
const char *get_game_lang(void) { return g_credentials.game_lang; }

/**
 * @brief Retrieves the currently set game path.
 *
 * @return Const pointer to the game path string.
 */
const char *get_game_path(void) { return g_credentials.game_path; }

/*----------------------------------------------------
 Server list URL.
----------------------------------------------------*/
const char *server_list_url_global = NULL;

/*----------------------------------------------------
 Synchronization Primitives
----------------------------------------------------*/

/* Mutex for protecting g_window_handle */
static CRITICAL_SECTION g_window_handle_cs;

/* SafeHWND: store a HWND plus protect with a critical section */
typedef struct {
  HWND raw;
} SafeHWND;

static SafeHWND g_window_handle = {NULL};

/* Event handles */
static HANDLE g_gameStatusEvent =
    NULL; /**< Signaled when game status changes. */
static HANDLE g_windowCreatedEvent =
    NULL; /**< Signaled when window is created. */

/**
 * @brief A variable that is set atomically to synchronize loading of the
 * launcher "window" and the game client.
 */
static volatile LONG GAME_RUNNING = 0;

/**
 * @brief Sets game run state as true (atomic write).
 */
static void set_game_running_true(void) {
  InterlockedExchange(&GAME_RUNNING, 1);
}

/**
 * @brief Sets game run state as false (atomic write).
 */
static void set_game_running_false(void) {
  InterlockedExchange(&GAME_RUNNING, 0);
}

/**
 * @brief Checks if the game is running (atomic read).
 *
 * @return true if the game is running, false otherwise.
 */
bool is_game_running(void) { return (GAME_RUNNING != 0); }

/*----------------------------------------------------
 Windows messaging constants
----------------------------------------------------*/
#ifndef WM_USER
#define WM_USER 0x0400
#endif
#define WM_GAME_EXITED (WM_USER + 1)

/*----------------------------------------------------
 Minimal logging functions
----------------------------------------------------*/
static void info(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  printf("[INFO] ");
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

static void error_log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "[ERROR] ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}

/**
 * @brief Converts a UTF-8 C string to a wide-character (UTF-16) string.
 *
 * @param s Null-terminated UTF-8 C string.
 * @return Dynamically allocated wide-character string, or NULL on failure.
 */
static WCHAR *to_wstring(const char *s) {
  if (!s) {
    return NULL;
  }

  int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if (len <= 0) {
    return NULL;
  }

  WCHAR *wbuf = malloc((size_t)len * sizeof(WCHAR));
  if (!wbuf) {
    error_log("Memory allocation failed in to_wstring");
    return NULL;
  }
  memset((void *)wbuf, 0, (size_t)len * sizeof(WCHAR));

  info("Input string: %s, input length: %lu, out_sz: %d", s,
       (unsigned long)strlen(s), len);

  MultiByteToWideChar(CP_UTF8, 0, s, -1, wbuf, len);
  return wbuf;
}

/**
 * @brief Initializes teralib, creating necessary synchronization objects.
 */
void teralib_init(void) {
  InitializeCriticalSection(&g_window_handle_cs);
  g_gameStatusEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  g_windowCreatedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  info("teralib initialized successfully");
}

/**
 * @brief Shuts down teralib, cleaning up synchronization objects.
 */
void teralib_shutdown(void) {
  CloseHandle(g_gameStatusEvent);
  CloseHandle(g_windowCreatedEvent);
  DeleteCriticalSection(&g_window_handle_cs);
  info("teralib shutdown completed");
}

/**
 * @brief Reset global state (window handle, sync objects).
 */
static void reset_global_state(void) {
  set_game_running_false();
  EnterCriticalSection(&g_window_handle_cs);
  g_window_handle.raw = NULL;
  LeaveCriticalSection(&g_window_handle_cs);

  info("Global state reset completed");
}

/**
 * @brief Sends a COPYDATASTRUCT to the given recipient, identifying the event
 *        by `event_id`. The payload is `data` with length `length`.
 *
 * @param recipient HWND cast in the wParam, i.e., (HWND)wParam from your
 * wnd_proc.
 * @param sender    The local HWND sending the message, typically `hWnd`.
 * @param event_id  The “dwData” to set in the COPYDATASTRUCT.
 * @param data      Pointer to the raw byte payload to send.
 * @param length    Number of bytes in `data`.
 */
static void send_response_message(WPARAM recipient, HWND sender, DWORD event_id,
                                  const void *data, size_t length) {
  COPYDATASTRUCT cds;
  cds.dwData = event_id;
  cds.cbData = (DWORD)length;
  /* Non-const cast is typical in WinAPI */
  cds.lpData = (void *)data;

  LRESULT result =
      SendMessageW((HWND)recipient, WM_COPYDATA, (WPARAM)sender, (LPARAM)&cds);

  /* For debugging/logging: */
  info("send_response_message: event_id=%lu, payload_len=%zu, result=%ld",
       (unsigned long)event_id, length, (long)result);
}

/**
 * @brief Handles a request for the account name. Responds with a UTF-16 string
 * (no null terminator).
 *
 * @param recipient The HWND of the recipient (wParam).
 * @param sender    The local HWND (hWnd).
 */
static void handle_account_name_request(WPARAM recipient, HWND sender) {
  const char *account_name = get_account_name();
  info("Account Name Request - Sending: %s", account_name);

  WCHAR *wbuf = to_wstring(account_name);
  if (!wbuf) {
    /* If conversion fails, send empty or handle error */
    send_response_message(recipient, sender, 2, NULL, 0);
    return;
  }

  /* I would prefer to measure this against wbuf, but since wcslen() seems to
     return an incorrect length in certain environments with winelib, strlen()
     against the input is safer here. */
  size_t cbSize = strlen(account_name) * sizeof(WCHAR);

  /* Send the wide chars as raw bytes WITHOUT null terminator and with
   * event_id=2 */
  send_response_message(recipient, sender, 2, (const void *)wbuf, cbSize);

  free(wbuf);
}

/**
 * @brief Handles a request for the session ticket. Sends it back as UTF-8 bytes
 * (no null terminator).
 *
 * @param recipient The HWND of the recipient (wParam).
 * @param sender    The local HWND (hWnd).
 */
static void handle_session_ticket_request(WPARAM recipient, HWND sender) {
  const char *session_ticket = get_ticket();
  info("Session Ticket Request - Sending Ticket");

  /* Send as raw UTF-8 (C string) WITHOUT null terminator. */
  size_t length = strlen(session_ticket);
  send_response_message(recipient, sender, 4, session_ticket, length);
}

/**
 * @brief Handles a request for the server list. Calls get_server_list() and
 * sends the resulting data (if any) back via WM_COPYDATA.
 *
 * @param recipient The HWND of the recipient (wParam).
 * @param sender    The local HWND (hWnd).
 */
static void handle_server_list_request(WPARAM recipient, HWND sender) {
  size_t out_size = 0;
  uint8_t *data = get_server_list(&out_size, server_list_url_global,
                                  get_characters_count());

  if (!data) {
    error_log("Failed to get server list; sending empty.");
    send_response_message(recipient, sender, 6, NULL, 0);
    return;
  }

  info("Server List Request - Sending %zu bytes.", out_size);

  /* Send with event_id=6 */
  send_response_message(recipient, sender, 6, data, out_size);

  free(data);
}

/**
 * @brief Handles the event indicating the game has started.
 *
 * @param recipient   Unused.
 * @param sender      Unused.
 * @param payload     Unused.
 * @param payload_size Unused.
 */
static void handle_game_start(WPARAM recipient, HWND sender,
                              const unsigned char *payload,
                              size_t payload_size) {
  (void)recipient;
  (void)sender;
  (void)payload;
  (void)payload_size;

  info("Game started.");
}

/**
 * @brief Logs the event of entering the lobby.
 */
static void on_lobby_entered(void) { info("Entered the lobby"); }

/**
 * @brief Logs the event of entering a specific world.
 *
 * @param world_name The name of the world being entered.
 */
static void on_world_entered(const char *world_name) {
  info("Entered the world: %s", world_name);
}

/**
 * @brief Handles entering a lobby or world. Processes the payload to determine
 *        if the player is entering a lobby (empty payload) or a specific world.
 *        Sends an appropriate response (event 8) back to the game client.
 *
 * @param recipient   The HWND of the recipient window as a WPARAM.
 * @param sender      The HWND of the sender window.
 * @param payload     Pointer to the payload containing world information (if
 * any).
 * @param payload_len Length of the payload in bytes.
 */
static void handle_enter_lobby_or_world(WPARAM recipient, HWND sender,
                                        const unsigned char *payload,
                                        size_t payload_len) {
  if (!payload || payload_len == 0) {
    on_lobby_entered();
    /* Send an empty payload to signify “lobby entered” */
    send_response_message(recipient, sender, 8, NULL, 0);
  } else {
    /* Ensure that payload_len is reasonable to prevent excessive memory
     * allocation */
    if (payload_len > 1024) {
      error_log("Payload size too large in handle_enter_lobby_or_world: %zu",
                payload_len);
      send_response_message(recipient, sender, 8, NULL, 0);
      return;
    }

    /* Convert payload bytes to a C string for logging or further use */
    char *world_name = (char *)malloc(payload_len + 1);
    if (!world_name) {
      error_log("Memory allocation failed in handle_enter_lobby_or_world");
      send_response_message(recipient, sender, 8, NULL, 0);
      return;
    }
    memcpy(world_name, payload, payload_len);
    world_name[payload_len] = '\0';

    on_world_entered(world_name);

    /* Send the same payload back with event_id=8 */
    send_response_message(recipient, sender, 8, payload, payload_len);

    free(world_name);
  }
}

/**
 * @brief Window procedure to handle messages related to game events and data
 * transfer.
 *
 * @param hWnd Window handle.
 * @param msg  Message code.
 * @param wParam Additional message information.
 * @param lParam Additional message information.
 * @return LRESULT indicating the result of message processing.
 */
static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) {
  switch (msg) {
  case WM_COPYDATA: {
    PCOPYDATASTRUCT copy_data = (PCOPYDATASTRUCT)lParam;
    DWORD event_id = copy_data->dwData;
    info("Received WM_COPYDATA with event_id=%lu", (unsigned long)event_id);

    const size_t payload_size = copy_data->cbData;
    unsigned char *payload = NULL;
    if (payload_size > 0) {
      payload = (unsigned char *)copy_data->lpData;
    }

    switch (event_id) {
    case 1:
      info("handle_account_name_request");
      handle_account_name_request(wParam, hWnd);
      break;
    case 3:
      info("handle_session_ticket_request");
      handle_session_ticket_request(wParam, hWnd);
      break;
    case 5:
      info("handle_server_list_request");
      handle_server_list_request(wParam, hWnd);
      break;
    case 7:
      info("handle_enter_lobby_or_world");
      handle_enter_lobby_or_world(wParam, hWnd, payload, payload_size);
      break;
    case 1000:
      info("handle_game_start");
      handle_game_start(wParam, hWnd, payload, payload_size);
      break;
    default:
      /* We do not need to do anything with these, this is just for visibility.
       */
      info("Unhandled event ID: %lu", (unsigned long)event_id);
      break;
    }
    return 1;
  }
  case WM_GAME_EXITED:
    info("Received WM_GAME_EXITED in wnd_proc");
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProc(hWnd, msg, wParam, lParam);
}

/**
 * @brief Creates an invisible launcher window and enters a message loop until
 * WM_QUIT.
 */
static void create_and_run_game_window(void) {
  const char *class_name = "LAUNCHER_CLASS";
  const char *window_name = "LAUNCHER_WINDOW";

  WNDCLASSEXA wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(WNDCLASSEXA);
  wc.style = 0;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleA(NULL);
  wc.hCursor = NULL;
  wc.lpszClassName = class_name;
  wc.hIconSm = NULL;

  ATOM atom = RegisterClassExA(&wc);
  if (!atom) {
    error_log("Failed to register window class for Pseudo launcher window");
    return;
  }

  HWND hwnd = CreateWindowExA(0, class_name, window_name, 0, 0, 0, 0, 0, NULL,
                              NULL, wc.hInstance, NULL);
  if (!hwnd) {
    error_log("Failed to create pseudo window for stub launcher");
    UnregisterClassA(class_name, wc.hInstance);
    return;
  }

  info("Pseudo window created with HWND=%p", (void *)hwnd);

  /* Protect access to g_window_handle */
  EnterCriticalSection(&g_window_handle_cs);
  g_window_handle.raw = hwnd;
  LeaveCriticalSection(&g_window_handle_cs);

  SetWindowTextA(hwnd, window_name);

  /* Signal that the window has been created */
  if (g_windowCreatedEvent) {
    SetEvent(g_windowCreatedEvent);
  }

  /* Standard ASCII message loop */
  MSG msg;
  while (GetMessageA(&msg, NULL, 0, 0) > 0) {
    if (msg.message == WM_GAME_EXITED) {
      info("Received WM_GAME_EXITED in message loop");
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
  info("Exiting message loop");

  DestroyWindow(hwnd);
  UnregisterClassA(class_name, wc.hInstance);
  reset_global_state();
}

/*----------------------------------------------------
 Launch a process and wait for it to finish
----------------------------------------------------*/
/**
 * @brief Launches a process (the game executable) with the specified path and
 * arguments.
 *
 * @param path Full path to the executable.
 * @param args Command-line arguments to pass.
 * @return PROCESS_INFORMATION struct describing the newly created
 * process/thread.
 */
static PROCESS_INFORMATION launch_process(const char *path, const char *args) {
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);

  /* Calculate the length needed for the command line */
  size_t path_len = strlen(path);
  size_t args_len = strlen(args);
  size_t neededLen = path_len + args_len +
                     4; /* Extra space for quotes, space, null terminator */

  char *cmdLine = (char *)calloc(neededLen, sizeof(char));
  if (!cmdLine) {
    error_log("Memory allocation failed in launch_process");
    return pi;
  }

  /* Example: "C:\\Path\\To\\Game.exe" -LANGUAGEEXT=en */
  int written = snprintf(cmdLine, neededLen, "\"%s\" %s", path, args);
  if (written < 0 || (size_t)written >= neededLen) {
    error_log("snprintf failed or buffer too small in launch_process");
    free(cmdLine);
    return pi;
  }

  info("Command Line: %s", cmdLine);

  BOOL success =
      CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);

  free(cmdLine);

  if (!success) {
    DWORD error_code = GetLastError();
    error_log("Failed to create process. GetLastError=%lu",
              (unsigned long)error_code);

    char error_msg[512];
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
    DWORD msgLen = FormatMessageA(flags, NULL, error_code, langId, error_msg,
                                  sizeof(error_msg), NULL);

    if (msgLen > 0) {
      if (error_msg[msgLen - 1] != '\n') {
        if (msgLen < sizeof(error_msg) - 1U) {
          error_msg[msgLen] = '\n';
          error_msg[msgLen + 1U] = '\0';
        }
      }
      error_log("CreateProcessA failed: %s", error_msg);
    } else {
      error_log("CreateProcessA failed: Unable to retrieve error message.");
    }
  } else {
    info("Process created successfully: PID=%lu",
         (unsigned long)pi.dwProcessId);
  }

  return pi;
}

/**
 * @brief Waits indefinitely for the given process to exit, returning its exit
 * code.
 *
 * @param pi A PROCESS_INFORMATION struct for the process.
 * @return The exit code of the process, or -1 on failure.
 */
static int wait_for_process_exit(PROCESS_INFORMATION pi) {
  if (pi.hProcess == NULL) {
    return -1;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitCode;
  if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
    return (int)exitCode;
  }
  return -1;
}

/*----------------------------------------------------
 The thread that creates the game window
----------------------------------------------------*/
/**
 * @brief Thread entry function which creates the launcher window
 *        and sets the game running state to true.
 *
 * @param param Unused thread parameter.
 * @return Exit code (0 on success, 1 if game was already running).
 */
static DWORD WINAPI launch_game_thread(LPVOID param) {
  (void)param; /* Unused */

  if (is_game_running()) {
    error_log("Game is already running, aborting launch_game_thread");
    return 1;
  }

  set_game_running_true();
  if (g_gameStatusEvent) {
    SetEvent(g_gameStatusEvent);
  }
  info("Game status set to running");
  info("Launching game for account: %s", get_account_name());

  /* Create the game window in this thread */
  create_and_run_game_window();
  return 0;
}

/**
 * @brief Launches the game, creates the TERA launcher window, then waits for
 * the game process to exit. Cleans up and signals WM_GAME_EXITED on completion.
 *
 * @param account_name     The account name (credential).
 * @param characters_count The character count (credential).
 * @param ticket           The session ticket (credential).
 * @param game_lang        The language code for the game (credential).
 * @param game_path        The file system path to the game executable.
 * @param server_list_url  URL to fetch the server list from.
 * @return The exit code of the game process, or -1 on failure.
 */
int run_game(const char *account_name, const char *characters_count,
             const char *ticket, const char *game_lang, const char *game_path,
             const char *server_list_url) {
  info("Starting run_game function");
  if (is_game_running()) {
    error_log("Game is already running");
    return -1;
  }

  set_credentials(account_name, characters_count, ticket, game_lang, game_path);

  info("Set credentials: Account=%s, CharCount=%s, Ticket=%s, Lang=%s, "
       "GamePath=%s, ServerListURL=%s",
       get_account_name(), get_characters_count(),
       "***", // If you want to view it for debugging purposes, modify this.
       get_game_lang(), get_game_path(), server_list_url);

  server_list_url_global = server_list_url;

  /* Create the game launch thread */
  HANDLE hThread = CreateThread(NULL, 0, launch_game_thread, NULL, 0, NULL);
  if (!hThread) {
    error_log("Failed to create launch_game_thread");
    return -1;
  }

  /* Wait for the window to be created */
  DWORD wait_res = WaitForSingleObject(g_windowCreatedEvent, INFINITE);
  if (wait_res != WAIT_OBJECT_0) {
    error_log("WaitForSingleObject() on g_windowCreatedEvent failed, %lu",
              (unsigned long)GetLastError());
  }

  /* Build minimal command-line arguments */
  char tmpArg[PATH_MAX];
  snprintf(tmpArg, sizeof(tmpArg), "-LANGUAGEEXT=%s", game_lang);

  PROCESS_INFORMATION pi = launch_process(game_path, tmpArg);
  if (pi.hProcess == NULL) {
    return -1;
  }

  info("Game process spawned, PID=%lu", (unsigned long)pi.dwProcessId);

  /* Wait for the game to exit */
  int code = wait_for_process_exit(pi);
  info("Game process exited with status=%d", code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  /* Post WM_GAME_EXITED to our window (if it exists) */
  EnterCriticalSection(&g_window_handle_cs);
  HWND local_hwnd = g_window_handle.raw;
  LeaveCriticalSection(&g_window_handle_cs);
  if (local_hwnd) {
    info("Posting WM_GAME_EXITED message to window");
    PostMessageW(local_hwnd, WM_GAME_EXITED, 0, 0);
  } else {
    error_log("Window handle not found when trying to post WM_GAME_EXITED");
  }

  return code;
}

#include "torrent_wrapper.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <string>
#include <thread>
#include <vector>

struct TorrentSession {
  lt::session *session{nullptr};
  std::thread *thread{nullptr};
  TorrentProgressCallback progress_cb{nullptr};
  void *progress_userdata{nullptr};
  std::atomic<bool> should_stop{false};
  std::string error_message{};
  lt::torrent_handle torrent_handle{};
  TorrentSession() = default;
};

static void torrent_download_thread(TorrentSession *ts) {
  using namespace lt;
  bool done = false;
  while (true) {
    if (ts->should_stop.load())
      break;
    std::vector<alert *> alerts;
    ts->session->pop_alerts(&alerts);
    for (alert *a : alerts) {
      if (const auto at = alert_cast<add_torrent_alert>(a)) {
        ts->torrent_handle = at->handle;
      }
      if (const auto st = alert_cast<state_update_alert>(a)) {
        if (!st->status.empty()) {
          const torrent_status &status = st->status[0];
          float progress_percent = status.progress * 100.0f;
          progress_percent = std::clamp(progress_percent, 0.0f, 100.0f);
          const uint64_t downloaded = status.total_wanted_done;
          const uint64_t total = status.total_wanted;
          const uint32_t rate = status.download_payload_rate;
          if (ts->progress_cb) {
            ts->progress_cb(progress_percent, downloaded, total, rate,
                            ts->progress_userdata);
          }
        }
      }
      if (alert_cast<torrent_finished_alert>(a)) {
        torrent_status s = ts->torrent_handle.status();
        if (ts->progress_cb) {
          ts->progress_cb(100.0f, s.total_wanted_done, s.total_wanted, 0,
                          ts->progress_userdata);
        }
        done = true;
      }
      if (const auto te = alert_cast<torrent_error_alert>(a)) {
        ts->error_message = te->error.message().empty()
                                ? "Unknown torrent error"
                                : te->error.message();
        if (ts->progress_cb) {
          ts->progress_cb(-1.0f, 0, 0, 0, ts->progress_userdata);
        }
        if (ts->torrent_handle.is_valid()) {
          ts->session->remove_torrent(ts->torrent_handle);
        }
        done = true;
      }
    }
    if (done || ts->should_stop.load())
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ts->session->post_torrent_updates();
  }
}

TorrentSession *
torrent_session_create(const TorrentProgressCallback progress_cb,
                       void *userdata) {
  using namespace lt;
  try {
    settings_pack pack;
    pack.set_int(settings_pack::alert_mask, alert_category::error |
                                                alert_category::storage |
                                                alert_category::status);
    pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:0,[::]:0");
    auto ses = std::make_unique<session>(pack);
    auto *ts = new TorrentSession();
    ts->session = ses.release();
    ts->progress_cb = progress_cb;
    ts->progress_userdata = userdata;
    return ts;
  } catch (...) {
    return nullptr;
  }
}

int torrent_session_start_download(TorrentSession *session,
                                   const char *magnet_link,
                                   const char *save_path) {
  if (!session || !magnet_link || !save_path)
    return -1;
  session->error_message.clear();
  using namespace lt;
  error_code ec;
  auto params = parse_magnet_uri(magnet_link, ec);
  if (ec) {
    session->error_message =
        ec.message().empty() ? "Failed to parse magnet link" : ec.message();
    return -1;
  }
  params.save_path = save_path;
  try {
    session->torrent_handle = session->session->add_torrent(std::move(params));
  } catch (const std::exception &e) {
    session->error_message = e.what();
    return -1;
  }
  try {
    session->thread = new std::thread(torrent_download_thread, session);
  } catch (...) {
    if (session->torrent_handle.is_valid()) {
      session->session->remove_torrent(session->torrent_handle,
                                       session::delete_files);
    }
    session->error_message = "Failed to start download thread";
    return -1;
  }
  return 0;
}

void torrent_session_close(TorrentSession *session) {
  if (!session)
    return;
  session->should_stop.store(true);
  if (session->thread) {
    session->thread->join();
    delete session->thread;
    session->thread = nullptr;
  }
  if (session->torrent_handle.is_valid()) {
    session->session->remove_torrent(session->torrent_handle);
  }
  session->session->abort();
  delete session->session;
  delete session;
}

const char *torrent_session_get_error(const TorrentSession *session) {
  return session ? session->error_message.c_str() : "";
}
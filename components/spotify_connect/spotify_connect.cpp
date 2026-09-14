#include <atomic>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "AccessKeyFetcher.h"
#include "AudioSink.h"
#include "BellTask.h"
#include "BellUtils.h"
#include "CentralAudioBuffer.h"
#include "CSpotContext.h"
#include "LoginBlob.h"
#include "Logger.h"
#include "SpircHandler.h"
#include "TrackPlayer.h"
#include "TrackQueue.h"
#include "Utils.h"

#include "board/audio.h"
#include "spotify_bridge.h"

/* Shared with spotify_webapi.cpp */
std::shared_ptr<cspot::AccessKeyFetcher> g_access_keys;
std::shared_ptr<cspot::Context> g_cspot_ctx;
std::string g_device_id;

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nlohmann/json.hpp"

static const char *TAG = "spotify_sink";

#ifndef SPOTIFY_DEVICE_NAME
#define SPOTIFY_DEVICE_NAME "Marten Player"
#endif

static std::atomic<bool> g_playing{false};
static std::atomic<bool> g_ready{false};
static std::atomic<bool> g_paired{false};
static std::atomic<int> g_pendingVolume{-1};

class MartenAudioSink;
class CSpotPlayer;

static std::shared_ptr<CSpotPlayer> g_player;
static std::shared_ptr<cspot::SpircHandler> g_handler;
static std::shared_ptr<cspot::LoginBlob> g_blob;
static std::atomic<bool> g_gotBlob{false};
static httpd_handle_t g_httpd = nullptr;
static spotify_track_cb_t g_track_cb = nullptr;
static void *g_track_user = nullptr;

class MartenAudioSink : public AudioSink {
 public:
  MartenAudioSink() {
    softwareVolumeControl = true;
    currentVolume.store(65535);
  }

  void volumeChanged(uint16_t volume) override { currentVolume.store(volume); }

  bool setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) override {
    (void)sampleRate;
    (void)channelCount;
    (void)bitDepth;
    return true;
  }

  void feedPCMFrames(const uint8_t *buffer, size_t bytes) override {
    if (!buffer || bytes == 0) {
      return;
    }
    const uint16_t vol = currentVolume.load();
    if (vol >= 65535) {
      board_audio_feed_pcm(buffer, bytes);
      return;
    }
    scaledPcm.resize(bytes);
    memcpy(scaledPcm.data(), buffer, bytes);
    int16_t *samples = reinterpret_cast<int16_t *>(scaledPcm.data());
    const size_t count = bytes / sizeof(int16_t);
    for (size_t i = 0; i < count; i++) {
      samples[i] = static_cast<int16_t>((static_cast<int32_t>(samples[i]) * vol) / 65535);
    }
    board_audio_feed_pcm(scaledPcm.data(), bytes);
  }

 private:
  std::atomic<uint16_t> currentVolume;
  std::vector<uint8_t> scaledPcm;
};

class CSpotPlayer : public bell::Task {
 public:
  CSpotPlayer(std::shared_ptr<cspot::SpircHandler> handlerIn, std::unique_ptr<MartenAudioSink> sink)
      : bell::Task("pcm_out", 12 * 1024, 0, 0, false),
        handler(std::move(handlerIn)),
        audioSink(std::move(sink)),
        centralAudioBuffer(std::make_shared<bell::CentralAudioBuffer>(64)),
        isPaused(false),
        playlistEnd(false) {
    audioSink->setParams(44100, 2, 16);

    auto hashFunc = std::hash<std::string_view>();
    this->handler->getTrackPlayer()->setDataCallback(
        [this, hashFunc](uint8_t *data, size_t bytes, std::string_view trackId) {
          return this->centralAudioBuffer->writePCM(data, bytes, hashFunc(trackId));
        });

    this->handler->setEventHandler([this](std::unique_ptr<cspot::SpircHandler::Event> event) {
      try {
        switch (event->eventType) {
          case cspot::SpircHandler::EventType::PLAY_PAUSE:
            if (std::holds_alternative<bool>(event->data)) {
              isPaused = std::get<bool>(event->data);
              g_playing = !isPaused;
              if (g_track_cb) {
                g_track_cb("", "", "", "", "", 0, g_playing.load(), g_track_user);
              }
            }
            break;
          case cspot::SpircHandler::EventType::TRACK_INFO:
            if (std::holds_alternative<cspot::TrackInfo>(event->data)) {
              const auto &info = std::get<cspot::TrackInfo>(event->data);
              g_playing = true;
              isPaused = false;
              ESP_LOGI(TAG, "Track: %s — %s (art=%s)", info.name.c_str(), info.artist.c_str(),
                       info.imageUrl.empty() ? "none" : "yes");
              if (g_track_cb) {
                g_track_cb(info.name.c_str(), info.artist.c_str(), info.album.c_str(),
                           info.trackId.c_str(), info.imageUrl.c_str(), info.duration, true,
                           g_track_user);
              }
            }
            break;
          case cspot::SpircHandler::EventType::FLUSH:
          case cspot::SpircHandler::EventType::SEEK:
          case cspot::SpircHandler::EventType::NEXT:
          case cspot::SpircHandler::EventType::PREV:
          case cspot::SpircHandler::EventType::DISC:
            centralAudioBuffer->clearBuffer();
            break;
          case cspot::SpircHandler::EventType::PLAYBACK_START:
            playlistEnd = false;
            centralAudioBuffer->clearBuffer();
            isPaused = false;
            g_playing = true;
            break;
          case cspot::SpircHandler::EventType::DEPLETED:
            playlistEnd = true;
            break;
          default:
            break;
        }
      } catch (...) {
        ESP_LOGW(TAG, "Ignoring malformed Spotify event");
      }
    });

    startTask();
  }

  MartenAudioSink *getSink() { return audioSink.get(); }

 private:
  std::shared_ptr<cspot::SpircHandler> handler;
  std::unique_ptr<MartenAudioSink> audioSink;
  std::shared_ptr<bell::CentralAudioBuffer> centralAudioBuffer;
  std::atomic<bool> isPaused;
  std::atomic<bool> playlistEnd;

  void runTask() override {
    size_t lastHash = 0;
    while (true) {
      if (!isPaused) {
        bell::CentralAudioBuffer::AudioChunk *chunk = centralAudioBuffer->readChunk();
        if (!chunk || chunk->pcmSize == 0) {
          if (playlistEnd) {
            handler->notifyAudioEnded();
            playlistEnd = false;
            g_playing = false;
          }
          BELL_SLEEP_MS(10);
          continue;
        }
        if (lastHash != chunk->trackHash) {
          lastHash = chunk->trackHash;
          handler->notifyAudioReachedPlayback();
        }
        audioSink->feedPCMFrames(chunk->pcmData, chunk->pcmSize);
      } else {
        BELL_SLEEP_MS(10);
      }
    }
  }
};

static int hexVal(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

static std::string formUrlDecode(const std::string &in)
{
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] == '+') {
      out.push_back(' ');
    } else if (in[i] == '%' && i + 2 < in.size()) {
      out.push_back(static_cast<char>((hexVal(in[i + 1]) << 4) | hexVal(in[i + 2])));
      i += 2;
    } else {
      out.push_back(in[i]);
    }
  }
  return out;
}

static std::map<std::string, std::string> parseFormUrlEncoded(const std::string &body)
{
  std::map<std::string, std::string> out;
  size_t start = 0;
  while (start < body.size()) {
    size_t amp = body.find('&', start);
    if (amp == std::string::npos) amp = body.size();
    size_t eq = body.find('=', start);
    if (eq != std::string::npos && eq < amp) {
      out[formUrlDecode(body.substr(start, eq - start))] =
          formUrlDecode(body.substr(eq + 1, amp - eq - 1));
    }
    start = amp + 1;
  }
  return out;
}

static esp_err_t spotifyInfoGet(httpd_req_t *req)
{
  if (!g_blob) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no blob");
    return ESP_FAIL;
  }
  std::string body = g_blob->buildZeroconfInfo();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, body.c_str(), body.size());
}

static esp_err_t spotifyInfoPost(httpd_req_t *req)
{
  if (!g_blob) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no blob");
    return ESP_FAIL;
  }
  std::string body;
  if (req->content_len > 0 && req->content_len < 64 * 1024) {
    body.resize(req->content_len);
    int received = 0;
    while (received < req->content_len) {
      int r = httpd_req_recv(req, &body[received], req->content_len - received);
      if (r <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
        return ESP_FAIL;
      }
      received += r;
    }
    auto queryMap = parseFormUrlEncoded(body);
    try {
      g_blob->loadZeroconfQuery(queryMap);
      if (g_blob->authData.empty()) {
        ESP_LOGW(TAG, "Pairing blob rejected (bad MAC / short payload)");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad blob");
        return ESP_FAIL;
      }
      g_gotBlob = true;
      g_paired = true;
      ESP_LOGI(TAG, "Spotify pairing credentials received");
    } catch (...) {
      ESP_LOGE(TAG, "Pairing blob parse crashed — ignored");
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "blob");
      return ESP_FAIL;
    }
  }
  nlohmann::json obj;
  obj["status"] = 101;
  obj["spotifyError"] = 0;
  obj["statusString"] = "ERROR-OK";
  std::string resp = obj.dump();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, resp.c_str(), resp.size());
}

static bool startZeroconfHttp()
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080;
  config.ctrl_port = 8081;
  config.max_uri_handlers = 4;
  config.stack_size = 16 * 1024;
  config.lru_purge_enable = true;
  if (httpd_start(&g_httpd, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP :8080");
    return false;
  }
  httpd_uri_t getUri = {.uri = "/spotify_info", .method = HTTP_GET, .handler = spotifyInfoGet,
                        .user_ctx = nullptr};
  httpd_uri_t postUri = {.uri = "/spotify_info", .method = HTTP_POST, .handler = spotifyInfoPost,
                         .user_ctx = nullptr};
  httpd_register_uri_handler(g_httpd, &getUri);
  httpd_register_uri_handler(g_httpd, &postUri);
  ESP_LOGI(TAG, "Zeroconf HTTP :8080/spotify_info");
  return true;
}

static bool registerSpotifyMdns(const std::string &deviceName)
{
  static char txtVersion[] = "1.0";
  static char txtCPath[] = "/spotify_info";
  static char txtStack[] = "SP";
  mdns_txt_item_t txt[] = {
      {.key = "VERSION", .value = txtVersion},
      {.key = "CPath", .value = txtCPath},
      {.key = "Stack", .value = txtStack},
  };
  mdns_service_remove("_spotify-connect", "_tcp");
  esp_err_t err =
      mdns_service_add(deviceName.c_str(), "_spotify-connect", "_tcp", 8080, txt, 3);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mdns_service_add failed: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(TAG, "mDNS: %s._spotify-connect._tcp", deviceName.c_str());
  return true;
}

static void applyVolumePercent(int percent)
{
  if (!g_player) {
    return;
  }
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  uint16_t spotifyVolume = static_cast<uint16_t>((percent * 65535) / 100);
  g_player->getSink()->volumeChanged(spotifyVolume);
  board_audio_set_volume(static_cast<uint8_t>(percent));
}

static void cspotMainTask(void *arg)
{
  (void)arg;
  bell::setDefaultLogger();
  vTaskDelay(pdMS_TO_TICKS(500));

  esp_err_t mdnsErr = mdns_init();
  if (mdnsErr != ESP_OK && mdnsErr != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(mdnsErr));
  }
  mdns_hostname_set("marten");
  mdns_instance_name_set(SPOTIFY_DEVICE_NAME);

  g_blob = std::make_shared<cspot::LoginBlob>(SPOTIFY_DEVICE_NAME);
  g_gotBlob = false;

  if (!startZeroconfHttp()) {
    ESP_LOGE(TAG, "HTTP server failed");
    vTaskDelete(nullptr);
    return;
  }
  registerSpotifyMdns(g_blob->getDeviceName());
  ESP_LOGI(TAG, "Waiting for Spotify — select '%s' on same Wi‑Fi", SPOTIFY_DEVICE_NAME);

  while (!g_gotBlob) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  ESP_LOGI(TAG, "Credentials OK — starting session");

  while (true) {
    try {
      auto ctx = cspot::Context::createFromBlob(g_blob);
      ctx->session->connectWithRandomAp();
      auto token = ctx->session->authenticate(g_blob);
      if (token.empty()) {
        ESP_LOGE(TAG, "Auth failed; retry in 5s");
        vTaskDelay(pdMS_TO_TICKS(5000));
        continue;
      }

      ctx->session->startTask();
      g_cspot_ctx = ctx;
      g_device_id = ctx->config.deviceId;
      g_access_keys = std::make_shared<cspot::AccessKeyFetcher>(ctx);
      g_handler = std::make_shared<cspot::SpircHandler>(ctx);
      g_handler->subscribeToMercury();

      if (!g_player) {
        auto sink = std::make_unique<MartenAudioSink>();
        g_player = std::make_shared<CSpotPlayer>(g_handler, std::move(sink));
      }
      if (g_pendingVolume >= 0) {
        applyVolumePercent(g_pendingVolume.load());
      }
      g_ready = true;
      ESP_LOGI(TAG, "Spotify session active (deviceId=%s)", g_device_id.c_str());

      while (true) {
        ctx->session->handlePacket();
      }
    } catch (const std::exception &e) {
      ESP_LOGE(TAG, "Session error: %s", e.what());
      g_ready = false;
      g_playing = false;
      g_access_keys.reset();
      g_handler.reset();
      vTaskDelay(pdMS_TO_TICKS(2000));
    } catch (...) {
      ESP_LOGE(TAG, "Session crashed");
      g_ready = false;
      g_playing = false;
      g_access_keys.reset();
      g_handler.reset();
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}

extern "C" void spotify_connect_start(void)
{
  ESP_LOGI(TAG, "Starting Spotify Connect as '%s'", SPOTIFY_DEVICE_NAME);

  constexpr size_t kStackBytes = 64 * 1024;
  constexpr uint32_t kStackWords = kStackBytes / sizeof(StackType_t);
  static StaticTask_t *tcb = nullptr;
  static StackType_t *stack = nullptr;
  if (!tcb) {
    tcb = static_cast<StaticTask_t *>(
        heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    stack = static_cast<StackType_t *>(
        heap_caps_malloc(kStackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (!tcb || !stack) {
    ESP_LOGE(TAG, "Failed to allocate cspot task memory");
    return;
  }
  TaskHandle_t handle =
      xTaskCreateStaticPinnedToCore(cspotMainTask, "cspot", kStackWords, nullptr, 5, stack, tcb, 1);
  if (!handle) {
    ESP_LOGE(TAG, "Failed to create cspot task");
  }
}

extern "C" void spotify_connect_set_volume(uint8_t percent)
{
  g_pendingVolume = percent;
  applyVolumePercent(percent);
}

extern "C" bool spotify_connect_is_playing(void) { return g_playing.load(); }
extern "C" bool spotify_connect_is_ready(void) { return g_ready.load(); }
extern "C" bool spotify_connect_is_paired(void) { return g_paired.load(); }

extern "C" void spotify_connect_play_pause(void)
{
  if (g_handler) {
    g_handler->setPause(g_playing.load());
  }
}

extern "C" void spotify_connect_next(void)
{
  if (g_handler) {
    g_handler->nextSong();
  }
}

extern "C" void spotify_connect_previous(void)
{
  if (g_handler) {
    g_handler->previousSong();
  }
}

extern "C" void spotify_connect_set_track_callback(spotify_track_cb_t cb, void *user)
{
  g_track_cb = cb;
  g_track_user = user;
}

extern "C" bool spotify_connect_play_track_id(const char *track_id)
{
  if (track_id && track_id[0] && g_handler) {
    auto tq = g_handler->getTrackQueue();
    if (tq) {
      int idx = tq->findIndexByTrackId(track_id);
      if (idx >= 0 && g_handler->jumpToTrackIndex(idx)) {
        ESP_LOGI(TAG, "Jump to Connect queue index %d", idx);
        return true;
      }
      ESP_LOGW(TAG, "Track %s not in Connect queue — Web API fallback", track_id);
    }
  }
  return spotify_connect_play_track_id_web(track_id);
}

/** Spotify track IDs are 22-char base62 encodings of the 16-byte GID. */
static std::string gidToBase62(const std::vector<uint8_t> &gid)
{
  static const char *alpha = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  if (gid.empty()) {
    return "";
  }
  std::vector<uint8_t> num(gid.begin(), gid.end());
  std::string out;
  while (!num.empty()) {
    bool all_zero = true;
    for (uint8_t b : num) {
      if (b) {
        all_zero = false;
        break;
      }
    }
    if (all_zero) {
      break;
    }
    int rem = 0;
    std::vector<uint8_t> div;
    div.reserve(num.size());
    for (uint8_t b : num) {
      int acc = rem * 256 + b;
      int q = acc / 62;
      rem = acc % 62;
      if (!div.empty() || q != 0) {
        div.push_back(static_cast<uint8_t>(q));
      }
    }
    out.insert(out.begin(), alpha[rem]);
    num.swap(div);
  }
  if (out.empty()) {
    out = "0";
  }
  while (out.size() < 22) {
    out.insert(out.begin(), '0');
  }
  return out;
}

static std::string refToTrackId(const cspot::TrackReference &ref)
{
  if (!ref.uri.empty()) {
    auto pos = ref.uri.rfind(':');
    if (pos != std::string::npos && pos + 1 < ref.uri.size()) {
      return ref.uri.substr(pos + 1);
    }
  }
  if (!ref.gid.empty()) {
    return gidToBase62(ref.gid);
  }
  return "";
}

extern "C" bool spotify_connect_jump_relative(int offset)
{
  if (!g_handler) {
    return false;
  }
  if (offset == 0) {
    return true;
  }
  if (offset < 0) {
    return false;
  }
  auto tq = g_handler->getTrackQueue();
  if (!tq) {
    return false;
  }
  int cur = tq->getCurrentIndex();
  size_t total = tq->getTrackCount();
  if (cur < 0 || total == 0) {
    return false;
  }
  int target = (int)(((size_t)cur + (size_t)offset) % total);
  /* Must go through SpircHandler so TrackPlayer resets — TrackQueue::jump alone
   * only retargets the queue and leaves audio on the old track. */
  if (g_handler->jumpToTrackIndex(target)) {
    ESP_LOGI(TAG, "Jump relative %+d → Connect index %d (was %d)", offset, target, cur);
    return true;
  }
  ESP_LOGW(TAG, "jump_relative %+d → %d failed", offset, target);
  return false;
}

extern "C" bool spotify_connect_jump_to_index(int index)
{
  if (!g_handler || index < 0) {
    return false;
  }
  auto tq = g_handler->getTrackQueue();
  if (!tq) {
    return false;
  }
  size_t total = tq->getTrackCount();
  if (total == 0 || (size_t)index >= total) {
    ESP_LOGW(TAG, "jump_to_index %d out of range (n=%u)", index, (unsigned)total);
    return false;
  }
  int was = tq->getCurrentIndex();
  if (g_handler->jumpToTrackIndex(index)) {
    ESP_LOGI(TAG, "Jump to Connect index %d (was %d)", index, was);
    return true;
  }
  ESP_LOGW(TAG, "jump_to_index %d failed", index);
  return false;
}

extern "C" bool spotify_connect_play_ring_slot(int offset, const char *expected_id)
{
  if (!g_handler || offset < 1) {
    return false;
  }
  auto tq = g_handler->getTrackQueue();
  if (!tq) {
    return false;
  }

  /* Fresh snapshot so we never use a stale absolute index from an old sync. */
  std::vector<cspot::TrackQueue::SnapshotEntry> snap;
  const size_t want = (size_t)offset + 2;
  if (tq->snapshotFromCurrent(snap, want < 8 ? 8 : want) == 0) {
    ESP_LOGW(TAG, "play_ring_slot: empty snapshot");
    return false;
  }

  auto jump_abs = [&](int abs, const char *why) -> bool {
    if (abs < 0) {
      return false;
    }
    int was = tq->getCurrentIndex();
    if (g_handler->jumpToTrackIndex(abs)) {
      ESP_LOGI(TAG, "ring slot offset=%d → abs=%d (%s, was %d)", offset, abs, why, was);
      return true;
    }
    return false;
  };

  /* 1) Prefer the cover's track id — but ONLY inside this short ring window. */
  if (expected_id && expected_id[0]) {
    for (size_t i = 0; i < snap.size(); i++) {
      std::string id = refToTrackId(snap[i].ref);
      if (id.empty() && snap[i].hasMeta && !snap[i].info.trackId.empty()) {
        id = gidToBase62(stringHexToBytes(snap[i].info.trackId));
      }
      if (!id.empty() && id == expected_id) {
        return jump_abs(snap[i].absIndex, "id-in-ring");
      }
      if (snap[i].hasMeta && !snap[i].info.trackId.empty()) {
        /* Mercury sometimes stores hex GID in trackId. */
        if (snap[i].info.trackId == expected_id) {
          return jump_abs(snap[i].absIndex, "hex-in-ring");
        }
      }
    }
    ESP_LOGW(TAG, "play_ring_slot: id %.22s not in fresh ring (n=%u) — use offset",
             expected_id, (unsigned)snap.size());
  }

  /* 2) Fall back to live offset (same slot the wedge represents). */
  if ((size_t)offset < snap.size()) {
    return jump_abs(snap[offset].absIndex, "fresh-offset");
  }
  return spotify_connect_jump_relative(offset);
}

static void fillFromTrackInfo(spotify_web_track_t *dst, const cspot::TrackInfo &info,
                              const std::string &id)
{
  int keep_idx = dst->connect_index;
  memset(dst, 0, sizeof(*dst));
  dst->connect_index = keep_idx;
  strncpy(dst->id, id.c_str(), sizeof(dst->id) - 1);
  strncpy(dst->title, info.name.c_str(), sizeof(dst->title) - 1);
  strncpy(dst->artist, info.artist.c_str(), sizeof(dst->artist) - 1);
  strncpy(dst->album, info.album.c_str(), sizeof(dst->album) - 1);
  strncpy(dst->image_url, info.imageUrl.c_str(), sizeof(dst->image_url) - 1);
  dst->duration_ms = info.duration;
}

extern "C" bool spotify_connect_ring_tracks(spotify_web_track_t *out, size_t max, size_t *count)
{
  if (count) {
    *count = 0;
  }
  if (!out || max == 0 || !g_handler) {
    return false;
  }
  auto tq = g_handler->getTrackQueue();
  if (!tq) {
    return false;
  }

  /* Local Connect queue only — do NOT call Web API here.
   * Concurrent bell::HTTPClient use with CDN audio starves Wi‑Fi / TWDT. */
  std::vector<cspot::TrackQueue::SnapshotEntry> snap;
  if (tq->snapshotFromCurrent(snap, max) == 0) {
    return false;
  }

  /* Keep snap[i] → out[i] 1:1. Store absolute Connect index for correct taps
   * even when getCurrentIndex() has advanced since the UI snapshot. */
  size_t n = 0;
  for (size_t i = 0; i < snap.size() && n < max; i++) {
    std::string id = refToTrackId(snap[i].ref);
    if (id.empty() && snap[i].hasMeta && !snap[i].info.trackId.empty()) {
      id = gidToBase62(stringHexToBytes(snap[i].info.trackId));
    }
    memset(&out[n], 0, sizeof(out[n]));
    out[n].connect_index = snap[i].absIndex;
    if (snap[i].hasMeta && !snap[i].info.name.empty()) {
      fillFromTrackInfo(&out[n], snap[i].info, id);
      out[n].connect_index = snap[i].absIndex;
    }
    if (!out[n].id[0] && !id.empty()) {
      strncpy(out[n].id, id.c_str(), sizeof(out[n].id) - 1);
    }
    n++;
  }
  if (count) {
    *count = n;
  }
  ESP_LOGI(TAG, "ring tracks → %u (snap=%u)", (unsigned)n, (unsigned)snap.size());
  return n > 0;
}

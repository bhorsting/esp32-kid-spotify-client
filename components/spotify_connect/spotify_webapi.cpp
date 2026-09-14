/**
 * Spotify Web API — curated playlist tracks + play-on-this-device.
 */
#include "spotify_bridge.h"

#include <cstdio>
#include <cstring>
#include <strings.h>
#include <memory>
#include <string>
#include <vector>

#include "AccessKeyFetcher.h"
#include "CSpotContext.h"
#include "HTTPClient.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nlohmann/json.hpp"

static const char *TAG = "spotify_web";

extern std::shared_ptr<cspot::AccessKeyFetcher> g_access_keys;
extern std::shared_ptr<cspot::Context> g_cspot_ctx;
extern std::string g_device_id;

static std::string url_encode(const std::string &in)
{
  static const char *hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(in.size() * 3);
  for (unsigned char c : in) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}

static bool ensure_token(std::string &token)
{
  if (!g_access_keys) {
    return false;
  }
  try {
    token = g_access_keys->getAccessKey();
  } catch (...) {
    return false;
  }
  return !token.empty();
}

static bell::HTTPClient::Headers auth_headers(const std::string &token, bool json_body)
{
  bell::HTTPClient::Headers h;
  h.push_back({"Authorization", "Bearer " + token});
  if (json_body) {
    h.push_back({"Content-Type", "application/json"});
  }
  return h;
}

static bool http_get_json(const std::string &url, const std::string &token, nlohmann::json &out)
{
  /* Use esp_http_client (same stack as cover downloads) — bell::HTTPClient
   * contending with CDN audio caused Wi‑Fi stalls / TWDT. */
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.timeout_ms = 12000;
  cfg.buffer_size = 4096;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    return false;
  }
  std::string auth = "Bearer " + token;
  esp_http_client_set_header(client, "Authorization", auth.c_str());
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "HTTP open fail: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }
  (void)esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status > 0 && (status < 200 || status >= 300)) {
    ESP_LOGW(TAG, "HTTP status %d", status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  std::string body;
  body.reserve(8192);
  char tmp[1024];
  while (true) {
    int n = esp_http_client_read(client, tmp, sizeof(tmp));
    if (n <= 0) {
      break;
    }
    body.append(tmp, tmp + n);
    if (body.size() > 256 * 1024) {
      break;
    }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (body.empty()) {
    return false;
  }
  try {
    out = nlohmann::json::parse(body);
    return true;
  } catch (const std::exception &e) {
    ESP_LOGW(TAG, "JSON parse failed: %s", e.what());
    return false;
  } catch (...) {
    return false;
  }
}

static bool http_put(const std::string &url, const std::string &token, const std::string &body)
{
  try {
    auto resp = std::make_unique<bell::HTTPClient::Response>();
    resp->connect(url);
    std::vector<uint8_t> bytes(body.begin(), body.end());
    auto headers = auth_headers(token, !body.empty());
    resp->rawRequest(url, "PUT", bytes, headers);
    if (!body.empty()) {
      (void)resp->body();
    }
    return true;
  } catch (const std::exception &e) {
    ESP_LOGW(TAG, "PUT failed: %s", e.what());
    return false;
  } catch (...) {
    return false;
  }
}

static void copy_str(char *dst, size_t dst_len, const std::string &src)
{
  if (!dst || dst_len == 0) {
    return;
  }
  strncpy(dst, src.c_str(), dst_len - 1);
  dst[dst_len - 1] = '\0';
}

/** Prefer ~300px Spotify image (good for 1.85" ring UI). */
static std::string track_image(const nlohmann::json &track)
{
  try {
    if (!track.contains("album") || !track["album"].contains("images") ||
        !track["album"]["images"].is_array() || track["album"]["images"].empty()) {
      return "";
    }
    const auto &images = track["album"]["images"];
    std::string best;
    int best_diff = 100000;
    for (const auto &im : images) {
      int w = im.value("width", 0);
      std::string url = im.value("url", "");
      if (url.empty()) {
        continue;
      }
      int diff = w > 0 ? abs(w - 300) : 500;
      if (diff < best_diff) {
        best_diff = diff;
        best = url;
      }
    }
    if (best.empty()) {
      best = images.back().value("url", "");
    }
    return best;
  } catch (...) {
  }
  return "";
}

static std::string artist_names(const nlohmann::json &track)
{
  std::string artists;
  try {
    if (!track.contains("artists") || !track["artists"].is_array()) {
      return artists;
    }
    for (const auto &a : track["artists"]) {
      if (!artists.empty()) {
        artists += ", ";
      }
      artists += a.value("name", "");
    }
  } catch (...) {
  }
  return artists;
}

static void fill_track(spotify_web_track_t *dst, const nlohmann::json &track)
{
  memset(dst, 0, sizeof(*dst));
  copy_str(dst->id, sizeof(dst->id), track.value("id", ""));
  copy_str(dst->title, sizeof(dst->title), track.value("name", ""));
  copy_str(dst->artist, sizeof(dst->artist), artist_names(track));
  if (track.contains("album")) {
    copy_str(dst->album, sizeof(dst->album), track["album"].value("name", ""));
  }
  copy_str(dst->image_url, sizeof(dst->image_url), track_image(track));
  dst->duration_ms = track.value("duration_ms", 0u);
  dst->connect_index = -1;
}

static std::string normalize_playlist_id(const char *id)
{
  if (!id || !id[0]) {
    return "";
  }
  std::string s(id);
  /* https://open.spotify.com/playlist/XXXX?... */
  auto web = s.find("playlist/");
  if (web != std::string::npos) {
    s = s.substr(web + 9);
    auto q = s.find('?');
    if (q != std::string::npos) {
      s = s.substr(0, q);
    }
    auto slash = s.find('/');
    if (slash != std::string::npos) {
      s = s.substr(0, slash);
    }
    return s;
  }
  auto pos = s.rfind("playlist:");
  if (pos != std::string::npos) {
    return s.substr(pos + 9);
  }
  return s;
}

static std::string normalize_playlist_uri(const char *id)
{
  std::string bare = normalize_playlist_id(id);
  if (bare.empty()) {
    return "";
  }
  return "spotify:playlist:" + bare;
}

static bool transfer_and_play_body(const std::string &token, const std::string &body)
{
  if (g_device_id.empty()) {
    ESP_LOGW(TAG, "No device id yet");
    return false;
  }
  std::string play_url =
      "https://api.spotify.com/v1/me/player/play?device_id=" + url_encode(g_device_id);
  if (http_put(play_url, token, body)) {
    ESP_LOGI(TAG, "Play request sent to device");
    return true;
  }
  nlohmann::json transfer;
  transfer["device_ids"] = nlohmann::json::array({g_device_id});
  transfer["play"] = true;
  http_put("https://api.spotify.com/v1/me/player", token, transfer.dump());
  return http_put(play_url, token, body);
}

extern "C" bool spotify_connect_get_device_id(char *buf, size_t buflen)
{
  if (!buf || buflen == 0 || g_device_id.empty()) {
    return false;
  }
  copy_str(buf, buflen, g_device_id);
  return true;
}

extern "C" bool spotify_connect_get_access_token(char *buf, size_t buflen)
{
  std::string token;
  if (!ensure_token(token) || !buf || buflen == 0) {
    return false;
  }
  copy_str(buf, buflen, token);
  return true;
}

extern "C" bool spotify_connect_play_track_id_web(const char *track_id)
{
  std::string token;
  if (!ensure_token(token) || !track_id) {
    return false;
  }
  std::string uri = track_id;
  if (uri.rfind("spotify:track:", 0) != 0) {
    uri = "spotify:track:" + uri;
  }
  nlohmann::json body;
  body["uris"] = nlohmann::json::array({uri});
  return transfer_and_play_body(token, body.dump());
}

extern "C" bool spotify_connect_play_playlist_id(const char *playlist_id)
{
  return spotify_connect_play_playlist_offset(playlist_id, 0);
}

extern "C" bool spotify_connect_play_playlist_offset(const char *playlist_id, int offset)
{
  std::string token;
  if (!ensure_token(token)) {
    return false;
  }
  std::string uri = normalize_playlist_uri(playlist_id);
  if (uri.empty()) {
    return false;
  }
  if (offset < 0) {
    offset = 0;
  }
  nlohmann::json body;
  body["context_uri"] = uri;
  body["offset"] = {{"position", offset}};
  return transfer_and_play_body(token, body.dump());
}

extern "C" bool spotify_connect_web_playlist_tracks(const char *playlist_id,
                                                    spotify_web_track_t *out, size_t max,
                                                    size_t *count)
{
  if (count) {
    *count = 0;
  }
  if (!out || max == 0) {
    return false;
  }
  std::string bare = normalize_playlist_id(playlist_id);
  if (bare.empty()) {
    return false;
  }
  std::string token;
  if (!ensure_token(token)) {
    return false;
  }

  char url[256];
  snprintf(url, sizeof(url),
           "https://api.spotify.com/v1/playlists/%s/tracks?limit=%u&market=from_token",
           url_encode(bare).c_str(), (unsigned)(max > 50 ? 50 : max));

  nlohmann::json json;
  if (!http_get_json(url, token, json)) {
    return false;
  }

  size_t n = 0;
  try {
    for (const auto &item : json["items"]) {
      if (n >= max) {
        break;
      }
      if (!item.contains("track") || item["track"].is_null()) {
        continue;
      }
      fill_track(&out[n], item["track"]);
      if (!out[n].id[0]) {
        continue;
      }
      n++;
    }
  } catch (...) {
    return false;
  }
  if (count) {
    *count = n;
  }
  ESP_LOGI(TAG, "playlist %s → %u tracks", bare.c_str(), (unsigned)n);
  return n > 0;
}

extern "C" bool spotify_connect_web_player_queue(spotify_web_track_t *currently,
                                                 spotify_web_track_t *upcoming,
                                                 size_t max_upcoming, size_t *upcoming_count)
{
  if (upcoming_count) {
    *upcoming_count = 0;
  }
  if (currently) {
    memset(currently, 0, sizeof(*currently));
  }
  std::string token;
  if (!ensure_token(token)) {
    return false;
  }

  nlohmann::json json;
  if (!http_get_json("https://api.spotify.com/v1/me/player/queue", token, json)) {
    return false;
  }

  try {
    if (currently && json.contains("currently_playing") && !json["currently_playing"].is_null()) {
      fill_track(currently, json["currently_playing"]);
    }
    if (!upcoming || max_upcoming == 0) {
      return currently && currently->id[0];
    }
    size_t n = 0;
    if (json.contains("queue") && json["queue"].is_array()) {
      for (const auto &track : json["queue"]) {
        if (n >= max_upcoming) {
          break;
        }
        if (track.is_null()) {
          continue;
        }
        fill_track(&upcoming[n], track);
        if (!upcoming[n].id[0]) {
          continue;
        }
        n++;
      }
    }
    if (upcoming_count) {
      *upcoming_count = n;
    }
    ESP_LOGI(TAG, "player queue → %u upcoming", (unsigned)n);
    return n > 0 || (currently && currently->id[0]);
  } catch (...) {
    return false;
  }
}

extern "C" bool spotify_connect_web_tracks_by_ids(const char *ids_csv, spotify_web_track_t *out,
                                                  size_t max, size_t *count)
{
  if (count) {
    *count = 0;
  }
  if (!ids_csv || !ids_csv[0] || !out || max == 0) {
    return false;
  }
  std::string token;
  if (!ensure_token(token)) {
    return false;
  }
  std::string url = "https://api.spotify.com/v1/tracks?ids=";
  url += ids_csv;
  url += "&market=from_token";

  nlohmann::json json;
  if (!http_get_json(url, token, json)) {
    return false;
  }
  size_t n = 0;
  try {
    if (!json.contains("tracks") || !json["tracks"].is_array()) {
      return false;
    }
    for (const auto &track : json["tracks"]) {
      if (n >= max) {
        break;
      }
      if (track.is_null()) {
        continue;
      }
      fill_track(&out[n], track);
      if (!out[n].id[0]) {
        continue;
      }
      n++;
    }
  } catch (...) {
    return false;
  }
  if (count) {
    *count = n;
  }
  ESP_LOGI(TAG, "tracks by id → %u", (unsigned)n);
  return n > 0;
}

extern "C" bool spotify_connect_enrich_tracks(spotify_web_track_t *tracks, size_t n)
{
  if (!tracks || n == 0) {
    return false;
  }
  std::string ids;
  size_t need = 0;
  for (size_t i = 0; i < n; i++) {
    if (!tracks[i].id[0]) {
      continue;
    }
    if (tracks[i].image_url[0] && tracks[i].title[0]) {
      continue;
    }
    if (!ids.empty()) {
      ids += ',';
    }
    ids += tracks[i].id;
    need++;
  }
  if (need == 0) {
    return true;
  }

  auto *fetched =
      static_cast<spotify_web_track_t *>(calloc(need, sizeof(spotify_web_track_t)));
  size_t got = 0;
  if (!fetched || !spotify_connect_web_tracks_by_ids(ids.c_str(), fetched, need, &got)) {
    ESP_LOGW(TAG, "enrich failed for %u ids (http/token)", (unsigned)need);
    free(fetched);
    return false;
  }
  for (size_t j = 0; j < got; j++) {
    if (!fetched[j].id[0]) {
      continue;
    }
    for (size_t i = 0; i < n; i++) {
      if (strcasecmp(tracks[i].id, fetched[j].id) != 0) {
        continue;
      }
      if (!tracks[i].title[0] && fetched[j].title[0]) {
        strncpy(tracks[i].title, fetched[j].title, sizeof(tracks[i].title) - 1);
      }
      if (!tracks[i].artist[0] && fetched[j].artist[0]) {
        strncpy(tracks[i].artist, fetched[j].artist, sizeof(tracks[i].artist) - 1);
      }
      if (!tracks[i].album[0] && fetched[j].album[0]) {
        strncpy(tracks[i].album, fetched[j].album, sizeof(tracks[i].album) - 1);
      }
      if (!tracks[i].image_url[0] && fetched[j].image_url[0]) {
        strncpy(tracks[i].image_url, fetched[j].image_url, sizeof(tracks[i].image_url) - 1);
      }
      if (tracks[i].duration_ms == 0) {
        tracks[i].duration_ms = fetched[j].duration_ms;
      }
      break;
    }
  }
  free(fetched);
  ESP_LOGI(TAG, "enriched %u/%u tracks with Web metadata", (unsigned)got, (unsigned)need);
  return got > 0;
}

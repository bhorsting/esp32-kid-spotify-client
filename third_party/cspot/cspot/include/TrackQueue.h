#pragma once

#include <stddef.h>  // for size_t
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include "BellTask.h"
#include "PlaybackState.h"
#include "TrackReference.h"

#include "protobuf/metadata.pb.h"  // for Track, _Track, AudioFile, Episode

namespace bell {
class WrappedSemaphore;
};

namespace cspot {
struct Context;
class AccessKeyFetcher;
class CDNAudioFile;

// Used in got track info event
struct TrackInfo {
  std::string name, album, artist, imageUrl, trackId;
  uint32_t duration, number, discNumber;

  void loadPbTrack(Track* pbTrack, const std::vector<uint8_t>& gid);
  void loadPbEpisode(Episode* pbEpisode, const std::vector<uint8_t>& gid);
};

class QueuedTrack {
 public:
  QueuedTrack(TrackReference& ref, std::shared_ptr<cspot::Context> ctx,
              uint32_t requestedPosition = 0);
  ~QueuedTrack();

  enum class State {
    QUEUED,
    PENDING_META,
    KEY_REQUIRED,
    PENDING_KEY,
    CDN_REQUIRED,
    READY,
    FAILED
  };

  std::shared_ptr<bell::WrappedSemaphore> loadedSemaphore;

  State state = State::QUEUED;  // Current state of the track
  TrackReference ref;           // Holds GID, URI and Context
  TrackInfo trackInfo;  // Full track information fetched from spotify, name etc

  uint32_t requestedPosition;
  std::string identifier;
  bool loading = false;

  // Will return nullptr if the track is not ready
  std::shared_ptr<cspot::CDNAudioFile> getAudioFile();

  // --- Steps ---
  void stepLoadMetadata(
      Track* pbTrack, Episode* pbEpisode, std::mutex& trackListMutex,
      std::shared_ptr<bell::WrappedSemaphore> updateSemaphore);

  void stepParseMetadata(Track* pbTrack, Episode* pbEpisode);

  void stepLoadAudioFile(
      std::mutex& trackListMutex,
      std::shared_ptr<bell::WrappedSemaphore> updateSemaphore);

  void stepLoadCDNUrl(const std::string& accessKey);

  void expire();

 private:
  std::shared_ptr<cspot::Context> ctx;

  uint64_t pendingMercuryRequest = 0;
  uint32_t pendingAudioKeyRequest = 0;

  std::vector<uint8_t> trackId, fileId, audioKey;
  std::string cdnUrl;
};

class TrackQueue : public bell::Task {
 public:
  TrackQueue(std::shared_ptr<cspot::Context> ctx,
             std::shared_ptr<cspot::PlaybackState> playbackState);
  ~TrackQueue();

  enum class SkipDirection { NEXT, PREV };

  std::shared_ptr<bell::WrappedSemaphore> playableSemaphore;
  std::atomic<bool> notifyPending = false;

  void runTask() override;
  void stopTask();

  bool hasTracks();
  bool isFinished();
  bool skipTrack(SkipDirection dir, bool expectNotify = true);
  bool updateTracks(uint32_t requestedPosition = 0, bool initial = false);
  TrackInfo getTrackInfo(std::string_view identifier);
  std::shared_ptr<QueuedTrack> consumeTrack(
      std::shared_ptr<QueuedTrack> prevSong, int& offset);

  /**
   * Copy current + following tracks for UI (uris / GIDs + metadata when preloaded).
   * Returns number of entries written to `out` (may be < max).
   */
  struct SnapshotEntry {
    TrackReference ref;
    TrackInfo info;
    bool hasMeta = false;
    /** Absolute index in currentTracks when the snapshot was taken. */
    int absIndex = -1;
  };
  size_t snapshotFromCurrent(std::vector<SnapshotEntry>& out, size_t max);

  /** Jump to absolute index in the current context queue and start that track. */
  bool jumpToIndex(int16_t index);

  /** Jump +offset from the currently playing index (wraps). Prefer over id lookup. */
  bool jumpRelative(int offset);

  /** Absolute index of the currently playing track (-1 if none). */
  int getCurrentIndex();

  /** Number of tracks in the current context queue. */
  size_t getTrackCount();

  /** Find index of a track by Spotify base62 id or hex GID; -1 if missing.
   *  Searches from the current track forward so duplicates resolve to the upcoming copy. */
  int findIndexByTrackId(const std::string& trackId);

 private:
  static const int MAX_TRACKS_PRELOAD = 6; /* head+1 audio; rest metadata for ring UI */
  static const int MAX_TRACKS_AUDIO = 2;

  std::shared_ptr<cspot::AccessKeyFetcher> accessKeyFetcher;
  std::shared_ptr<PlaybackState> playbackState;
  std::shared_ptr<cspot::Context> ctx;
  std::shared_ptr<bell::WrappedSemaphore> processSemaphore;

  std::deque<std::shared_ptr<QueuedTrack>> preloadedTracks;
  std::vector<TrackReference> currentTracks;
  std::mutex tracksMutex, runningMutex;

  // PB data
  Track pbTrack;
  Episode pbEpisode;

  std::string accessKey;

  int16_t currentTracksIndex = -1;

  bool isRunning = false;

  void processTrack(std::shared_ptr<QueuedTrack> track);
  bool queueNextTrack(int offset = 0, uint32_t positionMs = 0);
};
}  // namespace cspot

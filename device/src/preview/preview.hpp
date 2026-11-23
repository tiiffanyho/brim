// Demo preview: a small live copy of the camera for the laptop, while brim keeps the camera.
//
// The C270 can be opened by one program at a time, so the old test viewer (camview) and brim could
// not run together. With BRIM_PREVIEW_PORT set (`sudo sh start.sh preview`), brim itself serves its
// latest frame as a 320x240 BMP at http://<pi>:<port>/frame.bmp, the same address camview used, so
// `scripts/laptop/brim_test.py live --camera http://<pi>:8090` shows the hat's view on the laptop
// while the phone keeps getting brim's warnings.
//
// Off unless asked for. Frames stay in memory (one 230 KB buffer) and are never written to disk; the
// only copy that leaves the hat is the one the laptop asks for on the local hotspot. The pipeline
// never waits on it: offer() skips a frame rather than block, and costs well under 1 ms.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "capture/camera.hpp"

namespace brim::preview {

class Preview {
 public:
  static constexpr int kWidth = 320;
  static constexpr int kHeight = 240;

  ~Preview() { stop(); }

  // Starts the HTTP thread on `port`. false (and logged) if the port can't be opened.
  bool start(int port);
  void stop();

  // Called by the pipeline for every frame; keeps at most ~10 frames a second.
  void offer(const capture::Frame& f);

 private:
  void serve();

  int listen_fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::int64_t last_offer_ms_ = 0;
  std::vector<std::uint8_t> work_;  // pipeline thread only
  std::mutex mu_;
  std::vector<std::uint8_t> bmp_;  // latest frame, guarded by mu_
};

// The BMP for one frame: 320x240, 24-bit, bottom-up (exposed for the unit test).
void encode_bmp(const capture::Frame& f, std::vector<std::uint8_t>& out);

}  // namespace brim::preview

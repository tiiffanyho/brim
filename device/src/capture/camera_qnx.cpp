// Real camera on the Pi 5 under QNX 8.0, through the QNX Camera Framework (libcamapi).
//
// The Logitech C270 is a UVC webcam. QNX has no V4L2: the `sensor` service owns the camera and
// exposes it as a camera unit. On the Brim Pi it is SENSOR_UNIT_5 in
// /etc/config/sensor/sensor_rpi5.conf (type = usb_camera, 640x480 @ 30 fps), visible as
// /dev/sensor/camera5 once the webcam is plugged into a blue USB 3.0 port.
//
// Frames arrive on a libcamapi thread through the viewfinder callback. The callback converts the
// newest frame to packed RGB888 into a buffer allocated once, and grab() hands the newest frame to
// the pipeline. Older frames are overwritten, never queued: the pipeline always sees "now".
// Frames live in memory only — nothing is ever written to disk.
#include "capture/camera.hpp"

#include <camera/camera_api.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "common/log.hpp"
#include "common/timing.hpp"

namespace brim::capture {

namespace {

constexpr int kDefaultUnit = 5;           // SENSOR_UNIT_5 = USB camera on the Brim Pi image
constexpr int kGrabTimeoutMs = 1000;      // no frame for 1 s = camera unplugged/stalled
constexpr int kReopenIntervalMs = 2000;   // retry opening a lost camera this often

inline std::uint8_t clamp_u8(int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// BT.601 limited-range YCbCr -> RGB, integer only.
inline void yuv_to_rgb(int y, int u, int v, std::uint8_t* out) {
  const int c = 298 * (y - 16);
  const int d = u - 128;
  const int e = v - 128;
  out[0] = clamp_u8((c + 409 * e + 128) >> 8);
  out[1] = clamp_u8((c - 100 * d - 208 * e + 128) >> 8);
  out[2] = clamp_u8((c + 516 * d + 128) >> 8);
}

const char* frametype_name(camera_frametype_t t) {
  switch (t) {
    case CAMERA_FRAMETYPE_YCBYCR: return "YCBYCR(YUYV)";
    case CAMERA_FRAMETYPE_CBYCRY: return "CBYCRY(UYVY)";
    case CAMERA_FRAMETYPE_NV12: return "NV12";
    case CAMERA_FRAMETYPE_RGB888: return "RGB888";
    case CAMERA_FRAMETYPE_RGB8888: return "RGB8888";
    case CAMERA_FRAMETYPE_JPEG: return "JPEG";
    default: return "other";
  }
}

class QnxCamera final : public Camera {
 public:
  QnxCamera(int unit, int fps) : unit_(unit), fps_(fps > 0 ? fps : 15) {}
  ~QnxCamera() override { close(); }

  bool open() override {
    std::snprintf(name_, sizeof(name_), "qnx-camapi-unit%d", unit_);
    return open_unit();
  }

  bool grab(Frame& out) override {
    if (!open_) {
      const std::int64_t now = timing::mono_ms();
      if (now - last_reopen_try_ms_ < kReopenIntervalMs) {
        timing::sleep_ms(100);
        return false;
      }
      last_reopen_try_ms_ = now;
      BRIM_LOGW("capture", "camera lost; trying to reopen unit %d", unit_);
      if (!open_unit()) return false;
    }

    std::unique_lock<std::mutex> lk(mu_);
    const bool got = cv_.wait_for(lk, std::chrono::milliseconds(kGrabTimeoutMs),
                                  [&] { return latest_seq_ != delivered_seq_ || lost_; });
    if (lost_ || !got) {
      lk.unlock();
      BRIM_LOGE("capture", "%s", lost_ ? "camera disconnected" : "no camera frame for 1 s (blocked or unplugged?)");
      close();
      return false;
    }
    // Swap the newest frame out; the callback keeps writing into the other buffer.
    out.width = latest_w_;
    out.height = latest_h_;
    out.rgb.swap(latest_rgb_);
    out.t_capture_mono_ms = latest_t_ms_;
    out.index = latest_seq_;
    delivered_seq_ = latest_seq_;
    // Keep the spare buffer sized so the callback never allocates (after the first frames).
    if (latest_rgb_.size() != out.rgb.size()) latest_rgb_.resize(out.rgb.size());
    return true;
  }

  const char* name() const override { return name_; }

 private:
  bool open_unit() {
    camera_handle_t h = CAMERA_HANDLE_INVALID;
    // Read-only is enough: the sensor config already sets 640x480. We ask for write access only to
    // set the frame rate, and fall back to read-only if another client holds it.
    camera_error_t err = camera_open(static_cast<camera_unit_t>(unit_), CAMERA_MODE_RW, &h);
    if (err != CAMERA_EOK) err = camera_open(static_cast<camera_unit_t>(unit_), CAMERA_MODE_RO, &h);
    if (err != CAMERA_EOK) {
      BRIM_LOGE("capture", "camera_open(unit %d) failed: error %d (is the webcam in a blue USB port? ls /dev/sensor)",
                unit_, static_cast<int>(err));
      return false;
    }
    // Best effort: ask for our pipeline rate. Some USB cameras only offer fixed rates; that's fine,
    // grab() always takes the newest frame anyway.
    err = camera_set_vf_property(h, CAMERA_IMGPROP_FRAMERATE, static_cast<double>(fps_));
    if (err != CAMERA_EOK) BRIM_LOGW("capture", "could not set %d fps (error %d); using the camera's rate", fps_, static_cast<int>(err));

    {
      std::lock_guard<std::mutex> lk(mu_);
      lost_ = false;
      warned_format_ = false;
    }
    err = camera_start_viewfinder(h, &QnxCamera::on_frame, &QnxCamera::on_status, this);
    if (err != CAMERA_EOK) {
      BRIM_LOGE("capture", "camera_start_viewfinder(unit %d) failed: error %d", unit_, static_cast<int>(err));
      camera_close(h);
      return false;
    }
    handle_ = h;
    open_ = true;
    BRIM_LOGI("capture", "camera unit %d open, viewfinder running", unit_);
    return true;
  }

  void close() {
    if (!open_) return;
    camera_stop_viewfinder(handle_);
    camera_close(handle_);
    handle_ = CAMERA_HANDLE_INVALID;
    open_ = false;
  }

  static void on_status(camera_handle_t, camera_devstatus_t status, uint16_t extra, void* arg) {
    auto* self = static_cast<QnxCamera*>(arg);
    if (status == CAMERA_STATUS_DISCONNECTED || status == CAMERA_STATUS_POWERDOWN) {
      std::lock_guard<std::mutex> lk(self->mu_);
      self->lost_ = true;
      self->cv_.notify_all();
    }
  }

  static void on_frame(camera_handle_t, camera_buffer_t* buf, void* arg) {
    static_cast<QnxCamera*>(arg)->convert(buf);
  }

  // Runs on the libcamapi thread. Converts straight into latest_rgb_ under the lock: one 640x480
  // conversion is ~1 ms on the Pi 5, much shorter than a frame period.
  void convert(const camera_buffer_t* buf) {
    std::lock_guard<std::mutex> lk(mu_);
    int w = 0, h = 0;
    switch (buf->frametype) {
      case CAMERA_FRAMETYPE_YCBYCR:   // Y0 Cb Y1 Cr — the C270's native UVC format
      case CAMERA_FRAMETYPE_CBYCRY: { // Cb Y0 Cr Y1
        const bool uyvy = buf->frametype == CAMERA_FRAMETYPE_CBYCRY;
        // The two descriptors have different layouts (cbycry has bufsize/packing first).
        w = static_cast<int>(uyvy ? buf->framedesc.cbycry.width : buf->framedesc.ycbycr.width);
        h = static_cast<int>(uyvy ? buf->framedesc.cbycry.height : buf->framedesc.ycbycr.height);
        const std::uint32_t stride = uyvy ? buf->framedesc.cbycry.stride : buf->framedesc.ycbycr.stride;
        if (!ensure(w, h)) return;
        for (int y = 0; y < h; ++y) {
          const std::uint8_t* s = buf->framebuf + static_cast<std::size_t>(y) * stride;
          std::uint8_t* o = latest_rgb_.data() + static_cast<std::size_t>(y) * w * 3;
          for (int x = 0; x + 1 < w; x += 2, s += 4, o += 6) {
            const int y0 = uyvy ? s[1] : s[0], u = uyvy ? s[0] : s[1];
            const int y1 = uyvy ? s[3] : s[2], v = uyvy ? s[2] : s[3];
            yuv_to_rgb(y0, u, v, o);
            yuv_to_rgb(y1, u, v, o + 3);
          }
        }
        break;
      }
      case CAMERA_FRAMETYPE_NV12: {
        const auto& d = buf->framedesc.nv12;
        w = static_cast<int>(d.width);
        h = static_cast<int>(d.height);
        if (!ensure(w, h)) return;
        const std::uint8_t* uv_base = buf->framebuf + d.uv_offset;
        for (int y = 0; y < h; ++y) {
          const std::uint8_t* yp = buf->framebuf + static_cast<std::size_t>(y) * d.stride;
          const std::uint8_t* uvp = uv_base + static_cast<std::size_t>(y / 2) * d.uv_stride;
          std::uint8_t* o = latest_rgb_.data() + static_cast<std::size_t>(y) * w * 3;
          for (int x = 0; x < w; ++x, o += 3) yuv_to_rgb(yp[x], uvp[x & ~1], uvp[(x & ~1) + 1], o);
        }
        break;
      }
      case CAMERA_FRAMETYPE_RGB888: {
        const auto& d = buf->framedesc.rgb888;
        w = static_cast<int>(d.width);
        h = static_cast<int>(d.height);
        if (!ensure(w, h)) return;
        for (int y = 0; y < h; ++y)
          std::memcpy(latest_rgb_.data() + static_cast<std::size_t>(y) * w * 3,
                      buf->framebuf + static_cast<std::size_t>(y) * d.stride, static_cast<std::size_t>(w) * 3);
        break;
      }
      case CAMERA_FRAMETYPE_RGB8888: {
        // TODO(qnx-check): QNX RGB8888 is documented as 32-bit ARGB; on little-endian that is B,G,R,A
        // in memory. If colours look swapped on the dashboard, flip the 0/2 indices below.
        const auto& d = buf->framedesc.rgb8888;
        w = static_cast<int>(d.width);
        h = static_cast<int>(d.height);
        if (!ensure(w, h)) return;
        for (int y = 0; y < h; ++y) {
          const std::uint8_t* s = buf->framebuf + static_cast<std::size_t>(y) * d.stride;
          std::uint8_t* o = latest_rgb_.data() + static_cast<std::size_t>(y) * w * 3;
          for (int x = 0; x < w; ++x, s += 4, o += 3) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; }
        }
        break;
      }
      default:
        // JPEG/MJPEG would need a decoder per frame; the sensor config keeps the C270 on raw YUYV.
        if (!warned_format_) {
          BRIM_LOGE("capture", "unsupported camera frame type %d (%s); set the usb_camera unit to raw YUYV, not jpeg",
                    static_cast<int>(buf->frametype), frametype_name(buf->frametype));
          warned_format_ = true;
        }
        return;
    }
    if (!logged_format_) {
      BRIM_LOGI("capture", "first frame: %dx%d %s", w, h, frametype_name(buf->frametype));
      logged_format_ = true;
    }
    latest_w_ = w;
    latest_h_ = h;
    // frametimestamp is microseconds on CLOCK_MONOTONIC (camera_defs.h), same clock as t_mono_ms.
    latest_t_ms_ = buf->frametimestamp > 0 ? buf->frametimestamp / 1000 : timing::mono_ms();
    ++latest_seq_;
    cv_.notify_one();
  }

  // Sizes the write buffer. Allocates only when the frame size changes (i.e. the first frames).
  bool ensure(int w, int h) {
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    const std::size_t n = static_cast<std::size_t>(w) * h * 3;
    if (latest_rgb_.size() != n) latest_rgb_.resize(n);
    return true;
  }

  const int unit_;
  const int fps_;
  char name_[32] = "qnx-camapi";
  camera_handle_t handle_ = CAMERA_HANDLE_INVALID;
  bool open_ = false;
  std::int64_t last_reopen_try_ms_ = 0;

  std::mutex mu_;
  std::condition_variable cv_;
  std::vector<std::uint8_t> latest_rgb_;  // written by the callback, swapped out by grab()
  int latest_w_ = 0, latest_h_ = 0;
  std::int64_t latest_t_ms_ = 0;
  std::uint64_t latest_seq_ = 0, delivered_seq_ = 0;
  bool lost_ = false;
  bool warned_format_ = false;
  bool logged_format_ = false;
};

}  // namespace

std::unique_ptr<Camera> make_qnx_camera(int fps) {
  int unit = kDefaultUnit;
  if (const char* env = std::getenv("BRIM_CAMERA_UNIT")) {
    const int u = std::atoi(env);
    if (u >= 1 && u <= 16) unit = u;
  }
  return std::make_unique<QnxCamera>(unit, fps);
}

}  // namespace brim::capture

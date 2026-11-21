// EfficientDet-Lite0 person + object detector on tflite-runtime 2.21.0 (oss.qnx.com), C API.
//
// Model: models/efficientdet_lite0.tflite (source, licence and tensors in models/README.md).
//   input   [1,320,320,3] uint8 RGB (quantized; raw 0..255 pixels go straight in)
//   out0    [1,25,4] float  boxes   ymin, xmin, ymax, xmax in [0,1]
//   out1    [1,25]   float  classes COCO-90 index (labelmap.txt in the model's metadata)
//   out2    [1,25]   float  scores
//   out3    [1]      float  count
// The NMS step is inside the model (TFLite_Detection_PostProcess), so there is no anchor decoding here.
//
// Per-frame path is allocation-free: the frame is resized straight into the interpreter's input
// tensor (resize tables built once), and detections go into the caller's reused vector.
// No face model. Frames are never copied anywhere but the input tensor, never written to disk.
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/log.hpp"
#include "common/timing.hpp"
#include "detect/detector.hpp"

#if defined(BRIM_WITH_TFLITE)
#include "tensorflow/lite/c/c_api.h"
// XNNPACK: TFLite's fast ARM kernels. The header is vendored in device/third_party/tflite (the
// oss.qnx.com package exports the functions but doesn't ship the header). See its README.
#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"
#endif

namespace brim::detect {

#if defined(BRIM_WITH_TFLITE)

namespace {

double ms_since(std::int64_t t0_ns) { return static_cast<double>(timing::mono_ns() - t0_ns) / 1e6; }

class TfliteDetector final : public Detector {
 public:
  explicit TfliteDetector(DetectorConfig cfg) : cfg_(std::move(cfg)) {}
  ~TfliteDetector() override {
    if (interp_) TfLiteInterpreterDelete(interp_);
    if (xnnpack_) TfLiteXNNPackDelegateDelete(xnnpack_);  // after the interpreter that used it
    if (opts_) TfLiteInterpreterOptionsDelete(opts_);
    if (model_) TfLiteModelDelete(model_);
  }

  bool load() override {
    model_ = TfLiteModelCreateFromFile(cfg_.model_path.c_str());
    if (!model_) return fail("cannot load model " + cfg_.model_path + " (copy it to models/, see models/README.md)");
    opts_ = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsSetNumThreads(opts_, cfg_.threads);

    // Switch on XNNPACK for this uint8 model. Without it TFLite runs quantized ops on slow
    // reference kernels: measured ~360 ms per frame on the Pi 5 (budget 30 ms). QU8 = uint8
    // models like ours, QS8 = int8 ones; neither is on by default in this build.
    // Set BRIM_XNNPACK=0 to turn it off (to compare, or if a model misbehaves with it).
    const char* xnn_env = std::getenv("BRIM_XNNPACK");
    if (xnn_env == nullptr || std::strcmp(xnn_env, "0") != 0) {
      TfLiteXNNPackDelegateOptions xo = TfLiteXNNPackDelegateOptionsDefault();
      xo.num_threads = cfg_.threads;
      xo.flags |= TFLITE_XNNPACK_DELEGATE_FLAG_QU8 | TFLITE_XNNPACK_DELEGATE_FLAG_QS8;
      xnnpack_ = TfLiteXNNPackDelegateCreate(&xo);
      if (xnnpack_) {
        TfLiteInterpreterOptionsAddDelegate(opts_, xnnpack_);
      } else {
        BRIM_LOGW("detect", "XNNPACK delegate could not be created; using TFLite's default kernels");
      }
    }
    // TODO(qnx-check): TFLite's worker threads inherit brim's scheduling; check with `pidin -p brim`
    //   that they run at the pipeline's priority and not below app_server.
    interp_ = TfLiteInterpreterCreate(model_, opts_);
    if (!interp_) return fail("TfLiteInterpreterCreate failed");
    if (TfLiteInterpreterAllocateTensors(interp_) != kTfLiteOk) return fail("AllocateTensors failed");

    if (TfLiteInterpreterGetInputTensorCount(interp_) != 1) return fail("expected 1 input tensor");
    input_ = TfLiteInterpreterGetInputTensor(interp_, 0);
    if (TfLiteTensorType(input_) != kTfLiteUInt8 || TfLiteTensorNumDims(input_) != 4 ||
        TfLiteTensorDim(input_, 0) != 1 || TfLiteTensorDim(input_, 3) != 3)
      return fail("input must be [1,H,W,3] uint8 (is this the int8 EfficientDet-Lite0 with post-processing?)");
    in_h_ = TfLiteTensorDim(input_, 1);
    in_w_ = TfLiteTensorDim(input_, 2);

    if (TfLiteInterpreterGetOutputTensorCount(interp_) != 4) return fail("expected 4 outputs: boxes, classes, scores, count");
    for (int i = 0; i < 4; ++i) {
      out_[i] = TfLiteInterpreterGetOutputTensor(interp_, i);
      if (TfLiteTensorType(out_[i]) != kTfLiteFloat32) return fail("output " + std::to_string(i) + " is not float32");
    }
    max_n_ = TfLiteTensorDim(out_[0], 1);
    if (TfLiteTensorNumDims(out_[0]) != 3 || TfLiteTensorDim(out_[0], 2) != 4 || TfLiteTensorNumDims(out_[1]) != 2 ||
        TfLiteTensorDim(out_[1], 1) != max_n_ || TfLiteTensorNumDims(out_[2]) != 2 || TfLiteTensorDim(out_[2], 1) != max_n_ ||
        TfLiteTensorByteSize(out_[3]) < sizeof(float))
      return fail("unexpected output shapes (want [1,N,4], [1,N], [1,N], [1])");

    // Warm-up run so the first real frame is not the slow one (and to prove invoke works at all).
    std::memset(TfLiteTensorData(input_), 0, TfLiteTensorByteSize(input_));
    const std::int64_t t0 = timing::mono_ns();
    if (TfLiteInterpreterInvoke(interp_) != kTfLiteOk) return fail("warm-up invoke failed");
    BRIM_LOGI("detect", "loaded %s: input %dx%d uint8, up to %d boxes, %d threads, XNNPACK %s, warm-up %.1f ms",
              cfg_.model_path.c_str(), in_w_, in_h_, max_n_, cfg_.threads, xnnpack_ ? "on" : "off", ms_since(t0));
    healthy_ = true;
    return true;
  }

  std::vector<Detection> detect(const capture::Frame& frame) override {
    std::vector<Detection> v;
    detect(frame, v);
    return v;
  }

  void detect(const capture::Frame& frame, std::vector<Detection>& out) override {
    out.clear();
    times_ = StageTimes{};
    if (!interp_) return;
    const std::size_t need = static_cast<std::size_t>(frame.width) * frame.height * 3;
    if (frame.width <= 0 || frame.height <= 0 || frame.rgb.size() < need) {
      BRIM_LOGW("detect", "bad frame %dx%d (%zu bytes), skipped", frame.width, frame.height, frame.rgb.size());
      return;
    }

    std::int64_t t = timing::mono_ns();
    resizer_.resize(frame.rgb.data(), frame.width, frame.height, static_cast<std::uint8_t*>(TfLiteTensorData(input_)), in_w_,
                    in_h_);
    times_.resize_ms = ms_since(t);

    t = timing::mono_ns();
    if (TfLiteInterpreterInvoke(interp_) != kTfLiteOk) {
      if (healthy_) BRIM_LOGE("detect", "invoke failed");
      healthy_ = false;  // brim speaks the fault; we keep trying every frame
      return;
    }
    healthy_ = true;
    times_.invoke_ms = ms_since(t);

    t = timing::mono_ns();
    const float* boxes = static_cast<const float*>(TfLiteTensorData(out_[0]));
    const float* classes = static_cast<const float*>(TfLiteTensorData(out_[1]));
    const float* scores = static_cast<const float*>(TfLiteTensorData(out_[2]));
    int n = static_cast<int>(*static_cast<const float*>(TfLiteTensorData(out_[3])));
    if (n < 0) n = 0;
    if (n > max_n_) n = max_n_;
    decode_detections(boxes, classes, scores, n, cfg_.min_confidence, cfg_.phone_min_confidence, out);
    times_.decode_ms = ms_since(t);
  }

  const char* name() const override { return "tflite-efficientdet-lite0"; }
  bool healthy() const override { return healthy_; }
  StageTimes last_stage_times() const override { return times_; }

 private:
  bool fail(const std::string& why) {
    BRIM_LOGE("detect", "tflite detector: %s", why.c_str());
    healthy_ = false;
    return false;
  }

  DetectorConfig cfg_;
  TfLiteModel* model_ = nullptr;
  TfLiteInterpreterOptions* opts_ = nullptr;
  TfLiteInterpreter* interp_ = nullptr;
  TfLiteDelegate* xnnpack_ = nullptr;  // owned; deleted after interp_
  TfLiteTensor* input_ = nullptr;
  const TfLiteTensor* out_[4] = {nullptr, nullptr, nullptr, nullptr};
  int in_w_ = 0, in_h_ = 0, max_n_ = 0;
  RgbResizer resizer_;
  StageTimes times_;
  bool healthy_ = false;
};

}  // namespace

bool tflite_available() { return true; }
std::unique_ptr<Detector> make_tflite_detector(const DetectorConfig& cfg) { return std::make_unique<TfliteDetector>(cfg); }

#else  // !BRIM_WITH_TFLITE

namespace {
// Built without TFLite: never pretend to see. load() fails and healthy() stays false, so brim
// reports a fault and speaks "Brim cannot see" instead of announcing a clear path.
class UnavailableDetector final : public Detector {
 public:
  bool load() override {
    BRIM_LOGE("detect", "brim was built without TFLite (BRIM_WITH_TFLITE=OFF): detection is unavailable");
    return false;
  }
  std::vector<Detection> detect(const capture::Frame&) override { return {}; }
  void detect(const capture::Frame&, std::vector<Detection>& out) override { out.clear(); }
  const char* name() const override { return "unavailable"; }
  bool healthy() const override { return false; }
};
}  // namespace

bool tflite_available() { return false; }
std::unique_ptr<Detector> make_tflite_detector(const DetectorConfig&) { return std::make_unique<UnavailableDetector>(); }

#endif

}  // namespace brim::detect

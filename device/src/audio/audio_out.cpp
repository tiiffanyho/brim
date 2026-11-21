#include "audio/audio_out.hpp"

#include <atomic>

#include "common/log.hpp"
#include "common/timing.hpp"

namespace brim::audio {

namespace {

class LogAudioOut final : public AudioOut {
 public:
  void play(const SpeechItem& item) override {
    auto delay = timing::mono_ms() - item.created_ms;
    BRIM_LOGI("audio", "SAY [%s] \"%s\"%s%s (event->speech %lld ms, volume %d, rate %.2f)", to_string(item.priority),
              item.text.c_str(), item.clip.empty() ? "" : " clip=", item.clip.c_str(), static_cast<long long>(delay),
              volume_.load(), rate_.load());
    // Simulate speech duration so queue behaviour is realistic (~60 ms per character at rate 1.0,
    // max 2 s), scaled by the speaking speed.
    auto ms = static_cast<std::int64_t>(static_cast<double>(item.text.size()) * 60 / rate_.load());
    timing::sleep_ms(ms > 2000 ? 2000 : ms);
  }
  void interrupt() override {}
  void apply_settings(const schema::Settings& s) override {
    volume_ = static_cast<int>(s.volume);
    rate_ = s.speech_rate > 0 ? s.speech_rate : 1.0;
    BRIM_LOGI("audio", "settings applied: volume=%d speech_rate=%.2f", volume_.load(), rate_.load());
  }

 private:
  std::atomic<int> volume_{80};
  std::atomic<double> rate_{1.0};
};

class StubVad final : public VoiceActivity {
 public:
  bool someone_talking() override { return false; }
};

}  // namespace

std::unique_ptr<AudioOut> make_log_audio_out() { return std::make_unique<LogAudioOut>(); }

std::unique_ptr<AudioOut> make_qnx_audio_out(const std::string& audio_dir) {
  // TODO(hw): implement with libasound on QNX; until then, log.
  BRIM_LOGW("audio", "QNX audio output not implemented yet (audio_dir=%s); logging speech instead", audio_dir.c_str());
  return make_log_audio_out();
}

std::unique_ptr<VoiceActivity> make_stub_vad() { return std::make_unique<StubVad>(); }

}  // namespace brim::audio

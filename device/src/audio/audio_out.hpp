// Audio output + voice activity. Interfaces only; real implementations need the Pi.
#pragma once

#include <memory>
#include <string>

#include "audio/speak_queue.hpp"
#include "events.gen.hpp"

namespace brim::audio {

class AudioOut {
 public:
  virtual ~AudioOut() = default;
  // Plays the item (clip if present, else TTS of text). Blocks until done or interrupted.
  virtual void play(const SpeechItem& item) = 0;
  // Stops the current utterance (used when a safety item arrives mid-sentence).
  virtual void interrupt() = 0;
  // Applies the wearer's settings (volume, speaking speed). Called from brim's command thread when
  // app_server sends reload_settings, so it must be safe to call while play() is running.
  virtual void apply_settings(const schema::Settings& s) = 0;
};

// "Is anyone talking near the wearer?" Info-priority speech only plays when this is false.
class VoiceActivity {
 public:
  virtual ~VoiceActivity() = default;
  virtual bool someone_talking() = 0;
};

// Logs "SAY: ..." instead of producing sound. Used on the laptop and until audio lands.
std::unique_ptr<AudioOut> make_log_audio_out();
// TODO(hw): USB earpiece output via QNX io-snd.
//   QNX audio is libasound (snd_pcm_open_preferred / snd_pcm_plugin_write) — NOT ALSA-on-Linux.
//   TODO(qnx-check): on the Pi run `ls /dev/snd` and `io-snd` + `wave -a` to confirm the USB
//   earpiece device; then implement make_qnx_audio_out() playing WAVs from audio/ (see
//   scripts/make_audio.sh), with volume from Settings via snd_mixer_group_write.
std::unique_ptr<AudioOut> make_qnx_audio_out(const std::string& audio_dir);

// Always "nobody talking" (lets info speech through). Fake mode toggles it to exercise the gate.
std::unique_ptr<VoiceActivity> make_stub_vad();
// TODO(hw)/TODO(ai): microphone VAD on the C270's built-in mic — energy threshold first.
// Capture via snd_pcm_open_preferred(..., SND_PCM_OPEN_CAPTURE).

}  // namespace brim::audio

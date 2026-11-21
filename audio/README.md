# audio/

Pre-rendered speech clips (16 kHz, 16-bit mono WAV). Pre-rendering means **zero TTS latency** for
safety warnings and no cloud TTS. Generate the fixed phrases on a Mac with:

    scripts/make_audio.sh        # macOS `say` -> audio/*.wav

There are no per-person clips: Brim never learns who anyone is, so every phrase it can say is a
fixed one and lives here. Obstacle names, sides ("on your left") and closeness ("close by") are
separate clips that the speech layer stitches together.

Files are git-ignored; `scripts/deploy.sh` copies this folder to the Pi.

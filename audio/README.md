# audio/

Pre-rendered speech clips (16 kHz, 16-bit mono WAV). Pre-rendering means **zero TTS latency** for
safety warnings and no cloud TTS. Generate the fixed phrases on a Mac with:

    scripts/make_audio.sh        # macOS `say` -> audio/*.wav

Per-person name clips ("Sam") are recorded during enrollment and stored in `data/clips/<id>.wav`
(not here); "forget <name>" deletes them.

Files are git-ignored; `scripts/deploy.sh` copies this folder to the Pi.

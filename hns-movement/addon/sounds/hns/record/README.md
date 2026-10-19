Put the server record sound here as `server_record.wav` (16-bit PCM, 44.1 or 48 kHz; `.mp3` also works).
It compiles to `server_record.vsnd_c` and is played by the `hns.record` event in `soundevents/soundevents_hns.vsndevts`
whenever a jumpstat server record is broken. Keep it short (2-5 s) and at a similar loudness to `vip.wav`.

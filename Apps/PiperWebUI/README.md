# Piper Web UI

A full-featured web interface for [Piper TTS](https://github.com/OHF-Voice/piper1-gpl),
modeled after the official [piper-samples](https://rhasspy.github.io/piper-samples/) page.

It is a **drop-in replacement** for the stock `python3 -m piper.http_server`
(the minimal page currently running on port 5000) and keeps its HTTP API
fully compatible, while adding:

* **Voice browser** — every voice from the official
  [rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices) catalog,
  selectable by language → voice → quality (`x_low` / `low` / `medium` / `high`)
* **One-click voice download** from HuggingFace, straight from the browser
* **Speaker selection** for multi-speaker voices (with speaker names)
* **All synthesis options**: speed (length scale), noise scale, noise width,
  sentence silence, volume, audio normalization — with per-voice defaults and
  a reset button
* **Result history** with inline playback and WAV download
* Support for your own custom `.onnx` voices (just drop them in the data dir)
* Loaded voices are cached in memory (LRU, configurable) so switching voices
  stays fast without exhausting RAM

## Installation

```bash
sudo apt install python3-pip   # if needed
pip3 install -r requirements.txt
```

(`piper-tts` is already installed if the stock server was running.)

## Running

```bash
python3 piper_web_server.py --data-dir /srv/piper/voices --port 5000
```

Then open `http://<server>:5000/` in a browser. Voices you select are
downloaded on demand into the data directory; nothing needs to be
pre-installed.

All options:

| Option | Default | Description |
| --- | --- | --- |
| `--host` | `0.0.0.0` | Bind address |
| `--port` | `5000` | HTTP port |
| `-m`, `--model` | *(none)* | Default voice, e.g. `en_US-lessac-medium` (auto-downloaded) |
| `--data-dir` | current dir | Directory with `.onnx` voices; may be given multiple times |
| `--download-dir` | first data dir | Where downloaded voices are stored |
| `-s`, `--speaker` | `0` | Default speaker id for multi-speaker voices |
| `--length-scale` / `--noise-scale` / `--noise-w-scale` | voice default | Server-side synthesis defaults |
| `--sentence-silence` | `0.0` | Default seconds of silence between sentences |
| `--max-loaded-voices` | `3` | Voices kept loaded in memory at once |
| `--cuda` | off | Use GPU (requires `onnxruntime-gpu`) |
| `--debug` | off | Verbose logging |

## Replacing the stock server on port 5000

Stop the old server, then run this one on the same port. As a systemd
service (`/etc/systemd/system/piper-web.service`):

```ini
[Unit]
Description=Piper TTS Web UI
After=network.target

[Service]
ExecStart=/usr/bin/python3 /opt/piper-webui/piper_web_server.py --data-dir /srv/piper/voices --port 5000
Restart=on-failure
User=piper

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now piper-web
```

## HTTP API

The UI uses a small JSON API; the stock `piper.http_server` endpoints also
still work, so existing integrations (Home Assistant scripts, curl, etc.)
are unaffected.

### UI API

* `GET /api/voices` — catalog merged with local install state:
  `{"voices": [{"key", "name", "language", "quality", "num_speakers", "speaker_id_map", "installed", "defaults", ...}]}`.
  Add `?refresh=1` to re-fetch the catalog from HuggingFace.
* `POST /api/download` — body `{"voice": "en_US-bryce-medium"}`; downloads the
  `.onnx` + `.onnx.json` from HuggingFace.
* `POST /api/synthesize` — body:

  ```json
  {
    "text": "Hello world",
    "voice": "en_US-bryce-medium",
    "speaker_id": 0,
    "length_scale": 1.0,
    "noise_scale": 0.667,
    "noise_w_scale": 0.8,
    "sentence_silence": 0.2,
    "volume": 1.0,
    "normalize_audio": true
  }
  ```

  Returns `audio/wav`. Only `text` is required.

### OpenAI-compatible audio API (UltraAI)

The server also speaks the OpenAI `/v1/audio` dialect, so clients built for
OpenAI-style local servers — including the **UltraAI `local` TextToSpeech
adapter** — work against Piper without any code changes:

* `GET /v1/audio/voices` — `{"voices": [{"id", "name", "language", "gender", "description"}]}`
* `POST /v1/audio/speech` — body
  `{"input": "text", "voice": "en_US-bryce-medium", "speed": 1.0, "response_format": "wav"}`.
  Voices are auto-downloaded on first use. For multi-speaker voices append
  the speaker to the id: `"en_US-libritts_r-medium:p3922"` (name) or
  `"...:0"` (numeric id). `speed` maps to Piper's length scale.
  `response_format` values other than `wav` (mp3, opus, flac, aac) are
  transcoded with `ffmpeg` when it is installed on the server.

UltraAI usage (no adapter changes needed — note UltraAI's interim HTTP
client is loopback-only, so run the server on the same machine until
UltraNet transport lands):

```cpp
TextToSpeechConfig cfg;
cfg.providerId = "local";
cfg.endpoint   = "http://127.0.0.1:5000";

auto tts = CreateTextToSpeech(cfg);
auto voices = tts->ListVoices();          // full Piper catalog

SpeakRequest req;
req.text    = "Hello ULTRA OS.";
req.voiceId = "en_US-bryce-medium";
SpeakResult res = tts->Speak(req);        // res.audio = WAV bytes
```

### Stock-compatible API

* `GET /voices` — configs of installed voices
* `GET /all-voices` — raw `voices.json` from HuggingFace
* `POST /download` — `{"voice": "..."}`
* `POST /` — synthesis, same fields as `/api/synthesize`

CORS is enabled (`Access-Control-Allow-Origin: *`), so the API can also be
called from pages hosted elsewhere.

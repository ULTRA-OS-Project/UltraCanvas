#!/usr/bin/env python3
"""Piper TTS web server with a full-featured browser UI.

A drop-in replacement for ``python3 -m piper.http_server`` that adds a web
interface similar to https://rhasspy.github.io/piper-samples/ :

* Language / voice / quality selection across the whole official catalog
* Speaker selection for multi-speaker voices
* All synthesis options (speed, noise scale, noise width, sentence silence,
  volume, normalization)
* One-click voice download from HuggingFace, straight from the browser

The HTTP API of the stock server is kept intact (``GET /voices``,
``GET /all-voices``, ``POST /download``, ``POST /`` for synthesis), so any
existing clients keep working.

Run:
    python3 piper_web_server.py --data-dir /path/to/voices

Requires: piper-tts >= 1.4, flask
"""

import argparse
import io
import json
import logging
import shutil
import subprocess
import threading
import time
import wave
from collections import OrderedDict
from pathlib import Path
from typing import Any, Dict, List, Optional
from urllib.request import urlopen

from flask import Flask, Response, jsonify, request, send_from_directory

from piper import PiperVoice, SynthesisConfig
from piper.download_voices import VOICES_JSON, download_voice

_LOGGER = logging.getLogger("piper_web")

STATIC_DIR = Path(__file__).parent / "static"
VOICES_CACHE_TTL = 24 * 60 * 60  # refresh catalog once a day
_FFMPEG = shutil.which("ffmpeg") is not None


def _strip_suffix(name: str, suffix: str) -> str:
    return name[: -len(suffix)] if name.endswith(suffix) else name


class VoiceManager:
    """Finds, downloads and caches loaded Piper voices."""

    def __init__(
        self,
        data_dirs: List[Path],
        download_dir: Path,
        use_cuda: bool = False,
        max_loaded: int = 3,
    ) -> None:
        self.data_dirs = data_dirs
        self.download_dir = download_dir
        self.use_cuda = use_cuda
        self.max_loaded = max(1, max_loaded)

        self._loaded: "OrderedDict[str, PiperVoice]" = OrderedDict()
        self._lock = threading.Lock()
        self._download_locks: Dict[str, threading.Lock] = {}
        self._downloading: Dict[str, bool] = {}
        self._catalog: Optional[Dict[str, Any]] = None
        self._catalog_time: float = 0.0

    # ----- files on disk -------------------------------------------------

    def find_model(self, model_id: str) -> Optional[Path]:
        for data_dir in self.data_dirs:
            candidate = Path(data_dir) / f"{model_id}.onnx"
            if candidate.exists():
                return candidate
        return None

    def installed_voices(self) -> Dict[str, Dict[str, Any]]:
        """Map of model id -> parsed .onnx.json config for voices on disk."""
        voices: Dict[str, Dict[str, Any]] = {}
        for data_dir in self.data_dirs:
            if not Path(data_dir).is_dir():
                continue
            for onnx_path in sorted(Path(data_dir).glob("*.onnx")):
                config_path = Path(f"{onnx_path}.json")
                model_id = _strip_suffix(onnx_path.name, ".onnx")
                if model_id in voices or not config_path.exists():
                    continue
                try:
                    with open(config_path, "r", encoding="utf-8") as config_file:
                        voices[model_id] = json.load(config_file)
                except (OSError, json.JSONDecodeError) as err:
                    _LOGGER.warning("Bad config %s: %s", config_path, err)
        return voices

    # ----- official catalog ----------------------------------------------

    def catalog(self, force_refresh: bool = False) -> Dict[str, Any]:
        """voices.json from HuggingFace, cached in memory and on disk."""
        now = time.time()
        if (
            self._catalog is not None
            and not force_refresh
            and (now - self._catalog_time) < VOICES_CACHE_TTL
        ):
            return self._catalog

        cache_path = self.download_dir / "voices.json"
        catalog: Optional[Dict[str, Any]] = None
        try:
            with urlopen(VOICES_JSON, timeout=30) as response:
                catalog = json.load(response)
            try:
                cache_path.parent.mkdir(parents=True, exist_ok=True)
                with open(cache_path, "w", encoding="utf-8") as cache_file:
                    json.dump(catalog, cache_file)
            except OSError as err:
                _LOGGER.warning("Could not cache voices.json: %s", err)
        except Exception as err:  # network down: fall back to disk cache
            _LOGGER.warning("Could not fetch voices.json: %s", err)
            if cache_path.exists():
                with open(cache_path, "r", encoding="utf-8") as cache_file:
                    catalog = json.load(cache_file)

        if catalog is None:
            catalog = {}

        self._catalog = catalog
        self._catalog_time = now
        return catalog

    # ----- download -------------------------------------------------------

    def download(self, model_id: str, force_redownload: bool = False) -> None:
        with self._lock:
            lock = self._download_locks.setdefault(model_id, threading.Lock())
        with lock:
            if self.find_model(model_id) and not force_redownload:
                return
            self._downloading[model_id] = True
            try:
                download_voice(
                    model_id, self.download_dir, force_redownload=force_redownload
                )
            finally:
                self._downloading[model_id] = False

    def is_downloading(self, model_id: str) -> bool:
        return bool(self._downloading.get(model_id))

    # ----- loading --------------------------------------------------------

    def get_voice(self, model_id: str) -> PiperVoice:
        with self._lock:
            voice = self._loaded.get(model_id)
            if voice is not None:
                self._loaded.move_to_end(model_id)
                return voice

        model_path = self.find_model(model_id)
        if model_path is None:
            raise FileNotFoundError(model_id)

        _LOGGER.info("Loading voice %s", model_id)
        voice = PiperVoice.load(model_path, use_cuda=self.use_cuda)

        with self._lock:
            self._loaded[model_id] = voice
            self._loaded.move_to_end(model_id)
            while len(self._loaded) > self.max_loaded:
                evicted, _ = self._loaded.popitem(last=False)
                _LOGGER.info("Unloading voice %s", evicted)
        return voice


def build_voice_list(manager: VoiceManager) -> List[Dict[str, Any]]:
    """Merge the official catalog with locally installed voices."""
    installed = manager.installed_voices()
    catalog = manager.catalog()

    result: List[Dict[str, Any]] = []
    seen = set()

    for key, info in catalog.items():
        entry = {
            "key": key,
            "name": info.get("name", key),
            "language": info.get("language", {}),
            "quality": info.get("quality", ""),
            "num_speakers": info.get("num_speakers", 1),
            "speaker_id_map": info.get("speaker_id_map", {}),
            "installed": key in installed,
            "downloading": manager.is_downloading(key),
            "local_only": False,
        }
        config = installed.get(key)
        if config:
            inference = config.get("inference", {})
            entry["defaults"] = {
                "length_scale": inference.get("length_scale", 1.0),
                "noise_scale": inference.get("noise_scale", 0.667),
                "noise_w_scale": inference.get("noise_w", 0.8),
            }
            entry["sample_rate"] = config.get("audio", {}).get("sample_rate")
            # Config on disk is authoritative for speakers
            entry["num_speakers"] = config.get("num_speakers", entry["num_speakers"])
            entry["speaker_id_map"] = config.get(
                "speaker_id_map", entry["speaker_id_map"]
            )
        result.append(entry)
        seen.add(key)

    # Voices on disk that are not part of the official catalog (custom models)
    for key, config in installed.items():
        if key in seen:
            continue
        language = config.get("language", {})
        inference = config.get("inference", {})
        result.append(
            {
                "key": key,
                "name": config.get("dataset", key),
                "language": {
                    "code": language.get("code", ""),
                    "family": language.get("family", ""),
                    "name_english": language.get("name_english", "Custom"),
                    "country_english": language.get("country_english", ""),
                },
                "quality": config.get("audio", {}).get("quality", "custom"),
                "num_speakers": config.get("num_speakers", 1),
                "speaker_id_map": config.get("speaker_id_map", {}),
                "installed": True,
                "downloading": False,
                "local_only": True,
                "sample_rate": config.get("audio", {}).get("sample_rate"),
                "defaults": {
                    "length_scale": inference.get("length_scale", 1.0),
                    "noise_scale": inference.get("noise_scale", 0.667),
                    "noise_w_scale": inference.get("noise_w", 0.8),
                },
            }
        )

    result.sort(
        key=lambda voice: (
            voice["language"].get("name_english", ""),
            voice["language"].get("country_english", ""),
            voice["name"],
            voice["quality"],
        )
    )
    return result


def synthesize_wav_bytes(
    voice: PiperVoice,
    text: str,
    syn_config: SynthesisConfig,
    sentence_silence: float,
) -> bytes:
    with io.BytesIO() as wav_io:
        wav_file: wave.Wave_write = wave.open(wav_io, "wb")
        with wav_file:
            wav_params_set = False
            for i, audio_chunk in enumerate(voice.synthesize(text, syn_config)):
                if not wav_params_set:
                    wav_file.setframerate(audio_chunk.sample_rate)
                    wav_file.setsampwidth(audio_chunk.sample_width)
                    wav_file.setnchannels(audio_chunk.sample_channels)
                    wav_params_set = True

                if i > 0 and sentence_silence > 0:
                    silence_frames = int(
                        voice.config.sample_rate * sentence_silence
                    )
                    wav_file.writeframes(bytes(silence_frames * 2))

                wav_file.writeframes(audio_chunk.audio_int16_bytes)

        return wav_io.getvalue()


def request_json() -> Dict[str, Any]:
    """Parse the request body as JSON regardless of Content-Type.

    A non-JSON body is treated as plain text to synthesize.
    """
    raw = request.get_data(parse_form_data=False, cache=True)
    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        return {"text": raw.decode("utf-8", errors="replace")}
    if not isinstance(data, dict):
        return {"text": str(data)}
    return data


def create_app(manager: VoiceManager, args: argparse.Namespace) -> Flask:
    app = Flask(__name__)
    synth_lock = threading.Lock()

    @app.after_request
    def add_cors_headers(response: Response) -> Response:
        response.headers["Access-Control-Allow-Origin"] = "*"
        response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
        response.headers["Access-Control-Allow-Headers"] = "Content-Type"
        return response

    # ----- web UI ----------------------------------------------------------

    @app.route("/", methods=["GET"])
    def app_index() -> Response:
        return send_from_directory(STATIC_DIR, "index.html")

    # ----- JSON API used by the UI ------------------------------------------

    @app.route("/api/voices", methods=["GET"])
    def api_voices() -> Response:
        force = request.args.get("refresh") == "1"
        if force:
            manager.catalog(force_refresh=True)
        return jsonify({"voices": build_voice_list(manager)})

    @app.route("/api/download", methods=["POST"])
    def api_download() -> Response:
        data = request_json()
        model_id = data.get("voice")
        if not model_id:
            return jsonify({"error": "voice is required"}), 400
        try:
            manager.download(model_id, bool(data.get("force_redownload")))
        except Exception as err:
            _LOGGER.exception("Download failed for %s", model_id)
            return jsonify({"error": f"download failed: {err}"}), 502
        return jsonify({"voice": model_id, "installed": True})

    @app.route("/api/synthesize", methods=["POST", "OPTIONS"])
    def api_synthesize() -> Response:
        if request.method == "OPTIONS":
            return Response(status=204)
        data = request_json()
        try:
            wav_bytes = _synthesize(data)
        except FileNotFoundError as err:
            return jsonify({"error": f"voice not installed: {err}"}), 404
        except ValueError as err:
            return jsonify({"error": str(err)}), 400
        except Exception as err:
            _LOGGER.exception("Synthesis failed")
            return jsonify({"error": f"synthesis failed: {err}"}), 500
        return Response(wav_bytes, mimetype="audio/wav")

    # ----- OpenAI-compatible audio API ---------------------------------------
    # Matches clients that speak the OpenAI /v1/audio dialect, e.g. the
    # UltraAI "local" TextToSpeech adapter, LocalAI clients, etc.

    @app.route("/v1/audio/voices", methods=["GET"])
    def openai_voices() -> Response:
        voices = []
        for entry in build_voice_list(manager):
            language = entry["language"]
            bits = [entry["quality"]]
            if entry["num_speakers"] > 1:
                bits.append(f"{entry['num_speakers']} speakers")
            bits.append("installed" if entry["installed"] else "downloads on first use")
            voices.append(
                {
                    "id": entry["key"],
                    "name": f"{entry['name']} ({entry['quality']})",
                    "language": (language.get("code") or "").replace("_", "-"),
                    "gender": "",
                    "description": ", ".join(bits),
                }
            )
        return jsonify({"voices": voices})

    @app.route("/v1/audio/speech", methods=["POST", "OPTIONS"])
    def openai_speech() -> Response:
        if request.method == "OPTIONS":
            return Response(status=204)
        data = request_json()

        text = (data.get("input") or data.get("text") or "").strip()
        if not text:
            return jsonify({"error": "input is required"}), 400

        # voice: "en_US-bryce-medium" or "en_US-libritts_r-medium:p3922"
        # (speaker name or numeric id after the colon). Falls back to the
        # "model" field when it looks like a Piper voice key.
        voice_id = data.get("voice") or ""
        model = data.get("model") or ""
        if not voice_id and "-" in model:
            voice_id = model
        if not voice_id and args.model:
            voice_id = args.model
        if not voice_id:
            return jsonify({"error": "voice is required"}), 400

        speaker: Optional[str] = None
        if ":" in voice_id:
            voice_id, speaker = voice_id.split(":", 1)

        if manager.find_model(voice_id) is None:
            try:
                _LOGGER.info("Voice %s not installed, downloading", voice_id)
                manager.download(voice_id)
            except Exception as err:
                return jsonify({"error": f"unknown voice {voice_id}: {err}"}), 404

        response_format = (data.get("response_format") or "wav").lower()
        if response_format != "wav" and not _FFMPEG:
            return (
                jsonify(
                    {
                        "error": f"response_format '{response_format}' needs ffmpeg "
                        "on the server; only 'wav' is available"
                    }
                ),
                400,
            )

        synth_request: Dict[str, Any] = {"text": text, "voice": voice_id}
        speed = data.get("speed")
        if speed:
            synth_request["length_scale"] = 1.0 / float(speed)
        if speaker is not None:
            if speaker.isdigit():
                synth_request["speaker_id"] = int(speaker)
            else:
                synth_request["speaker"] = speaker

        try:
            wav_bytes = _synthesize(synth_request)
        except FileNotFoundError as err:
            return jsonify({"error": f"voice not installed: {err}"}), 404
        except ValueError as err:
            return jsonify({"error": str(err)}), 400

        if response_format == "wav":
            return Response(wav_bytes, mimetype="audio/wav")
        return _transcode(wav_bytes, response_format)

    def _transcode(wav_bytes: bytes, fmt: str) -> Response:
        mimetypes = {
            "mp3": "audio/mpeg",
            "opus": "audio/ogg",
            "ogg": "audio/ogg",
            "flac": "audio/flac",
            "aac": "audio/aac",
        }
        if fmt not in mimetypes:
            return jsonify({"error": f"unsupported response_format: {fmt}"}), 400
        proc = subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-i", "pipe:0", "-f", fmt, "pipe:1"],
            input=wav_bytes,
            capture_output=True,
        )
        if proc.returncode != 0:
            return (
                jsonify({"error": f"ffmpeg failed: {proc.stderr.decode(errors='replace')[:200]}"}),
                500,
            )
        return Response(proc.stdout, mimetype=mimetypes[fmt])

    # ----- stock piper.http_server compatible API ---------------------------

    @app.route("/voices", methods=["GET"])
    def app_installed_voices() -> Dict[str, Any]:
        return manager.installed_voices()

    @app.route("/all-voices", methods=["GET"])
    def app_all_voices() -> Dict[str, Any]:
        return manager.catalog()

    @app.route("/download", methods=["POST"])
    def app_download() -> str:
        data = request_json()
        model_id = data.get("voice")
        if not model_id:
            raise ValueError("voice is required")
        manager.download(model_id, bool(data.get("force_redownload")))
        return model_id

    @app.route("/", methods=["POST"])
    def app_synthesize() -> Response:
        data = request_json()
        try:
            wav_bytes = _synthesize(data)
        except FileNotFoundError as err:
            return jsonify({"error": f"voice not installed: {err}"}), 404
        except ValueError as err:
            return jsonify({"error": str(err)}), 400
        return Response(wav_bytes, mimetype="audio/wav")

    # ----- shared synthesis helper ------------------------------------------

    def _synthesize(data: Dict[str, Any]) -> bytes:
        text = (data.get("text") or "").strip()
        if not text:
            raise ValueError("No text provided")

        model_id = data.get("voice") or args.model
        if not model_id:
            raise ValueError("No voice selected")

        voice = manager.get_voice(model_id)

        speaker_id: Optional[int] = data.get("speaker_id")
        if speaker_id is not None:
            speaker_id = int(speaker_id)
        elif voice.config.num_speakers > 1:
            speaker = data.get("speaker")
            if speaker:
                speaker_id = voice.config.speaker_id_map.get(speaker)
            if speaker_id is None:
                speaker_id = args.speaker or 0
        if (speaker_id is not None) and (speaker_id >= voice.config.num_speakers):
            speaker_id = 0

        def _param(name: str, arg_value: Optional[float], config_value: float) -> float:
            value = data.get(name)
            if value is None:
                value = arg_value if arg_value is not None else config_value
            return float(value)

        syn_config = SynthesisConfig(
            speaker_id=speaker_id,
            length_scale=_param(
                "length_scale", args.length_scale, voice.config.length_scale
            ),
            noise_scale=_param(
                "noise_scale", args.noise_scale, voice.config.noise_scale
            ),
            noise_w_scale=_param(
                "noise_w_scale", args.noise_w_scale, voice.config.noise_w_scale
            ),
            volume=float(data.get("volume", 1.0)),
            normalize_audio=bool(data.get("normalize_audio", True)),
        )
        sentence_silence = float(
            data.get("sentence_silence", args.sentence_silence)
        )

        _LOGGER.info(
            "Synthesizing %d chars with %s (speaker=%s)",
            len(text),
            model_id,
            speaker_id,
        )
        # onnxruntime sessions are not guaranteed thread-safe per voice
        with synth_lock:
            return synthesize_wav_bytes(voice, text, syn_config, sentence_silence)

    return app


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0", help="HTTP server host")
    parser.add_argument("--port", type=int, default=5000, help="HTTP server port")
    parser.add_argument(
        "-m",
        "--model",
        help="Default voice (e.g. en_US-lessac-medium); optional, "
        "downloaded automatically if missing",
    )
    parser.add_argument("-s", "--speaker", type=int, help="Default speaker id")
    parser.add_argument(
        "--length-scale", "--length_scale", type=float, help="Phoneme length"
    )
    parser.add_argument(
        "--noise-scale", "--noise_scale", type=float, help="Generator noise"
    )
    parser.add_argument(
        "--noise-w-scale",
        "--noise_w_scale",
        "--noise-w",
        "--noise_w",
        type=float,
        help="Phoneme width noise",
    )
    parser.add_argument(
        "--sentence-silence",
        "--sentence_silence",
        type=float,
        default=0.0,
        help="Seconds of silence after each sentence",
    )
    parser.add_argument("--cuda", action="store_true", help="Use GPU")
    parser.add_argument(
        "--data-dir",
        "--data_dir",
        action="append",
        default=[],
        help="Data directory to check for downloaded models "
        "(default: current directory)",
    )
    parser.add_argument(
        "--download-dir",
        "--download_dir",
        help="Path to download voices (default: first data dir)",
    )
    parser.add_argument(
        "--max-loaded-voices",
        type=int,
        default=3,
        help="Maximum number of voices kept in memory (default: 3)",
    )
    parser.add_argument(
        "--debug", action="store_true", help="Print DEBUG messages to console"
    )
    args = parser.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.debug else logging.INFO)

    data_dirs = [Path(d) for d in (args.data_dir or [str(Path.cwd())])]
    download_dir = Path(args.download_dir) if args.download_dir else data_dirs[0]
    download_dir.mkdir(parents=True, exist_ok=True)

    manager = VoiceManager(
        data_dirs=data_dirs,
        download_dir=download_dir,
        use_cuda=args.cuda,
        max_loaded=args.max_loaded_voices,
    )

    if args.model:
        model_id = _strip_suffix(Path(args.model).name, ".onnx")
        if manager.find_model(model_id) is None:
            _LOGGER.info("Downloading default voice %s", model_id)
            manager.download(model_id)
        manager.get_voice(model_id)
        args.model = model_id

    app = create_app(manager, args)
    _LOGGER.info("Listening on http://%s:%s", args.host, args.port)
    app.run(host=args.host, port=args.port, threaded=True)


if __name__ == "__main__":
    main()

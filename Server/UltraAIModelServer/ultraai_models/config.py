"""Settings, from the environment (or the EnvironmentFile systemd loads).

The super administrator's token lives here and nowhere else: never in the
database (whose accounts hold only hashes), never in a client."""

from __future__ import annotations

import os
from dataclasses import dataclass


@dataclass
class Settings:
    database: str = "ultraai-models.sqlite3"
    # Where uploaded model files are kept: <data_dir>/blobs/<aa>/<sha256>,
    # and <data_dir>/tmp for uploads in flight (same file system, so the
    # finished upload is a rename).
    data_dir: str = "ultraai-models-data"
    host: str = "127.0.0.1"
    port: int = 8793
    # Where the service is reached from the internet. An uploaded file's URL
    # in catalogue.json is <base>/files/<sha256>/<name>.
    public_base_url: str = "http://127.0.0.1:8793"
    # The super administrator. Empty: the admin API answers 503 for every
    # request that is not made with an account's token, and no account can be
    # made - set it before the first start.
    super_token: str = ""
    # The largest upload accepted, in bytes (a 70B model in Q4 is ~40 GB).
    max_upload_bytes: int = 80 * 1024 ** 3
    # Fill an empty database with the framework's built-in gallery, catalogue
    # and topics on the first start, so a new server publishes what a client
    # already has and the administrator edits from there.
    seed: bool = True

    @classmethod
    def from_environment(cls) -> "Settings":
        s = cls()
        env = os.environ.get
        s.database = env("ULTRAAI_MODELS_DB", s.database)
        s.data_dir = env("ULTRAAI_MODELS_DATA", s.data_dir)
        s.host = env("ULTRAAI_MODELS_HOST", s.host)
        s.port = int(env("ULTRAAI_MODELS_PORT", s.port))
        s.public_base_url = env("ULTRAAI_MODELS_BASE_URL", s.public_base_url).rstrip("/")
        s.super_token = env("ULTRAAI_MODELS_SUPER_TOKEN", "").strip()
        s.max_upload_bytes = int(env("ULTRAAI_MODELS_MAX_UPLOAD", s.max_upload_bytes))
        s.seed = env("ULTRAAI_MODELS_SEED", "1").strip().lower() not in ("0", "false", "no", "off")
        return s

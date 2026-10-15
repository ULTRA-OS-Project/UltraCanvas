"""`python -m oauth_callback [--host H] [--port P]` runs the service with
uvicorn, one worker (the sessions live in memory)."""

from __future__ import annotations

import argparse
import logging

import uvicorn


def main() -> None:
    parser = argparse.ArgumentParser(description="ULTRA OS OAuth sign-in service")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--log-level", default="info")
    args = parser.parse_args()
    logging.basicConfig(level=args.log_level.upper(),
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    uvicorn.run("oauth_callback.app:app", host=args.host, port=args.port,
                workers=1, proxy_headers=True, forwarded_allow_ips="127.0.0.1",
                log_level=args.log_level)


if __name__ == "__main__":
    main()

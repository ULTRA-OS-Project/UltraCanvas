"""A per-address sliding-window rate limiter, in memory."""

from __future__ import annotations

import time
from collections import deque


class RateLimiter:
    def __init__(self, per_minute: int):
        self.per_minute = per_minute
        self._hits: dict[str, deque[float]] = {}

    def allow(self, key: str, now: float | None = None) -> bool:
        if self.per_minute <= 0:
            return True
        now = time.time() if now is None else now
        window = self._hits.setdefault(key, deque())
        cutoff = now - 60.0
        while window and window[0] <= cutoff:
            window.popleft()
        if len(window) >= self.per_minute:
            return False
        window.append(now)
        # Keep the table from growing with every address that ever called.
        if len(self._hits) > 50000:
            for k in [k for k, w in self._hits.items() if not w or w[-1] <= cutoff]:
                self._hits.pop(k, None)
        return True

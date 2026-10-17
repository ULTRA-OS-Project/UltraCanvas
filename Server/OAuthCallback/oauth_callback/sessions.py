"""In-flight sign-ins, in memory.

A session is created when an application starts a sign-in and lives until
the application has picked up the authorization code (or the provider's
refusal), or until it expires. Nothing is written to disk and no token ever
passes through here: the session holds the code, which is useless without
the PKCE verifier the application kept.

One process holds the store, so the service runs as one worker; a second
worker would not see the first one's sessions.
"""

from __future__ import annotations

import asyncio
import secrets
import time
from dataclasses import dataclass, field


@dataclass
class Session:
    session_id: str
    state: str
    provider_id: str
    code_challenge: str
    created_at: float
    expires_at: float
    # Filled by the provider's redirect.
    code: str = ""
    error: str = ""
    error_description: str = ""
    answered: asyncio.Event = field(default_factory=asyncio.Event)

    @property
    def is_answered(self) -> bool:
        return bool(self.code or self.error)


class SessionStore:
    def __init__(self, session_ttl: int, result_ttl: int, max_sessions: int = 10000):
        self.session_ttl = session_ttl
        self.result_ttl = result_ttl
        self.max_sessions = max_sessions
        self._by_id: dict[str, Session] = {}
        self._by_state: dict[str, Session] = {}

    def __len__(self) -> int:
        return len(self._by_id)

    def create(self, provider_id: str, code_challenge: str, now: float | None = None) -> Session:
        now = time.time() if now is None else now
        self.expire(now)
        if len(self._by_id) >= self.max_sessions:
            raise OverflowError("too many sign-ins in flight")
        session = Session(
            session_id=secrets.token_urlsafe(32),
            state=secrets.token_urlsafe(32),
            provider_id=provider_id,
            code_challenge=code_challenge,
            created_at=now,
            expires_at=now + self.session_ttl,
        )
        self._by_id[session.session_id] = session
        self._by_state[session.state] = session
        return session

    def get(self, session_id: str, now: float | None = None) -> Session | None:
        now = time.time() if now is None else now
        session = self._by_id.get(session_id)
        if session is None or session.expires_at <= now:
            if session is not None:
                self._drop(session)
            return None
        return session

    def answer(self, provider_id: str, state: str, code: str, error: str,
               error_description: str, now: float | None = None) -> Session | None:
        """Record the provider's redirect for the session `state` names.
        None when no such sign-in is waiting (expired, unknown, already
        answered, or the state belongs to another provider's sign-in)."""
        now = time.time() if now is None else now
        session = self._by_state.get(state)
        if session is None or session.expires_at <= now or session.is_answered \
                or session.provider_id != provider_id:
            return None
        session.code = code
        session.error = error
        session.error_description = error_description
        # The answer waits for the application only briefly.
        session.expires_at = min(session.expires_at, now + self.result_ttl)
        session.answered.set()
        return session

    def take(self, session: Session) -> None:
        """The application has the answer; forget the session."""
        self._drop(session)

    def expire(self, now: float | None = None) -> int:
        now = time.time() if now is None else now
        dead = [s for s in self._by_id.values() if s.expires_at <= now]
        for s in dead:
            self._drop(s)
        return len(dead)

    def _drop(self, session: Session) -> None:
        self._by_id.pop(session.session_id, None)
        self._by_state.pop(session.state, None)
        # Wake a poller still waiting, so it sees the session is gone.
        session.answered.set()

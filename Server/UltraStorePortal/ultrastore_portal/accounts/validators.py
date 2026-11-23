"""The longest password the portal takes. Django sets a shortest one and no
longest; a password is hashed whatever its length, so the bound is not for
the hash but for the person and the forms: something pasted by mistake is
not accepted as a password.

The length is counted in characters, not in bytes: a password is Unicode
text (the pages and the forms are UTF-8), and "ä" or "€" is one character
of it however many bytes carry it.

It is a rule for a password being chosen - sign-up, a reset, a change,
createsuperuser - and never for one being typed to sign in: an account
whose password is older than the rule keeps it.

Version: 0.3.0
"""

from __future__ import annotations

from django.core.exceptions import ValidationError


class MaximumLengthValidator:
    def __init__(self, max_length: int = 64):
        self.max_length = max_length

    def validate(self, password: str, user=None) -> None:
        if len(password) > self.max_length:
            raise ValidationError(
                f"This password is too long. It must contain at most {self.max_length} characters.",
                code="password_too_long", params={"max_length": self.max_length})

    def get_help_text(self) -> str:
        return f"Your password must contain at most {self.max_length} characters."

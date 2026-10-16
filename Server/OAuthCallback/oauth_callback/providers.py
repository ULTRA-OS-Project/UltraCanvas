"""The provider table: endpoints, scopes and consent parameters per provider.

This is the server-side twin of OAuthConfigFor() in
Apps/UltraMail/engine/UltraMailOAuth.cpp and of the OAuthConfig() of
UltraCloud's providers: the same endpoints and the same scopes, so an
account signed in either way gets a token the mail servers and the cloud
APIs accept. The registration behind it (client id, secret) comes from the
settings.
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class Provider:
    id: str
    display_name: str
    authorization_endpoint: str
    token_endpoint: str
    # What a sign-in asks for when the application names no scope.
    default_scopes: tuple[str, ...]
    # Every scope an application may ask for through this service. A sign-in
    # with any other scope is refused: the consent page carries our name, so
    # it must not be usable to ask a user for anything we did not decide on.
    allowed_scopes: frozenset[str]
    # Extra query parameters on the consent URL.
    extra_auth_params: dict[str, str] = field(default_factory=dict)
    # Where the client secret goes on the token endpoint: the form body
    # (Google, Microsoft) or HTTP Basic (Yahoo, RFC 6749 section 2.3.1).
    secret_in_body: bool = True
    # Whether the token request repeats the scopes: Microsoft wants them.
    scopes_on_token_request: bool = False

    def scopes_for(self, requested: str) -> tuple[str, ...]:
        """The scopes to ask for, from a space-separated request. Raises
        ValueError on a scope outside allowed_scopes."""
        if not requested.strip():
            return self.default_scopes
        out: list[str] = []
        for scope in requested.split():
            if scope not in self.allowed_scopes:
                raise ValueError(f"scope {scope!r} is not offered for {self.id}")
            if scope not in out:
                out.append(scope)
        return tuple(out)


GOOGLE_MAIL = "https://mail.google.com/"
GOOGLE_DRIVE = "https://www.googleapis.com/auth/drive"
GOOGLE_DRIVE_FILE = "https://www.googleapis.com/auth/drive.file"
MICROSOFT_IMAP = "https://outlook.office.com/IMAP.AccessAsUser.All"
MICROSOFT_SMTP = "https://outlook.office.com/SMTP.Send"
MICROSOFT_FILES = "Files.ReadWrite"
MICROSOFT_USER = "User.Read"
OFFLINE_ACCESS = "offline_access"
DROPBOX_SCOPES = ("account_info.read", "files.metadata.read", "files.content.read",
                  "files.content.write", "sharing.read", "sharing.write")

PROVIDERS: dict[str, Provider] = {
    "google": Provider(
        id="google",
        display_name="Google",
        authorization_endpoint="https://accounts.google.com/o/oauth2/v2/auth",
        token_endpoint="https://oauth2.googleapis.com/token",
        # The full-mailbox scope is what IMAP/SMTP XOAUTH2 requires.
        default_scopes=(GOOGLE_MAIL,),
        # Drive is what UltraCloud's Google Drive sign-in asks for, through
        # the same registration (its consent screen lists both).
        allowed_scopes=frozenset({GOOGLE_MAIL, GOOGLE_DRIVE, GOOGLE_DRIVE_FILE}),
        # A refresh token is only issued for offline access and - on a repeat
        # consent - only when consent is asked for again.
        extra_auth_params={"access_type": "offline", "prompt": "consent"},
        secret_in_body=True,
    ),
    "microsoft": Provider(
        id="microsoft",
        display_name="Microsoft",
        # "common" tenant: personal accounts (outlook.com, hotmail.com, ...)
        # and every Microsoft 365 organisation.
        authorization_endpoint="https://login.microsoftonline.com/common/oauth2/v2.0/authorize",
        token_endpoint="https://login.microsoftonline.com/common/oauth2/v2.0/token",
        # The Outlook resource scopes IMAP and SMTP XOAUTH2 accept;
        # offline_access is what yields a refresh token.
        default_scopes=(MICROSOFT_IMAP, MICROSOFT_SMTP, OFFLINE_ACCESS),
        # Files and User.Read are what UltraCloud's OneDrive sign-in asks for.
        allowed_scopes=frozenset({MICROSOFT_IMAP, MICROSOFT_SMTP, MICROSOFT_FILES, MICROSOFT_USER,
                                  OFFLINE_ACCESS}),
        extra_auth_params={"prompt": "select_account"},
        secret_in_body=True,
        scopes_on_token_request=True,
    ),
    "yahoo": Provider(
        id="yahoo",
        display_name="Yahoo",
        authorization_endpoint="https://api.login.yahoo.com/oauth2/request_auth",
        token_endpoint="https://api.login.yahoo.com/oauth2/get_token",
        # No scope parameter: Yahoo rejects OAuth2 scope strings on the auth
        # request (invalid_scope) and grants the API permissions enabled on
        # the app instead, so Mail (read/write) must be ticked in the Yahoo
        # console.
        default_scopes=(),
        allowed_scopes=frozenset(),
        extra_auth_params={},
        # Yahoo authenticates the client with HTTP Basic on get_token.
        secret_in_body=False,
    ),
    "dropbox": Provider(
        id="dropbox",
        display_name="Dropbox",
        authorization_endpoint="https://www.dropbox.com/oauth2/authorize",
        token_endpoint="https://api.dropboxapi.com/oauth2/token",
        # What UltraCloud's Dropbox provider asks for: files, metadata and
        # sharing, for the composer's cloud links.
        default_scopes=DROPBOX_SCOPES,
        allowed_scopes=frozenset(DROPBOX_SCOPES),
        # A refresh token is only issued for offline token access.
        extra_auth_params={"token_access_type": "offline"},
        secret_in_body=True,
    ),
}

"""The ULTRA OS OAuth sign-in service.

A small FastAPI application that stands between a desktop application
(UltraMail) and the OAuth2 providers of mail accounts (Google, Microsoft,
Yahoo). It holds the provider registrations - the client id and the client
secret - so the desktop application ships none, and it is the public https
address the providers redirect to, which Yahoo insists on (no loopback
redirect) and which a desktop application cannot be.

See README.md for the protocol and the deployment.
"""

__version__ = "1.0.0"

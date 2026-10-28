// Apps/UltraMail/engine/UltraMailSecureMime.cpp
// See UltraMailSecureMime.h. The MIME of the secure forms - RFC 1847
// multipart/signed and multipart/encrypted, RFC 8551 application/pkcs7-mime,
// RFC 3156 PGP/MIME, inline PGP - taken apart for reading and put together
// for sending; the cryptography is the backends' (UltraMailSecureBackends.h).
// Version: 0.2.0 - OpenPGP signs with the account's chosen key (SecureKeys::openPgpSigningKeyFor)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSecureMime.h"
#include "UltraMailSecureBackends.h"

#include "UltraNet/UltraNetMime.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <algorithm>
#include <cctype>

namespace UltraMail {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The line break `text` uses: CRLF when it has one.
std::string LineBreakOf(const std::string& text) {
    return text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
}

// The header lines of a block, each with its folded continuations.
std::vector<std::string> HeaderFields(const std::string& headers) {
    std::vector<std::string> fields;
    size_t at = 0;
    while (at < headers.size()) {
        size_t end = headers.find('\n', at);
        if (end == std::string::npos) end = headers.size();
        std::string line = headers.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t') && !fields.empty())
            fields.back() += "\n" + line;
        else if (!line.empty())
            fields.push_back(line);
        at = end + 1;
    }
    return fields;
}

std::string FieldName(const std::string& field) {
    const size_t colon = field.find(':');
    return colon == std::string::npos ? std::string() : Lower(Trim(field.substr(0, colon)));
}

std::string Boundary(const std::string& kind) {
    std::vector<uint8_t> random;
    UltraCrypt_RandomBytes(random, 12);
    return "----=_UltraMail_" + kind + "_" + UltraCrypt_ToHex(random);
}

// The address in a From header, lowercased: "Erika <e@x.de>" -> "e@x.de".
std::string AddressOf(const std::string& value) {
    const size_t open = value.rfind('<');
    const size_t close = value.rfind('>');
    std::string address = open != std::string::npos && close != std::string::npos && close > open
                              ? value.substr(open + 1, close - open - 1)
                              : value;
    return Lower(Trim(address));
}

// The From address of a whole message.
std::string FromOf(const std::string& raw) {
    std::string headers, body;
    if (!SplitEntity(raw, headers, body)) return std::string();
    return AddressOf(UltraNet_MimeDecodeHeader(EntityHeader(headers, "From")));
}

std::string ContentTypeOf(const std::string& entity) {
    std::string headers, body;
    if (!SplitEntity(entity, headers, body)) return "text/plain";
    const std::string ct = EntityHeader(headers, "Content-Type");
    return ct.empty() ? "text/plain" : ct;
}

// Does the signature's signer stand for `from`? S/MIME: any address its
// certificate is for; OpenPGP: the key's user id.
void MarkFrom(SignatureCheck& check, const std::string& from) {
    if (from.empty()) return;
    if (check.method == SecureMethod::Smime && !check.certificatePem.empty()) {
        CertificateInfo info;
        if (ReadCertificate(check.certificatePem, info)) {
            for (const std::string& e : info.emails)
                if (e == from) {
                    check.matchesFrom = true;
                    check.signerEmail = e;
                }
        }
    }
    if (!check.signerEmail.empty() && check.signerEmail == from) check.matchesFrom = true;
}

// A body in the transfer encoding that keeps it intact on any mail path: an
// 8bit or binary text body becomes quoted-printable, recursively through
// multiparts. A signature is made over these bytes, so nothing on the way
// may rewrite them.
std::string SevenBitEntity(const std::string& entity) {
    std::string headers, body;
    if (!SplitEntity(entity, headers, body)) return entity;
    const std::string ct = EntityHeader(headers, "Content-Type");
    if (Lower(MediaType(ct)).rfind("multipart/", 0) == 0) {
        const std::string boundary = MediaTypeParameter(ct, "boundary");
        const std::vector<std::string> parts = MultipartParts(body, boundary);
        if (boundary.empty() || parts.empty()) return entity;
        std::string out = headers + "\r\n\r\n";
        for (const std::string& part : parts) out += "--" + boundary + "\r\n" + SevenBitEntity(part) + "\r\n";
        out += "--" + boundary + "--\r\n";
        return out;
    }
    const std::string cte = Lower(Trim(EntityHeader(headers, "Content-Transfer-Encoding")));
    const bool eightBit = std::any_of(body.begin(), body.end(), [](char c) { return (c & 0x80) != 0; });
    bool longLine = false;
    for (size_t at = 0, n = 0; at < body.size(); ++at) {
        n = body[at] == '\n' ? 0 : n + 1;
        if (n > 76) { longLine = true; break; }
    }
    if ((cte == "base64" || cte == "quoted-printable") || (!eightBit && !longLine)) return entity;
    std::string kept;
    for (const std::string& field : HeaderFields(headers))
        if (FieldName(field) != "content-transfer-encoding") kept += field + "\r\n";
    return kept + "Content-Transfer-Encoding: quoted-printable\r\n\r\n" + UltraNet_QuotedPrintableEncode(body);
}

std::string Base64Lines(const std::vector<uint8_t>& bytes) {
    std::string text = UltraNet_Base64Encode(bytes, /*wrap76Cols=*/true);
    if (!text.empty() && text.back() != '\n') text += "\r\n";
    return text;
}

// The armored block in `text` from its BEGIN line to its END line, or "".
std::string ArmoredBlock(const std::string& text, const std::string& kind, size_t* start = nullptr,
                         size_t* end = nullptr) {
    const std::string begin = "-----BEGIN PGP " + kind + "-----";
    const std::string stop = kind == "SIGNED MESSAGE" ? "-----END PGP SIGNATURE-----" : "-----END PGP " + kind + "-----";
    const size_t a = text.find(begin);
    if (a == std::string::npos) return std::string();
    const size_t b = text.find(stop, a);
    if (b == std::string::npos) return std::string();
    const size_t e = b + stop.size();
    if (start) *start = a;
    if (end) *end = e;
    return text.substr(a, e - a);
}

std::string TextEntity(const std::string& text) {
    return "Content-Type: text/plain; charset=utf-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n" + text;
}

} // namespace

const char* SecureMethodName(SecureMethod method) {
    switch (method) {
        case SecureMethod::Smime:   return "S/MIME";
        case SecureMethod::OpenPgp: return "OpenPGP";
        case SecureMethod::Plain:    break;
    }
    return "";
}

bool SmimeAvailable() { return secure::SmimeBuilt(); }
bool OpenPgpAvailable() { return secure::OpenPgpBuilt(); }
std::vector<OpenPgpKey> OpenPgpSecretKeys() { return secure::PgpSecretKeys(); }

// ---- raw MIME --------------------------------------------------------------

bool SplitEntity(const std::string& entity, std::string& headers, std::string& body) {
    // The header block may be empty: an entity that starts with its blank line.
    if (entity.rfind("\r\n", 0) == 0) { headers.clear(); body = entity.substr(2); return true; }
    if (entity.rfind("\n", 0) == 0) { headers.clear(); body = entity.substr(1); return true; }
    const size_t crlf = entity.find("\r\n\r\n");
    const size_t lf = entity.find("\n\n");
    if (crlf == std::string::npos && lf == std::string::npos) return false;
    if (crlf != std::string::npos && (lf == std::string::npos || crlf < lf)) {
        headers = entity.substr(0, crlf);
        body = entity.substr(crlf + 4);
    } else {
        headers = entity.substr(0, lf);
        if (!headers.empty() && headers.back() == '\r') headers.pop_back();
        body = entity.substr(lf + 2);
    }
    return true;
}

std::string EntityHeader(const std::string& headers, const std::string& name) {
    const std::string wanted = Lower(name);
    for (const std::string& field : HeaderFields(headers)) {
        if (FieldName(field) != wanted) continue;
        std::string value = field.substr(field.find(':') + 1);
        std::string unfolded;
        for (char c : value) unfolded += (c == '\n' || c == '\r') ? ' ' : c;
        return Trim(unfolded);
    }
    return std::string();
}

std::string MediaType(const std::string& contentType) {
    return Lower(Trim(contentType.substr(0, contentType.find(';'))));
}

std::string MediaTypeParameter(const std::string& contentType, const std::string& name) {
    const std::string wanted = Lower(name);
    size_t at = contentType.find(';');
    while (at != std::string::npos && at < contentType.size()) {
        ++at;
        const size_t eq = contentType.find('=', at);
        if (eq == std::string::npos) break;
        const std::string key = Lower(Trim(contentType.substr(at, eq - at)));
        size_t v = eq + 1;
        while (v < contentType.size() && std::isspace(static_cast<unsigned char>(contentType[v]))) ++v;
        std::string value;
        size_t next;
        if (v < contentType.size() && contentType[v] == '"') {
            size_t q = v + 1;
            while (q < contentType.size() && contentType[q] != '"') {
                if (contentType[q] == '\\' && q + 1 < contentType.size()) ++q;
                value += contentType[q++];
            }
            next = contentType.find(';', q);
        } else {
            next = contentType.find(';', v);
            value = Trim(contentType.substr(v, next == std::string::npos ? std::string::npos : next - v));
        }
        if (key == wanted) return value;
        at = next;
    }
    return std::string();
}

std::vector<std::string> MultipartParts(const std::string& body, const std::string& boundary) {
    std::vector<std::string> parts;
    if (boundary.empty()) return parts;
    const std::string delimiter = "--" + boundary;
    // Every boundary line: where its line break before starts (the break
    // belongs to it), and where the content after its own line begins.
    struct Line { size_t breakAt; size_t contentAt; bool closing; };
    std::vector<Line> lines;
    size_t at = 0;
    while (at <= body.size()) {
        const size_t lineStart = at;
        size_t end = body.find('\n', at);
        if (end == std::string::npos) end = body.size();
        if (body.compare(lineStart, delimiter.size(), delimiter) == 0) {
            const std::string rest = Trim(body.substr(lineStart + delimiter.size(), end - lineStart - delimiter.size()));
            if (rest.empty() || rest == "--") {
                size_t breakAt = lineStart;
                if (breakAt > 0 && body[breakAt - 1] == '\n') {
                    --breakAt;
                    if (breakAt > 0 && body[breakAt - 1] == '\r') --breakAt;
                }
                lines.push_back({breakAt, end < body.size() ? end + 1 : body.size(), rest == "--"});
                if (rest == "--") break;
            }
        }
        if (end >= body.size()) break;
        at = end + 1;
    }
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
        if (lines[i].closing) break;
        const size_t from = lines[i].contentAt;
        const size_t to = std::max(from, lines[i + 1].breakAt);
        parts.push_back(body.substr(from, to - from));
    }
    return parts;
}

std::string CanonicalCrlf(const std::string& text) {
    std::string out;
    out.reserve(text.size() + text.size() / 32);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) out += '\r';
        out += text[i];
    }
    return out;
}

std::string DecodedBody(const std::string& part) {
    std::string headers, body;
    if (!SplitEntity(part, headers, body)) return part;
    const std::string cte = Lower(Trim(EntityHeader(headers, "Content-Transfer-Encoding")));
    if (cte == "base64") {
        std::vector<uint8_t> bytes;
        if (UltraNet_Base64Decode(body, bytes)) return std::string(bytes.begin(), bytes.end());
        return std::string();
    }
    if (cte == "quoted-printable") return UltraNet_QuotedPrintableDecode(body);
    return body;
}

std::string ReplaceContent(const std::string& outer, const std::string& entity) {
    std::string outerHeaders, outerBody;
    if (!SplitEntity(outer, outerHeaders, outerBody)) return entity;
    const std::string nl = LineBreakOf(outer);
    std::string out;
    for (const std::string& field : HeaderFields(outerHeaders)) {
        const std::string name = FieldName(field);
        if (name.rfind("content-", 0) == 0 || name == "mime-version") continue;
        std::string line;
        for (char c : field) {
            if (c == '\n') line += nl;
            else line += c;
        }
        out += line + nl;
    }
    out += "MIME-Version: 1.0" + nl;
    std::string headers, body;
    if (!SplitEntity(entity, headers, body)) {
        headers = "Content-Type: text/plain";
        body = entity;
    }
    for (const std::string& field : HeaderFields(headers)) {
        std::string line;
        for (char c : field) {
            if (c == '\n') line += nl;
            else line += c;
        }
        out += line + nl;
    }
    return out + nl + body;
}

// ---- reading ---------------------------------------------------------------

SecureMethod SecureFormOf(const std::string& raw, bool* encrypted) {
    if (encrypted) *encrypted = false;
    std::string headers, body;
    if (!SplitEntity(raw, headers, body)) return SecureMethod::Plain;
    const std::string ct = EntityHeader(headers, "Content-Type");
    const std::string type = MediaType(ct);
    const std::string protocol = Lower(MediaTypeParameter(ct, "protocol"));
    if (type == "multipart/signed") {
        if (Contains(protocol, "pkcs7-signature")) return SecureMethod::Smime;
        if (Contains(protocol, "pgp-signature")) return SecureMethod::OpenPgp;
        return SecureMethod::Plain;
    }
    if (type == "application/pkcs7-mime" || type == "application/x-pkcs7-mime") {
        if (encrypted) *encrypted = Lower(MediaTypeParameter(ct, "smime-type")) != "signed-data";
        return SecureMethod::Smime;
    }
    if (type == "multipart/encrypted" && Contains(protocol, "pgp-encrypted")) {
        if (encrypted) *encrypted = true;
        return SecureMethod::OpenPgp;
    }
    if (type.empty() || type == "text/plain") {
        const std::string text = DecodedBody(raw);
        if (Contains(text, "-----BEGIN PGP MESSAGE-----")) {
            if (encrypted) *encrypted = true;
            return SecureMethod::OpenPgp;
        }
        if (Contains(text, "-----BEGIN PGP SIGNED MESSAGE-----")) return SecureMethod::OpenPgp;
    }
    return SecureMethod::Plain;
}

SecureMessage OpenSecureMessage(const std::string& raw, const SecureKeys& keys) {
    SecureMessage result;
    result.raw = raw;
    const std::string from = FromOf(raw);
    std::vector<SmimeIdentity> identities;
    bool identitiesRead = false;

    // Taken apart a layer at a time: encrypted, then signed inside, and the
    // like - four layers at most, a loop guard against a message that nests
    // itself.
    for (int depth = 0; depth < 4; ++depth) {
        std::string headers, body;
        if (!SplitEntity(result.raw, headers, body)) break;
        const std::string ct = EntityHeader(headers, "Content-Type");
        const std::string type = MediaType(ct);
        const std::string protocol = Lower(MediaTypeParameter(ct, "protocol"));

        if (type == "multipart/signed") {
            const std::vector<std::string> parts = MultipartParts(body, MediaTypeParameter(ct, "boundary"));
            if (parts.size() < 2) {
                result.problem = "the signed message is incomplete";
                break;
            }
            SignatureCheck check;
            const std::string content = CanonicalCrlf(parts[0]);
            if (Contains(protocol, "pkcs7-signature")) {
                const std::string sig = DecodedBody(parts[1]);
                check = secure::SmimeVerifyDetached(content, std::vector<uint8_t>(sig.begin(), sig.end()));
            } else if (Contains(protocol, "pgp-signature")) {
                check = secure::PgpVerifyDetached(content, DecodedBody(parts[1]));
            } else {
                break;
            }
            MarkFrom(check, from);
            result.signatures.push_back(check);
            result.raw = ReplaceContent(result.raw, parts[0]);
            continue;
        }

        if (type == "application/pkcs7-mime" || type == "application/x-pkcs7-mime") {
            const std::string bytes = DecodedBody(result.raw);
            const std::vector<uint8_t> der(bytes.begin(), bytes.end());
            if (secure::SmimeIsSignedData(der) || Lower(MediaTypeParameter(ct, "smime-type")) == "signed-data") {
                std::string content;
                SignatureCheck check;
                const bool read = secure::SmimeVerifyOpaque(der, content, check);
                MarkFrom(check, from);
                result.signatures.push_back(check);
                if (!read) {
                    result.problem = check.detail;
                    break;
                }
                result.raw = ReplaceContent(result.raw, content);
                continue;
            }
            result.encrypted = true;
            result.encryption = SecureMethod::Smime;
            if (!identitiesRead && keys.smimeIdentities) identities = keys.smimeIdentities();
            identitiesRead = true;
            std::string content, error;
            if (!secure::SmimeDecrypt(der, identities, content, error)) {
                result.problem = "This S/MIME message cannot be decrypted: " + error + ".";
                break;
            }
            result.decrypted = true;
            result.raw = ReplaceContent(result.raw, content);
            continue;
        }

        if (type == "multipart/encrypted" && Contains(protocol, "pgp-encrypted")) {
            result.encrypted = true;
            result.encryption = SecureMethod::OpenPgp;
            const std::vector<std::string> parts = MultipartParts(body, MediaTypeParameter(ct, "boundary"));
            if (parts.size() < 2) {
                result.problem = "the encrypted message is incomplete";
                break;
            }
            std::string plain, error;
            std::vector<SignatureCheck> signatures;
            if (!secure::PgpDecrypt(DecodedBody(parts[1]), plain, signatures, error)) {
                result.problem = "This OpenPGP message cannot be decrypted: " + error + ".";
                break;
            }
            result.decrypted = true;
            for (SignatureCheck& s : signatures) {
                MarkFrom(s, from);
                result.signatures.push_back(s);
            }
            result.raw = ReplaceContent(result.raw, plain);
            continue;
        }

        if (type.empty() || type == "text/plain") {
            const std::string text = DecodedBody(result.raw);
            size_t a = 0, b = 0;
            const std::string message = ArmoredBlock(text, "MESSAGE", &a, &b);
            if (!message.empty()) {
                result.encrypted = true;
                result.encryption = SecureMethod::OpenPgp;
                std::string plain, error;
                std::vector<SignatureCheck> signatures;
                if (!secure::PgpDecrypt(message, plain, signatures, error)) {
                    result.problem = "This OpenPGP message cannot be decrypted: " + error + ".";
                    break;
                }
                result.decrypted = true;
                for (SignatureCheck& s : signatures) {
                    MarkFrom(s, from);
                    result.signatures.push_back(s);
                }
                result.raw = ReplaceContent(result.raw, TextEntity(text.substr(0, a) + plain + text.substr(b)));
                break;
            }
            const std::string clear = ArmoredBlock(text, "SIGNED MESSAGE", &a, &b);
            if (!clear.empty()) {
                std::string signedText;
                SignatureCheck check;
                if (secure::PgpVerifyClearSigned(clear, signedText, check))
                    result.raw = ReplaceContent(result.raw, TextEntity(text.substr(0, a) + signedText + text.substr(b)));
                MarkFrom(check, from);
                result.signatures.push_back(check);
            }
        }
        break;
    }
    return result;
}

// ---- sending ---------------------------------------------------------------

std::vector<std::string> MissingRecipientKeys(const SecureSendOptions& options, const SecureKeys& keys) {
    std::vector<std::string> wanted;
    for (const std::string& r : options.recipients) {
        const std::string a = AddressOf(r);
        if (!a.empty() && std::find(wanted.begin(), wanted.end(), a) == wanted.end()) wanted.push_back(a);
    }
    const std::string self = AddressOf(options.from);
    if (!self.empty() && std::find(wanted.begin(), wanted.end(), self) == wanted.end()) wanted.push_back(self);
    if (options.method == SecureMethod::OpenPgp) return secure::PgpMissingKeys(wanted);
    std::vector<std::string> missing;
    for (const std::string& a : wanted) {
        std::string pem;
        SmimeIdentity own;
        const bool have = (a == self && keys.smimeIdentityFor && keys.smimeIdentityFor(a, own))
                       || (keys.smimeCertificateFor && keys.smimeCertificateFor(a, pem));
        if (!have) missing.push_back(a);
    }
    return missing;
}

// What OpenPGP signs as for `address`: the account's chosen key, else the
// address (the key whose user id it is).
static std::string OpenPgpSigner(const std::string& address, const SecureKeys& keys) {
    const std::string chosen = keys.openPgpSigningKeyFor ? keys.openPgpSigningKeyFor(address) : std::string();
    return chosen.empty() ? address : chosen;
}

bool CanSignAs(SecureMethod method, const std::string& address, const SecureKeys& keys) {
    const std::string a = AddressOf(address);
    if (method == SecureMethod::OpenPgp) return secure::PgpHasSecretKey(OpenPgpSigner(a, keys));
    SmimeIdentity own;
    return method == SecureMethod::Smime && keys.smimeIdentityFor && keys.smimeIdentityFor(a, own);
}

bool SecureOutgoingMessage(const std::string& raw, const SecureSendOptions& options,
                           const SecureKeys& keys, std::string& out, std::string& error) {
    out.clear();
    if (options.method == SecureMethod::Plain || (!options.sign && !options.encrypt)) {
        out = raw;
        return true;
    }
    if (options.method == SecureMethod::Smime && !SmimeAvailable()) {
        error = "S/MIME is not built into this UltraMail";
        return false;
    }
    if (options.method == SecureMethod::OpenPgp && !OpenPgpAvailable()) {
        error = "OpenPGP is not available (GnuPG is not installed, or this UltraMail was built without GPGME)";
        return false;
    }
    std::string headers, body;
    if (!SplitEntity(raw, headers, body)) {
        error = "the message to send is not a MIME message";
        return false;
    }
    // The content as one MIME entity: its Content-* headers and its body, in
    // a transfer encoding no mail path rewrites, with CRLF line ends.
    std::string entityHeaders;
    for (const std::string& field : HeaderFields(headers)) {
        if (FieldName(field).rfind("content-", 0) != 0) continue;
        std::string line;
        for (char c : field) line += c == '\n' ? std::string("\r\n") : std::string(1, c);
        entityHeaders += line + "\r\n";
    }
    if (entityHeaders.empty()) entityHeaders = "Content-Type: text/plain; charset=utf-8\r\n";
    std::string entity = CanonicalCrlf(SevenBitEntity(entityHeaders + "\r\n" + body));

    const std::string self = AddressOf(options.from);
    std::vector<std::string> recipients;
    for (const std::string& r : options.recipients) {
        const std::string a = AddressOf(r);
        if (!a.empty() && std::find(recipients.begin(), recipients.end(), a) == recipients.end()) recipients.push_back(a);
    }

    if (options.method == SecureMethod::Smime) {
        SmimeIdentity own;
        const bool haveOwn = keys.smimeIdentityFor && keys.smimeIdentityFor(self, own);
        if (options.sign) {
            if (!haveOwn) {
                error = "there is no S/MIME certificate for " + self + " (Account Settings)";
                return false;
            }
            std::vector<uint8_t> signature;
            if (!secure::SmimeSignDetached(entity, own, signature, error)) return false;
            const std::string boundary = Boundary("signed");
            entity = "Content-Type: multipart/signed; protocol=\"application/pkcs7-signature\"; micalg=sha-256;\r\n"
                     " boundary=\"" + boundary + "\"\r\n\r\n"
                     "This is a cryptographically signed message in MIME format.\r\n\r\n"
                     "--" + boundary + "\r\n" + entity + "\r\n"
                     "--" + boundary + "\r\n"
                     "Content-Type: application/pkcs7-signature; name=\"smime.p7s\"\r\n"
                     "Content-Transfer-Encoding: base64\r\n"
                     "Content-Disposition: attachment; filename=\"smime.p7s\"\r\n"
                     "Content-Description: S/MIME Cryptographic Signature\r\n\r\n" +
                     Base64Lines(signature) +
                     "--" + boundary + "--\r\n";
        }
        if (options.encrypt) {
            std::vector<std::string> certificates;
            std::vector<std::string> missing;
            for (const std::string& a : recipients) {
                if (a == self && haveOwn) continue;   // added below
                std::string pem;
                if (keys.smimeCertificateFor && keys.smimeCertificateFor(a, pem)) certificates.push_back(pem);
                else missing.push_back(a);
            }
            if (!missing.empty()) {
                std::string list;
                for (const std::string& m : missing) list += (list.empty() ? "" : ", ") + m;
                error = "no S/MIME certificate is known for " + list +
                        " (one is learnt from a signed message they send)";
                return false;
            }
            // And to the sender, so the copy in Sent opens again.
            if (haveOwn) certificates.push_back(own.certificatePem);
            std::vector<uint8_t> der;
            if (!secure::SmimeEncrypt(entity, certificates, der, error)) return false;
            entity = "Content-Type: application/pkcs7-mime; smime-type=enveloped-data; name=\"smime.p7m\"\r\n"
                     "Content-Transfer-Encoding: base64\r\n"
                     "Content-Disposition: attachment; filename=\"smime.p7m\"\r\n"
                     "Content-Description: S/MIME Encrypted Message\r\n\r\n" + Base64Lines(der);
        }
    } else {
        if (options.encrypt) {
            std::vector<std::string> to = recipients;
            if (!self.empty() && std::find(to.begin(), to.end(), self) == to.end()) to.push_back(self);
            std::string armored;
            if (!secure::PgpEncrypt(entity, to, options.sign ? OpenPgpSigner(self, keys) : std::string(), armored,
                                    error))
                return false;
            const std::string boundary = Boundary("encrypted");
            entity = "Content-Type: multipart/encrypted; protocol=\"application/pgp-encrypted\";\r\n"
                     " boundary=\"" + boundary + "\"\r\n\r\n"
                     "This is an OpenPGP/MIME encrypted message (RFC 4880 and 3156).\r\n\r\n"
                     "--" + boundary + "\r\n"
                     "Content-Type: application/pgp-encrypted\r\n"
                     "Content-Description: PGP/MIME version identification\r\n\r\n"
                     "Version: 1\r\n\r\n"
                     "--" + boundary + "\r\n"
                     "Content-Type: application/octet-stream; name=\"encrypted.asc\"\r\n"
                     "Content-Description: OpenPGP encrypted message\r\n"
                     "Content-Disposition: inline; filename=\"encrypted.asc\"\r\n\r\n" +
                     CanonicalCrlf(armored) + "\r\n"
                     "--" + boundary + "--\r\n";
        } else {
            std::string signature, micalg;
            if (!secure::PgpSignDetached(entity, OpenPgpSigner(self, keys), signature, micalg, error)) return false;
            const std::string boundary = Boundary("signed");
            entity = "Content-Type: multipart/signed; micalg=" + micalg + ";\r\n"
                     " protocol=\"application/pgp-signature\"; boundary=\"" + boundary + "\"\r\n\r\n"
                     "This is an OpenPGP/MIME signed message (RFC 4880 and 3156).\r\n\r\n"
                     "--" + boundary + "\r\n" + entity + "\r\n"
                     "--" + boundary + "\r\n"
                     "Content-Type: application/pgp-signature; name=\"signature.asc\"\r\n"
                     "Content-Description: OpenPGP digital signature\r\n"
                     "Content-Disposition: attachment; filename=\"signature.asc\"\r\n\r\n" +
                     CanonicalCrlf(signature) + "\r\n"
                     "--" + boundary + "--\r\n";
        }
    }
    out = CanonicalCrlf(ReplaceContent(raw, entity));
    return true;
}

// ---- keys ------------------------------------------------------------------

bool ImportPkcs12(const std::vector<uint8_t>& p12, const std::string& password,
                  SmimeIdentity& out, std::string& error) {
    return secure::SmimeImportPkcs12(p12, password, out, error);
}

bool ReadCertificate(const std::string& certificatePem, CertificateInfo& out) {
    return secure::SmimeReadCertificate(certificatePem, out);
}

int ImportOpenPgpKeys(const std::string& armored, std::string& error) {
    return secure::PgpImportKeys(armored, error);
}

} // namespace UltraMail

// Apps/UltraMail/engine/UltraMailOpenPgp.cpp
// OpenPGP's byte work on GPGME (UltraMailSecureBackends.h): the user's own
// GnuPG - their keyring, their trust settings, and gpg-agent, which asks for
// a secret key's passphrase in its own window (pinentry) and remembers it as
// it is set up to. UltraMail keeps no OpenPGP key of its own.
//
// A key is used for an address only when one of its user ids is that address
// exactly and the key can do the job (sign, encrypt) and is not revoked,
// expired or disabled. Encrypting to a key the keyring does not vouch for is
// allowed (GPGME_ENCRYPT_ALWAYS_TRUST): the address match is what chose it,
// as other mail programs do; the signature check, by contrast, reports a key
// nobody vouched for as such.
//
// Without GPGME (ULTRAMAIL_HAVE_OPENPGP unset) every function says so.
//
// On macOS GPGME finds GnuPG (gpgconf, and through it gpg and gpg-agent) on
// PATH alone, and an app started from the Finder or the Dock has only
// /usr/bin:/bin:/usr/sbin:/sbin - none of where GnuPG is installed. So the
// usual places (Homebrew on Apple silicon and on Intel, GPG Suite, MacPorts)
// are added to PATH once, at load, when no gpgconf is on it already.
// Version: 0.2.0 - macOS: GnuPG found where Homebrew, GPG Suite or MacPorts put it
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSecureBackends.h"

#if defined(ULTRAMAIL_HAVE_OPENPGP)

#include <gpgme.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>

#if defined(__APPLE__)
#include <cstdlib>
#include <unistd.h>
#endif

namespace UltraMail::secure {

namespace {

#if defined(__APPLE__)
bool HasGpgconf(const std::string& dir) {
    return !dir.empty() && access((dir + "/gpgconf").c_str(), X_OK) == 0;
}

// At static initialisation: before any thread could read the environment, and
// before GPGME (gpgme_check_version) looks for gpgconf.
[[maybe_unused]] const bool g_gnupgOnPath = [] {
    const char* current = std::getenv("PATH");
    std::string path = current ? current : "/usr/bin:/bin:/usr/sbin:/sbin";
    for (size_t start = 0; start <= path.size();) {
        const size_t end = std::min(path.find(':', start), path.size());
        if (HasGpgconf(path.substr(start, end - start))) return true;
        start = end + 1;
    }
    bool added = false;
    for (const char* dir : { "/opt/homebrew/bin", "/usr/local/bin", "/usr/local/MacGPG2/bin", "/opt/local/bin" }) {
        if (!HasGpgconf(dir)) continue;
        path += std::string(":") + dir;
        added = true;
    }
    if (added) setenv("PATH", path.c_str(), 1);
    return added;
}();
#endif

std::once_flag g_init;
bool g_ready = false;

bool Ready() {
    std::call_once(g_init, [] {
        if (!gpgme_check_version(nullptr)) return;
        g_ready = gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP) == GPG_ERR_NO_ERROR;
    });
    return g_ready;
}

struct CtxFree { void operator()(gpgme_ctx_t c) const { gpgme_release(c); } };
struct DataFree { void operator()(gpgme_data_t d) const { gpgme_data_release(d); } };
struct KeyFree { void operator()(gpgme_key_t k) const { gpgme_key_unref(k); } };
using Ctx = std::unique_ptr<std::remove_pointer_t<gpgme_ctx_t>, CtxFree>;
using Data = std::unique_ptr<std::remove_pointer_t<gpgme_data_t>, DataFree>;
using KeyRef = std::unique_ptr<std::remove_pointer_t<gpgme_key_t>, KeyFree>;

Ctx NewContext() {
    gpgme_ctx_t ctx = nullptr;
    if (!Ready() || gpgme_new(&ctx) != GPG_ERR_NO_ERROR) return Ctx();
    gpgme_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP);
    gpgme_set_armor(ctx, 1);
    return Ctx(ctx);
}

Data DataOf(const std::string& bytes) {
    gpgme_data_t d = nullptr;
    // Copied (the last argument), so the string may go before the data does.
    if (gpgme_data_new_from_mem(&d, bytes.data(), bytes.size(), 1) != GPG_ERR_NO_ERROR) return Data();
    return Data(d);
}

Data EmptyData() {
    gpgme_data_t d = nullptr;
    if (gpgme_data_new(&d) != GPG_ERR_NO_ERROR) return Data();
    return Data(d);
}

std::string Read(gpgme_data_t d) {
    size_t length = 0;
    char* bytes = gpgme_data_release_and_get_mem(d, &length);
    std::string out = bytes ? std::string(bytes, length) : std::string();
    if (bytes) gpgme_free(bytes);
    return out;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string ErrorText(gpgme_error_t err) { return gpgme_strerror(err); }

// The keyring's key for `email` that can `sign` (a secret key) or encrypt.
KeyRef KeyFor(gpgme_ctx_t ctx, const std::string& email, bool secret) {
    const std::string wanted = Lower(email);
    if (gpgme_op_keylist_start(ctx, ("<" + wanted + ">").c_str(), secret ? 1 : 0) != GPG_ERR_NO_ERROR)
        return KeyRef();
    KeyRef chosen;
    gpgme_key_t key = nullptr;
    while (gpgme_op_keylist_next(ctx, &key) == GPG_ERR_NO_ERROR) {
        KeyRef held(key);
        if (chosen || key->revoked || key->expired || key->disabled || key->invalid) continue;
        if (secret ? !key->can_sign : !key->can_encrypt) continue;
        bool matches = false;
        for (gpgme_user_id_t uid = key->uids; uid && !matches; uid = uid->next)
            matches = !uid->revoked && !uid->invalid && uid->email && Lower(uid->email) == wanted;
        if (matches) chosen = std::move(held);
    }
    gpgme_op_keylist_end(ctx);
    return chosen;
}

// One signature of a verify result, in UltraMail's words.
SignatureCheck Judge(gpgme_ctx_t ctx, gpgme_signature_t sig) {
    SignatureCheck check;
    check.method = SecureMethod::OpenPgp;
    check.fingerprint = sig->fpr ? sig->fpr : "";
    gpgme_key_t key = nullptr;
    if (sig->fpr && gpgme_get_key(ctx, sig->fpr, &key, 0) == GPG_ERR_NO_ERROR && key) {
        KeyRef held(key);
        if (key->uids) {
            check.signerName = key->uids->name ? key->uids->name : "";
            check.signerEmail = key->uids->email ? Lower(key->uids->email) : "";
        }
        if (key->subkeys && key->subkeys->fpr) check.fingerprint = key->subkeys->fpr;
    }
    const gpgme_err_code_t code = gpgme_err_code(sig->status);
    if (code == GPG_ERR_BAD_SIGNATURE) {
        check.verdict = SignatureVerdict::Invalid;
        check.detail = "The message was changed after it was signed.";
    } else if (code == GPG_ERR_NO_PUBKEY) {
        check.verdict = SignatureVerdict::UnknownSigner;
        check.detail = "Signed with a key that is not in your keyring (" + check.fingerprint +
                       "), so the signature cannot be checked.";
    } else if (code == GPG_ERR_CERT_REVOKED || (sig->summary & GPGME_SIGSUM_KEY_REVOKED)) {
        check.verdict = SignatureVerdict::Revoked;
        check.detail = "The key that signed it was revoked.";
    } else if (code == GPG_ERR_KEY_EXPIRED || code == GPG_ERR_SIG_EXPIRED ||
               (sig->summary & (GPGME_SIGSUM_KEY_EXPIRED | GPGME_SIGSUM_SIG_EXPIRED))) {
        check.verdict = SignatureVerdict::Expired;
        check.detail = "The signature holds, but the key (or the signature) has expired.";
    } else if (code == GPG_ERR_NO_ERROR) {
        const bool vouched = sig->validity == GPGME_VALIDITY_FULL || sig->validity == GPGME_VALIDITY_ULTIMATE;
        check.verdict = vouched ? SignatureVerdict::Valid : SignatureVerdict::Untrusted;
        check.detail = vouched ? "The signature holds, and your keyring trusts the key."
                               : "The signature holds, but your keyring does not vouch for the key "
                                 "(compare its fingerprint with the sender before you trust it).";
    } else {
        check.detail = "The signature could not be checked: " + ErrorText(sig->status);
    }
    return check;
}

std::vector<SignatureCheck> Signatures(gpgme_ctx_t ctx) {
    std::vector<SignatureCheck> out;
    gpgme_verify_result_t result = gpgme_op_verify_result(ctx);
    for (gpgme_signature_t sig = result ? result->signatures : nullptr; sig; sig = sig->next)
        out.push_back(Judge(ctx, sig));
    return out;
}

} // namespace

bool OpenPgpBuilt() { return Ready(); }

SignatureCheck PgpVerifyDetached(const std::string& content, const std::string& signature) {
    SignatureCheck check;
    check.method = SecureMethod::OpenPgp;
    Ctx ctx = NewContext();
    Data sig = DataOf(signature), signedText = DataOf(content);
    if (!ctx || !sig || !signedText) {
        check.detail = "GnuPG cannot be reached.";
        return check;
    }
    const gpgme_error_t err = gpgme_op_verify(ctx.get(), sig.get(), signedText.get(), nullptr);
    std::vector<SignatureCheck> found = Signatures(ctx.get());
    if (found.empty()) {
        check.detail = "The signature cannot be read: " + ErrorText(err);
        return check;
    }
    return found.front();
}

bool PgpDecrypt(const std::string& armored, std::string& plain, std::vector<SignatureCheck>& signatures,
                std::string& error) {
    Ctx ctx = NewContext();
    Data cipher = DataOf(armored), out = EmptyData();
    if (!ctx || !cipher || !out) {
        error = "GnuPG cannot be reached";
        return false;
    }
    const gpgme_error_t err = gpgme_op_decrypt_verify(ctx.get(), cipher.get(), out.get());
    if (err != GPG_ERR_NO_ERROR) {
        const gpgme_err_code_t code = gpgme_err_code(err);
        error = code == GPG_ERR_NO_SECKEY ? std::string("it was encrypted to a key whose secret key is not in your keyring")
              : code == GPG_ERR_CANCELED  ? std::string("the passphrase was not given")
              : code == GPG_ERR_BAD_PASSPHRASE ? std::string("the passphrase was wrong")
                                               : ErrorText(err);
        return false;
    }
    plain = Read(out.release());
    signatures = Signatures(ctx.get());
    return true;
}

bool PgpVerifyClearSigned(const std::string& clearSigned, std::string& text, SignatureCheck& check) {
    check = SignatureCheck{};
    check.method = SecureMethod::OpenPgp;
    Ctx ctx = NewContext();
    Data sig = DataOf(clearSigned), out = EmptyData();
    if (!ctx || !sig || !out) {
        check.detail = "GnuPG cannot be reached.";
        return false;
    }
    const gpgme_error_t err = gpgme_op_verify(ctx.get(), sig.get(), nullptr, out.get());
    text = Read(out.release());
    std::vector<SignatureCheck> found = Signatures(ctx.get());
    if (found.empty()) {
        check.detail = "The signature cannot be read: " + ErrorText(err);
        return !text.empty();
    }
    check = found.front();
    return !text.empty();
}

bool PgpSignDetached(const std::string& content, const std::string& signer, std::string& signature,
                     std::string& micalg, std::string& error) {
    Ctx ctx = NewContext();
    if (!ctx) {
        error = "GnuPG cannot be reached";
        return false;
    }
    KeyRef key = KeyFor(ctx.get(), signer, /*secret=*/true);
    if (!key) {
        error = "there is no OpenPGP secret key for " + signer + " in your keyring";
        return false;
    }
    gpgme_signers_clear(ctx.get());
    gpgme_signers_add(ctx.get(), key.get());
    Data in = DataOf(content), out = EmptyData();
    const gpgme_error_t err = gpgme_op_sign(ctx.get(), in.get(), out.get(), GPGME_SIG_MODE_DETACH);
    if (err != GPG_ERR_NO_ERROR) {
        error = gpgme_err_code(err) == GPG_ERR_CANCELED ? std::string("the passphrase was not given")
                                                         : "signing failed: " + ErrorText(err);
        return false;
    }
    signature = Read(out.release());
    micalg = "pgp-sha256";
    if (gpgme_sign_result_t result = gpgme_op_sign_result(ctx.get()); result && result->signatures)
        if (const char* hash = gpgme_hash_algo_name(result->signatures->hash_algo))
            micalg = "pgp-" + Lower(hash);
    return true;
}

bool PgpEncrypt(const std::string& content, const std::vector<std::string>& recipients,
                const std::string& signer, std::string& armored, std::string& error) {
    Ctx ctx = NewContext();
    if (!ctx) {
        error = "GnuPG cannot be reached";
        return false;
    }
    std::vector<KeyRef> keys;
    for (const std::string& r : recipients) {
        KeyRef key = KeyFor(ctx.get(), r, /*secret=*/false);
        if (!key) {
            error = "there is no OpenPGP key for " + r + " in your keyring";
            return false;
        }
        keys.push_back(std::move(key));
    }
    std::vector<gpgme_key_t> list;
    for (const KeyRef& k : keys) list.push_back(k.get());
    list.push_back(nullptr);
    Data in = DataOf(content), out = EmptyData();
    gpgme_error_t err;
    const auto flags = static_cast<gpgme_encrypt_flags_t>(GPGME_ENCRYPT_ALWAYS_TRUST);
    if (signer.empty()) {
        err = gpgme_op_encrypt(ctx.get(), list.data(), flags, in.get(), out.get());
    } else {
        KeyRef own = KeyFor(ctx.get(), signer, /*secret=*/true);
        if (!own) {
            error = "there is no OpenPGP secret key for " + signer + " in your keyring";
            return false;
        }
        gpgme_signers_clear(ctx.get());
        gpgme_signers_add(ctx.get(), own.get());
        err = gpgme_op_encrypt_sign(ctx.get(), list.data(), flags, in.get(), out.get());
    }
    if (err != GPG_ERR_NO_ERROR) {
        error = gpgme_err_code(err) == GPG_ERR_CANCELED ? std::string("the passphrase was not given")
                                                         : "encrypting failed: " + ErrorText(err);
        return false;
    }
    armored = Read(out.release());
    return true;
}

bool PgpHasSecretKey(const std::string& email) {
    Ctx ctx = NewContext();
    return ctx && KeyFor(ctx.get(), email, /*secret=*/true) != nullptr;
}

std::vector<std::string> PgpMissingKeys(const std::vector<std::string>& emails) {
    std::vector<std::string> missing;
    Ctx ctx = NewContext();
    for (const std::string& e : emails)
        if (!ctx || !KeyFor(ctx.get(), e, /*secret=*/false)) missing.push_back(e);
    return missing;
}

int PgpImportKeys(const std::string& armored, std::string& error) {
    Ctx ctx = NewContext();
    Data in = DataOf(armored);
    if (!ctx || !in) {
        error = "GnuPG cannot be reached";
        return 0;
    }
    const gpgme_error_t err = gpgme_op_import(ctx.get(), in.get());
    gpgme_import_result_t result = gpgme_op_import_result(ctx.get());
    if (err != GPG_ERR_NO_ERROR || !result) {
        error = "the keys could not be imported: " + ErrorText(err);
        return 0;
    }
    return result->imported + result->unchanged;
}

} // namespace UltraMail::secure

#else // !ULTRAMAIL_HAVE_OPENPGP

namespace UltraMail::secure {

namespace {
const char* kNotBuilt = "OpenPGP is not built into this UltraMail (no GPGME)";
SignatureCheck NotBuilt() {
    SignatureCheck check;
    check.method = SecureMethod::OpenPgp;
    check.detail = std::string(kNotBuilt) + ".";
    return check;
}
} // namespace

bool OpenPgpBuilt() { return false; }
SignatureCheck PgpVerifyDetached(const std::string&, const std::string&) { return NotBuilt(); }
bool PgpDecrypt(const std::string&, std::string&, std::vector<SignatureCheck>&, std::string& error) {
    error = kNotBuilt;
    return false;
}
bool PgpVerifyClearSigned(const std::string&, std::string&, SignatureCheck& check) {
    check = NotBuilt();
    return false;
}
bool PgpSignDetached(const std::string&, const std::string&, std::string&, std::string&, std::string& error) {
    error = kNotBuilt;
    return false;
}
bool PgpEncrypt(const std::string&, const std::vector<std::string>&, const std::string&, std::string&,
                std::string& error) {
    error = kNotBuilt;
    return false;
}
bool PgpHasSecretKey(const std::string&) { return false; }
std::vector<std::string> PgpMissingKeys(const std::vector<std::string>& emails) { return emails; }
int PgpImportKeys(const std::string&, std::string& error) {
    error = kNotBuilt;
    return 0;
}

} // namespace UltraMail::secure

#endif

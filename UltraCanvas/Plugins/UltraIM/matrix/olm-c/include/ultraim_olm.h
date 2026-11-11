/* ultraim_olm.h
 * C ABI for the ultraim_olm Rust shim crate: vodozemac's Olm accounts and
 * sessions and Megolm group sessions, for the UltraIM Matrix provider.
 * Version: 0.1.0
 * Last Modified: 2026-10-21
 * Author: UltraCanvas Framework
 *
 * Conventions:
 *  - Keys, signatures, ciphertexts and session keys are unpadded base64, as
 *    Matrix carries them.
 *  - A returned char* is owned by the caller and released with
 *    uim_olm_string_free. NULL means failure; uim_olm_last_error() then says
 *    why (per thread, valid until the next call on the same thread).
 *  - Pickles are vodozemac pickles encrypted with a 32-byte pickle key.
 *  - Objects are not thread-safe: use one from one thread at a time.
 */
#ifndef ULTRAIM_OLM_H
#define ULTRAIM_OLM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UimOlmAccount     UimOlmAccount;
typedef struct UimOlmSession     UimOlmSession;
typedef struct UimMegolmOutbound UimMegolmOutbound;
typedef struct UimMegolmInbound  UimMegolmInbound;

const char* uim_olm_version(void);          /* static string */
const char* uim_olm_last_error(void);       /* "" when the last call succeeded */
void        uim_olm_string_free(char* text);

/* ---- account: this device's identity and one-time keys ------------------ */
UimOlmAccount* uim_olm_account_new(void);
UimOlmAccount* uim_olm_account_unpickle(const char* pickle, const uint8_t key[32]);
char*          uim_olm_account_pickle(const UimOlmAccount* account, const uint8_t key[32]);
void           uim_olm_account_free(UimOlmAccount* account);
char*          uim_olm_account_curve25519(const UimOlmAccount* account);
char*          uim_olm_account_ed25519(const UimOlmAccount* account);
char*          uim_olm_account_sign(const UimOlmAccount* account, const uint8_t* message, size_t length);
size_t         uim_olm_account_max_one_time_keys(const UimOlmAccount* account);
void           uim_olm_account_generate_one_time_keys(UimOlmAccount* account, size_t count);
/* Unpublished keys as a JSON object {"<key id>": "<curve25519 key>", ...}. */
char*          uim_olm_account_one_time_keys(const UimOlmAccount* account);
void           uim_olm_account_generate_fallback_key(UimOlmAccount* account);
/* The unpublished fallback key, the same shape ("{}" when there is none). */
char*          uim_olm_account_fallback_key(const UimOlmAccount* account);
void           uim_olm_account_mark_keys_as_published(UimOlmAccount* account);
UimOlmSession* uim_olm_account_outbound_session(const UimOlmAccount* account, const char* their_identity_key,
                                                const char* their_one_time_key);
/* From a pre-key message (type 0); *plaintext receives its decrypted body. */
UimOlmSession* uim_olm_account_inbound_session(UimOlmAccount* account, const char* their_identity_key,
                                               const char* pre_key_message, char** plaintext);

/* ---- olm session: one double ratchet with one other device -------------- */
UimOlmSession* uim_olm_session_unpickle(const char* pickle, const uint8_t key[32]);
char*          uim_olm_session_pickle(const UimOlmSession* session, const uint8_t key[32]);
void           uim_olm_session_free(UimOlmSession* session);
char*          uim_olm_session_id(const UimOlmSession* session);
/* 1 when a pre-key message belongs to this session, 0 when not, -1 when it
 * does not decode. */
int            uim_olm_session_matches(const UimOlmSession* session, const char* pre_key_message);
int            uim_olm_session_has_received_message(const UimOlmSession* session);
/* Returns the message type (0 pre-key, 1 normal) and the ciphertext in *body,
 * or -1. */
int            uim_olm_session_encrypt(UimOlmSession* session, const uint8_t* plaintext, size_t length,
                                       char** body);
char*          uim_olm_session_decrypt(UimOlmSession* session, int message_type, const char* body);

/* ---- megolm: one sender's session for a room ---------------------------- */
UimMegolmOutbound* uim_megolm_outbound_new(void);
UimMegolmOutbound* uim_megolm_outbound_unpickle(const char* pickle, const uint8_t key[32]);
char*              uim_megolm_outbound_pickle(const UimMegolmOutbound* session, const uint8_t key[32]);
void               uim_megolm_outbound_free(UimMegolmOutbound* session);
char*              uim_megolm_outbound_id(const UimMegolmOutbound* session);
/* The key that lets others decrypt from the next message on (m.room_key). */
char*              uim_megolm_outbound_session_key(const UimMegolmOutbound* session);
uint32_t           uim_megolm_outbound_message_index(const UimMegolmOutbound* session);
char*              uim_megolm_outbound_encrypt(UimMegolmOutbound* session, const uint8_t* plaintext, size_t length);

/* From an m.room_key session key, or (import) an exported key as in
 * m.forwarded_room_key and key export files. */
UimMegolmInbound* uim_megolm_inbound_new(const char* session_key);
UimMegolmInbound* uim_megolm_inbound_import(const char* exported_session_key);
UimMegolmInbound* uim_megolm_inbound_unpickle(const char* pickle, const uint8_t key[32]);
char*             uim_megolm_inbound_pickle(const UimMegolmInbound* session, const uint8_t key[32]);
void              uim_megolm_inbound_free(UimMegolmInbound* session);
char*             uim_megolm_inbound_id(const UimMegolmInbound* session);
uint32_t          uim_megolm_inbound_first_known_index(const UimMegolmInbound* session);
/* An exported key from the given index on; NULL when the session no longer
 * reaches that far back. */
char*             uim_megolm_inbound_export_at(UimMegolmInbound* session, uint32_t index);
char*             uim_megolm_inbound_decrypt(UimMegolmInbound* session, const char* ciphertext,
                                             uint32_t* message_index);

/* ---- signatures ----------------------------------------------------------- */
/* 1 when signature is a valid ed25519 signature of message by key, else 0. */
int uim_olm_ed25519_verify(const char* key, const uint8_t* message, size_t length, const char* signature);

#ifdef __cplusplus
}
#endif

#endif /* ULTRAIM_OLM_H */

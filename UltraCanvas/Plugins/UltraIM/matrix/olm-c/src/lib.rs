// ultraim_olm — C ABI shim around vodozemac (Olm and Megolm) for the UltraIM
// Matrix provider. See include/ultraim_olm.h for the contract.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

use std::cell::RefCell;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::ptr;
use std::slice;

use vodozemac::megolm::{
    ExportedSessionKey, GroupSession, GroupSessionPickle, InboundGroupSession, InboundGroupSessionPickle,
    MegolmMessage, SessionConfig as MegolmConfig, SessionKey,
};
use vodozemac::olm::{Account, AccountPickle, OlmMessage, Session, SessionConfig, SessionPickle};
use vodozemac::{base64_decode, base64_encode, Curve25519PublicKey, Ed25519PublicKey, Ed25519Signature};

pub struct UimOlmAccount(Account);
pub struct UimOlmSession(Session);
pub struct UimMegolmOutbound(GroupSession);
pub struct UimMegolmInbound(InboundGroupSession);

const VERSION: &str = concat!("vodozemac 0.11.1 / ultraim_olm ", env!("CARGO_PKG_VERSION"), "\0");

thread_local! {
    static LAST_ERROR: RefCell<CString> = RefCell::new(CString::default());
}

fn set_error(message: impl ToString) {
    let text = message.to_string().replace('\0', " ");
    LAST_ERROR.with(|e| *e.borrow_mut() = CString::new(text).unwrap_or_default());
}

fn clear_error() {
    LAST_ERROR.with(|e| *e.borrow_mut() = CString::default());
}

// Ok(value) -> value with the error cleared; Err -> the error kept, None.
fn ok<T, E: ToString>(result: Result<T, E>) -> Option<T> {
    match result {
        Ok(value) => {
            clear_error();
            Some(value)
        }
        Err(error) => {
            set_error(error);
            None
        }
    }
}

fn out_string(text: String) -> *mut c_char {
    match CString::new(text) {
        Ok(c) => {
            clear_error();
            c.into_raw()
        }
        Err(_) => {
            set_error("the text holds a NUL byte");
            ptr::null_mut()
        }
    }
}

fn out_bytes(bytes: Vec<u8>) -> *mut c_char {
    match String::from_utf8(bytes) {
        Ok(text) => out_string(text),
        Err(_) => {
            set_error("the plaintext is not UTF-8");
            ptr::null_mut()
        }
    }
}

unsafe fn in_str<'a>(text: *const c_char) -> Option<&'a str> {
    if text.is_null() {
        set_error("a string argument is NULL");
        return None;
    }
    ok(CStr::from_ptr(text).to_str())
}

unsafe fn in_bytes<'a>(data: *const u8, length: usize) -> &'a [u8] {
    if data.is_null() || length == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, length)
    }
}

unsafe fn in_key<'a>(key: *const u8) -> Option<&'a [u8; 32]> {
    if key.is_null() {
        set_error("the pickle key is NULL");
        return None;
    }
    Some(&*(key as *const [u8; 32]))
}

fn boxed<T>(value: T) -> *mut T {
    clear_error();
    Box::into_raw(Box::new(value))
}

fn key_map_json(keys: impl Iterator<Item = (String, String)>) -> String {
    // Key ids and keys are base64: nothing in them needs escaping.
    let mut pairs: Vec<(String, String)> = keys.collect();
    pairs.sort();
    let body: Vec<String> = pairs.iter().map(|(id, key)| format!("\"{}\":\"{}\"", id, key)).collect();
    format!("{{{}}}", body.join(","))
}

unsafe fn pre_key_message(body: *const c_char) -> Option<vodozemac::olm::PreKeyMessage> {
    let body = in_str(body)?;
    let bytes = ok(base64_decode(body))?;
    match ok(OlmMessage::from_parts(0, &bytes))? {
        OlmMessage::PreKey(message) => Some(message),
        OlmMessage::Normal(_) => {
            set_error("not a pre-key message");
            None
        }
    }
}

// ---- general -------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn uim_olm_version() -> *const c_char {
    VERSION.as_ptr() as *const c_char
}

#[no_mangle]
pub extern "C" fn uim_olm_last_error() -> *const c_char {
    LAST_ERROR.with(|e| e.borrow().as_ptr())
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_string_free(text: *mut c_char) {
    if !text.is_null() {
        drop(CString::from_raw(text));
    }
}

// ---- account ---------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn uim_olm_account_new() -> *mut UimOlmAccount {
    boxed(UimOlmAccount(Account::new()))
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_unpickle(pickle: *const c_char, key: *const u8) -> *mut UimOlmAccount {
    let (Some(pickle), Some(key)) = (in_str(pickle), in_key(key)) else { return ptr::null_mut() };
    match ok(AccountPickle::from_encrypted(pickle, key)) {
        Some(p) => boxed(UimOlmAccount(Account::from_pickle(p))),
        None => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_pickle(account: *const UimOlmAccount, key: *const u8) -> *mut c_char {
    let Some(key) = in_key(key) else { return ptr::null_mut() };
    out_string((*account).0.pickle().encrypt(key))
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_free(account: *mut UimOlmAccount) {
    if !account.is_null() {
        drop(Box::from_raw(account));
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_curve25519(account: *const UimOlmAccount) -> *mut c_char {
    out_string((*account).0.curve25519_key().to_base64())
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_ed25519(account: *const UimOlmAccount) -> *mut c_char {
    out_string((*account).0.ed25519_key().to_base64())
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_sign(account: *const UimOlmAccount, message: *const u8,
                                              length: usize) -> *mut c_char {
    out_string((*account).0.sign(in_bytes(message, length)).to_base64())
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_max_one_time_keys(account: *const UimOlmAccount) -> usize {
    (*account).0.max_number_of_one_time_keys()
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_generate_one_time_keys(account: *mut UimOlmAccount, count: usize) {
    (*account).0.generate_one_time_keys(count);
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_one_time_keys(account: *const UimOlmAccount) -> *mut c_char {
    let keys = (*account).0.one_time_keys();
    out_string(key_map_json(keys.into_iter().map(|(id, key)| (id.to_base64(), key.to_base64()))))
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_generate_fallback_key(account: *mut UimOlmAccount) {
    (*account).0.generate_fallback_key();
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_fallback_key(account: *const UimOlmAccount) -> *mut c_char {
    let keys = (*account).0.fallback_key();
    out_string(key_map_json(keys.into_iter().map(|(id, key)| (id.to_base64(), key.to_base64()))))
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_mark_keys_as_published(account: *mut UimOlmAccount) {
    (*account).0.mark_keys_as_published();
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_outbound_session(account: *const UimOlmAccount,
                                                          their_identity_key: *const c_char,
                                                          their_one_time_key: *const c_char) -> *mut UimOlmSession {
    let (Some(identity), Some(one_time)) = (in_str(their_identity_key), in_str(their_one_time_key)) else {
        return ptr::null_mut();
    };
    let (Some(identity), Some(one_time)) =
        (ok(Curve25519PublicKey::from_base64(identity)), ok(Curve25519PublicKey::from_base64(one_time))) else {
        return ptr::null_mut();
    };
    match ok((*account).0.create_outbound_session(SessionConfig::version_1(), identity, one_time)) {
        Some(session) => boxed(UimOlmSession(session)),
        None => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_account_inbound_session(account: *mut UimOlmAccount,
                                                         their_identity_key: *const c_char,
                                                         pre_key: *const c_char,
                                                         plaintext: *mut *mut c_char) -> *mut UimOlmSession {
    if !plaintext.is_null() {
        *plaintext = ptr::null_mut();
    }
    let Some(identity) = in_str(their_identity_key) else { return ptr::null_mut() };
    let Some(identity) = ok(Curve25519PublicKey::from_base64(identity)) else { return ptr::null_mut() };
    let Some(message) = pre_key_message(pre_key) else { return ptr::null_mut() };
    let Some(result) = ok((*account).0.create_inbound_session(SessionConfig::version_1(), identity, &message)) else {
        return ptr::null_mut();
    };
    let text = out_bytes(result.plaintext);
    if text.is_null() {
        return ptr::null_mut();
    }
    if plaintext.is_null() {
        uim_olm_string_free(text);
    } else {
        *plaintext = text;
    }
    boxed(UimOlmSession(result.session))
}

// ---- olm session -------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn uim_olm_session_unpickle(pickle: *const c_char, key: *const u8) -> *mut UimOlmSession {
    let (Some(pickle), Some(key)) = (in_str(pickle), in_key(key)) else { return ptr::null_mut() };
    match ok(SessionPickle::from_encrypted(pickle, key)) {
        Some(p) => boxed(UimOlmSession(Session::from_pickle(p))),
        None => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_session_pickle(session: *const UimOlmSession, key: *const u8) -> *mut c_char {
    let Some(key) = in_key(key) else { return ptr::null_mut() };
    out_string((*session).0.pickle().encrypt(key))
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_session_free(session: *mut UimOlmSession) {
    if !session.is_null() {
        drop(Box::from_raw(session));
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_session_id(session: *const UimOlmSession) -> *mut c_char {
    out_string((*session).0.session_id())
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_session_matches(session: *const UimOlmSession, pre_key: *const c_char) -> c_int {
    match pre_key_message(pre_key) {
        Some(message) => {
            clear_error();
            (message.session_id() == (*session).0.session_id()) as c_int
        }
        None => -1,
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_session_has_received_message(session: *const UimOlmSession) -> c_int {
    (*session).0.has_received_message() as c_int
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_session_encrypt(session: *mut UimOlmSession, plaintext: *const u8, length: usize,
                                                 body: *mut *mut c_char) -> c_int {
    if body.is_null() {
        set_error("body is NULL");
        return -1;
    }
    *body = ptr::null_mut();
    let Some(message) = ok((*session).0.encrypt(in_bytes(plaintext, length))) else { return -1 };
    let (message_type, bytes) = message.to_parts();
    *body = out_string(base64_encode(bytes));
    if (*body).is_null() { -1 } else { message_type as c_int }
}

#[no_mangle]
pub unsafe extern "C" fn uim_olm_session_decrypt(session: *mut UimOlmSession, message_type: c_int,
                                                 body: *const c_char) -> *mut c_char {
    let Some(body) = in_str(body) else { return ptr::null_mut() };
    let Some(bytes) = ok(base64_decode(body)) else { return ptr::null_mut() };
    let Some(message) = ok(OlmMessage::from_parts(message_type.max(0) as usize, &bytes)) else {
        return ptr::null_mut();
    };
    match ok((*session).0.decrypt(&message)) {
        Some(plaintext) => out_bytes(plaintext),
        None => ptr::null_mut(),
    }
}

// ---- megolm outbound ---------------------------------------------------------------

#[no_mangle]
pub extern "C" fn uim_megolm_outbound_new() -> *mut UimMegolmOutbound {
    boxed(UimMegolmOutbound(GroupSession::new(MegolmConfig::version_1())))
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_outbound_unpickle(pickle: *const c_char,
                                                      key: *const u8) -> *mut UimMegolmOutbound {
    let (Some(pickle), Some(key)) = (in_str(pickle), in_key(key)) else { return ptr::null_mut() };
    match ok(GroupSessionPickle::from_encrypted(pickle, key)) {
        Some(p) => boxed(UimMegolmOutbound(GroupSession::from_pickle(p))),
        None => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_outbound_pickle(session: *const UimMegolmOutbound,
                                                    key: *const u8) -> *mut c_char {
    let Some(key) = in_key(key) else { return ptr::null_mut() };
    out_string((*session).0.pickle().encrypt(key))
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_outbound_free(session: *mut UimMegolmOutbound) {
    if !session.is_null() {
        drop(Box::from_raw(session));
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_outbound_id(session: *const UimMegolmOutbound) -> *mut c_char {
    out_string((*session).0.session_id())
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_outbound_session_key(session: *const UimMegolmOutbound) -> *mut c_char {
    out_string((*session).0.session_key().to_base64())
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_outbound_message_index(session: *const UimMegolmOutbound) -> u32 {
    (*session).0.message_index()
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_outbound_encrypt(session: *mut UimMegolmOutbound, plaintext: *const u8,
                                                     length: usize) -> *mut c_char {
    out_string((*session).0.encrypt(in_bytes(plaintext, length)).to_base64())
}

// ---- megolm inbound ----------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_inbound_new(session_key: *const c_char) -> *mut UimMegolmInbound {
    let Some(text) = in_str(session_key) else { return ptr::null_mut() };
    match ok(SessionKey::from_base64(text)) {
        Some(key) => boxed(UimMegolmInbound(InboundGroupSession::new(&key, MegolmConfig::version_1()))),
        None => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_inbound_import(exported_key: *const c_char) -> *mut UimMegolmInbound {
    let Some(text) = in_str(exported_key) else { return ptr::null_mut() };
    match ok(ExportedSessionKey::from_base64(text)) {
        Some(key) => boxed(UimMegolmInbound(InboundGroupSession::import(&key, MegolmConfig::version_1()))),
        None => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_inbound_unpickle(pickle: *const c_char,
                                                     key: *const u8) -> *mut UimMegolmInbound {
    let (Some(pickle), Some(key)) = (in_str(pickle), in_key(key)) else { return ptr::null_mut() };
    match ok(InboundGroupSessionPickle::from_encrypted(pickle, key)) {
        Some(p) => boxed(UimMegolmInbound(InboundGroupSession::from_pickle(p))),
        None => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_inbound_pickle(session: *const UimMegolmInbound,
                                                   key: *const u8) -> *mut c_char {
    let Some(key) = in_key(key) else { return ptr::null_mut() };
    out_string((*session).0.pickle().encrypt(key))
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_inbound_free(session: *mut UimMegolmInbound) {
    if !session.is_null() {
        drop(Box::from_raw(session));
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_inbound_id(session: *const UimMegolmInbound) -> *mut c_char {
    out_string((*session).0.session_id())
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_inbound_first_known_index(session: *const UimMegolmInbound) -> u32 {
    (*session).0.first_known_index()
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_inbound_export_at(session: *mut UimMegolmInbound, index: u32) -> *mut c_char {
    match (*session).0.export_at(index) {
        Some(key) => out_string(key.to_base64()),
        None => {
            set_error("the session does not reach back to that index");
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn uim_megolm_inbound_decrypt(session: *mut UimMegolmInbound, ciphertext: *const c_char,
                                                    message_index: *mut u32) -> *mut c_char {
    let Some(text) = in_str(ciphertext) else { return ptr::null_mut() };
    let Some(message) = ok(MegolmMessage::from_base64(text)) else { return ptr::null_mut() };
    match ok((*session).0.decrypt(&message)) {
        Some(decrypted) => {
            if !message_index.is_null() {
                *message_index = decrypted.message_index;
            }
            out_bytes(decrypted.plaintext)
        }
        None => ptr::null_mut(),
    }
}

// ---- signatures ----------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn uim_olm_ed25519_verify(key: *const c_char, message: *const u8, length: usize,
                                                signature: *const c_char) -> c_int {
    let (Some(key), Some(signature)) = (in_str(key), in_str(signature)) else { return 0 };
    let (Some(key), Some(signature)) =
        (ok(Ed25519PublicKey::from_base64(key)), ok(Ed25519Signature::from_base64(signature))) else {
        return 0;
    };
    ok(key.verify(in_bytes(message, length), &signature)).is_some() as c_int
}

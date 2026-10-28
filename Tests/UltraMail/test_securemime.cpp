// Tests/UltraMail/test_securemime.cpp
// Signed and encrypted mail (UltraMailSecureMime.h): the raw MIME the secure
// forms need, then S/MIME and OpenPGP end to end - a message signed and read
// back, one changed after signing, one encrypted to its recipient (and to its
// sender) and opened by each, a recipient without a key refused before
// anything is sent, a PKCS #12 file imported - with each library where the
// build has it.
//
// The S/MIME material below is a test CA and two identities made for this
// test only (openssl, valid to 2126); the CA is trusted nowhere, so a good
// signature reads "Untrusted". The OpenPGP keys are made for each run in a
// GnuPG home of its own (GNUPGHOME), without passphrases.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailOutbox.h"
#include "UltraMailSecureMime.h"

#include "UltraNet/UltraNetMime.h"
#include <UltraDatabase/UltraDatabase.h>

#include <cstdlib>
#include <map>
#include <filesystem>
#include <string>
#include <vector>

#if defined(ULTRAMAIL_HAVE_OPENPGP)
  #include <gpgme.h>
#endif

using namespace UltraMail;
namespace fs = std::filesystem;

namespace {

const char* kCaPem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDXTCCAkWgAwIBAgIUCuUIxK92EEdI7v4n9nQXgW+Q4VEwDQYJKoZIhvcNAQEL\n"
    "BQAwNTEXMBUGA1UECgwOVWx0cmFNYWlsIFRlc3QxGjAYBgNVBAMMEVVsdHJhTWFp\n"
    "bCBUZXN0IENBMCAXDTI2MTAyNzE2MjQ0NloYDzIxMjYxMDAzMTYyNDQ2WjA1MRcw\n"
    "FQYDVQQKDA5VbHRyYU1haWwgVGVzdDEaMBgGA1UEAwwRVWx0cmFNYWlsIFRlc3Qg\n"
    "Q0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDPmkFN133pU1MXhglS\n"
    "Ons0G3vb2Ueiodt4AcijjXukZdIdn+dbK6ew/9KcNaDN1F6E/eJ+1pBg0dHmUHeX\n"
    "SXFe22y7aBxcsmSgwpCqVL5AKv8y8si8W0Vs0BeH+55mq++TxjAnezsLCyqFWmO1\n"
    "nLlfipe0mghS9yq7iWoSquZLXTafU3eFLvTheaH8o07NtkNTDSDO2BX65XB1WAwo\n"
    "joMngnNh5/9i+FhFpTfKFLzluq5K9AnNfrYt8oKGK505ELHLOw5Crd4H1Ks0rNc/\n"
    "+D0HYdoLQiQHw0D5BEfu/g3lwYL8Eoa5yEFEflOHknnIGoUDx6orle3xOaw+2M//\n"
    "WukPAgMBAAGjYzBhMB0GA1UdDgQWBBSGQ/aY4IT34ZEM8NRe0tlhH+P8czAfBgNV\n"
    "HSMEGDAWgBSGQ/aY4IT34ZEM8NRe0tlhH+P8czAPBgNVHRMBAf8EBTADAQH/MA4G\n"
    "A1UdDwEB/wQEAwIBBjANBgkqhkiG9w0BAQsFAAOCAQEAxXvNhNx1bMtq/gax09aR\n"
    "mPXId6B6rf7d+0XDR4i3jwes/0/dsFZuxzcUwKdJ9W5kzvi3fqxlCaxiuHcuOVbc\n"
    "C7TVW9qacNWLnkFdq3CLT7WTt2fNAyFNOlhCpEPtiiZBHU6wypjrgwFBp0phEqhV\n"
    "NOyehXz9Zk70PLZHSQLEhKXtakx3jGpTrUx3uJY2apuXyVQI52YMhrNkIluXfNmE\n"
    "+rz4QBzcmZXA7MdSad1LaRd6B2qg1tIdLL/ZyuX5zgS7+kp23xtp+vC1JV5iGHfi\n"
    "xIIvNs5RFRfpj0sTHw7Nuve6AYjXvYGqqketewOKa8hxRIxqiMOR2dq6lDNKb/r1\n"
    "Ew==\n"
    "-----END CERTIFICATE-----\n";

const char* kAlicePem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjjCCAnagAwIBAgIUQkOFx0TAJgiBEOuo7vhxRS3MJJQwDQYJKoZIhvcNAQEL\n"
    "BQAwNTEXMBUGA1UECgwOVWx0cmFNYWlsIFRlc3QxGjAYBgNVBAMMEVVsdHJhTWFp\n"
    "bCBUZXN0IENBMCAXDTI2MTAyNzE2MjQ0NloYDzIxMjYxMDAzMTYyNDQ2WjA3MRMw\n"
    "EQYDVQQDDApBbGljZSBUZXN0MSAwHgYJKoZIhvcNAQkBFhFhbGljZUBleGFtcGxl\n"
    "LmNvbTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALRoCy3fePpaC527\n"
    "LKLFeQlEzSVo+M0xKeyi0Maa3jXoJkuYTEQVUd12Yy7tYwZLe2ZHqfF4kKRswL10\n"
    "AlQgN8E7UUmKKh9FfDbARPi9Z6t0Lf/3QgOGI05X2Jr/OSMT4E0Myfj1ozxopkLg\n"
    "YIeFSpJfzMD2T6fPzeB7Uz5DG25EWd/yz8mkWwVBYfnhQwLeDRbeX21a2JmqQfqZ\n"
    "gpgi+xhswmPm/ZHXrVl0CQFxNLXYg7r77K3/syUTHhIHEbIPEC4MtUq8nvXXyekc\n"
    "kBA45iL75XZVp0l4yDj6o2Ds4tKLUt7Oe2SYtZpUGoxDgZ6k5AxGjysZrW/8EBL8\n"
    "LcheVR8CAwEAAaOBkTCBjjAJBgNVHRMEAjAAMA4GA1UdDwEB/wQEAwIFoDATBgNV\n"
    "HSUEDDAKBggrBgEFBQcDBDAcBgNVHREEFTATgRFhbGljZUBleGFtcGxlLmNvbTAd\n"
    "BgNVHQ4EFgQUk17j0e8YRyVT1N+XMt1T2PBKc70wHwYDVR0jBBgwFoAUhkP2mOCE\n"
    "9+GRDPDUXtLZYR/j/HMwDQYJKoZIhvcNAQELBQADggEBAKdeM92jSISdY8lZZ+Gh\n"
    "Nnhcmyl+GzIpN7VvGOFYsO5QACF90xwLfDa8mW2zK8RI2LVoabnAggggbK/+M85s\n"
    "ffB4VGj7vkMyY16eKOTOYdLJUKGaTIEvXL8SBh7v//YS24D8pSxWTTaXJ5btFL4E\n"
    "p9kluEJyWjVYcXtIfAjFY5W1Jt5ub9eDdnnc18t+G9bh8qvxd0LNkoyMmqs1Iqu3\n"
    "SjEFu19ZYN233KqbeYNQl/yvth5PQhaDbsa2XF8Wi+FtaV0kJxCDfzpjXYxYJ5tz\n"
    "9idCYCCBcG8WRrpOIARi9elghHm1ILKPU6ozMukdTpB8LRYbKIb2kFTH+le38HHS\n"
    "L4s=\n"
    "-----END CERTIFICATE-----\n";

const char* kAliceKey =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEugIBADANBgkqhkiG9w0BAQEFAASCBKQwggSgAgEAAoIBAQC0aAst33j6Wgud\n"
    "uyyixXkJRM0laPjNMSnsotDGmt416CZLmExEFVHddmMu7WMGS3tmR6nxeJCkbMC9\n"
    "dAJUIDfBO1FJiiofRXw2wET4vWerdC3/90IDhiNOV9ia/zkjE+BNDMn49aM8aKZC\n"
    "4GCHhUqSX8zA9k+nz83ge1M+QxtuRFnf8s/JpFsFQWH54UMC3g0W3l9tWtiZqkH6\n"
    "mYKYIvsYbMJj5v2R161ZdAkBcTS12IO6++yt/7MlEx4SBxGyDxAuDLVKvJ7118np\n"
    "HJAQOOYi++V2VadJeMg4+qNg7OLSi1LezntkmLWaVBqMQ4GepOQMRo8rGa1v/BAS\n"
    "/C3IXlUfAgMBAAECgf9kcoPazEZNSi73/CTTRtLbC+Hz16NuvCOcswba/yodrgnY\n"
    "Hziy61eiqPXIEl7MAbghouj1pM1HjgR1g93P/VnYSg//eBcD4z5O+l2gXPrj6hCo\n"
    "xsCSlPyGl36q3kijkaQhUlnhEeXMmBTI+g+faUtckejBy01DSRyG9aieRKFVpkZD\n"
    "tElQabyKNWEISVpoiHRa33Gk+3tNFFvbMm3uXMmxxJb7IgZKUFFXUugApt11hkl4\n"
    "ylZFQudoamJ3x8L/bbTeZMh1+EZcZm41AmRZStTYzrJik6OSgRMbrRpMxeVTx7xk\n"
    "3UJ4JhJmxX2BRypzuMV4A9dn5/SdbEPGjqTfOLkCgYEA5NU6DVKxKbXTlGLpZZ8T\n"
    "CkV2KYq53kOTrz5TkMuFzkiqcbQFLhUjM1GUYOBqvAntnWkDuz60fdTcTEwp2k1V\n"
    "sJDGeRj/wO7KjGb6TIrmrPPheYe5BZaC7Es62ntvV0QWmbLLi/1GmyWlHe0T0p8i\n"
    "yUWOoVVexQxWihsyM5K4gxkCgYEAydMFbhULNyw00Vc/NRmDFUWEgA3fTFiMCRoA\n"
    "8lsYQOPeCiWrlhcf2tkh0x7G42zPaAPH7thpvXwYSLv0UbHgBHdCjvlpoEPWHOBb\n"
    "pf+yNy4C9Apz706WKdbVsGTf7NHmjv56m8jWoJJXZmHCPQFTOuTwJddMIbZT8BAV\n"
    "UvepmPcCgYBh2avRNqL7Kv0mXoVfJv6FTd6RNPj1yf/E1+nz6JN2+iuoI0pzwiYj\n"
    "M6c3Jb9+WOB6hwVJdC7J/9OT7J2Y86jIr5+u+LmkTnfeQcqWIG8zVbX8ucSsVwf0\n"
    "8tZfxph9wGfJoWLYoZ0niploipAHaHT/i/bJli/IItZ+h9KXqw2rQQKBgBCcb6v5\n"
    "y2yFFOvN9IPYT7OmfxtTCPftqcjt/Pn1iW990b6gDxlRWevXRzNrKw942zFCbmg4\n"
    "I6vW1ODXp2o566gycKX33Tu1mZ/Sz3hALVjhTpjra1Nc8nsLFZdpFg0V0ZYfV9QO\n"
    "maupn8vra+M9gq4sPZ+/sqJrsUjm6fpkK+9NAoGAA0VT01Cj3YNVYrFnyZ4UUp9C\n"
    "ZGUgKCPjs8kbksnCIglacbWNq8mCWoy8qXmAqOnbwBAIBY0d6Cq0GpMErbA35Ze+\n"
    "DS8z4xhFrs0VQYObHVza1u1ANMuvsfia4GN6Mb3UaogiFPCr9K7Pgjfza4knucsN\n"
    "RGySFdR8aJ4utHFyZCE=\n"
    "-----END PRIVATE KEY-----\n";

const char* kBobPem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDiDCCAnCgAwIBAgIUQkOFx0TAJgiBEOuo7vhxRS3MJJUwDQYJKoZIhvcNAQEL\n"
    "BQAwNTEXMBUGA1UECgwOVWx0cmFNYWlsIFRlc3QxGjAYBgNVBAMMEVVsdHJhTWFp\n"
    "bCBUZXN0IENBMCAXDTI2MTAyNzE2MjQ0N1oYDzIxMjYxMDAzMTYyNDQ3WjAzMREw\n"
    "DwYDVQQDDAhCb2IgVGVzdDEeMBwGCSqGSIb3DQEJARYPYm9iQGV4YW1wbGUuY29t\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAohcecCXJraZiikwo+hOH\n"
    "2KJWODOqiERROLIjtpl1uSxX6fVfxgcF1vYgwJGgtUx7D6NpfNqM6xph7gRu/+Mi\n"
    "FisoCQhRB5bxwLtgNjjsZXRp88qT0nPrGR636e6sx4nk0mwayTmxxqv5S2yIX2Zm\n"
    "ECD0bljv7PNak7Pf60gIkiLx/KkjjAKCMY814IoQdMPr3Hke1XNRYDiAZL7P6Eek\n"
    "8OLPTIsn/trjYisdiUzpPl3HziWP+1H9YL5RhYApW4OlaNHZNPgP9B6feaulDkVJ\n"
    "zdCvIpAzohfqe5wMPgYytfaxUegLzm95c1KtAebxpDkLxPqBsK1VNseKfkWbHQOc\n"
    "PwIDAQABo4GPMIGMMAkGA1UdEwQCMAAwDgYDVR0PAQH/BAQDAgWgMBMGA1UdJQQM\n"
    "MAoGCCsGAQUFBwMEMBoGA1UdEQQTMBGBD2JvYkBleGFtcGxlLmNvbTAdBgNVHQ4E\n"
    "FgQUjg7K3Dt4eyx0YBNT3GAFJqJKekUwHwYDVR0jBBgwFoAUhkP2mOCE9+GRDPDU\n"
    "XtLZYR/j/HMwDQYJKoZIhvcNAQELBQADggEBAH+HBWa/HoqCn1xRyIkhouzDGAlq\n"
    "0+0pmC6lmR0qoAD0MOjiiV6+t+xfUjhPvLlpkzw+JqADEaVDijNLmDnY+wyPOF6m\n"
    "JPPzHhF7Hqoqpg5NSiG5yDtJhGLxJR6ZJjF69E9PTiWgN1HyW6gwzrpi3L5DbKo9\n"
    "IGIMjc6gLujiIOv5vM6azmfEj97B0tf48w09I1j9ARmFUg0ULqamhflZDQtTvbJL\n"
    "r2YzqNxQqIS0LVvk3kV7OS4uPOUUdvDWG3FC2tFZlE3vchlN/dYIR2Vxl9AnHyNO\n"
    "jRhlvYuG/en8iPjujpWYr0kGv1P6niyrrgTP6IguHwPONqouQMipk2Nogeg=\n"
    "-----END CERTIFICATE-----\n";

const char* kBobKey =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCiFx5wJcmtpmKK\n"
    "TCj6E4fYolY4M6qIRFE4siO2mXW5LFfp9V/GBwXW9iDAkaC1THsPo2l82ozrGmHu\n"
    "BG7/4yIWKygJCFEHlvHAu2A2OOxldGnzypPSc+sZHrfp7qzHieTSbBrJObHGq/lL\n"
    "bIhfZmYQIPRuWO/s81qTs9/rSAiSIvH8qSOMAoIxjzXgihB0w+vceR7Vc1FgOIBk\n"
    "vs/oR6Tw4s9Miyf+2uNiKx2JTOk+XcfOJY/7Uf1gvlGFgClbg6Vo0dk0+A/0Hp95\n"
    "q6UORUnN0K8ikDOiF+p7nAw+BjK19rFR6AvOb3lzUq0B5vGkOQvE+oGwrVU2x4p+\n"
    "RZsdA5w/AgMBAAECggEAD5dpth/sZme60tLAnk2Co7P51y4hGTp5FZsNFGNuj4FU\n"
    "c4zGyaWjSioivGNFsirJK5fsbbYYGAVRpjOG+dY/USKVl0jVgoVie8eKWazgEGJC\n"
    "TtENpxC/bIsn5bivpgH9oMAFU5XZI+Mm97pY49RVvPdui67CZWiDLfH/eAJV0Lik\n"
    "lpqLYiEDkujET6/6vutjlGa4wryh1/RR8UuPKTR05qsE72BaSR2JH9Z9oqRV4TZf\n"
    "dZGhJFmQLKjjG8tUUFhZ09nT1ZxE7Ksyz67Zmmc4OhP1K0Wk6vXfymf6ydAJz/IM\n"
    "LKaQFXslYgAqkOdrgpTn8pyVbswHxkh97x2oK5jmAQKBgQDTnY6/PAfHfKFAfAgd\n"
    "JvkWgh4Qu906vpqN3wxvO8icPcOhC9q0PAW0S4WG+OgSkzEI3JOT8VQ83eICwc0e\n"
    "TF0K24LfmL6wFPQc4MpH0JoKSf/H1AH6GW8GuCAYouqqKUkSINxE1Xb4GgyxsOVD\n"
    "oYBlI4/GiM1oARlmikWQA6f6aQKBgQDEFl3if46jzmsQ/QYTL6elR76U3PDd6sm6\n"
    "aoJHI+cR/n7fNhQe6NmkJkGhPSV3lJX6PD8xuaNA6ICz70u6qpNZFPT/exU17M5E\n"
    "be37Zzgi35XOpdUKE+q6KyDGs1u2CExRRmBgeSoYNKmIRJSrAWenn+7z3wsxVs50\n"
    "3MnRi6t8ZwKBgFW8+44dy2UW6NqQplTxanClLXtIMn1Ppp0epxIWXdR37eA9RlJK\n"
    "E2GWS7aA+L415uMZQ63oVGII4D6PuQSymewBPkhLwc7j3ngo16w7J35NtxWGn3pu\n"
    "fZ5yUYbJ01w4OAPhTn2RI4+oNkZ0Tp+VOsk/nEwrN1YRQ/JYyZBs+s0pAoGAAn7O\n"
    "Xw2EgS0lS6gMpb3bgkzxbe+J2SkdvBOS6NjaHe6BKtIDW4lXC7G48BZZHbZ0WiTe\n"
    "IQHdw/uxd5i5+/wjt+R+YantUSxcS56igqqDml4CCAkwNtqeCBR4bE7oAd8FKZud\n"
    "qlygXoJ9DdDhm3N/IDD213+lItGZ+v68SsBeRE0CgYEAqgdKvwTzpI8yQo8a9w8t\n"
    "aTAAJ9gROm/SloKxzlhCgcYVCMzkp4Ipd6soY3CoP+n8JPfQzrpdxXwEgWms6/Bj\n"
    "4f8pMJZgalDuS6RBtzkwh/UEl7vDNyhjK9ByrAoqlpLqGX6yA8tNJpyb8aKURjls\n"
    "BddUAynGiZEqoOzD3sNqCpg=\n"
    "-----END PRIVATE KEY-----\n";

// alice.p12 (her key, certificate and the CA), password "geheim", base64.
const char* kAliceP12 =
    "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"
    "dcWFVv5gFAQIeMnBNytZjY4CAggA";

const std::string kMessage =
    "From: Alice Test <alice@example.com>\r\n"
    "To: Bob Test <bob@example.com>\r\n"
    "Subject: Quartalszahlen\r\n"
    "Date: Mon, 26 Oct 2026 10:00:00 +0100\r\n"
    "Message-ID: <q3@example.com>\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Content-Transfer-Encoding: 8bit\r\n"
    "\r\n"
    "Die Zahlen f\xC3\xBCr Q3 sind gut: 1.234.567 \xE2\x82\xAC Umsatz.\r\n";

SecureKeys KeysOf(std::vector<SmimeIdentity> own, std::vector<std::pair<std::string, std::string>> known) {
    SecureKeys keys;
    keys.smimeIdentities = [own]() { return own; };
    keys.smimeIdentityFor = [own](const std::string& address, SmimeIdentity& out) {
        for (const SmimeIdentity& id : own) {
            CertificateInfo info;
            if (!ReadCertificate(id.certificatePem, info)) continue;
            for (const std::string& e : info.emails)
                if (e == address) { out = id; return true; }
        }
        return false;
    };
    keys.smimeCertificateFor = [known](const std::string& address, std::string& pem) {
        for (const auto& [a, p] : known)
            if (a == address) { pem = p; return true; }
        return false;
    };
    return keys;
}

SmimeIdentity Alice() { return { std::string(kAlicePem) + kCaPem, kAliceKey }; }
SmimeIdentity Bob() { return { std::string(kBobPem) + kCaPem, kBobKey }; }

std::string BodyText(const std::string& raw) {
    UltraNetMimeMessage parsed;
    if (!UltraNet_MimeParse(raw, parsed)) return std::string();
    std::string text;
    bool html = false;
    UltraNet_MimeGetDisplayBody(parsed, text, html);
    return text;
}

} // namespace

TEST(securemime_raw_mime_pieces) {
    std::string headers, body;
    REQUIRE(SplitEntity("A: 1\r\nB: two\r\n  folded\r\n\r\nbody\r\n", headers, body));
    REQUIRE_EQ(body, std::string("body\r\n"));
    REQUIRE_EQ(EntityHeader(headers, "b"), std::string("two   folded"));
    const std::string ct = "multipart/signed; micalg=sha-256;\r\n protocol=\"application/pkcs7-signature\"; boundary=\"b;1\"";
    REQUIRE_EQ(MediaType(ct), std::string("multipart/signed"));
    REQUIRE_EQ(MediaTypeParameter(ct, "Protocol"), std::string("application/pkcs7-signature"));
    REQUIRE_EQ(MediaTypeParameter(ct, "boundary"), std::string("b;1"));
    // A part is exactly what stands between its boundary lines: the line
    // break before a boundary belongs to the boundary.
    const std::vector<std::string> parts =
        MultipartParts("preamble\r\n--b;1\r\nfirst\r\n\r\n--b;1\r\nsecond\r\n--b;1--\r\nepilogue", "b;1");
    REQUIRE_EQ(parts.size(), size_t(2));
    REQUIRE_EQ(parts[0], std::string("first\r\n"));
    REQUIRE_EQ(parts[1], std::string("second"));
    REQUIRE_EQ(CanonicalCrlf("a\nb\r\nc\n"), std::string("a\r\nb\r\nc\r\n"));
    const std::string replaced = ReplaceContent(kMessage, "Content-Type: text/html\r\n\r\n<p>x</p>");
    REQUIRE(replaced.find("Subject: Quartalszahlen\r\n") != std::string::npos);
    REQUIRE(replaced.find("Content-Type: text/html\r\n\r\n<p>x</p>") != std::string::npos);
    REQUIRE(replaced.find("text/plain") == std::string::npos);
    REQUIRE(SecureFormOf(kMessage) == SecureMethod::Plain);
}

TEST(securemime_smime_sign_and_check) {
    if (!SmimeAvailable()) return;
    const SecureKeys keys = KeysOf({ Alice() }, {});
    SecureSendOptions options;
    options.method = SecureMethod::Smime;
    options.sign = true;
    options.from = "alice@example.com";
    options.recipients = { "bob@example.com" };
    REQUIRE(CanSignAs(SecureMethod::Smime, "Alice <alice@example.com>", keys));
    std::string sent, error;
    REQUIRE(SecureOutgoingMessage(kMessage, options, keys, sent, error));
    REQUIRE(sent.find("multipart/signed") != std::string::npos);
    REQUIRE(sent.find("Subject: Quartalszahlen") != std::string::npos);
    // 8bit text goes out quoted-printable: no mail path may rewrite signed bytes.
    REQUIRE(sent.find("quoted-printable") != std::string::npos);
    REQUIRE(SecureFormOf(sent) == SecureMethod::Smime);

    SecureMessage read = OpenSecureMessage(sent, SecureKeys{});
    REQUIRE_EQ(read.signatures.size(), size_t(1));
    const SignatureCheck& check = read.signatures[0];
    // The test CA is trusted nowhere: the signature holds, the signer is not vouched for.
    REQUIRE(check.verdict == SignatureVerdict::Untrusted);
    REQUIRE(check.matchesFrom);
    REQUIRE_EQ(check.signerEmail, std::string("alice@example.com"));
    REQUIRE_EQ(check.signerName, std::string("Alice Test"));
    REQUIRE(!check.certificatePem.empty());
    REQUIRE(BodyText(read.raw).find("Umsatz") != std::string::npos);
    REQUIRE(read.raw.find("smime.p7s") == std::string::npos);   // the signature part is gone

    // Changed after signing: one letter of the text.
    std::string changed = sent;
    const size_t at = changed.find("Umsatz");
    REQUIRE(at != std::string::npos);
    changed[at] = 'V';
    read = OpenSecureMessage(changed, SecureKeys{});
    REQUIRE_EQ(read.signatures.size(), size_t(1));
    REQUIRE(read.signatures[0].verdict == SignatureVerdict::Invalid);

    // Received with LF line ends (a store that dropped the CRs): still holds.
    std::string lf;
    for (char c : sent) if (c != '\r') lf += c;
    read = OpenSecureMessage(lf, SecureKeys{});
    REQUIRE(read.signatures.size() == 1 && read.signatures[0].verdict == SignatureVerdict::Untrusted);
}

TEST(securemime_smime_encrypt_to_recipient_and_sender) {
    if (!SmimeAvailable()) return;
    const SecureKeys aliceKeys = KeysOf({ Alice() }, { { "bob@example.com", kBobPem } });
    SecureSendOptions options;
    options.method = SecureMethod::Smime;
    options.sign = true;
    options.encrypt = true;
    options.from = "alice@example.com";
    options.recipients = { "Bob Test <bob@example.com>" };
    REQUIRE(MissingRecipientKeys(options, aliceKeys).empty());
    std::string sent, error;
    REQUIRE(SecureOutgoingMessage(kMessage, options, aliceKeys, sent, error));
    REQUIRE(sent.find("Umsatz") == std::string::npos);
    REQUIRE(sent.find("enveloped-data") != std::string::npos);
    bool encrypted = false;
    REQUIRE(SecureFormOf(sent, &encrypted) == SecureMethod::Smime && encrypted);

    // Bob opens it, and finds Alice's signature inside.
    SecureMessage read = OpenSecureMessage(sent, KeysOf({ Bob() }, {}));
    REQUIRE(read.encrypted && read.decrypted);
    REQUIRE(read.problem.empty());
    REQUIRE(BodyText(read.raw).find("Umsatz") != std::string::npos);
    REQUIRE_EQ(read.signatures.size(), size_t(1));
    REQUIRE(read.signatures[0].matchesFrom);
    // So does Alice - the copy in her Sent folder.
    read = OpenSecureMessage(sent, KeysOf({ Alice() }, {}));
    REQUIRE(read.decrypted);
    // Nobody else.
    read = OpenSecureMessage(sent, SecureKeys{});
    REQUIRE(read.encrypted && !read.decrypted && !read.problem.empty());

    // A recipient without a certificate: refused before anything is made.
    options.recipients = { "bob@example.com", "carol@example.com" };
    const std::vector<std::string> missing = MissingRecipientKeys(options, aliceKeys);
    REQUIRE(missing.size() == 1 && missing[0] == "carol@example.com");
    REQUIRE(!SecureOutgoingMessage(kMessage, options, aliceKeys, sent, error));
    REQUIRE(error.find("carol@example.com") != std::string::npos);
    REQUIRE(sent.empty());
}

TEST(securemime_smime_pkcs12_and_certificates) {
    if (!SmimeAvailable()) return;
    std::vector<uint8_t> p12;
    REQUIRE(UltraNet_Base64Decode(kAliceP12, p12));
    SmimeIdentity identity;
    std::string error;
    REQUIRE(!ImportPkcs12(p12, "falsch", identity, error));
    REQUIRE(error.find("password") != std::string::npos);
    REQUIRE(ImportPkcs12(p12, "geheim", identity, error));
    REQUIRE(identity.privateKeyPem.find("PRIVATE KEY") != std::string::npos);
    CertificateInfo info;
    REQUIRE(ReadCertificate(identity.certificatePem, info));
    REQUIRE_EQ(info.name, std::string("Alice Test"));
    REQUIRE(info.emails.size() == 1 && info.emails[0] == "alice@example.com");
    REQUIRE(info.issuer.find("UltraMail Test") != std::string::npos);
    REQUIRE(info.validUntil > 4000000000LL);   // 2126
    REQUIRE(info.fingerprint.size() == 95);    // 32 bytes as AA:BB:...
    // The imported identity signs.
    const SecureKeys keys = KeysOf({ identity }, {});
    SecureSendOptions options;
    options.method = SecureMethod::Smime;
    options.sign = true;
    options.from = "alice@example.com";
    std::string sent;
    REQUIRE(SecureOutgoingMessage(kMessage, options, keys, sent, error));
    REQUIRE(OpenSecureMessage(sent, SecureKeys{}).signatures.size() == 1);
}


namespace {

// The outgoing server: what it was handed.
class RecordingSmtp : public IMailProtocolPlugin {
public:
    std::vector<std::string> wire;   // what went out, per message
    std::vector<std::vector<std::string>> envelopes;
    std::string GetName() const override { return "RecordingSMTP"; }
    std::string GetVersion() const override { return "0"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"smtp"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}
    UltraNetResult SendMail(const UltraNetMailMessage& m, const UltraNetMailOptions&) override {
        wire.push_back(UltraNet_MimeBuildMailMessage(m));
        std::vector<std::string> to = m.to;
        to.insert(to.end(), m.bcc.begin(), m.bcc.end());
        envelopes.push_back(to);
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
};

// The incoming server: what was filed in which folder.
class RecordingMailbox : public IMailboxProtocolPlugin {
public:
    std::map<std::string, std::vector<std::string>> appended;
    std::string GetName() const override { return "RecordingMailbox"; }
    std::string GetVersion() const override { return "0"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"imaps"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}
    UltraNetResult SendMail(const UltraNetMailMessage&, const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme, "fake");
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult ListFolders(const std::string&, std::vector<UltraNetMailFolder>&,
                               const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult GetMailboxStatus(const std::string&, const std::string&, UltraNetMailboxStatus&,
                                    const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult FetchEnvelopes(const std::string&, const std::string&, uint32_t,
                                  std::vector<UltraNetMailEnvelope>& out, const UltraNetMailOptions&) override {
        out.clear();
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessage(const std::string&, const std::string&, uint32_t, std::string&,
                                const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult StoreFlags(const std::string&, const std::string&, uint32_t, UltraNetMailFlags, bool,
                              const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult MoveMessage(const std::string&, const std::string&, uint32_t, const std::string&,
                               const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult AppendMessage(const std::string&, const std::string& folder, const std::string& raw,
                                 UltraNetMailFlags, const UltraNetMailOptions&) override {
        appended[folder].push_back(raw);
        return UltraNetResult::Ok();
    }
};

} // namespace

TEST(securemime_outbox_sends_and_files_only_ciphertext) {
    if (!SmimeAvailable()) return;
    OutboxStore store;
    REQUIRE(store.Open("securemime-outbox", ":memory:").success);
    Draft d;
    d.fromName = "Alice Test";
    d.fromAddr = "alice@example.com";
    d.to = { "bob@example.com" };
    d.bcc = { "bob.archive@example.com" };
    d.subject = "Quartalszahlen";
    d.body = "Die Zahlen f\xC3\xBCr Q3: 1.234.567 \xE2\x82\xAC Umsatz.";
    d.secureMethod = "S/MIME";
    d.sign = true;
    d.encrypt = true;
    int64_t id = 0;
    REQUIRE(store.Enqueue("alice", "smtp://example.com", d, id).success);

    Outbox outbox(store);
    RecordingSmtp smtp;
    RecordingMailbox mailbox;
    ServerCopies copies;
    copies.imap = &mailbox;
    copies.prepare = [](const std::string&, ServerFolders& where) {
        where.serverUrl = "imaps://example.com";
        where.draftsFolder = "Drafts";
        where.sentFolder = "Sent";
        return UltraNetResult::Ok();
    };

    // No certificate for the Bcc address: nothing is sent, nothing filed.
    outbox.SetSecureKeys(KeysOf({ Alice() }, { { "bob@example.com", kBobPem } }));
    Outbox::FlushStats stats = outbox.Flush(smtp, Outbox::OptionsResolver{}, &copies);
    REQUIRE_EQ(stats.sent, 0);
    REQUIRE_EQ(stats.failed, 1);
    REQUIRE(stats.lastFailure.message.find("bob.archive@example.com") != std::string::npos);
    REQUIRE(smtp.wire.empty());
    REQUIRE(mailbox.appended.empty());

    // With every certificate: out, in Drafts while waiting, and in Sent - the
    // same ciphertext everywhere, opened by Bob and by Alice.
    outbox.SetSecureKeys(KeysOf({ Alice() }, { { "bob@example.com", kBobPem },
                                               { "bob.archive@example.com", kBobPem } }));
    stats = outbox.Flush(smtp, Outbox::OptionsResolver{}, &copies);
    REQUIRE_EQ(stats.sent, 1);
    REQUIRE_EQ(smtp.wire.size(), size_t(1));
    const std::string& sent = smtp.wire[0];
    REQUIRE(sent.find("Umsatz") == std::string::npos);
    REQUIRE(sent.find("enveloped-data") != std::string::npos);
    REQUIRE(sent.find("bob.archive") == std::string::npos);    // Bcc only in the envelope
    REQUIRE(smtp.envelopes[0].size() == 2);
    REQUIRE(mailbox.appended["Drafts"].size() == 1 && mailbox.appended["Sent"].size() == 1);
    REQUIRE(mailbox.appended["Drafts"][0].find("Umsatz") == std::string::npos);
    REQUIRE(mailbox.appended["Drafts"][0].find("X-UltraMail-Outbox") != std::string::npos);
    REQUIRE_EQ(mailbox.appended["Sent"][0], sent);
    SecureMessage read = OpenSecureMessage(sent, KeysOf({ Bob() }, {}));
    REQUIRE(read.decrypted && BodyText(read.raw).find("Umsatz") != std::string::npos);
    REQUIRE(read.signatures.size() == 1 && read.signatures[0].matchesFrom);
    REQUIRE(OpenSecureMessage(mailbox.appended["Sent"][0], KeysOf({ Alice() }, {})).decrypted);
    UltraDb_CloseConnection("securemime-outbox");
}

#if defined(ULTRAMAIL_HAVE_OPENPGP)
namespace {

// A GnuPG home of this run's own, with keys for alice and bob.
bool MakePgpHome() {
    static int made = -1;
    if (made >= 0) return made == 1;
    const fs::path home = fs::temp_directory_path() / "ultramail-gnupg-test";
    std::error_code ec;
    fs::remove_all(home, ec);
    fs::create_directories(home);
    fs::permissions(home, fs::perms::owner_all, fs::perm_options::replace, ec);
#if defined(_WIN32)
    _putenv_s("GNUPGHOME", home.string().c_str());
#else
    setenv("GNUPGHOME", home.c_str(), 1);
#endif
    made = 0;
    if (!OpenPgpAvailable()) return false;
    for (const char* uid : { "Alice Test <alice@example.com>", "Bob Test <bob@example.com>" }) {
        gpgme_ctx_t ctx = nullptr;
        if (gpgme_new(&ctx) != GPG_ERR_NO_ERROR) return false;
        const gpgme_error_t err = gpgme_op_createkey(ctx, uid, "default", 0, 0, nullptr,
                                                     GPGME_CREATE_NOPASSWD | GPGME_CREATE_FORCE | GPGME_CREATE_NOEXPIRE);
        gpgme_release(ctx);
        if (err != GPG_ERR_NO_ERROR) {
            std::printf("         (OpenPGP keys could not be made: %s)\n", gpgme_strerror(err));
            return false;
        }
    }
    made = 1;
    return true;
}

} // namespace

TEST(securemime_openpgp_sign_and_check) {
    if (!MakePgpHome()) return;
    SecureSendOptions options;
    options.method = SecureMethod::OpenPgp;
    options.sign = true;
    options.from = "alice@example.com";
    options.recipients = { "bob@example.com" };
    REQUIRE(CanSignAs(SecureMethod::OpenPgp, "alice@example.com", SecureKeys{}));
    REQUIRE(!CanSignAs(SecureMethod::OpenPgp, "carol@example.com", SecureKeys{}));
    std::string sent, error;
    REQUIRE(SecureOutgoingMessage(kMessage, options, SecureKeys{}, sent, error));
    REQUIRE(sent.find("application/pgp-signature") != std::string::npos);
    REQUIRE(SecureFormOf(sent) == SecureMethod::OpenPgp);
    SecureMessage read = OpenSecureMessage(sent, SecureKeys{});
    REQUIRE_EQ(read.signatures.size(), size_t(1));
    // The keyring made the key, so it trusts it ultimately.
    REQUIRE(read.signatures[0].verdict == SignatureVerdict::Valid);
    REQUIRE(read.signatures[0].matchesFrom);
    REQUIRE(BodyText(read.raw).find("Umsatz") != std::string::npos);

    std::string changed = sent;
    changed[changed.find("Umsatz")] = 'V';
    read = OpenSecureMessage(changed, SecureKeys{});
    REQUIRE(read.signatures.size() == 1 && read.signatures[0].verdict == SignatureVerdict::Invalid);
}

TEST(securemime_openpgp_encrypt_and_inline) {
    if (!MakePgpHome()) return;
    SecureSendOptions options;
    options.method = SecureMethod::OpenPgp;
    options.sign = true;
    options.encrypt = true;
    options.from = "alice@example.com";
    options.recipients = { "bob@example.com" };
    REQUIRE(MissingRecipientKeys(options, SecureKeys{}).empty());
    std::string sent, error;
    REQUIRE(SecureOutgoingMessage(kMessage, options, SecureKeys{}, sent, error));
    REQUIRE(sent.find("multipart/encrypted") != std::string::npos);
    REQUIRE(sent.find("Umsatz") == std::string::npos);
    SecureMessage read = OpenSecureMessage(sent, SecureKeys{});
    REQUIRE(read.encrypted && read.decrypted);
    REQUIRE(BodyText(read.raw).find("Umsatz") != std::string::npos);
    REQUIRE(read.signatures.size() == 1 && read.signatures[0].verdict == SignatureVerdict::Valid);

    options.recipients = { "carol@example.com" };
    REQUIRE(MissingRecipientKeys(options, SecureKeys{}).size() == 1);
    REQUIRE(!SecureOutgoingMessage(kMessage, options, SecureKeys{}, sent, error));

    // Inline PGP: an armored message in a plain-text body.
    gpgme_ctx_t ctx = nullptr;
    REQUIRE(gpgme_new(&ctx) == GPG_ERR_NO_ERROR);
    gpgme_set_armor(ctx, 1);
    gpgme_key_t bob = nullptr;
    REQUIRE(gpgme_op_keylist_start(ctx, "<bob@example.com>", 0) == GPG_ERR_NO_ERROR);
    REQUIRE(gpgme_op_keylist_next(ctx, &bob) == GPG_ERR_NO_ERROR);
    gpgme_op_keylist_end(ctx);
    gpgme_key_t to[] = { bob, nullptr };
    gpgme_data_t in = nullptr, out = nullptr;
    const std::string text = "Treffen um 15 Uhr.\n";
    gpgme_data_new_from_mem(&in, text.data(), text.size(), 1);
    gpgme_data_new(&out);
    REQUIRE(gpgme_op_encrypt(ctx, to, GPGME_ENCRYPT_ALWAYS_TRUST, in, out) == GPG_ERR_NO_ERROR);
    size_t length = 0;
    char* armored = gpgme_data_release_and_get_mem(out, &length);
    const std::string block(armored, length);
    gpgme_free(armored);
    gpgme_data_release(in);
    gpgme_key_unref(bob);
    gpgme_release(ctx);
    const std::string inlineMail = "From: alice@example.com\r\nSubject: inline\r\n"
                                   "Content-Type: text/plain\r\n\r\nHallo,\r\n\r\n" + block;
    bool encrypted = false;
    REQUIRE(SecureFormOf(inlineMail, &encrypted) == SecureMethod::OpenPgp && encrypted);
    read = OpenSecureMessage(inlineMail, SecureKeys{});
    REQUIRE(read.decrypted);
    REQUIRE(BodyText(read.raw).find("Treffen um 15 Uhr.") != std::string::npos);
    REQUIRE(BodyText(read.raw).find("Hallo,") != std::string::npos);
}
#endif

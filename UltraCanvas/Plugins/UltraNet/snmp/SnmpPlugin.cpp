// UltraCanvas/Plugins/UltraNet/snmp/SnmpPlugin.cpp
// SNMP v2c plug-in. Implements IDirectoryProtocolPlugin::Search where:
//   - url      = "snmp://<host>[:port]" (port defaults to 161)
//   - baseDn   = starting OID (e.g. "1.3.6.1.2.1.1" for sysContact tree)
//   - scope    = 0 (base, one GET) / 1 or 2 (walk subtree via GETNEXT)
//   - filter   = community string (defaults to "public")
//   - sizeLimit = max OIDs to return when walking
//   - timeLimitSeconds = per-request timeout
//
// Each returned UltraNetDirectoryEntry has:
//   dn                   = the OID in dotted form
//   attributes["value"]  = stringified SNMP value
//   attributes["type"]   = SNMP type name (INTEGER / OCTET_STRING / etc.)
//
// Backend: net-snmp (libsnmp). v3 (authentication / encryption) is out of
// scope for this v0.1 plug-in.
// Version: 0.1.1
// Last Modified: 2026-07-05
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>

// net-snmp pulls in massive headers; isolate them.
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::once_flag g_initOnce;
void InitSnmpOnce() {
    std::call_once(g_initOnce, []{
        init_snmp("ultranet_snmp");
    });
}

std::string OidToString(const oid* o, std::size_t len) {
    std::ostringstream os;
    for (std::size_t i = 0; i < len; ++i) {
        if (i) os << '.';
        os << o[i];
    }
    return os.str();
}

bool ParseOid(const std::string& s, oid* out, std::size_t* outLen) {
    *outLen = MAX_OID_LEN;
    return snmp_parse_oid(s.c_str(), out, outLen) != nullptr;
}

const char* VarTypeName(u_char t) {
    switch (t) {
        case ASN_INTEGER:    return "INTEGER";
        case ASN_OCTET_STR:  return "OCTET_STRING";
        case ASN_OBJECT_ID:  return "OID";
        case ASN_IPADDRESS:  return "IpAddress";
        case ASN_COUNTER:    return "Counter32";
        case ASN_GAUGE:      return "Gauge32";
        case ASN_TIMETICKS:  return "TimeTicks";
        case ASN_COUNTER64:  return "Counter64";
        case ASN_NULL:       return "NULL";
        default:             return "UNKNOWN";
    }
}

std::string VarValueToString(const netsnmp_variable_list* v) {
    if (!v) return {};
    char buf[256];
    switch (v->type) {
        case ASN_INTEGER:
            std::snprintf(buf, sizeof buf, "%ld", *v->val.integer);
            return buf;
        case ASN_COUNTER:
        case ASN_GAUGE:
        case ASN_TIMETICKS:
            std::snprintf(buf, sizeof buf, "%lu",
                          static_cast<unsigned long>(*v->val.integer));
            return buf;
        case ASN_COUNTER64:
            std::snprintf(buf, sizeof buf, "%llu",
                          (unsigned long long)v->val.counter64->high << 32 |
                          v->val.counter64->low);
            return buf;
        case ASN_OCTET_STR:
            return std::string(reinterpret_cast<const char*>(v->val.string),
                               v->val_len);
        case ASN_OBJECT_ID:
            return OidToString(v->val.objid, v->val_len / sizeof(oid));
        case ASN_IPADDRESS:
            if (v->val_len == 4) {
                std::snprintf(buf, sizeof buf, "%u.%u.%u.%u",
                              v->val.string[0], v->val.string[1],
                              v->val.string[2], v->val.string[3]);
                return buf;
            }
            return {};
        case ASN_NULL:
            return {};
        default:
            return "<unsupported-type>";
    }
}

bool IsOidPrefix(const oid* prefix, std::size_t prefixLen,
                 const oid* candidate, std::size_t candidateLen) {
    if (candidateLen < prefixLen) return false;
    for (std::size_t i = 0; i < prefixLen; ++i) {
        if (prefix[i] != candidate[i]) return false;
    }
    return true;
}

// Issues one SNMP request (GET or GETNEXT) and returns the response.
// Caller must snmp_free_pdu the response. snmp_session ownership stays
// with the caller too.
netsnmp_pdu* DoRequest(netsnmp_session* sess, int reqType,
                       const oid* o, std::size_t oLen) {
    netsnmp_pdu* req = snmp_pdu_create(reqType);
    if (!req) return nullptr;
    snmp_add_null_var(req, o, oLen);

    netsnmp_pdu* resp = nullptr;
    const int rc = snmp_synch_response(sess, req, &resp);
    if (rc != STAT_SUCCESS || !resp ||
        resp->errstat != SNMP_ERR_NOERROR) {
        if (resp) snmp_free_pdu(resp);
        return nullptr;
    }
    return resp;
}

class SnmpPlugin : public IDirectoryProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-SNMP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"snmp"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        InitSnmpOnce();
        return UltraNetResult::Ok();
    }
    void Shutdown() override {}

    UltraNetResult Search(const std::string& url,
                          const UltraNetDirectoryQuery& q,
                          std::vector<UltraNetDirectoryEntry>& out) override {
        out.clear();
        InitSnmpOnce();

        UltraNetUrlComponents c;
        if (!UltraNet_ParseUrl(url, c) || c.scheme != "snmp") {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "expected snmp:// URL");
        }
        const int port = c.port > 0 ? c.port : 161;
        std::ostringstream peer;
        peer << "udp:" << c.host << ':' << port;

        const std::string community = q.filter.empty() ? "public" : q.filter;

        // Build a per-call SNMP session. v2c == reasonable default; v3
        // wiring is a future extension.
        netsnmp_session sess{};
        snmp_sess_init(&sess);
        sess.peername      = const_cast<char*>(peer.str().c_str());
        sess.version       = SNMP_VERSION_2c;
        sess.community     = const_cast<u_char*>(
            reinterpret_cast<const u_char*>(community.c_str()));
        sess.community_len = community.size();
        if (q.timeLimitSeconds > 0) {
            sess.timeout = static_cast<long>(q.timeLimitSeconds) * 1000000L;
        }
        netsnmp_session* live = snmp_open(&sess);
        if (!live) {
            return UltraNetResult::Error(UltraNetResultCode::ConnectionRefused,
                                         "snmp_open failed");
        }

        oid baseOid[MAX_OID_LEN];
        std::size_t baseLen = 0;
        if (!ParseOid(q.baseDn, baseOid, &baseLen) || baseLen == 0) {
            snmp_close(live);
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "baseDn is not a valid OID");
        }

        const bool walk = (q.scope >= 1);
        const std::size_t maxResults = q.sizeLimit > 0
            ? static_cast<std::size_t>(q.sizeLimit)
            : (walk ? 64 : 1);

        oid cur[MAX_OID_LEN];
        std::memcpy(cur, baseOid, baseLen * sizeof(oid));
        std::size_t curLen = baseLen;

        for (std::size_t produced = 0; produced < maxResults; ++produced) {
            netsnmp_pdu* resp = DoRequest(
                live, walk ? SNMP_MSG_GETNEXT : SNMP_MSG_GET,
                cur, curLen);
            if (!resp) break;

            // GET returns one var, GETNEXT returns one var (the next).
            netsnmp_variable_list* v = resp->variables;
            if (!v) { snmp_free_pdu(resp); break; }

            // Stop walking when we leave the requested subtree.
            if (walk && !IsOidPrefix(baseOid, baseLen, v->name, v->name_length)) {
                snmp_free_pdu(resp);
                break;
            }
            // End-of-MIB markers.
            if (v->type == SNMP_NOSUCHOBJECT ||
                v->type == SNMP_NOSUCHINSTANCE ||
                v->type == SNMP_ENDOFMIBVIEW) {
                snmp_free_pdu(resp);
                break;
            }

            UltraNetDirectoryEntry e;
            e.dn = OidToString(v->name, v->name_length);
            e.attributes["value"].push_back(VarValueToString(v));
            e.attributes["type"].push_back(VarTypeName(v->type));
            out.push_back(std::move(e));

            if (!walk) { snmp_free_pdu(resp); break; }

            // Advance cur to the next OID for the following GETNEXT.
            std::memcpy(cur, v->name, v->name_length * sizeof(oid));
            curLen = v->name_length;
            snmp_free_pdu(resp);
        }

        snmp_close(live);
        return UltraNetResult::Ok();
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<SnmpPlugin>());
}
#if !defined(_WIN32) && !defined(_WIN64)  // v1 resolves UltraNet_RegisterPlugin from the host at dlopen(); POSIX-only, Windows uses the v2 UltraNet_PluginInit vtable above
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<SnmpPlugin>());
}
#endif

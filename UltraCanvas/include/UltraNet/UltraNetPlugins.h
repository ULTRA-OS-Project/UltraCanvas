// include/UltraNet/UltraNetPlugins.h
// Plugin registry: protocol plug-ins (SMTP/IMAP/POP3, MQTT/AMQP, SSH/Telnet,
// LDAP, RTSP/RTMP/RTP, CoAP, SNMP, mDNS, ...) implement one of the
// I<Category>ProtocolPlugin interfaces below and self-register through the
// UltraNet_RegisterPlugin / Unregister / Get* surface.
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Plugin-facing data structures (declared here so plug-ins can implement the
// specialised interfaces without dragging extra headers).
// ============================================================================
struct UltraNetMailMessage {
    std::string from;
    std::vector<std::string> to;
    std::vector<std::string> cc;
    std::vector<std::string> bcc;
    std::string subject;
    std::string body;
    std::string contentType;            // "text/plain" | "text/html"
    std::map<std::string, std::string> headers;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> attachments; // (name, bytes)
};

struct UltraNetMailOptions {
    UltraNetCredentials credentials;
    bool useTls = true;
    bool implicitTls = false;
    int connectTimeoutMs = 10000;
    int operationTimeoutMs = 30000;
    int maxMessages = 0;                // 0 = unlimited
};

struct UltraNetMessagingOptions {
    UltraNetCredentials credentials;
    std::string clientId;
    int qos = 0;                        // MQTT QoS 0/1/2
    bool cleanSession = true;
    int keepAliveSeconds = 60;
    bool useTls = false;
    int connectTimeoutMs = 10000;
};

struct UltraNetRemoteOptions {
    UltraNetCredentials credentials;
    std::vector<uint8_t> privateKeyPem;
    std::string privateKeyPassword;
    int connectTimeoutMs = 10000;
    int idleTimeoutMs = 0;              // 0 = no idle timeout
    bool requestPty = false;
    std::string term;                   // e.g. "xterm-256color"
};

struct UltraNetDirectoryQuery {
    std::string baseDn;
    std::string filter;
    std::vector<std::string> attributes;
    int scope = 2;                      // 0=base, 1=onelevel, 2=subtree
    int sizeLimit = 0;
    int timeLimitSeconds = 0;
};

struct UltraNetDirectoryEntry {
    std::string dn;
    std::map<std::string, std::vector<std::string>> attributes;
};

struct UltraNetStreamOptions {
    UltraNetCredentials credentials;
    int connectTimeoutMs = 10000;
    bool preferTcp = true;
    std::string transport;              // "udp" | "tcp" | "rtsp"
};

// ============================================================================
// Base plugin interface. Every plug-in implements this; specialised plug-ins
// extend it via one of the I*ProtocolPlugin interfaces.
// ============================================================================
class IUltraNetPlugin {
public:
    virtual ~IUltraNetPlugin() = default;

    virtual std::string GetName() const = 0;
    virtual std::string GetVersion() const = 0;
    virtual std::vector<std::string> GetSupportedSchemes() const = 0;
    virtual UltraNetResult Initialize(const UltraNetConfig& config) = 0;
    virtual void Shutdown() = 0;
};

// SMTP / IMAP / POP3.
class IMailProtocolPlugin : public IUltraNetPlugin {
public:
    virtual UltraNetResult SendMail(
        const UltraNetMailMessage& message,
        const UltraNetMailOptions& options) = 0;

    virtual UltraNetResult FetchMessages(
        const std::string& url,
        std::vector<UltraNetMailMessage>& outMessages,
        const UltraNetMailOptions& options) = 0;
};

// MQTT / AMQP.
class IMessagingProtocolPlugin : public IUltraNetPlugin {
public:
    virtual UltraNetHandle Connect(
        const std::string& url,
        const UltraNetMessagingOptions& options) = 0;

    virtual UltraNetResult Publish(
        UltraNetHandle handle,
        const std::string& topic,
        const std::vector<uint8_t>& payload) = 0;

    virtual UltraNetResult Subscribe(
        UltraNetHandle handle,
        const std::string& topic,
        std::function<void(const std::string&, const std::vector<uint8_t>&)> onMessage) = 0;
};

// SSH / Telnet.
class IRemoteAccessPlugin : public IUltraNetPlugin {
public:
    virtual UltraNetHandle OpenShell(
        const std::string& url,
        const UltraNetRemoteOptions& options) = 0;

    virtual UltraNetResult ExecuteCommand(
        UltraNetHandle handle,
        const std::string& command,
        std::string& outStdOut,
        std::string& outStdErr,
        int& outExitCode) = 0;
};

// LDAP.
class IDirectoryProtocolPlugin : public IUltraNetPlugin {
public:
    virtual UltraNetResult Search(
        const std::string& url,
        const UltraNetDirectoryQuery& query,
        std::vector<UltraNetDirectoryEntry>& outEntries) = 0;
};

// RTSP / RTMP / RTP.
class IStreamingProtocolPlugin : public IUltraNetPlugin {
public:
    virtual UltraNetHandle OpenStream(
        const std::string& url,
        const UltraNetStreamOptions& options) = 0;

    virtual UltraNetResult ReadFrame(
        UltraNetHandle handle,
        std::vector<uint8_t>& outFrame) = 0;
};

// ============================================================================
// Registry surface.
// ============================================================================
void UltraNet_RegisterPlugin(std::shared_ptr<IUltraNetPlugin> plugin);
void UltraNet_UnregisterPlugin(const std::string& pluginName);

std::shared_ptr<IUltraNetPlugin> UltraNet_GetPlugin(const std::string& scheme);
std::vector<std::shared_ptr<IUltraNetPlugin>> UltraNet_GetAllPlugins();

// No-op until dynamic plug-in loading lands; reserved for ABI stability.
void UltraNet_RefreshPlugins();

std::string UltraNet_GetPluginDirectory();
void        UltraNet_SetPluginDirectory(const std::string& path);

// Union of (core schemes baked into UltraNet) and (scheme strings reported
// by every currently-registered plug-in).
std::vector<std::string> UltraNet_GetSupportedSchemes();

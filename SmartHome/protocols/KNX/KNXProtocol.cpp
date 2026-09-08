// KNXProtocol.cpp
// KNX Protocol Implementation for UltraCanvas SmartHome
// Version: 1.0.0
// Last Modified: 2025-12-09
// Author: UltraCanvas Framework

#include "KNXProtocol.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <fstream>
#include <regex>

// Platform-specific socket includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

namespace UltraCanvas {
namespace SmartHome {

// ============================================================================
// KNX ADDRESS PARSING
// ============================================================================

KNXIndividualAddress KNXIndividualAddress::FromString(const std::string& str) {
    KNXIndividualAddress addr;
    std::regex pattern(R"((\d+)\.(\d+)\.(\d+))");
    std::smatch match;
    
    if (std::regex_match(str, match, pattern)) {
        addr.Area = static_cast<uint8_t>(std::stoi(match[1].str()));
        addr.Line = static_cast<uint8_t>(std::stoi(match[2].str()));
        addr.Device = static_cast<uint8_t>(std::stoi(match[3].str()));
    }
    
    return addr;
}

KNXGroupAddress KNXGroupAddress::FromString(const std::string& str) {
    KNXGroupAddress addr;
    
    // Try 3-level format: main/middle/sub
    std::regex pattern3(R"((\d+)/(\d+)/(\d+))");
    std::smatch match;
    
    if (std::regex_match(str, match, pattern3)) {
        addr.Main = static_cast<uint8_t>(std::stoi(match[1].str()));
        addr.Middle = static_cast<uint8_t>(std::stoi(match[2].str()));
        addr.Sub = static_cast<uint8_t>(std::stoi(match[3].str()));
        addr.Raw = ((addr.Main & 0x1F) << 11) | ((addr.Middle & 0x07) << 8) | addr.Sub;
        return addr;
    }
    
    // Try 2-level format: main/sub
    std::regex pattern2(R"((\d+)/(\d+))");
    if (std::regex_match(str, match, pattern2)) {
        uint8_t main = static_cast<uint8_t>(std::stoi(match[1].str()));
        uint16_t sub = static_cast<uint16_t>(std::stoi(match[2].str()));
        addr.Main = main;
        addr.Middle = (sub >> 8) & 0x07;
        addr.Sub = sub & 0xFF;
        addr.Raw = ((main & 0x1F) << 11) | (sub & 0x07FF);
        return addr;
    }
    
    // Try raw format
    try {
        addr.Raw = static_cast<uint16_t>(std::stoi(str));
        addr.Main = (addr.Raw >> 11) & 0x1F;
        addr.Middle = (addr.Raw >> 8) & 0x07;
        addr.Sub = addr.Raw & 0xFF;
    } catch (...) {}
    
    return addr;
}

// ============================================================================
// DATAPOINT TYPE HELPERS
// ============================================================================

int GetDPTSize(KNXDatapointType dpt) {
    uint16_t major = static_cast<uint16_t>(dpt) >> 8;
    
    switch (major) {
        case 0x01: return 0;  // 1 bit (in APCI)
        case 0x02: return 0;  // 1 bit controlled
        case 0x03: return 0;  // 3 bit controlled
        case 0x04: return 1;  // Character
        case 0x05: return 1;  // 8-bit unsigned
        case 0x06: return 1;  // 8-bit signed
        case 0x07: return 2;  // 2-byte unsigned
        case 0x08: return 2;  // 2-byte signed
        case 0x09: return 2;  // 2-byte float
        case 0x0A: return 3;  // Time
        case 0x0B: return 3;  // Date
        case 0x0C: return 4;  // 4-byte unsigned
        case 0x0D: return 4;  // 4-byte signed
        case 0x0E: return 4;  // 4-byte float
        case 0x0F: return 4;  // Access data
        case 0x10: return 14; // String
        case 0x11: return 1;  // Scene number
        case 0x12: return 1;  // Scene control
        case 0x13: return 8;  // Date/Time
        case 0x14: return 1;  // 1-byte enum
        case 0xE8: return 3;  // RGB color
        default: return 0;
    }
}

std::string GetDPTName(KNXDatapointType dpt) {
    switch (dpt) {
        case KNXDatapointType::DPT_Switch: return "Switch";
        case KNXDatapointType::DPT_Bool: return "Boolean";
        case KNXDatapointType::DPT_UpDown: return "Up/Down";
        case KNXDatapointType::DPT_OpenClose: return "Open/Close";
        case KNXDatapointType::DPT_Scaling: return "Scaling %";
        case KNXDatapointType::DPT_Angle: return "Angle";
        case KNXDatapointType::DPT_Control_Dimming: return "Dimming Control";
        case KNXDatapointType::DPT_Control_Blinds: return "Blinds Control";
        case KNXDatapointType::DPT_Value_Temp: return "Temperature";
        case KNXDatapointType::DPT_Value_Humidity: return "Humidity";
        case KNXDatapointType::DPT_Value_Lux: return "Illuminance";
        case KNXDatapointType::DPT_Value_Power: return "Power";
        case KNXDatapointType::DPT_SceneNumber: return "Scene Number";
        case KNXDatapointType::DPT_HVAC_Mode: return "HVAC Mode";
        case KNXDatapointType::DPT_Colour_RGB: return "RGB Color";
        case KNXDatapointType::DPT_String_ASCII: return "String ASCII";
        default: return "Unknown";
    }
}

// ============================================================================
// KNX TELEGRAM
// ============================================================================

std::vector<uint8_t> KNXTelegram::Encode() const {
    std::vector<uint8_t> frame;
    
    // Control byte
    uint8_t ctrl = 0xBC;  // Standard frame, no repeat
    ctrl |= (static_cast<uint8_t>(Priority) << 2);
    if (Repeated) ctrl &= ~0x20;
    frame.push_back(ctrl);
    
    // Source address
    uint16_t srcRaw = Source.ToRaw();
    frame.push_back((srcRaw >> 8) & 0xFF);
    frame.push_back(srcRaw & 0xFF);
    
    // Destination address
    frame.push_back((Destination.Raw >> 8) & 0xFF);
    frame.push_back(Destination.Raw & 0xFF);
    
    // Address type and routing/length counter
    uint8_t atHopLen = (IsGroupAddress ? 0x80 : 0x00);
    atHopLen |= 0x60;  // Hop count = 6
    atHopLen |= (Data.size() & 0x0F);
    frame.push_back(atHopLen);
    
    // TPCI/APCI
    uint8_t tpci = 0x00;  // UDT (Unnumbered Data Telegram)
    uint8_t apci = static_cast<uint8_t>(APCI) << 6;
    
    if (Data.empty()) {
        frame.push_back(tpci);
        frame.push_back(apci);
    } else if (Data.size() == 1 && GetDPTSize(KNXDatapointType::DPT_Switch) == 0) {
        // Small data in APCI
        frame.push_back(tpci);
        frame.push_back(apci | (Data[0] & 0x3F));
    } else {
        frame.push_back(tpci);
        frame.push_back(apci);
        for (uint8_t b : Data) {
            frame.push_back(b);
        }
    }
    
    return frame;
}

KNXTelegram KNXTelegram::Decode(const std::vector<uint8_t>& raw) {
    KNXTelegram telegram;
    
    if (raw.size() < 8) {
        return telegram;
    }
    
    // Control byte
    uint8_t ctrl = raw[0];
    telegram.Priority = static_cast<KNXPriority>((ctrl >> 2) & 0x03);
    telegram.Repeated = !(ctrl & 0x20);
    
    // Source address
    telegram.Source = KNXIndividualAddress((raw[1] << 8) | raw[2]);
    
    // Destination address
    telegram.Destination = KNXGroupAddress((raw[3] << 8) | raw[4]);
    
    // Address type
    telegram.IsGroupAddress = (raw[5] & 0x80) != 0;
    
    // Length
    uint8_t length = raw[5] & 0x0F;
    
    // APCI
    telegram.APCI = static_cast<KNXAPCI>((raw[7] >> 6) & 0x03);
    
    // Data
    if (length == 0) {
        // Small data in APCI
        telegram.Data.push_back(raw[7] & 0x3F);
    } else if (raw.size() > 8) {
        telegram.Data.assign(raw.begin() + 8, raw.begin() + 8 + length);
    }
    
    return telegram;
}

bool KNXTelegram::GetBool() const {
    if (Data.empty()) return false;
    return (Data[0] & 0x01) != 0;
}

uint8_t KNXTelegram::GetUInt8() const {
    if (Data.empty()) return 0;
    return Data[0];
}

int8_t KNXTelegram::GetInt8() const {
    if (Data.empty()) return 0;
    return static_cast<int8_t>(Data[0]);
}

uint16_t KNXTelegram::GetUInt16() const {
    if (Data.size() < 2) return 0;
    return (Data[0] << 8) | Data[1];
}

int16_t KNXTelegram::GetInt16() const {
    if (Data.size() < 2) return 0;
    return static_cast<int16_t>((Data[0] << 8) | Data[1]);
}

uint32_t KNXTelegram::GetUInt32() const {
    if (Data.size() < 4) return 0;
    return (Data[0] << 24) | (Data[1] << 16) | (Data[2] << 8) | Data[3];
}

int32_t KNXTelegram::GetInt32() const {
    if (Data.size() < 4) return 0;
    return static_cast<int32_t>((Data[0] << 24) | (Data[1] << 16) | (Data[2] << 8) | Data[3]);
}

float KNXTelegram::GetFloat16() const {
    if (Data.size() < 2) return 0.0f;
    return KNXProtocol::DecodeFloat16(Data[0], Data[1]);
}

float KNXTelegram::GetFloat32() const {
    if (Data.size() < 4) return 0.0f;
    return KNXProtocol::DecodeFloat32(Data.data());
}

std::string KNXTelegram::GetString() const {
    std::string result;
    for (uint8_t b : Data) {
        if (b == 0) break;
        result += static_cast<char>(b);
    }
    return result;
}

void KNXTelegram::SetBool(bool value) {
    Data.clear();
    Data.push_back(value ? 0x01 : 0x00);
}

void KNXTelegram::SetUInt8(uint8_t value) {
    Data.clear();
    Data.push_back(value);
}

void KNXTelegram::SetInt8(int8_t value) {
    Data.clear();
    Data.push_back(static_cast<uint8_t>(value));
}

void KNXTelegram::SetUInt16(uint16_t value) {
    Data.clear();
    Data.push_back((value >> 8) & 0xFF);
    Data.push_back(value & 0xFF);
}

void KNXTelegram::SetInt16(int16_t value) {
    SetUInt16(static_cast<uint16_t>(value));
}

void KNXTelegram::SetUInt32(uint32_t value) {
    Data.clear();
    Data.push_back((value >> 24) & 0xFF);
    Data.push_back((value >> 16) & 0xFF);
    Data.push_back((value >> 8) & 0xFF);
    Data.push_back(value & 0xFF);
}

void KNXTelegram::SetInt32(int32_t value) {
    SetUInt32(static_cast<uint32_t>(value));
}

void KNXTelegram::SetFloat16(float value) {
    Data.resize(2);
    KNXProtocol::EncodeFloat16(value, Data[0], Data[1]);
}

void KNXTelegram::SetFloat32(float value) {
    Data.resize(4);
    KNXProtocol::EncodeFloat32(value, Data.data());
}

void KNXTelegram::SetString(const std::string& value, size_t maxLen) {
    Data.clear();
    size_t len = std::min(value.length(), maxLen);
    for (size_t i = 0; i < len; i++) {
        Data.push_back(static_cast<uint8_t>(value[i]));
    }
    // Pad with zeros
    while (Data.size() < maxLen) {
        Data.push_back(0);
    }
}

void KNXTelegram::SetScaling(uint8_t percent) {
    Data.clear();
    // Scale 0-100 to 0-255
    uint8_t scaled = static_cast<uint8_t>((percent * 255) / 100);
    Data.push_back(scaled);
}

uint8_t KNXTelegram::GetScaling() const {
    if (Data.empty()) return 0;
    // Scale 0-255 to 0-100
    return static_cast<uint8_t>((Data[0] * 100) / 255);
}

// ============================================================================
// FLOAT CONVERSION (DPT 9.x and 14.x)
// ============================================================================

float KNXProtocol::DecodeFloat16(uint8_t high, uint8_t low) {
    // DPT 9.x format: SEEEEMMM MMMMMMMM
    // S = sign, E = exponent (4 bits), M = mantissa (11 bits, two's complement)
    
    int sign = (high & 0x80) ? -1 : 1;
    int exp = (high >> 3) & 0x0F;
    int mant = ((high & 0x07) << 8) | low;
    
    // Handle negative mantissa (two's complement for 11 bits)
    if (high & 0x80) {
        mant = mant - 2048;
    }
    
    float value = (0.01f * mant) * std::pow(2.0f, exp);
    return value;
}

void KNXProtocol::EncodeFloat16(float value, uint8_t& high, uint8_t& low) {
    // DPT 9.x encoding
    int sign = (value < 0) ? 1 : 0;
    float absVal = std::fabs(value);
    
    int exp = 0;
    float mant = absVal * 100.0f;
    
    while (mant > 2047.0f && exp < 15) {
        mant /= 2.0f;
        exp++;
    }
    
    int mantInt = static_cast<int>(mant);
    if (sign) {
        mantInt = 2048 - mantInt;
    }
    
    high = (sign << 7) | (exp << 3) | ((mantInt >> 8) & 0x07);
    low = mantInt & 0xFF;
}

float KNXProtocol::DecodeFloat32(const uint8_t* data) {
    // DPT 14.x - IEEE 754 float (big endian)
    uint32_t raw = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    float result;
    std::memcpy(&result, &raw, sizeof(float));
    return result;
}

void KNXProtocol::EncodeFloat32(float value, uint8_t* data) {
    // DPT 14.x - IEEE 754 float (big endian)
    uint32_t raw;
    std::memcpy(&raw, &value, sizeof(float));
    data[0] = (raw >> 24) & 0xFF;
    data[1] = (raw >> 16) & 0xFF;
    data[2] = (raw >> 8) & 0xFF;
    data[3] = raw & 0xFF;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

KNXProtocol::KNXProtocol() {
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

KNXProtocol::~KNXProtocol() {
    Shutdown();
    
#ifdef _WIN32
    WSACleanup();
#endif
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool KNXProtocol::Initialize() {
    if (initialized) {
        return true;
    }
    
    // Create UDP socket
    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        std::cerr << "[KNX] Failed to create socket" << std::endl;
        return false;
    }
    
    // Set socket options
    int reuseAddr = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));
    
    // Set non-blocking
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(udpSocket, FIONBIO, &mode);
#else
    int flags = fcntl(udpSocket, F_GETFL, 0);
    fcntl(udpSocket, F_SETFL, flags | O_NONBLOCK);
#endif
    
    // Bind to local port
    struct sockaddr_in localAddr;
    std::memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(0);  // Any available port
    
    if (bind(udpSocket, reinterpret_cast<struct sockaddr*>(&localAddr), sizeof(localAddr)) < 0) {
        std::cerr << "[KNX] Failed to bind socket" << std::endl;
        closesocket(udpSocket);
        udpSocket = -1;
        return false;
    }
    
    initialized = true;
    std::cout << "[KNX] Initialized" << std::endl;
    return true;
}

void KNXProtocol::Shutdown() {
    if (!initialized) {
        return;
    }
    
    Disconnect();
    
    if (udpSocket != INVALID_SOCKET) {
        closesocket(udpSocket);
        udpSocket = -1;
    }
    
    initialized = false;
    std::cout << "[KNX] Shutdown complete" << std::endl;
}

// ============================================================================
// CONNECTION
// ============================================================================

void KNXProtocol::SetGateway(const std::string& ip, uint16_t port) {
    gatewayIP = ip;
    gatewayPort = port;
}

std::string KNXProtocol::GetGateway() const {
    return gatewayIP + ":" + std::to_string(gatewayPort);
}

bool KNXProtocol::Connect() {
    if (!initialized) {
        if (!Initialize()) {
            return false;
        }
    }
    
    if (connected) {
        return true;
    }
    
    // Build CONNECT_REQUEST
    std::vector<uint8_t> request;
    
    // Header (6 bytes)
    request.push_back(0x06);  // Header length
    request.push_back(0x10);  // Protocol version
    request.push_back(0x02);  // Service type high (CONNECT_REQUEST)
    request.push_back(0x05);  // Service type low
    request.push_back(0x00);  // Total length high
    request.push_back(0x1A);  // Total length low (26 bytes)
    
    // Host Protocol Address Information (HPAI) - Control endpoint
    request.push_back(0x08);  // Length
    request.push_back(0x01);  // Protocol code (UDP)
    
    // Get local address
    struct sockaddr_in localAddr;
    socklen_t addrLen = sizeof(localAddr);
    getsockname(udpSocket, reinterpret_cast<struct sockaddr*>(&localAddr), &addrLen);
    
    uint32_t localIP = ntohl(localAddr.sin_addr.s_addr);
    uint16_t localPort = ntohs(localAddr.sin_port);
    
    request.push_back((localIP >> 24) & 0xFF);
    request.push_back((localIP >> 16) & 0xFF);
    request.push_back((localIP >> 8) & 0xFF);
    request.push_back(localIP & 0xFF);
    request.push_back((localPort >> 8) & 0xFF);
    request.push_back(localPort & 0xFF);
    
    // HPAI - Data endpoint (same as control)
    request.push_back(0x08);
    request.push_back(0x01);
    request.push_back((localIP >> 24) & 0xFF);
    request.push_back((localIP >> 16) & 0xFF);
    request.push_back((localIP >> 8) & 0xFF);
    request.push_back(localIP & 0xFF);
    request.push_back((localPort >> 8) & 0xFF);
    request.push_back(localPort & 0xFF);
    
    // Connection Request Information (CRI)
    request.push_back(0x04);  // Length
    request.push_back(static_cast<uint8_t>(KNXnetIPConnectionType::Tunnel));
    request.push_back(static_cast<uint8_t>(KNXnetIPTunnelLayer::LinkLayer));
    request.push_back(0x00);  // Reserved
    
    // Send to gateway
    struct sockaddr_in gatewayAddr;
    std::memset(&gatewayAddr, 0, sizeof(gatewayAddr));
    gatewayAddr.sin_family = AF_INET;
    gatewayAddr.sin_port = htons(gatewayPort);
    inet_pton(AF_INET, gatewayIP.c_str(), &gatewayAddr.sin_addr);
    
    ssize_t sent = sendto(udpSocket, reinterpret_cast<const char*>(request.data()), 
                          request.size(), 0,
                          reinterpret_cast<struct sockaddr*>(&gatewayAddr), 
                          sizeof(gatewayAddr));
    
    if (sent < 0) {
        std::cerr << "[KNX] Failed to send connect request" << std::endl;
        return false;
    }
    
    // Wait for response
    std::vector<uint8_t> response(256);
    struct sockaddr_in fromAddr;
    socklen_t fromLen = sizeof(fromAddr);
    
    // Poll for response
    for (int i = 0; i < 50; i++) {  // 5 second timeout
        ssize_t received = recvfrom(udpSocket, reinterpret_cast<char*>(response.data()),
                                    response.size(), 0,
                                    reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);
        
        if (received > 0) {
            response.resize(received);
            
            // Check if CONNECT_RESPONSE
            if (response.size() >= 8 && response[2] == 0x02 && response[3] == 0x06) {
                HandleConnectResponse(response);
                
                if (connected) {
                    // Start receive and heartbeat threads
                    stopThreads = false;
                    receiveThread = std::thread(&KNXProtocol::ReceiveLoop, this);
                    heartbeatThread = std::thread(&KNXProtocol::HeartbeatLoop, this);
                    
                    std::cout << "[KNX] Connected to " << gatewayIP << ":" << gatewayPort 
                              << " (channel " << (int)channelId << ")" << std::endl;
                    
                    if (onConnection) {
                        onConnection(true, "Connected");
                    }
                    
                    return true;
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cerr << "[KNX] Connection timeout" << std::endl;
    return false;
}

void KNXProtocol::Disconnect() {
    if (!connected) {
        return;
    }
    
    // Stop threads
    stopThreads = true;
    if (receiveThread.joinable()) {
        receiveThread.join();
    }
    if (heartbeatThread.joinable()) {
        heartbeatThread.join();
    }
    
    // Send DISCONNECT_REQUEST
    std::vector<uint8_t> request;
    request.push_back(0x06);  // Header length
    request.push_back(0x10);  // Protocol version
    request.push_back(0x02);  // Service type high (DISCONNECT_REQUEST)
    request.push_back(0x09);  // Service type low
    request.push_back(0x00);  // Total length high
    request.push_back(0x10);  // Total length low (16 bytes)
    request.push_back(channelId);
    request.push_back(0x00);  // Reserved
    
    // HPAI
    struct sockaddr_in localAddr;
    socklen_t addrLen = sizeof(localAddr);
    getsockname(udpSocket, reinterpret_cast<struct sockaddr*>(&localAddr), &addrLen);
    
    uint32_t localIP = ntohl(localAddr.sin_addr.s_addr);
    uint16_t localPort = ntohs(localAddr.sin_port);
    
    request.push_back(0x08);
    request.push_back(0x01);
    request.push_back((localIP >> 24) & 0xFF);
    request.push_back((localIP >> 16) & 0xFF);
    request.push_back((localIP >> 8) & 0xFF);
    request.push_back(localIP & 0xFF);
    request.push_back((localPort >> 8) & 0xFF);
    request.push_back(localPort & 0xFF);
    
    struct sockaddr_in gatewayAddr;
    std::memset(&gatewayAddr, 0, sizeof(gatewayAddr));
    gatewayAddr.sin_family = AF_INET;
    gatewayAddr.sin_port = htons(gatewayPort);
    inet_pton(AF_INET, gatewayIP.c_str(), &gatewayAddr.sin_addr);
    
    sendto(udpSocket, reinterpret_cast<const char*>(request.data()), request.size(), 0,
           reinterpret_cast<struct sockaddr*>(&gatewayAddr), sizeof(gatewayAddr));
    
    connected = false;
    channelId = 0;
    sequenceCounter = 0;
    
    if (onConnection) {
        onConnection(false, "Disconnected");
    }
    
    std::cout << "[KNX] Disconnected" << std::endl;
}

void KNXProtocol::EnableRouting(bool enable) {
    if (!initialized) return;
    
    if (enable && !routingEnabled) {
        // Join multicast group
        struct ip_mreq mreq;
        inet_pton(AF_INET, KNX_MULTICAST_ADDR, &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = INADDR_ANY;
        
        if (setsockopt(udpSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                       reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == 0) {
            routingEnabled = true;
            std::cout << "[KNX] Routing enabled (multicast " << KNX_MULTICAST_ADDR << ")" << std::endl;
        }
    } else if (!enable && routingEnabled) {
        // Leave multicast group
        struct ip_mreq mreq;
        inet_pton(AF_INET, KNX_MULTICAST_ADDR, &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = INADDR_ANY;
        
        setsockopt(udpSocket, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                   reinterpret_cast<const char*>(&mreq), sizeof(mreq));
        routingEnabled = false;
        std::cout << "[KNX] Routing disabled" << std::endl;
    }
}

std::vector<KNXnetIPDeviceInfo> KNXProtocol::SearchDevices(int timeoutMs) {
    std::vector<KNXnetIPDeviceInfo> devices;
    
    if (!initialized) {
        if (!Initialize()) {
            return devices;
        }
    }
    
    // Build SEARCH_REQUEST
    std::vector<uint8_t> request;
    request.push_back(0x06);  // Header length
    request.push_back(0x10);  // Protocol version
    request.push_back(0x02);  // Service type high (SEARCH_REQUEST)
    request.push_back(0x01);  // Service type low
    request.push_back(0x00);  // Total length high
    request.push_back(0x0E);  // Total length low (14 bytes)
    
    // HPAI - Discovery endpoint
    struct sockaddr_in localAddr;
    socklen_t addrLen = sizeof(localAddr);
    getsockname(udpSocket, reinterpret_cast<struct sockaddr*>(&localAddr), &addrLen);
    
    uint32_t localIP = ntohl(localAddr.sin_addr.s_addr);
    uint16_t localPort = ntohs(localAddr.sin_port);
    
    request.push_back(0x08);
    request.push_back(0x01);
    request.push_back((localIP >> 24) & 0xFF);
    request.push_back((localIP >> 16) & 0xFF);
    request.push_back((localIP >> 8) & 0xFF);
    request.push_back(localIP & 0xFF);
    request.push_back((localPort >> 8) & 0xFF);
    request.push_back(localPort & 0xFF);
    
    // Send to multicast
    struct sockaddr_in multicastAddr;
    std::memset(&multicastAddr, 0, sizeof(multicastAddr));
    multicastAddr.sin_family = AF_INET;
    multicastAddr.sin_port = htons(KNX_PORT);
    inet_pton(AF_INET, KNX_MULTICAST_ADDR, &multicastAddr.sin_addr);
    
    sendto(udpSocket, reinterpret_cast<const char*>(request.data()), request.size(), 0,
           reinterpret_cast<struct sockaddr*>(&multicastAddr), sizeof(multicastAddr));
    
    // Also send to broadcast
    struct sockaddr_in broadcastAddr;
    std::memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(KNX_PORT);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;
    
    int broadcast = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST, 
               reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
    
    sendto(udpSocket, reinterpret_cast<const char*>(request.data()), request.size(), 0,
           reinterpret_cast<struct sockaddr*>(&broadcastAddr), sizeof(broadcastAddr));
    
    // Collect responses
    auto startTime = std::chrono::steady_clock::now();
    std::vector<uint8_t> response(256);
    
    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        
        if (elapsed >= timeoutMs) break;
        
        struct sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);
        
        ssize_t received = recvfrom(udpSocket, reinterpret_cast<char*>(response.data()),
                                    response.size(), 0,
                                    reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);
        
        if (received > 0) {
            // Check if SEARCH_RESPONSE
            if (response[2] == 0x02 && response[3] == 0x02) {
                KNXnetIPDeviceInfo info;
                
                // Parse response
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &fromAddr.sin_addr, ipStr, sizeof(ipStr));
                info.IPAddress = ipStr;
                info.Port = ntohs(fromAddr.sin_port);
                
                // Device info starts at offset 6+8 = 14
                if (received > 14 + 54) {
                    // Individual address
                    info.IndividualAddress = KNXIndividualAddress(
                        (response[14 + 2] << 8) | response[14 + 3]);
                    
                    // Serial number (6 bytes at offset 14+4)
                    std::stringstream ss;
                    for (int i = 0; i < 6; i++) {
                        ss << std::hex << std::setfill('0') << std::setw(2) 
                           << static_cast<int>(response[14 + 4 + i]);
                    }
                    info.SerialNumber = ss.str();
                    
                    // MAC address (6 bytes at offset 14+10)
                    ss.str("");
                    for (int i = 0; i < 6; i++) {
                        if (i > 0) ss << ":";
                        ss << std::hex << std::setfill('0') << std::setw(2)
                           << static_cast<int>(response[14 + 10 + i]);
                    }
                    info.MACAddress = ss.str();
                    
                    // Device name (30 bytes at offset 14+16)
                    char name[31] = {0};
                    std::memcpy(name, &response[14 + 16], 30);
                    info.Name = name;
                }
                
                info.LastSeen = std::chrono::system_clock::now();
                devices.push_back(info);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return devices;
}

// ============================================================================
// DISCOVERY (SmartHome Interface)
// ============================================================================

bool KNXProtocol::StartDiscovery(int timeoutSeconds) {
    if (!initialized) {
        return false;
    }
    
    discovering = true;
    
    {
        std::lock_guard<std::mutex> lock(discoveredMutex);
        discoveredDevices.clear();
        discoveredGateways.clear();
    }
    
    // Search for gateways
    auto gateways = SearchDevices(timeoutSeconds * 1000);
    
    {
        std::lock_guard<std::mutex> lock(discoveredMutex);
        discoveredGateways = gateways;
        
        // Convert to SmartHomeDeviceInfo
        for (const auto& gw : gateways) {
            SmartHomeDeviceInfo info;
            info.DeviceId = "knx-gw-" + gw.IPAddress;
            info.Name = gw.Name.empty() ? "KNX Gateway" : gw.Name;
            info.Manufacturer = "KNX";
            info.Model = "KNXnet/IP Gateway";
            info.Protocol = SmartHomeProtocolType::KNX;
            info.Category = SmartHomeDeviceCategory::Hub;
            info.State = SmartHomeDeviceState::Online;
            
            discoveredDevices.push_back(info);
            
            if (onDeviceDiscovered) {
                onDeviceDiscovered(gw);
            }
        }
    }
    
    discovering = false;
    return true;
}

void KNXProtocol::StopDiscovery() {
    discovering = false;
}

std::vector<SmartHomeDeviceInfo> KNXProtocol::GetDiscoveredDevices() {
    std::lock_guard<std::mutex> lock(discoveredMutex);
    return discoveredDevices;
}

// ============================================================================
// PAIRING
// ============================================================================

bool KNXProtocol::PairDevice(const std::string& deviceId,
                             const std::map<std::string, std::string>& params) {
    // KNX doesn't require pairing - devices are configured via ETS
    // Connect to gateway if IP provided
    
    if (params.count("ip")) {
        SetGateway(params.at("ip"));
    }
    
    return Connect();
}

bool KNXProtocol::UnpairDevice(const std::string& deviceId) {
    // Remove from group objects
    KNXGroupAddress addr = ParseDeviceId(deviceId);
    UnregisterGroupObject(addr);
    return true;
}

// ============================================================================
// COMMANDS
// ============================================================================

bool KNXProtocol::SendCommand(const std::string& deviceId, const std::string& command,
                              const std::map<std::string, std::string>& params) {
    if (!connected) {
        return false;
    }
    
    KNXGroupAddress address = ParseDeviceId(deviceId);
    if (address.Raw == 0) {
        // Try to get address from params
        if (params.count("address")) {
            address = KNXGroupAddress::FromString(params.at("address"));
        } else {
            return false;
        }
    }
    
    // Handle commands
    if (command == "on" || command == "turnOn") {
        return SwitchOn(address);
    }
    else if (command == "off" || command == "turnOff") {
        return SwitchOff(address);
    }
    else if (command == "toggle") {
        return SwitchToggle(address);
    }
    else if (command == "setLevel" || command == "setBrightness") {
        uint8_t level = 100;
        if (params.count("level")) {
            level = static_cast<uint8_t>(std::stoi(params.at("level")));
        } else if (params.count("brightness")) {
            level = static_cast<uint8_t>(std::stoi(params.at("brightness")));
        }
        return SetDimLevel(address, level);
    }
    else if (command == "setColor") {
        uint8_t r = 255, g = 255, b = 255;
        if (params.count("red")) r = static_cast<uint8_t>(std::stoi(params.at("red")));
        if (params.count("green")) g = static_cast<uint8_t>(std::stoi(params.at("green")));
        if (params.count("blue")) b = static_cast<uint8_t>(std::stoi(params.at("blue")));
        return GroupWriteRGB(address, r, g, b);
    }
    else if (command == "setPosition") {
        uint8_t pos = 0;
        if (params.count("position")) {
            pos = static_cast<uint8_t>(std::stoi(params.at("position")));
        }
        return SetBlindPosition(address, pos);
    }
    else if (command == "up") {
        return MoveBlind(address, false);
    }
    else if (command == "down") {
        return MoveBlind(address, true);
    }
    else if (command == "stop") {
        return StopBlind(address);
    }
    else if (command == "setTemperature" || command == "setTargetTemp") {
        float temp = 21.0f;
        if (params.count("temperature")) {
            temp = std::stof(params.at("temperature"));
        }
        return SetTemperature(address, temp);
    }
    else if (command == "setHVACMode") {
        uint8_t mode = 0;
        if (params.count("mode")) {
            mode = static_cast<uint8_t>(std::stoi(params.at("mode")));
        }
        return SetHVACMode(address, mode);
    }
    else if (command == "activateScene") {
        uint8_t scene = 0;
        if (params.count("scene")) {
            scene = static_cast<uint8_t>(std::stoi(params.at("scene")));
        }
        return ActivateScene(address, scene, false);
    }
    else if (command == "storeScene") {
        uint8_t scene = 0;
        if (params.count("scene")) {
            scene = static_cast<uint8_t>(std::stoi(params.at("scene")));
        }
        return ActivateScene(address, scene, true);
    }
    else if (command == "read") {
        return GroupRead(address);
    }
    
    return false;
}

bool KNXProtocol::GetDeviceState(const std::string& deviceId,
                                 std::map<std::string, std::string>& state) {
    KNXGroupAddress address = ParseDeviceId(deviceId);
    if (address.Raw == 0) {
        return false;
    }
    
    // Get cached value
    std::vector<uint8_t> value = GetGroupValue(address);
    
    state["address"] = address.ToString();
    state["online"] = connected ? "true" : "false";
    
    if (!value.empty()) {
        // Get DPT from group object if registered
        KNXDatapointType dpt = KNXDatapointType::DPT_Unknown;
        const KNXGroupObject* obj = GetGroupObject(address);
        if (obj) {
            dpt = obj->DPT;
        }
        
        // Decode based on DPT
        uint16_t dptMajor = static_cast<uint16_t>(dpt) >> 8;
        
        switch (dptMajor) {
            case 0x01:  // 1-bit
                state["value"] = (value[0] & 0x01) ? "true" : "false";
                state["on"] = state["value"];
                break;
                
            case 0x05:  // 8-bit unsigned
                if (dpt == KNXDatapointType::DPT_Scaling) {
                    state["level"] = std::to_string((value[0] * 100) / 255);
                    state["brightness"] = state["level"];
                } else {
                    state["value"] = std::to_string(value[0]);
                }
                break;
                
            case 0x09:  // 2-byte float
                if (value.size() >= 2) {
                    float f = DecodeFloat16(value[0], value[1]);
                    state["value"] = std::to_string(f);
                    if (dpt == KNXDatapointType::DPT_Value_Temp) {
                        state["temperature"] = state["value"];
                    } else if (dpt == KNXDatapointType::DPT_Value_Humidity) {
                        state["humidity"] = state["value"];
                    } else if (dpt == KNXDatapointType::DPT_Value_Lux) {
                        state["luminance"] = state["value"];
                    }
                }
                break;
                
            case 0xE8:  // RGB
                if (value.size() >= 3) {
                    state["red"] = std::to_string(value[0]);
                    state["green"] = std::to_string(value[1]);
                    state["blue"] = std::to_string(value[2]);
                }
                break;
                
            default:
                // Raw hex
                std::stringstream ss;
                for (uint8_t b : value) {
                    ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
                }
                state["rawValue"] = ss.str();
                break;
        }
    }
    
    return true;
}

std::vector<SmartHomeDeviceInfo> KNXProtocol::GetPairedDevices() {
    std::vector<SmartHomeDeviceInfo> devices;
    
    std::lock_guard<std::mutex> lock(groupObjectsMutex);
    for (const auto& [rawAddr, obj] : groupObjects) {
        devices.push_back(ConvertToDeviceInfo(obj));
    }
    
    return devices;
}

// ============================================================================
// GROUP COMMUNICATION
// ============================================================================

bool KNXProtocol::GroupWrite(const KNXGroupAddress& address, const std::vector<uint8_t>& value) {
    KNXTelegram telegram;
    telegram.Destination = address;
    telegram.APCI = KNXAPCI::GroupValueWrite;
    telegram.Data = value;
    
    return SendTelegram(telegram);
}

bool KNXProtocol::GroupWriteBool(const KNXGroupAddress& address, bool value) {
    return GroupWrite(address, {static_cast<uint8_t>(value ? 0x01 : 0x00)});
}

bool KNXProtocol::GroupWriteScaling(const KNXGroupAddress& address, uint8_t percent) {
    uint8_t scaled = static_cast<uint8_t>((percent * 255) / 100);
    return GroupWrite(address, {scaled});
}

bool KNXProtocol::GroupWriteFloat16(const KNXGroupAddress& address, float value) {
    uint8_t high, low;
    EncodeFloat16(value, high, low);
    return GroupWrite(address, {high, low});
}

bool KNXProtocol::GroupWriteFloat32(const KNXGroupAddress& address, float value) {
    std::vector<uint8_t> data(4);
    EncodeFloat32(value, data.data());
    return GroupWrite(address, data);
}

bool KNXProtocol::GroupWriteUInt8(const KNXGroupAddress& address, uint8_t value) {
    return GroupWrite(address, {value});
}

bool KNXProtocol::GroupWriteUInt16(const KNXGroupAddress& address, uint16_t value) {
    return GroupWrite(address, {static_cast<uint8_t>((value >> 8) & 0xFF),
                                static_cast<uint8_t>(value & 0xFF)});
}

bool KNXProtocol::GroupWriteUInt32(const KNXGroupAddress& address, uint32_t value) {
    return GroupWrite(address, {static_cast<uint8_t>((value >> 24) & 0xFF),
                                static_cast<uint8_t>((value >> 16) & 0xFF),
                                static_cast<uint8_t>((value >> 8) & 0xFF),
                                static_cast<uint8_t>(value & 0xFF)});
}

bool KNXProtocol::GroupWriteRGB(const KNXGroupAddress& address, uint8_t r, uint8_t g, uint8_t b) {
    return GroupWrite(address, {r, g, b});
}

bool KNXProtocol::GroupWriteScene(const KNXGroupAddress& address, uint8_t scene) {
    return GroupWrite(address, {static_cast<uint8_t>(scene & 0x3F)});
}

bool KNXProtocol::GroupWriteString(const KNXGroupAddress& address, const std::string& text) {
    std::vector<uint8_t> data(14, 0);
    size_t len = std::min(text.length(), size_t(14));
    for (size_t i = 0; i < len; i++) {
        data[i] = static_cast<uint8_t>(text[i]);
    }
    return GroupWrite(address, data);
}

bool KNXProtocol::GroupRead(const KNXGroupAddress& address) {
    KNXTelegram telegram;
    telegram.Destination = address;
    telegram.APCI = KNXAPCI::GroupValueRead;
    telegram.Data.clear();
    
    return SendTelegram(telegram);
}

std::vector<uint8_t> KNXProtocol::GetGroupValue(const KNXGroupAddress& address) const {
    std::lock_guard<std::mutex> lock(valueCacheMutex);
    auto it = valueCache.find(address.Raw);
    if (it != valueCache.end()) {
        return it->second;
    }
    return {};
}

// ============================================================================
// DEVICE COMMANDS
// ============================================================================

bool KNXProtocol::SwitchOn(const KNXGroupAddress& address) {
    return GroupWriteBool(address, true);
}

bool KNXProtocol::SwitchOff(const KNXGroupAddress& address) {
    return GroupWriteBool(address, false);
}

bool KNXProtocol::SwitchToggle(const KNXGroupAddress& address) {
    auto value = GetGroupValue(address);
    bool currentState = !value.empty() && (value[0] & 0x01);
    return GroupWriteBool(address, !currentState);
}

bool KNXProtocol::SetDimLevel(const KNXGroupAddress& address, uint8_t percent) {
    return GroupWriteScaling(address, percent);
}

bool KNXProtocol::StartDimming(const KNXGroupAddress& address, bool up, uint8_t steps) {
    // DPT 3.007: CSSSS where C=direction, SSSS=steps (0=break)
    uint8_t value = (up ? 0x08 : 0x00) | (steps & 0x07);
    return GroupWrite(address, {value});
}

bool KNXProtocol::StopDimming(const KNXGroupAddress& address) {
    return StartDimming(address, false, 0);  // Steps=0 means stop
}

bool KNXProtocol::SetBlindPosition(const KNXGroupAddress& address, uint8_t percent) {
    return GroupWriteScaling(address, percent);
}

bool KNXProtocol::SetBlindSlat(const KNXGroupAddress& address, uint8_t percent) {
    return GroupWriteScaling(address, percent);
}

bool KNXProtocol::MoveBlind(const KNXGroupAddress& address, bool down) {
    return GroupWriteBool(address, down);  // DPT 1.008: 0=up, 1=down
}

bool KNXProtocol::StopBlind(const KNXGroupAddress& address) {
    return GroupWriteBool(address, true);  // DPT 1.007: Step/Stop
}

bool KNXProtocol::SetHVACMode(const KNXGroupAddress& address, uint8_t mode) {
    return GroupWriteUInt8(address, mode);
}

bool KNXProtocol::SetTemperature(const KNXGroupAddress& address, float celsius) {
    return GroupWriteFloat16(address, celsius);
}

bool KNXProtocol::ActivateScene(const KNXGroupAddress& address, uint8_t scene, bool store) {
    // DPT 18.001: CSSSSSSS where C=store(1)/recall(0), S=scene number
    uint8_t value = (store ? 0x80 : 0x00) | (scene & 0x3F);
    return GroupWrite(address, {value});
}

// ============================================================================
// GROUP OBJECT MANAGEMENT
// ============================================================================

void KNXProtocol::RegisterGroupObject(const KNXGroupObject& obj) {
    std::lock_guard<std::mutex> lock(groupObjectsMutex);
    groupObjects[obj.Address.Raw] = obj;
}

void KNXProtocol::UnregisterGroupObject(const KNXGroupAddress& address) {
    std::lock_guard<std::mutex> lock(groupObjectsMutex);
    groupObjects.erase(address.Raw);
}

const KNXGroupObject* KNXProtocol::GetGroupObject(const KNXGroupAddress& address) const {
    std::lock_guard<std::mutex> lock(groupObjectsMutex);
    auto it = groupObjects.find(address.Raw);
    if (it != groupObjects.end()) {
        return &it->second;
    }
    return nullptr;
}

std::map<uint16_t, KNXGroupObject> KNXProtocol::GetGroupObjects() const {
    std::lock_guard<std::mutex> lock(groupObjectsMutex);
    return groupObjects;
}

// ============================================================================
// PROJECT IMPORT
// ============================================================================

bool KNXProtocol::ImportProject(const std::string& filePath) {
    // Basic ETS project import - would need XML parsing for full support
    std::cerr << "[KNX] ETS project import not yet implemented: " << filePath << std::endl;
    return false;
}

bool KNXProtocol::ImportGroupAddresses(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[KNX] Failed to open CSV file: " << filePath << std::endl;
        return false;
    }
    
    std::string line;
    bool firstLine = true;
    
    while (std::getline(file, line)) {
        // Skip header
        if (firstLine) {
            firstLine = false;
            if (line.find("Group") != std::string::npos || line.find("Address") != std::string::npos) {
                continue;
            }
        }
        
        // Parse CSV line
        std::stringstream ss(line);
        std::string addressStr, name, dptStr;
        
        std::getline(ss, addressStr, ',');
        std::getline(ss, name, ',');
        std::getline(ss, dptStr, ',');
        
        if (!addressStr.empty()) {
            KNXGroupObject obj;
            obj.Address = KNXGroupAddress::FromString(addressStr);
            obj.Name = name;
            
            // Parse DPT if provided
            if (!dptStr.empty()) {
                // Try to match DPT string to enum
                // Format might be "1.001" or "DPT-1.001"
            }
            
            RegisterGroupObject(obj);
        }
    }
    
    std::cout << "[KNX] Imported " << groupObjects.size() << " group addresses" << std::endl;
    return true;
}

void KNXProtocol::ClearProject() {
    project = KNXProject();
    
    std::lock_guard<std::mutex> lock(groupObjectsMutex);
    groupObjects.clear();
}

// ============================================================================
// TELEGRAM SEND/RECEIVE
// ============================================================================

bool KNXProtocol::SendTelegram(const KNXTelegram& telegram) {
    if (!connected && !routingEnabled) {
        return false;
    }
    
    std::vector<uint8_t> cemiFrame;
    
    // cEMI message code: L_Data.req = 0x11
    cemiFrame.push_back(0x11);
    
    // Additional info length
    cemiFrame.push_back(0x00);
    
    // Control byte 1
    uint8_t ctrl1 = 0xBC;  // Standard frame, no repeat, broadcast
    ctrl1 |= (static_cast<uint8_t>(telegram.Priority) << 2);
    cemiFrame.push_back(ctrl1);
    
    // Control byte 2
    uint8_t ctrl2 = telegram.IsGroupAddress ? 0xE0 : 0x00;  // Group address, hop count 6
    cemiFrame.push_back(ctrl2);
    
    // Source address (0.0.0 = gateway decides)
    cemiFrame.push_back(0x00);
    cemiFrame.push_back(0x00);
    
    // Destination address
    cemiFrame.push_back((telegram.Destination.Raw >> 8) & 0xFF);
    cemiFrame.push_back(telegram.Destination.Raw & 0xFF);
    
    // Data length
    uint8_t dataLen = telegram.Data.empty() ? 1 : telegram.Data.size() + 1;
    cemiFrame.push_back(dataLen);
    
    // TPCI/APCI
    cemiFrame.push_back(0x00);  // TPCI
    
    if (telegram.Data.empty()) {
        cemiFrame.push_back(static_cast<uint8_t>(telegram.APCI) << 6);
    } else if (telegram.Data.size() == 1 && telegram.Data[0] <= 0x3F) {
        // Small data in APCI
        cemiFrame.push_back((static_cast<uint8_t>(telegram.APCI) << 6) | (telegram.Data[0] & 0x3F));
    } else {
        cemiFrame.push_back(static_cast<uint8_t>(telegram.APCI) << 6);
        for (uint8_t b : telegram.Data) {
            cemiFrame.push_back(b);
        }
    }
    
    if (routingEnabled) {
        // Send as routing indication
        std::vector<uint8_t> frame;
        frame.push_back(0x06);  // Header length
        frame.push_back(0x10);  // Protocol version
        frame.push_back(0x05);  // ROUTING_INDICATION high
        frame.push_back(0x30);  // ROUTING_INDICATION low
        
        uint16_t totalLen = 6 + cemiFrame.size();
        frame.push_back((totalLen >> 8) & 0xFF);
        frame.push_back(totalLen & 0xFF);
        
        frame.insert(frame.end(), cemiFrame.begin(), cemiFrame.end());
        
        struct sockaddr_in multicastAddr;
        std::memset(&multicastAddr, 0, sizeof(multicastAddr));
        multicastAddr.sin_family = AF_INET;
        multicastAddr.sin_port = htons(KNX_PORT);
        inet_pton(AF_INET, KNX_MULTICAST_ADDR, &multicastAddr.sin_addr);
        
        sendto(udpSocket, reinterpret_cast<const char*>(frame.data()), frame.size(), 0,
               reinterpret_cast<struct sockaddr*>(&multicastAddr), sizeof(multicastAddr));
        
        return true;
    }
    
    // Send as tunneling request
    std::vector<uint8_t> frame;
    frame.push_back(0x06);  // Header length
    frame.push_back(0x10);  // Protocol version
    frame.push_back(0x04);  // TUNNELING_REQUEST high
    frame.push_back(0x20);  // TUNNELING_REQUEST low
    
    uint16_t totalLen = 6 + 4 + cemiFrame.size();  // Header + connection header + cEMI
    frame.push_back((totalLen >> 8) & 0xFF);
    frame.push_back(totalLen & 0xFF);
    
    // Connection header
    frame.push_back(0x04);  // Structure length
    frame.push_back(channelId);
    frame.push_back(sequenceCounter++);
    frame.push_back(0x00);  // Reserved
    
    frame.insert(frame.end(), cemiFrame.begin(), cemiFrame.end());
    
    struct sockaddr_in gatewayAddr;
    std::memset(&gatewayAddr, 0, sizeof(gatewayAddr));
    gatewayAddr.sin_family = AF_INET;
    gatewayAddr.sin_port = htons(gatewayPort);
    inet_pton(AF_INET, gatewayIP.c_str(), &gatewayAddr.sin_addr);
    
    ssize_t sent = sendto(udpSocket, reinterpret_cast<const char*>(frame.data()), frame.size(), 0,
                          reinterpret_cast<struct sockaddr*>(&gatewayAddr), sizeof(gatewayAddr));
    
    if (sent < 0) {
        return false;
    }
    
    // Wait for ACK
    {
        std::unique_lock<std::mutex> lock(ackMutex);
        ackReceived = false;
        if (!ackCondition.wait_for(lock, std::chrono::milliseconds(1000), 
                                    [this] { return ackReceived; })) {
            std::cerr << "[KNX] Tunneling ACK timeout" << std::endl;
            return false;
        }
    }
    
    NotifyTelegram(telegram);
    return true;
}

void KNXProtocol::ReceiveLoop() {
    std::vector<uint8_t> buffer(256);
    
    while (!stopThreads) {
        struct sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);
        
        ssize_t received = recvfrom(udpSocket, reinterpret_cast<char*>(buffer.data()),
                                    buffer.size(), 0,
                                    reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);
        
        if (received > 0) {
            std::vector<uint8_t> frame(buffer.begin(), buffer.begin() + received);
            ProcessKNXnetIPFrame(frame);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void KNXProtocol::HeartbeatLoop() {
    while (!stopThreads) {
        // Send heartbeat every 60 seconds
        std::this_thread::sleep_for(std::chrono::seconds(60));
        
        if (connected && !stopThreads) {
            SendConnectionstateRequest();
        }
    }
}

bool KNXProtocol::ProcessKNXnetIPFrame(const std::vector<uint8_t>& frame) {
    if (frame.size() < 6) return false;
    
    // Check header
    if (frame[0] != 0x06 || frame[1] != 0x10) {
        return false;
    }
    
    uint16_t serviceType = (frame[2] << 8) | frame[3];
    
    switch (static_cast<KNXnetIPServiceType>(serviceType)) {
        case KNXnetIPServiceType::SearchResponse:
            HandleSearchResponse(frame);
            break;
            
        case KNXnetIPServiceType::ConnectResponse:
            HandleConnectResponse(frame);
            break;
            
        case KNXnetIPServiceType::ConnectionstateResponse:
            HandleConnectionstateResponse(frame);
            break;
            
        case KNXnetIPServiceType::DisconnectResponse:
            HandleDisconnectResponse(frame);
            break;
            
        case KNXnetIPServiceType::TunnelingRequest:
            HandleTunnelingRequest(frame);
            break;
            
        case KNXnetIPServiceType::TunnelingAck:
            HandleTunnelingAck(frame);
            break;
            
        case KNXnetIPServiceType::RoutingIndication:
            HandleRoutingIndication(frame);
            break;
            
        default:
            break;
    }
    
    return true;
}

void KNXProtocol::HandleConnectResponse(const std::vector<uint8_t>& data) {
    if (data.size() < 8) return;
    
    channelId = data[6];
    uint8_t status = data[7];
    
    if (status == 0x00) {
        connected = true;
        sequenceCounter = 0;
        receivedSequence = 0;
    } else {
        std::cerr << "[KNX] Connection failed with status: " << (int)status << std::endl;
    }
}

void KNXProtocol::HandleConnectionstateResponse(const std::vector<uint8_t>& data) {
    if (data.size() < 8) return;
    
    uint8_t status = data[7];
    if (status != 0x00) {
        std::cerr << "[KNX] Connection state error: " << (int)status << std::endl;
        // Connection lost
        connected = false;
        if (onConnection) {
            onConnection(false, "Connection lost");
        }
    }
}

void KNXProtocol::HandleDisconnectResponse(const std::vector<uint8_t>& data) {
    connected = false;
    channelId = 0;
}

void KNXProtocol::HandleTunnelingRequest(const std::vector<uint8_t>& data) {
    if (data.size() < 10) return;
    
    uint8_t rxChannelId = data[7];
    uint8_t rxSequence = data[8];
    
    // Send ACK
    SendTunnelingAck(rxChannelId, rxSequence);
    
    // Parse cEMI frame (starts at offset 10)
    if (data.size() > 10) {
        uint8_t messageCode = data[10];
        
        if (messageCode == 0x29) {  // L_Data.ind
            // Parse telegram
            size_t cemiStart = 10;
            size_t addInfoLen = data[cemiStart + 1];
            size_t telegramStart = cemiStart + 2 + addInfoLen;
            
            if (data.size() > telegramStart + 6) {
                KNXTelegram telegram;
                
                // Source address
                telegram.Source = KNXIndividualAddress(
                    (data[telegramStart + 2] << 8) | data[telegramStart + 3]);
                
                // Destination address
                telegram.Destination = KNXGroupAddress(
                    (data[telegramStart + 4] << 8) | data[telegramStart + 5]);
                
                // Group address flag
                telegram.IsGroupAddress = (data[telegramStart + 1] & 0x80) != 0;
                
                // Data length
                uint8_t dataLen = data[telegramStart + 6];
                
                // APCI
                if (data.size() > telegramStart + 8) {
                    telegram.APCI = static_cast<KNXAPCI>((data[telegramStart + 8] >> 6) & 0x03);
                    
                    // Data
                    if (dataLen == 1) {
                        telegram.Data.push_back(data[telegramStart + 8] & 0x3F);
                    } else if (data.size() > telegramStart + 8 + dataLen) {
                        telegram.Data.assign(data.begin() + telegramStart + 9,
                                            data.begin() + telegramStart + 8 + dataLen);
                    }
                }
                
                // Update cache
                if (telegram.IsGroupAddress && 
                    (telegram.APCI == KNXAPCI::GroupValueWrite ||
                     telegram.APCI == KNXAPCI::GroupValueResponse)) {
                    {
                        std::lock_guard<std::mutex> lock(valueCacheMutex);
                        valueCache[telegram.Destination.Raw] = telegram.Data;
                        valueCacheTime[telegram.Destination.Raw] = std::chrono::system_clock::now();
                    }
                    
                    NotifyGroupValue(telegram.Destination, telegram.Data);
                }
                
                NotifyTelegram(telegram);
            }
        }
    }
}

void KNXProtocol::HandleTunnelingAck(const std::vector<uint8_t>& data) {
    if (data.size() < 10) return;
    
    uint8_t status = data[9];
    if (status == 0x00) {
        std::lock_guard<std::mutex> lock(ackMutex);
        ackReceived = true;
        ackCondition.notify_one();
    }
}

void KNXProtocol::HandleRoutingIndication(const std::vector<uint8_t>& data) {
    // Same as tunneling but without connection header
    if (data.size() < 8) return;
    
    // cEMI starts at offset 6
    HandleTunnelingRequest(data);  // Reuse parsing logic
}

void KNXProtocol::HandleSearchResponse(const std::vector<uint8_t>& data) {
    // Handled in SearchDevices
}

void KNXProtocol::SendTunnelingAck(uint8_t rxChannelId, uint8_t rxSequence) {
    std::vector<uint8_t> ack;
    ack.push_back(0x06);  // Header length
    ack.push_back(0x10);  // Protocol version
    ack.push_back(0x04);  // TUNNELING_ACK high
    ack.push_back(0x21);  // TUNNELING_ACK low
    ack.push_back(0x00);  // Total length high
    ack.push_back(0x0A);  // Total length low (10 bytes)
    ack.push_back(0x04);  // Connection header length
    ack.push_back(rxChannelId);
    ack.push_back(rxSequence);
    ack.push_back(0x00);  // Status: no error
    
    struct sockaddr_in gatewayAddr;
    std::memset(&gatewayAddr, 0, sizeof(gatewayAddr));
    gatewayAddr.sin_family = AF_INET;
    gatewayAddr.sin_port = htons(gatewayPort);
    inet_pton(AF_INET, gatewayIP.c_str(), &gatewayAddr.sin_addr);
    
    sendto(udpSocket, reinterpret_cast<const char*>(ack.data()), ack.size(), 0,
           reinterpret_cast<struct sockaddr*>(&gatewayAddr), sizeof(gatewayAddr));
}

void KNXProtocol::SendConnectionstateRequest() {
    std::vector<uint8_t> request;
    request.push_back(0x06);  // Header length
    request.push_back(0x10);  // Protocol version
    request.push_back(0x02);  // CONNECTIONSTATE_REQUEST high
    request.push_back(0x07);  // CONNECTIONSTATE_REQUEST low
    request.push_back(0x00);  // Total length high
    request.push_back(0x10);  // Total length low (16 bytes)
    request.push_back(channelId);
    request.push_back(0x00);  // Reserved
    
    // HPAI
    struct sockaddr_in localAddr;
    socklen_t addrLen = sizeof(localAddr);
    getsockname(udpSocket, reinterpret_cast<struct sockaddr*>(&localAddr), &addrLen);
    
    uint32_t localIP = ntohl(localAddr.sin_addr.s_addr);
    uint16_t localPort = ntohs(localAddr.sin_port);
    
    request.push_back(0x08);
    request.push_back(0x01);
    request.push_back((localIP >> 24) & 0xFF);
    request.push_back((localIP >> 16) & 0xFF);
    request.push_back((localIP >> 8) & 0xFF);
    request.push_back(localIP & 0xFF);
    request.push_back((localPort >> 8) & 0xFF);
    request.push_back(localPort & 0xFF);
    
    struct sockaddr_in gatewayAddr;
    std::memset(&gatewayAddr, 0, sizeof(gatewayAddr));
    gatewayAddr.sin_family = AF_INET;
    gatewayAddr.sin_port = htons(gatewayPort);
    inet_pton(AF_INET, gatewayIP.c_str(), &gatewayAddr.sin_addr);
    
    sendto(udpSocket, reinterpret_cast<const char*>(request.data()), request.size(), 0,
           reinterpret_cast<struct sockaddr*>(&gatewayAddr), sizeof(gatewayAddr));
}

// ============================================================================
// HELPER METHODS
// ============================================================================

SmartHomeDeviceInfo KNXProtocol::ConvertToDeviceInfo(const KNXGroupObject& obj) const {
    SmartHomeDeviceInfo info;
    
    info.DeviceId = MakeDeviceId(obj.Address);
    info.Name = obj.Name.empty() ? obj.Address.ToString() : obj.Name;
    info.Manufacturer = "KNX";
    info.Model = GetDPTName(obj.DPT);
    info.Protocol = SmartHomeProtocolType::KNX;
    info.Category = DetermineCategory(obj);
    info.State = SmartHomeDeviceState::Online;
    
    return info;
}

SmartHomeDeviceCategory KNXProtocol::DetermineCategory(const KNXGroupObject& obj) const {
    uint16_t dptMajor = static_cast<uint16_t>(obj.DPT) >> 8;
    
    switch (obj.DPT) {
        case KNXDatapointType::DPT_Switch:
        case KNXDatapointType::DPT_Bool:
            return SmartHomeDeviceCategory::Switch;
            
        case KNXDatapointType::DPT_Scaling:
        case KNXDatapointType::DPT_Control_Dimming:
            return SmartHomeDeviceCategory::Light;
            
        case KNXDatapointType::DPT_UpDown:
        case KNXDatapointType::DPT_OpenClose:
        case KNXDatapointType::DPT_Control_Blinds:
            return SmartHomeDeviceCategory::Blind;
            
        case KNXDatapointType::DPT_Value_Temp:
        case KNXDatapointType::DPT_HVAC_Mode:
            return SmartHomeDeviceCategory::Thermostat;
            
        case KNXDatapointType::DPT_Value_Humidity:
        case KNXDatapointType::DPT_Value_Lux:
            return SmartHomeDeviceCategory::Sensor;
            
        case KNXDatapointType::DPT_Colour_RGB:
            return SmartHomeDeviceCategory::Light;
            
        default:
            break;
    }
    
    // Check by main/middle group
    if (obj.Address.Main == 1) {
        return SmartHomeDeviceCategory::Light;
    } else if (obj.Address.Main == 2) {
        return SmartHomeDeviceCategory::Blind;
    } else if (obj.Address.Main == 3) {
        return SmartHomeDeviceCategory::Thermostat;
    }
    
    return SmartHomeDeviceCategory::Unknown;
}

std::string KNXProtocol::MakeDeviceId(const KNXGroupAddress& address) const {
    return "knx-" + address.ToString();
}

KNXGroupAddress KNXProtocol::ParseDeviceId(const std::string& deviceId) const {
    // Format: knx-main/middle/sub
    if (deviceId.substr(0, 4) == "knx-") {
        return KNXGroupAddress::FromString(deviceId.substr(4));
    }
    return KNXGroupAddress();
}

void KNXProtocol::NotifyTelegram(const KNXTelegram& telegram) {
    if (onTelegram) {
        onTelegram(telegram);
    }
}

void KNXProtocol::NotifyGroupValue(const KNXGroupAddress& address, const std::vector<uint8_t>& value) {
    if (onGroupValue) {
        onGroupValue(address, value);
    }
    
    // Also notify SmartHome callbacks
    if (deviceStateChangedCallback) {
        std::map<std::string, std::string> state;
        GetDeviceState(MakeDeviceId(address), state);
        deviceStateChangedCallback(MakeDeviceId(address), state);
    }
}

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

std::unique_ptr<ISmartHomeProtocol> CreateKNXProtocol() {
    return std::make_unique<KNXProtocol>();
}

} // namespace SmartHome
} // namespace UltraCanvas

// MatterProtocol.cpp
// Matter (CHIP) Protocol Implementation with connectedhomeip SDK
// Version: 1.1.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#include "MatterProtocol.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <random>

// ===== MATTER SDK INTEGRATION =====
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#ifdef ULTRACANVAS_WITH_MATTER
// connectedhomeip. The set mirrors what chip-tool's controller set-up pulls
// in; ExamplePersistentStorage and the example credentials issuer are SDK
// example code compiled into this target by SmartHome/CMakeLists.txt.
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>
#include <controller/CHIPDeviceController.h>
#include <controller/CHIPDeviceControllerFactory.h>
#include <controller/CommissioningDelegate.h>
#include <controller/DeviceDiscoveryDelegate.h>
#include <controller/ExampleOperationalCredentialsIssuer.h>
#include <controller/ExamplePersistentStorage.h>
#include <controller/InvokeInteraction.h>
#include <controller/SetUpCodePairer.h>
#include <app/InteractionModelEngine.h>
#include <app/ReadClient.h>
#include <app/WriteClient.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <credentials/GroupDataProviderImpl.h>
#include <credentials/PersistentStorageOpCertStore.h>
#include <credentials/attestation_verifier/DefaultDeviceAttestationVerifier.h>
#include <credentials/attestation_verifier/FileAttestationTrustStore.h>
#include <credentials/attestation_verifier/TestPAAStore.h>
#include <crypto/DefaultSessionKeystore.h>
#include <crypto/PersistentStorageOperationalKeystore.h>
#include <data-model-providers/codegen/Instance.h>
#include <lib/core/CASEAuthTag.h>
#include <lib/core/TLVReader.h>
#include <lib/support/CHIPMem.h>
#include <lib/core/ErrorStr.h>
#include <lib/support/TestGroupData.h>
#endif // ULTRACANVAS_WITH_MATTER

namespace UltraCanvas {
namespace SmartHome {

// ===== MATTER DEVICE TYPE IDS =====
namespace MatterDeviceTypes {
    constexpr uint32_t OnOffLight = 0x0100;
    constexpr uint32_t DimmableLight = 0x0101;
    constexpr uint32_t ColorTemperatureLight = 0x010C;
    constexpr uint32_t ExtendedColorLight = 0x010D;
    constexpr uint32_t OnOffPlug = 0x010A;
    constexpr uint32_t DimmablePlug = 0x010B;
    constexpr uint32_t OnOffSwitch = 0x0103;
    constexpr uint32_t DimmerSwitch = 0x0104;
    constexpr uint32_t ColorDimmerSwitch = 0x0105;
    constexpr uint32_t Thermostat = 0x0301;
    constexpr uint32_t DoorLock = 0x000A;
    constexpr uint32_t WindowCovering = 0x0202;
    constexpr uint32_t Fan = 0x002B;
    constexpr uint32_t TemperatureSensor = 0x0302;
    constexpr uint32_t HumiditySensor = 0x0307;
    constexpr uint32_t OccupancySensor = 0x0107;
    constexpr uint32_t ContactSensor = 0x0015;
    constexpr uint32_t LightSensor = 0x0106;
    constexpr uint32_t SmokeDetector = 0x0076;
    constexpr uint32_t Speaker = 0x0022;
}

// ===== MATTER CLUSTER IDS =====
namespace MatterClusters {
    constexpr uint32_t Identify = 0x0003;
    constexpr uint32_t Groups = 0x0004;
    constexpr uint32_t Scenes = 0x0005;
    constexpr uint32_t OnOff = 0x0006;
    constexpr uint32_t LevelControl = 0x0008;
    constexpr uint32_t Descriptor = 0x001D;
    constexpr uint32_t Binding = 0x001E;
    constexpr uint32_t BasicInformation = 0x0028;
    constexpr uint32_t OTAProvider = 0x0029;
    constexpr uint32_t OTARequestor = 0x002A;
    constexpr uint32_t GeneralCommissioning = 0x0030;
    constexpr uint32_t NetworkCommissioning = 0x0031;
    constexpr uint32_t OperationalCredentials = 0x003E;
    constexpr uint32_t GroupKeyManagement = 0x003F;
    constexpr uint32_t DoorLock = 0x0101;
    constexpr uint32_t WindowCovering = 0x0102;
    constexpr uint32_t Thermostat = 0x0201;
    constexpr uint32_t FanControl = 0x0202;
    constexpr uint32_t ColorControl = 0x0300;
    constexpr uint32_t TemperatureMeasurement = 0x0402;
    constexpr uint32_t RelativeHumidityMeasurement = 0x0405;
    constexpr uint32_t OccupancySensing = 0x0406;
}

// ===== MATTER SDK WRAPPER CLASS =====
//
// Everything that touches connectedhomeip lives here, behind
// ULTRACANVAS_WITH_MATTER. The shape follows chip-tool's own controller
// set-up (examples/chip-tool/commands/common/CHIPCommand.cpp), which is the
// one recipe the SDK keeps working: file-backed storage, persistent keystores,
// the example credentials issuer generating this controller's operational
// certificate chain, a DeviceCommissioner set up through the factory, the
// default IPK installed for the fabric, and the test PAA store for device
// attestation unless a directory of production PAAs is given.
//
// Threading: connectedhomeip runs on its own event loop and its objects may
// only be touched from that thread. Every call that reaches the SDK is
// therefore scheduled onto that thread with PlatformMgr().ScheduleWork, and
// the synchronous methods below wait for the outcome on a condition variable.
// They must not be called from the SDK's own callbacks.

class MatterProtocol::MatterSDKWrapper
#ifdef ULTRACANVAS_WITH_MATTER
    : public chip::Controller::DevicePairingDelegate,
      public chip::Controller::DeviceDiscoveryDelegate
#endif
{
public:
    MatterProtocol* protocol = nullptr;

    // Where the fabric lives between runs, and where production PAA
    // certificates would be looked for. Both set before Initialize().
    std::string storageDirectory;
    std::string paaTrustStorePath;

    // What a synchronous caller waits on while the SDK thread does the work.
    struct Outcome {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        bool ok = false;
        std::string value;

        void Finish(bool success, std::string v = {}) {
            std::lock_guard<std::mutex> lock(m);
            if (done) return;   // first answer wins; late errors are noise
            done = true;
            ok = success;
            value = std::move(v);
            cv.notify_all();
        }
        bool Wait(uint32_t timeoutMs) {
            std::unique_lock<std::mutex> lock(m);
            cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return done; });
            return done && ok;
        }
    };
    static constexpr uint32_t kRequestTimeoutMs = 15000;
    static constexpr uint16_t kTimedInvokeMs = 10000;

#ifdef ULTRACANVAS_WITH_MATTER
    // ----- what a commissioner is made of -----
    ::PersistentStorage storage;                                      // file-backed (chip-tool's, global namespace)
    chip::PersistentStorageOperationalKeystore operationalKeystore;
    chip::Credentials::PersistentStorageOpCertStore opCertStore;
    chip::Crypto::DefaultSessionKeystore sessionKeystore;
    chip::Credentials::GroupDataProviderImpl groupDataProvider;
    chip::Controller::ExampleOperationalCredentialsIssuer opCredsIssuer;
    chip::Crypto::P256Keypair controllerKey;
    uint8_t nocBuffer[chip::Credentials::kMaxCHIPCertLength];
    uint8_t icacBuffer[chip::Credentials::kMaxCHIPCertLength];
    uint8_t rcacBuffer[chip::Credentials::kMaxCHIPCertLength];
    std::unique_ptr<chip::Credentials::FileAttestationTrustStore> paaStore;
    std::unique_ptr<chip::Controller::DeviceCommissioner> commissioner;

    chip::FabricId fabricId = 1;
    chip::NodeId localNodeId = 112233;
    std::atomic<uint64_t> nextNodeId{0x100};
    bool stackUp = false;
    bool eventLoopRunning = false;

    // Commissioning in flight: node id handed to PairDevice, awaiting
    // OnCommissioningComplete.
    std::atomic<uint64_t> commissioningNodeId{0};

    // Commissionable nodes seen since StartDiscovery.
    struct DiscoveredNode {
        std::string InstanceName;
        std::string DeviceName;
        uint16_t VendorId = 0;
        uint16_t ProductId = 0;
        uint16_t Discriminator = 0;
        std::string Address;
    };
    std::vector<DiscoveredNode> discoveredNodes;
    std::mutex discoveryMutex;

    // ----- running on the SDK thread -----

    static void Trampoline(intptr_t arg) {
        auto* fn = reinterpret_cast<std::function<void()>*>(arg);
        (*fn)();
        delete fn;
    }

    bool RunOnChipThread(std::function<void()> fn) {
        if (!eventLoopRunning) {
            // Before the loop runs, the stack is ours to use directly.
            fn();
            return true;
        }
        auto* heap = new std::function<void()>(std::move(fn));
        if (chip::DeviceLayer::PlatformMgr().ScheduleWork(&Trampoline,
                                                          reinterpret_cast<intptr_t>(heap)) != CHIP_NO_ERROR) {
            delete heap;
            return false;
        }
        return true;
    }

    // Runs `fn` on the SDK thread and waits for it to return. Only for work
    // that completes synchronously there; anything that ends in a callback
    // uses an Outcome of its own.
    bool RunOnChipThreadAndWait(std::function<void()> fn) {
        auto outcome = std::make_shared<Outcome>();
        if (!RunOnChipThread([fn = std::move(fn), outcome]() {
                fn();
                outcome->Finish(true);
            })) {
            return false;
        }
        return outcome->Wait(kRequestTimeoutMs);
    }

    static std::string ErrorText(CHIP_ERROR err) {
        return err.AsString() ? std::string(err.AsString()) : std::to_string(err.AsInteger());
    }

    // ----- session to a commissioned node -----
    //
    // A CASE session is set up (or reused) by GetConnectedDevice, which
    // answers through a pair of C callbacks. The context keeps those callback
    // objects alive until one of them has fired.

    using OnSession = std::function<void(chip::Messaging::ExchangeManager&, const chip::SessionHandle&)>;
    using OnNoSession = std::function<void(CHIP_ERROR)>;

    struct ConnectContext {
        OnSession onSession;
        OnNoSession onNoSession;
        chip::Callback::Callback<chip::OnDeviceConnected> connected;
        chip::Callback::Callback<chip::OnDeviceConnectionFailure> failed;

        ConnectContext(OnSession s, OnNoSession f)
            : onSession(std::move(s)), onNoSession(std::move(f)),
              connected(&ConnectContext::OnConnected, this),
              failed(&ConnectContext::OnFailed, this) {}

        static void OnConnected(void* ctx, chip::Messaging::ExchangeManager& exchangeMgr,
                                const chip::SessionHandle& session) {
            auto* self = static_cast<ConnectContext*>(ctx);
            self->onSession(exchangeMgr, session);
            delete self;
        }
        static void OnFailed(void* ctx, const chip::ScopedNodeId&, CHIP_ERROR error) {
            auto* self = static_cast<ConnectContext*>(ctx);
            self->onNoSession(error);
            delete self;
        }
    };

    // Must be called on the SDK thread.
    void WithSession(chip::NodeId nodeId, OnSession onSession, OnNoSession onNoSession) {
        if (!commissioner) {
            onNoSession(CHIP_ERROR_INCORRECT_STATE);
            return;
        }
        auto* ctx = new ConnectContext(std::move(onSession), std::move(onNoSession));
        CHIP_ERROR err = commissioner->GetConnectedDevice(nodeId, &ctx->connected, &ctx->failed);
        if (err != CHIP_NO_ERROR) {
            ctx->onNoSession(err);
            delete ctx;
        }
    }

    // ----- TLV to text -----
    //
    // Attribute values cross the facade as strings. This renders the
    // primitive TLV types; structures and lists are named, not expanded.
    static std::string TlvToString(chip::TLV::TLVReader& reader) {
        using namespace chip::TLV;
        switch (reader.GetType()) {
            case kTLVType_Boolean: { bool v = false; (void)reader.Get(v); return v ? "true" : "false"; }
            case kTLVType_SignedInteger: { int64_t v = 0; (void)reader.Get(v); return std::to_string(v); }
            case kTLVType_UnsignedInteger: { uint64_t v = 0; (void)reader.Get(v); return std::to_string(v); }
            case kTLVType_FloatingPointNumber: { double v = 0; (void)reader.Get(v); return std::to_string(v); }
            case kTLVType_UTF8String: {
                chip::CharSpan span;
                if (reader.Get(span) != CHIP_NO_ERROR) return {};
                return std::string(span.data(), span.size());
            }
            case kTLVType_ByteString: {
                chip::ByteSpan span;
                if (reader.Get(span) != CHIP_NO_ERROR) return {};
                std::ostringstream hex;
                hex << std::hex << std::setfill('0');
                for (uint8_t b : span) hex << std::setw(2) << static_cast<int>(b);
                return hex.str();
            }
            case kTLVType_Null: return "null";
            case kTLVType_Structure: return "<structure>";
            case kTLVType_Array: return "<array>";
            case kTLVType_List: return "<list>";
            default: return "<unknown>";
        }
    }

    // ----- read / subscribe -----
    //
    // One ReadClient per request. It deletes itself from OnDone, which the
    // SDK guarantees to call last, for reads and subscriptions alike.

    class AttributeReader : public chip::app::ReadClient::Callback {
    public:
        std::function<void(bool, const std::string&)> onValue;
        std::unique_ptr<chip::app::ReadClient> client;
        bool subscription = false;
        bool answered = false;

        void OnAttributeData(const chip::app::ConcreteDataAttributePath&, chip::TLV::TLVReader* data,
                             const chip::app::StatusIB& status) override {
            answered = true;
            if (status.IsSuccess() && data) {
                chip::TLV::TLVReader copy;
                copy.Init(*data);
                onValue(true, TlvToString(copy));
            } else {
                onValue(false, "");
            }
        }
        void OnError(CHIP_ERROR error) override {
            answered = true;
            onValue(false, ErrorText(error));
        }
        void OnDone(chip::app::ReadClient*) override {
            if (!answered) onValue(false, "");
            delete this;
        }
        void OnDeallocatePaths(chip::app::ReadPrepareParams&& params) override {
            delete[] params.mpAttributePathParamsList;
        }
    };

    // Must be called on the SDK thread, with a live session.
    void StartRead(chip::Messaging::ExchangeManager& exchangeMgr, const chip::SessionHandle& session,
                   chip::EndpointId endpoint, chip::ClusterId cluster, chip::AttributeId attribute,
                   bool subscribe, uint16_t minInterval, uint16_t maxInterval,
                   std::function<void(bool, const std::string&)> onValue) {
        auto* reader = new AttributeReader();
        reader->onValue = std::move(onValue);
        reader->subscription = subscribe;
        reader->client = std::make_unique<chip::app::ReadClient>(
            chip::app::InteractionModelEngine::GetInstance(), &exchangeMgr, *reader,
            subscribe ? chip::app::ReadClient::InteractionType::Subscribe
                      : chip::app::ReadClient::InteractionType::Read);

        chip::app::ReadPrepareParams params(session);
        auto* path = new chip::app::AttributePathParams[1]{
            chip::app::AttributePathParams(endpoint, cluster, attribute)};
        params.mpAttributePathParamsList = path;
        params.mAttributePathParamsListSize = 1;

        CHIP_ERROR err;
        if (subscribe) {
            params.mMinIntervalFloorSeconds = minInterval;
            params.mMaxIntervalCeilingSeconds = maxInterval;
            params.mKeepSubscriptions = true;
            err = reader->client->SendAutoResubscribeRequest(std::move(params));
        } else {
            err = reader->client->SendRequest(params);
            delete[] path;   // a plain read does not take ownership
        }
        if (err != CHIP_NO_ERROR) {
            reader->onValue(false, ErrorText(err));
            delete reader;
        }
    }

    // ----- write -----

    class AttributeWriter : public chip::app::WriteClient::Callback {
    public:
        std::shared_ptr<Outcome> outcome;
        std::unique_ptr<chip::app::WriteClient> client;

        void OnResponse(const chip::app::WriteClient*, const chip::app::ConcreteDataAttributePath&,
                        chip::app::StatusIB status) override {
            outcome->Finish(status.IsSuccess(), status.IsSuccess() ? "" : "device rejected the write");
        }
        void OnError(const chip::app::WriteClient*, CHIP_ERROR error) override {
            outcome->Finish(false, ErrorText(error));
        }
        void OnDone(chip::app::WriteClient*) override {
            outcome->Finish(false, "no response");
            delete this;
        }
    };

    // The facade carries values as text and does not know the attribute's
    // type. "true"/"false" go as booleans, integers as signed integers,
    // numbers with a point as doubles, everything else as a string. The
    // device checks the type against its schema and refuses a mismatch,
    // which is reported as a failed write — it cannot silently take a wrong
    // one.
    template <typename Encoder>
    static CHIP_ERROR EncodeTextValue(Encoder&& encode, const std::string& text) {
        if (text == "true" || text == "false") return encode(text == "true");
        if (!text.empty() && text.find_first_not_of("-0123456789") == std::string::npos) {
            return encode(static_cast<int64_t>(std::stoll(text)));
        }
        if (!text.empty() && text.find_first_not_of("-0123456789.eE") == std::string::npos) {
            return encode(std::stod(text));
        }
        return encode(chip::CharSpan(text.data(), text.size()));
    }

    // Must be called on the SDK thread, with a live session.
    void StartWrite(chip::Messaging::ExchangeManager& exchangeMgr, const chip::SessionHandle& session,
                    chip::EndpointId endpoint, chip::ClusterId cluster, chip::AttributeId attribute,
                    const std::string& text, std::shared_ptr<Outcome> outcome) {
        auto* writer = new AttributeWriter();
        writer->outcome = outcome;
        writer->client = std::make_unique<chip::app::WriteClient>(&exchangeMgr, writer, chip::NullOptional);

        chip::app::AttributePathParams path(endpoint, cluster, attribute);
        CHIP_ERROR err = EncodeTextValue(
            [&](auto value) { return writer->client->EncodeAttribute(path, value); }, text);
        if (err == CHIP_NO_ERROR) err = writer->client->SendWriteRequest(session);
        if (err != CHIP_NO_ERROR) {
            outcome->Finish(false, ErrorText(err));
            delete writer;
        }
    }

    // ----- invoke -----

    // Must be called on the SDK thread, with a live session. The response
    // type is whatever the request declares; only success or failure is
    // reported back.
    template <typename RequestT>
    void StartInvoke(chip::Messaging::ExchangeManager& exchangeMgr, const chip::SessionHandle& session,
                     chip::EndpointId endpoint, const RequestT& request, std::shared_ptr<Outcome> outcome) {
        using ResponseT = typename RequestT::ResponseType;
        // Some commands (door locks, for one) must be timed: the device is
        // told a deadline first, so a delayed replay cannot act on it.
        chip::Optional<uint16_t> timedInvokeTimeoutMs;
        if (RequestT::MustUseTimedInvoke()) timedInvokeTimeoutMs.SetValue(kTimedInvokeMs);
        CHIP_ERROR err = chip::Controller::InvokeCommandRequest(
            &exchangeMgr, session, endpoint, request,
            [outcome](const chip::app::ConcreteCommandPath&, const chip::app::StatusIB& status,
                      const ResponseT&) {
                outcome->Finish(status.IsSuccess(), status.IsSuccess() ? "" : "device returned an error status");
            },
            [outcome](CHIP_ERROR error) { outcome->Finish(false, ErrorText(error)); },
            timedInvokeTimeoutMs);
        if (err != CHIP_NO_ERROR) outcome->Finish(false, ErrorText(err));
    }

    // Connects, invokes, waits. Called from application threads.
    template <typename RequestT>
    bool Invoke(chip::NodeId nodeId, chip::EndpointId endpoint, const RequestT& request) {
        auto outcome = std::make_shared<Outcome>();
        const bool scheduled = RunOnChipThread([this, nodeId, endpoint, request, outcome]() {
            WithSession(nodeId,
                [this, endpoint, request, outcome](chip::Messaging::ExchangeManager& em,
                                                   const chip::SessionHandle& s) {
                    StartInvoke(em, s, endpoint, request, outcome);
                },
                [outcome](CHIP_ERROR error) { outcome->Finish(false, ErrorText(error)); });
        });
        if (!scheduled) return false;
        const bool ok = outcome->Wait(kRequestTimeoutMs);
        if (!ok && protocol) {
            protocol->Log(1, "Matter command to node " + std::to_string(nodeId) + " failed: " +
                              (outcome->value.empty() ? std::string("timed out") : outcome->value));
        }
        return ok;
    }
#endif

    // ===== LIFECYCLE =====

    bool Initialize(const std::string& storagePath) {
#ifdef ULTRACANVAS_WITH_MATTER
        storageDirectory = storagePath;

        CHIP_ERROR err = chip::Platform::MemoryInit();
        if (err != CHIP_NO_ERROR) return Fail("MemoryInit", err);
        err = chip::DeviceLayer::PlatformMgr().InitChipStack();
        if (err != CHIP_NO_ERROR) return Fail("InitChipStack", err);
        stackUp = true;

        // Storage: an ini file under storagePath, so the fabric and the
        // controller's credentials survive a restart. chip-tool's own class.
        err = storage.Init("ultracanvas", storagePath.empty() ? nullptr : storagePath.c_str());
        if (err != CHIP_NO_ERROR) return Fail("storage Init", err);
        err = operationalKeystore.Init(&storage);
        if (err != CHIP_NO_ERROR) return Fail("operational keystore Init", err);
        err = opCertStore.Init(&storage);
        if (err != CHIP_NO_ERROR) return Fail("operational cert store Init", err);

        groupDataProvider.SetStorageDelegate(&storage);
        groupDataProvider.SetSessionKeystore(&sessionKeystore);
        err = groupDataProvider.Init();
        if (err != CHIP_NO_ERROR) return Fail("group data provider Init", err);

        chip::Controller::FactoryInitParams factoryParams;
        factoryParams.fabricIndependentStorage = &storage;
        factoryParams.operationalKeystore = &operationalKeystore;
        factoryParams.opCertStore = &opCertStore;
        factoryParams.sessionKeystore = &sessionKeystore;
        factoryParams.groupDataProvider = &groupDataProvider;
        factoryParams.dataModelProvider = chip::app::CodegenDataModelProviderInstance(&storage);
        factoryParams.enableServerInteractions = false;
        err = chip::Controller::DeviceControllerFactory::GetInstance().Init(factoryParams);
        if (err != CHIP_NO_ERROR) return Fail("DeviceControllerFactory Init", err);
        return true;
#else
        (void)storagePath;
        return true;
#endif
    }

    bool InitializeController() {
#ifdef ULTRACANVAS_WITH_MATTER
        // This controller's own operational credentials: a root CA and NOC
        // minted by the example issuer. That issuer is what chip-tool uses;
        // it is not a production PKI, and STATUS.md says so.
        CHIP_ERROR err = opCredsIssuer.Initialize(storage);
        if (err != CHIP_NO_ERROR) return Fail("credentials issuer Initialize", err);
        err = controllerKey.Initialize(chip::Crypto::ECPKeyTarget::ECDSA);
        if (err != CHIP_NO_ERROR) return Fail("controller keypair Initialize", err);

        chip::MutableByteSpan noc(nocBuffer), icac(icacBuffer), rcac(rcacBuffer);
        err = opCredsIssuer.GenerateNOCChainAfterValidation(localNodeId, fabricId, chip::kUndefinedCATs,
                                                             controllerKey.Pubkey(), rcac, icac, noc);
        if (err != CHIP_NO_ERROR) return Fail("GenerateNOCChain", err);

        // Device attestation: production PAAs from a directory when one is
        // configured, otherwise the SDK's test PAAs, which accept test
        // devices (chip-tool's default too).
        const chip::Credentials::AttestationTrustStore* trustStore = nullptr;
        if (!paaTrustStorePath.empty()) {
            paaStore = std::make_unique<chip::Credentials::FileAttestationTrustStore>(paaTrustStorePath.c_str());
            trustStore = paaStore.get();
        } else {
            trustStore = chip::Credentials::GetTestAttestationTrustStore();
            if (protocol) protocol->Log(1, "Matter: using the SDK's test PAA store; production devices will not attest");
        }

        chip::Controller::SetupParams params;
        params.operationalCredentialsDelegate = &opCredsIssuer;
        params.operationalKeypair = &controllerKey;
        params.controllerNOC = noc;
        params.controllerICAC = icac;
        params.controllerRCAC = rcac;
        params.controllerVendorId = chip::VendorId::TestVendor1;
        params.pairingDelegate = this;
        params.permitMultiControllerFabrics = true;
        params.deviceAttestationVerifier = chip::Credentials::GetDefaultDACVerifier(trustStore);

        commissioner = std::make_unique<chip::Controller::DeviceCommissioner>();
        err = chip::Controller::DeviceControllerFactory::GetInstance().SetupCommissioner(params, *commissioner);
        if (err != CHIP_NO_ERROR) return Fail("SetupCommissioner", err);

        // The fabric's IPK, without which no device can be commissioned.
        const chip::FabricIndex fabricIndex = commissioner->GetFabricIndex();
        uint8_t compressedFabricId[sizeof(uint64_t)];
        chip::MutableByteSpan compressedSpan(compressedFabricId);
        err = commissioner->GetCompressedFabricIdBytes(compressedSpan);
        if (err != CHIP_NO_ERROR) return Fail("GetCompressedFabricIdBytes", err);
        err = chip::GroupTesting::InitData(&groupDataProvider, fabricIndex, compressedSpan);
        if (err != CHIP_NO_ERROR) return Fail("group data InitData", err);
        err = chip::Credentials::SetSingleIpkEpochKey(&groupDataProvider, fabricIndex,
                                                      chip::GroupTesting::DefaultIpkValue::GetDefaultIpk(),
                                                      compressedSpan);
        if (err != CHIP_NO_ERROR) return Fail("SetSingleIpkEpochKey", err);

        commissioner->RegisterDeviceDiscoveryDelegate(this);
        fabricId = commissioner->GetFabricId();
        localNodeId = commissioner->GetNodeId();

        // From here on the SDK owns its thread; everything else goes through
        // RunOnChipThread.
        err = chip::DeviceLayer::PlatformMgr().StartEventLoopTask();
        if (err != CHIP_NO_ERROR) return Fail("StartEventLoopTask", err);
        eventLoopRunning = true;
        return true;
#else
        return true;
#endif
    }

    void Shutdown() {
#ifdef ULTRACANVAS_WITH_MATTER
        if (eventLoopRunning) {
            (void)chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
            eventLoopRunning = false;
        }
        if (commissioner) {
            commissioner->Shutdown();
            commissioner.reset();
        }
        chip::Controller::DeviceControllerFactory::GetInstance().Shutdown();
        if (stackUp) {
            chip::DeviceLayer::PlatformMgr().Shutdown();
            stackUp = false;
            chip::Platform::MemoryShutdown();
        }
#endif
    }

#ifdef ULTRACANVAS_WITH_MATTER
    bool Fail(const char* what, CHIP_ERROR err) {
        if (protocol) protocol->Log(0, std::string("Matter: ") + what + " failed: " + ErrorText(err));
        return false;
    }

    // ----- DevicePairingDelegate -----

    void OnStatusUpdate(chip::Controller::DevicePairingDelegate::Status status) override {
        if (!protocol) return;
        switch (status) {
            case chip::Controller::DevicePairingDelegate::SecurePairingSuccess:
                protocol->Log(3, "Matter: PASE session established"); break;
            case chip::Controller::DevicePairingDelegate::SecurePairingFailed:
                protocol->Log(1, "Matter: PASE session failed"); break;
        }
    }

    void OnPairingComplete(CHIP_ERROR error) override {
        if (error != CHIP_NO_ERROR && protocol) {
            // Commissioning never started; OnCommissioningComplete will not fire.
            protocol->OnCommissioningComplete(commissioningNodeId.exchange(0), false,
                                              "pairing failed: " + ErrorText(error));
        }
    }

    void OnCommissioningComplete(chip::NodeId nodeId, CHIP_ERROR error) override {
        commissioningNodeId = 0;
        if (protocol) {
            protocol->OnCommissioningComplete(nodeId, error == CHIP_NO_ERROR,
                                              error == CHIP_NO_ERROR ? "" : ErrorText(error));
        }
    }

    // ----- DeviceDiscoveryDelegate -----

    void OnDiscoveredDevice(const chip::Dnssd::CommissionNodeData& nodeData) override {
        DiscoveredNode node;
        node.InstanceName = nodeData.instanceName;
        node.DeviceName = nodeData.deviceName;
        node.VendorId = nodeData.vendorId;
        node.ProductId = nodeData.productId;
        node.Discriminator = nodeData.longDiscriminator;
        if (nodeData.numIPs > 0) {
            char buffer[chip::Inet::IPAddress::kMaxStringLength];
            nodeData.ipAddress[0].ToString(buffer);
            node.Address = buffer;
        }
        {
            std::lock_guard<std::mutex> lock(discoveryMutex);
            discoveredNodes.push_back(node);
        }
        if (protocol) {
            protocol->Log(2, "Matter: commissionable node " + node.InstanceName + " (" +
                              node.DeviceName + ", discriminator " + std::to_string(node.Discriminator) + ")");
        }
    }
#endif

    // ===== DISCOVERY =====

    bool StartDiscovery() {
#ifdef ULTRACANVAS_WITH_MATTER
        {
            std::lock_guard<std::mutex> lock(discoveryMutex);
            discoveredNodes.clear();
        }
        bool ok = false;
        RunOnChipThreadAndWait([this, &ok]() {
            if (!commissioner) return;
            ok = commissioner->DiscoverCommissionableNodes(chip::Dnssd::DiscoveryFilter()) == CHIP_NO_ERROR;
        });
        return ok;
#else
        return true;
#endif
    }

    void StopDiscovery() {
#ifdef ULTRACANVAS_WITH_MATTER
        RunOnChipThreadAndWait([this]() {
            if (commissioner) (void)commissioner->StopCommissionableDiscovery();
        });
#endif
    }

    // ===== COMMISSIONING =====

    // Starts commissioning with a manual pairing code or a QR payload
    // ("MT:..."); the SDK's SetUpCodePairer reads both. Completion arrives
    // through OnCommissioningComplete, on the SDK thread.
    bool CommissionWithSetupCode(const std::string& setupCode, uint32_t timeoutSeconds) {
        (void)timeoutSeconds;
#ifdef ULTRACANVAS_WITH_MATTER
        if (commissioningNodeId != 0) {
            if (protocol) protocol->Log(1, "Matter: a commissioning is already in progress");
            return false;
        }
        const chip::NodeId nodeId = nextNodeId++;
        commissioningNodeId = nodeId;
        bool ok = false;
        RunOnChipThreadAndWait([this, nodeId, setupCode, &ok]() {
            if (!commissioner) return;
            chip::Controller::CommissioningParameters params;
            CHIP_ERROR err = commissioner->PairDevice(nodeId, setupCode.c_str(), params,
                                                      chip::Controller::DiscoveryType::kAll);
            ok = err == CHIP_NO_ERROR;
            if (!ok) Fail("PairDevice", err);
        });
        if (!ok) commissioningNodeId = 0;
        return ok;
#else
        // Stub: simulate commissioning
        if (protocol) {
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                protocol->OnCommissioningComplete(0x1234567890ABCDEF, true, "");
            }).detach();
        }
        return true;
#endif
    }

    bool CommissionWithQRPayload(const std::string& qrPayload) {
        return CommissionWithSetupCode(qrPayload, 120);
    }

    // ===== COMMAND SENDING =====

    bool SendOnOffCommand(uint64_t nodeId, uint16_t endpoint, uint8_t command) {
#ifdef ULTRACANVAS_WITH_MATTER
        namespace OnOff = chip::app::Clusters::OnOff::Commands;
        switch (command) {
            case 0: return Invoke(nodeId, endpoint, OnOff::Off::Type{});
            case 1: return Invoke(nodeId, endpoint, OnOff::On::Type{});
            case 2: return Invoke(nodeId, endpoint, OnOff::Toggle::Type{});
            default: return false;
        }
#else
        (void)nodeId; (void)endpoint; (void)command;
        return true;
#endif
    }

    bool SendLevelCommand(uint64_t nodeId, uint16_t endpoint, uint8_t level, uint16_t transitionTime) {
#ifdef ULTRACANVAS_WITH_MATTER
        chip::app::Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Type cmd;
        cmd.level = level;
        cmd.transitionTime.SetNonNull(transitionTime);
        return Invoke(nodeId, endpoint, cmd);
#else
        (void)nodeId; (void)endpoint; (void)level; (void)transitionTime;
        return true;
#endif
    }

    bool SendColorCommand(uint64_t nodeId, uint16_t endpoint,
                          uint16_t hue, uint8_t saturation, uint16_t transitionTime) {
#ifdef ULTRACANVAS_WITH_MATTER
        chip::app::Clusters::ColorControl::Commands::MoveToHueAndSaturation::Type cmd;
        cmd.hue = static_cast<uint8_t>(hue);
        cmd.saturation = saturation;
        cmd.transitionTime = transitionTime;
        return Invoke(nodeId, endpoint, cmd);
#else
        (void)nodeId; (void)endpoint; (void)hue; (void)saturation; (void)transitionTime;
        return true;
#endif
    }

    bool SendColorTempCommand(uint64_t nodeId, uint16_t endpoint,
                              uint16_t colorTemp, uint16_t transitionTime) {
#ifdef ULTRACANVAS_WITH_MATTER
        chip::app::Clusters::ColorControl::Commands::MoveToColorTemperature::Type cmd;
        cmd.colorTemperatureMireds = colorTemp;
        cmd.transitionTime = transitionTime;
        return Invoke(nodeId, endpoint, cmd);
#else
        (void)nodeId; (void)endpoint; (void)colorTemp; (void)transitionTime;
        return true;
#endif
    }

    bool SendDoorLockCommand(uint64_t nodeId, uint16_t endpoint, uint8_t command) {
#ifdef ULTRACANVAS_WITH_MATTER
        namespace DoorLock = chip::app::Clusters::DoorLock::Commands;
        if (command == 0) return Invoke(nodeId, endpoint, DoorLock::LockDoor::Type{});
        return Invoke(nodeId, endpoint, DoorLock::UnlockDoor::Type{});
#else
        (void)nodeId; (void)endpoint; (void)command;
        return true;
#endif
    }

    bool SendWindowCoveringCommand(uint64_t nodeId, uint16_t endpoint,
                                   uint8_t command, uint8_t position = 0) {
#ifdef ULTRACANVAS_WITH_MATTER
        namespace WC = chip::app::Clusters::WindowCovering::Commands;
        switch (command) {
            case 0: return Invoke(nodeId, endpoint, WC::UpOrOpen::Type{});
            case 1: return Invoke(nodeId, endpoint, WC::DownOrClose::Type{});
            case 2: return Invoke(nodeId, endpoint, WC::StopMotion::Type{});
            case 5: {
                WC::GoToLiftPercentage::Type cmd;
                cmd.liftPercent100thsValue = static_cast<uint16_t>(position * 100);
                return Invoke(nodeId, endpoint, cmd);
            }
            default: return false;
        }
#else
        (void)nodeId; (void)endpoint; (void)command; (void)position;
        return true;
#endif
    }

    bool SendThermostatCommand(uint64_t nodeId, uint16_t endpoint, int16_t temp) {
        // OccupiedHeatingSetpoint (0x0012), in hundredths of a degree.
        return WriteAttribute(nodeId, endpoint, MatterClusters::Thermostat, 0x0012,
                              std::to_string(static_cast<int>(temp) * 100));
    }

    // ===== ATTRIBUTE ACCESS =====

    // Reads one attribute and reports its value as text. Blocks the caller
    // until the device answers or the request times out.
    void ReadAttribute(uint64_t nodeId, uint16_t endpoint, uint32_t clusterId,
                       uint32_t attributeId,
                       std::function<void(bool, const std::string&)> callback) {
#ifdef ULTRACANVAS_WITH_MATTER
        auto outcome = std::make_shared<Outcome>();
        const bool scheduled = RunOnChipThread([this, nodeId, endpoint, clusterId, attributeId, outcome]() {
            WithSession(nodeId,
                [this, endpoint, clusterId, attributeId, outcome](chip::Messaging::ExchangeManager& em,
                                                                  const chip::SessionHandle& s) {
                    StartRead(em, s, endpoint, clusterId, attributeId, false, 0, 0,
                              [outcome](bool ok, const std::string& value) { outcome->Finish(ok, value); });
                },
                [outcome](CHIP_ERROR error) { outcome->Finish(false, ErrorText(error)); });
        });
        const bool ok = scheduled && outcome->Wait(kRequestTimeoutMs);
        if (callback) callback(ok, ok ? outcome->value : std::string());
#else
        (void)nodeId; (void)endpoint; (void)clusterId; (void)attributeId;
        if (callback) callback(true, "0");
#endif
    }

    bool WriteAttribute(uint64_t nodeId, uint16_t endpoint, uint32_t clusterId,
                        uint32_t attributeId, const std::string& value) {
#ifdef ULTRACANVAS_WITH_MATTER
        auto outcome = std::make_shared<Outcome>();
        const bool scheduled = RunOnChipThread([this, nodeId, endpoint, clusterId, attributeId, value, outcome]() {
            WithSession(nodeId,
                [this, endpoint, clusterId, attributeId, value, outcome](chip::Messaging::ExchangeManager& em,
                                                                         const chip::SessionHandle& s) {
                    StartWrite(em, s, endpoint, clusterId, attributeId, value, outcome);
                },
                [outcome](CHIP_ERROR error) { outcome->Finish(false, ErrorText(error)); });
        });
        return scheduled && outcome->Wait(kRequestTimeoutMs);
#else
        (void)nodeId; (void)endpoint; (void)clusterId; (void)attributeId; (void)value;
        return true;
#endif
    }

    // Subscribes and reports every change through `callback`, on the SDK
    // thread. Returns once the subscription request is on its way; the
    // first report confirms it took.
    bool SubscribeAttribute(uint64_t nodeId, uint16_t endpoint, uint32_t clusterId,
                            uint32_t attributeId, uint16_t minInterval, uint16_t maxInterval,
                            std::function<void(const std::string&)> callback) {
#ifdef ULTRACANVAS_WITH_MATTER
        return RunOnChipThread([this, nodeId, endpoint, clusterId, attributeId, minInterval, maxInterval, callback]() {
            WithSession(nodeId,
                [this, endpoint, clusterId, attributeId, minInterval, maxInterval, callback](
                    chip::Messaging::ExchangeManager& em, const chip::SessionHandle& s) {
                    StartRead(em, s, endpoint, clusterId, attributeId, true, minInterval, maxInterval,
                              [callback](bool ok, const std::string& value) {
                                  if (ok && callback) callback(value);
                              });
                },
                [this, nodeId](CHIP_ERROR error) {
                    if (protocol) protocol->Log(1, "Matter: no session to node " + std::to_string(nodeId) +
                                                     " for subscription: " + ErrorText(error));
                });
        });
#else
        (void)nodeId; (void)endpoint; (void)clusterId; (void)attributeId;
        (void)minInterval; (void)maxInterval; (void)callback;
        return true;
#endif
    }

    // ===== GROUPS =====

    bool AddDeviceToGroup(uint64_t nodeId, uint16_t endpoint, uint16_t groupId,
                          const std::string& groupName) {
#ifdef ULTRACANVAS_WITH_MATTER
        chip::app::Clusters::Groups::Commands::AddGroup::Type cmd;
        cmd.groupID = groupId;
        cmd.groupName = chip::CharSpan(groupName.data(), groupName.size());
        return Invoke(nodeId, endpoint, cmd);
#else
        (void)nodeId; (void)endpoint; (void)groupId; (void)groupName;
        return true;
#endif
    }

    bool RemoveDeviceFromGroup(uint64_t nodeId, uint16_t endpoint, uint16_t groupId) {
#ifdef ULTRACANVAS_WITH_MATTER
        chip::app::Clusters::Groups::Commands::RemoveGroup::Type cmd;
        cmd.groupID = groupId;
        return Invoke(nodeId, endpoint, cmd);
#else
        (void)nodeId; (void)endpoint; (void)groupId;
        return true;
#endif
    }

    // ===== BINDING =====

    // Writes the source's Binding attribute with a single target. This
    // replaces the whole list — Matter bindings are a list attribute — and
    // the target must already grant the source access in its ACL, which is
    // the commissioner's job and is not done here yet.
    bool CreateBinding(uint64_t sourceNode, uint16_t sourceEndpoint,
                       uint64_t targetNode, uint16_t targetEndpoint,
                       uint32_t clusterId) {
#ifdef ULTRACANVAS_WITH_MATTER
        auto outcome = std::make_shared<Outcome>();
        const bool scheduled = RunOnChipThread(
            [this, sourceNode, sourceEndpoint, targetNode, targetEndpoint, clusterId, outcome]() {
                WithSession(sourceNode,
                    [this, sourceEndpoint, targetNode, targetEndpoint, clusterId, outcome](
                        chip::Messaging::ExchangeManager& em, const chip::SessionHandle& s) {
                        using namespace chip::app::Clusters::Binding;
                        Structs::TargetStruct::Type target;
                        target.node.SetValue(targetNode);
                        target.endpoint.SetValue(targetEndpoint);
                        target.cluster.SetValue(clusterId);
                        target.fabricIndex = commissioner ? commissioner->GetFabricIndex() : chip::kUndefinedFabricIndex;
                        Attributes::Binding::TypeInfo::Type list(&target, 1);

                        auto* writer = new AttributeWriter();
                        writer->outcome = outcome;
                        writer->client = std::make_unique<chip::app::WriteClient>(&em, writer, chip::NullOptional);
                        chip::app::AttributePathParams path(sourceEndpoint, Id, Attributes::Binding::Id);
                        CHIP_ERROR err = writer->client->EncodeAttribute(path, list);
                        if (err == CHIP_NO_ERROR) err = writer->client->SendWriteRequest(s);
                        if (err != CHIP_NO_ERROR) {
                            outcome->Finish(false, ErrorText(err));
                            delete writer;
                        }
                    },
                    [outcome](CHIP_ERROR error) { outcome->Finish(false, ErrorText(error)); });
            });
        return scheduled && outcome->Wait(kRequestTimeoutMs);
#else
        (void)sourceNode; (void)sourceEndpoint; (void)targetNode; (void)targetEndpoint; (void)clusterId;
        return true;
#endif
    }

    // ===== OTA =====

    // Acting as an OTA provider means serving the OTA Software Update
    // Provider cluster and an image over BDX. Neither exists here; saying so
    // beats reporting an update that will never start.
    bool InitiateOTAUpdate(uint64_t nodeId, const std::string& firmwarePath) {
        (void)nodeId; (void)firmwarePath;
        if (protocol) protocol->Log(1, "Matter: OTA provider role is not implemented");
        return false;
    }
};

// ===== CONSTRUCTOR/DESTRUCTOR =====

MatterProtocol::MatterProtocol()
    : SmartHomeProtocolBase(SmartHomeProtocolType::Matter, "Matter")
    , sdkWrapper(std::make_unique<MatterSDKWrapper>()) {
    
    protocolVersion = "1.3";
    
    SetCapability(ProtocolCapability::Discovery);
    SetCapability(ProtocolCapability::Pairing);
    SetCapability(ProtocolCapability::Binding);
    SetCapability(ProtocolCapability::Groups);
    SetCapability(ProtocolCapability::OTA);
    SetCapability(ProtocolCapability::Security);
    SetCapability(ProtocolCapability::MultiFabric);
}

MatterProtocol::~MatterProtocol() {
    if (IsInitialized()) {
        Shutdown();
    }
}

// ===== LIFECYCLE =====

bool MatterProtocol::Initialize() {
    if (IsInitialized()) return true;
    
    SetState(ProtocolState::Initializing);
    Log(2, "Initializing Matter protocol...");
    
    sdkWrapper->protocol = this;
    
    if (!sdkWrapper->Initialize(storagePath)) {
        ReportError(-100, "Failed to initialize Matter stack");
        SetState(ProtocolState::Error);
        return false;
    }
    
    if (!sdkWrapper->InitializeController()) {
        ReportError(-101, "Failed to initialize Matter controller");
        SetState(ProtocolState::Error);
        return false;
    }
    
    running = true;
    eventLoopThread = std::thread(&MatterProtocol::EventLoopThread, this);
    
    SetState(ProtocolState::Ready);
    Log(2, "Matter protocol initialized successfully");
    return true;
}

void MatterProtocol::Shutdown() {
    if (!IsInitialized()) return;
    
    Log(2, "Shutting down Matter protocol...");
    
    running = false;
    discovering = false;
    pairing = false;
    commissioningInProgress = false;
    
    if (discoveryThread.joinable()) discoveryThread.join();
    if (commissioningThread.joinable()) commissioningThread.join();
    if (eventLoopThread.joinable()) eventLoopThread.join();
    
    {
        std::lock_guard<std::mutex> lock(matterMutex);
        matterNodes.clear();
        nodeIdToDeviceId.clear();
        fabrics.clear();
        groups.clear();
        groupMembers.clear();
        bindings.clear();
        attributeSubscriptions.clear();
    }
    
    sdkWrapper->Shutdown();
    SetState(ProtocolState::Uninitialized);
    Log(2, "Matter protocol shutdown complete");
}

// ===== HARDWARE =====

bool MatterProtocol::IsHardwareAvailable() const {
    return true;  // Matter works over IP networks
}

// ===== DEVICE REGISTRY VIEWS =====

std::vector<SmartHomeDeviceInfo> MatterProtocol::GetPairedDevices() {
    std::vector<SmartHomeDeviceInfo> devices;
    std::lock_guard<std::mutex> lock(matterMutex);
    devices.reserve(matterNodes.size());
    for (const auto& [id, node] : matterNodes) {
        devices.push_back(NodeToDeviceInfo(node));
    }
    return devices;
}

std::vector<std::shared_ptr<ISmartHomeDevice>> MatterProtocol::GetDevices() const {
    // The object-level view is not built yet; the info-level one above is.
    return {};
}

std::shared_ptr<ISmartHomeDevice> MatterProtocol::GetDevice(const std::string&) const {
    return nullptr;
}

std::string MatterProtocol::GetHardwareInfo() const {
    // Matter needs no radio of its own: it rides on IP, and Thread devices
    // are reached through a border router. What there is to report is the
    // controller's fabric and the SDK behind it.
    std::string info = "Matter controller over IP";
#ifdef ULTRACANVAS_WITH_MATTER
    // The SDK has no version string of its own; its error formatter is the
    // nearest thing that only the real library can answer.
    info += ", connectedhomeip linked (" + std::string(chip::ErrorStr(CHIP_NO_ERROR)) + ")";
#else
    info += ", connectedhomeip not compiled in";
#endif
    return info;
}

bool MatterProtocol::PairDevice(const std::string& deviceId,
                                const std::map<std::string, std::string>& params) {
    // A Matter device is commissioned with its setup code — the 11- or
    // 21-digit manual code or the QR payload ("MT:..."). Either key works;
    // the device id is not known until commissioning assigns a node id.
    (void)deviceId;
    for (const char* key : {"code", "setupCode", "qr", "qrCode"}) {
        if (auto it = params.find(key); it != params.end() && !it->second.empty()) {
            return CommissionWithCode(it->second);
        }
    }
    ReportError(-203, "PairDevice needs a setup code (\"code\") or QR payload (\"qr\")");
    return false;
}

bool MatterProtocol::UnpairDevice(const std::string& deviceId) {
    return RemoveDevice(deviceId);
}

bool MatterProtocol::GetDeviceState(const std::string& deviceId,
                                    std::map<std::string, std::string>& state) {
    MatterNode node;
    {
        std::lock_guard<std::mutex> lock(matterMutex);
        auto it = matterNodes.find(deviceId);
        if (it == matterNodes.end()) return false;
        node = it->second;
    }
    state["online"] = node.IsOnline ? "true" : "false";
    state["nodeId"] = std::to_string(node.NodeId);
    state["fabricId"] = std::to_string(node.FabricId);
    state["vendorId"] = std::to_string(node.VendorId);
    state["productId"] = std::to_string(node.ProductId);
    if (!node.VendorName.empty()) state["vendorName"] = node.VendorName;
    if (!node.ProductName.empty()) state["productName"] = node.ProductName;
    if (!node.SoftwareVersion.empty()) state["softwareVersion"] = node.SoftwareVersion;
    state["endpoints"] = std::to_string(node.Endpoints.size());
    return true;
}

std::vector<std::string> MatterProtocol::GetAvailableAdapters() const {
    std::vector<std::string> adapters;
#ifdef __linux__
    std::ifstream netDev("/proc/net/dev");
    std::string line;
    while (std::getline(netDev, line)) {
        if (line.find("eth") != std::string::npos || line.find("wlan") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string iface = line.substr(0, pos);
                iface.erase(0, iface.find_first_not_of(" \t"));
                adapters.push_back(iface);
            }
        }
    }
#endif
    if (adapters.empty()) adapters.push_back("default");
    return adapters;
}

bool MatterProtocol::SelectAdapter(const std::string& adapterId) {
    selectedAdapter = adapterId;
    return true;
}

// ===== NETWORK =====

bool MatterProtocol::FormNetwork(const std::string& networkName) {
    SmartHomeNetworkInfo info;
    info.Protocol = SmartHomeProtocolType::Matter;
    info.NetworkName = networkName;
    info.Active = false;
    SetNetworkInfo(info);
    hasNetwork = true;
    return true;
}

bool MatterProtocol::JoinNetwork(const std::string& networkId) { return true; }

bool MatterProtocol::LeaveNetwork() {
    hasNetwork = false;
    ClearNetwork();
    return true;
}

NetworkTopology MatterProtocol::GetTopology() const {
    NetworkTopology topology;
    topology.NetworkId = "matter_fabric";
    
    std::lock_guard<std::mutex> lock(matterMutex);
    
    // Matter has no mesh of its own to draw: the controller reaches every
    // node over IP, so the picture is a star around it.
    NetworkNode controllerNode;
    controllerNode.NodeId = "matter_controller";
    controllerNode.IsCoordinator = true;
    controllerNode.IsBorderRouter = true;
    controllerNode.IsRouter = true;
    controllerNode.Depth = 0;
    topology.Nodes.push_back(controllerNode);
    
    for (const auto& [deviceId, node] : matterNodes) {
        NetworkNode netNode;
        netNode.NodeId = deviceId;
        netNode.DeviceId = deviceId;
        netNode.ParentId = "matter_controller";
        netNode.Depth = 1;
        topology.Nodes.push_back(netNode);
        ++topology.EndDeviceCount;
    }
    topology.MaxDepth = matterNodes.empty() ? 0 : 1;
    
    return topology;
}

// ===== DISCOVERY =====

bool MatterProtocol::StartDiscovery(int timeoutSeconds) {
    if (discovering) return false;
    
    Log(2, "Starting Matter device discovery via DNS-SD...");
    discovering = true;
    ClearDiscoveredDevices();
    
    if (!sdkWrapper->StartDiscovery()) {
        discovering = false;
        return false;
    }
    
    std::thread([this, timeoutSeconds]() {
        std::this_thread::sleep_for(std::chrono::seconds(timeoutSeconds));
        StopDiscovery();
    }).detach();
    
    return true;
}

void MatterProtocol::StopDiscovery() {
    if (!discovering) return;
    Log(2, "Stopping Matter device discovery");
    sdkWrapper->StopDiscovery();
    discovering = false;
}

// ===== PAIRING =====

bool MatterProtocol::StartPairing(int timeoutSeconds) {
    if (pairing || commissioningInProgress) return false;
    Log(2, "Starting Matter pairing mode...");
    pairing = true;
    return true;
}

void MatterProtocol::StopPairing() {
    Log(2, "Stopping Matter pairing mode");
    pairing = false;
    commissioningInProgress = false;
}

bool MatterProtocol::PermitJoin(int timeoutSeconds) {
    return StartPairing(timeoutSeconds);
}

bool MatterProtocol::CommissionWithCode(const std::string& setupCode) {
    if (!pairing) {
        ReportError(-200, "Must start pairing mode first");
        return false;
    }
    
    Log(2, "Commissioning with setup code");
    commissioningInProgress = true;
    
    bool result = sdkWrapper->CommissionWithSetupCode(setupCode, 120);
    if (!result) commissioningInProgress = false;
    return result;
}

bool MatterProtocol::CommissionWithQR(const std::string& qrPayload) {
    if (!pairing) {
        ReportError(-200, "Must start pairing mode first");
        return false;
    }
    
    Log(2, "Commissioning with QR code");
    commissioningInProgress = true;
    
    bool result = sdkWrapper->CommissionWithQRPayload(qrPayload);
    if (!result) commissioningInProgress = false;
    return result;
}

bool MatterProtocol::CommissionWithParams(const MatterCommissioningParams& params) {
    if (!params.SetupCode.empty()) return CommissionWithCode(params.SetupCode);
    if (!params.QRCode.empty()) return CommissionWithQR(params.QRCode);
    ReportError(-201, "No setup code or QR code provided");
    return false;
}

void MatterProtocol::OnCommissioningComplete(uint64_t nodeId, bool success, const std::string& error) {
    commissioningInProgress = false;
    
    if (success) {
        Log(2, "Device commissioned successfully, NodeId: " + std::to_string(nodeId));
        
        MatterNode node;
        node.NodeId = nodeId;
        node.DeviceId = "matter_" + std::to_string(nodeId);
        node.IsOnline = true;
        node.DeviceTypes.push_back(MatterDeviceTypes::OnOffLight);  // Default
        
        {
            std::lock_guard<std::mutex> lock(matterMutex);
            matterNodes[node.DeviceId] = node;
            nodeIdToDeviceId[nodeId] = node.DeviceId;
        }
        
        SmartHomeDeviceInfo info = NodeToDeviceInfo(node);
        AddPairedDevice(info);
    } else {
        ReportError(-202, "Commissioning failed: " + error);
    }
    
    pairing = false;
}

void MatterProtocol::UpdateNodeAttribute(uint64_t nodeId, const std::string& attrName, 
                                          const std::string& value) {
    std::lock_guard<std::mutex> lock(matterMutex);
    
    auto it = nodeIdToDeviceId.find(nodeId);
    if (it == nodeIdToDeviceId.end()) return;
    
    auto nodeIt = matterNodes.find(it->second);
    if (nodeIt == matterNodes.end()) return;
    
    if (attrName == "vendorName") nodeIt->second.VendorName = value;
    else if (attrName == "productName") nodeIt->second.ProductName = value;
    
    SmartHomeDeviceInfo info = NodeToDeviceInfo(nodeIt->second);
    UpdatePairedDevice(info);
}

// ===== DEVICE OPERATIONS =====

bool MatterProtocol::RemoveDevice(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(matterMutex);
    
    auto it = matterNodes.find(deviceId);
    if (it == matterNodes.end()) return false;
    
    nodeIdToDeviceId.erase(it->second.NodeId);
    matterNodes.erase(it);
    RemovePairedDevice(deviceId);
    
    Log(2, "Removed Matter device: " + deviceId);
    return true;
}

bool MatterProtocol::InterviewDevice(const std::string& deviceId) {
    Log(2, "Interviewing Matter device: " + deviceId);
    return true;
}

// ===== COMMANDS =====

bool MatterProtocol::SendCommand(const std::string& deviceId,
                                  const std::string& command,
                                  const std::map<std::string, std::string>& params) {
    uint64_t nodeId;
    {
        std::lock_guard<std::mutex> lock(matterMutex);
        auto it = matterNodes.find(deviceId);
        if (it == matterNodes.end()) {
            ReportError(-300, "Device not found: " + deviceId);
            return false;
        }
        nodeId = it->second.NodeId;
    }
    
    uint16_t endpoint = 1;
    if (params.count("endpoint")) {
        endpoint = static_cast<uint16_t>(std::stoi(params.at("endpoint")));
    }
    
    Log(3, "Sending command to " + deviceId + ": " + command);
    
    if (command == "turnOn" || command == "on") {
        return sdkWrapper->SendOnOffCommand(nodeId, endpoint, 1);
    }
    else if (command == "turnOff" || command == "off") {
        return sdkWrapper->SendOnOffCommand(nodeId, endpoint, 0);
    }
    else if (command == "toggle") {
        return sdkWrapper->SendOnOffCommand(nodeId, endpoint, 2);
    }
    else if (command == "setLevel" || command == "setBrightness") {
        uint8_t level = 254;
        uint16_t transitionTime = 10;
        
        if (params.count("level")) level = static_cast<uint8_t>(std::stoi(params.at("level")));
        if (params.count("brightness")) {
            int brightness = std::stoi(params.at("brightness"));
            level = static_cast<uint8_t>((brightness * 254) / 100);
        }
        if (params.count("transition")) transitionTime = static_cast<uint16_t>(std::stoi(params.at("transition")));
        
        return sdkWrapper->SendLevelCommand(nodeId, endpoint, level, transitionTime);
    }
    else if (command == "setColor") {
        uint16_t hue = 0;
        uint8_t saturation = 254;
        uint16_t transitionTime = 10;
        
        if (params.count("hue")) {
            int h = std::stoi(params.at("hue"));
            hue = static_cast<uint16_t>((h * 254) / 360);
        }
        if (params.count("saturation")) saturation = static_cast<uint8_t>(std::stoi(params.at("saturation")));
        
        return sdkWrapper->SendColorCommand(nodeId, endpoint, hue, saturation, transitionTime);
    }
    else if (command == "setColorTemp") {
        uint16_t colorTemp = 370;
        uint16_t transitionTime = 10;
        
        if (params.count("colorTemp")) {
            int kelvin = std::stoi(params.at("colorTemp"));
            colorTemp = static_cast<uint16_t>(1000000 / kelvin);
        }
        if (params.count("mireds")) colorTemp = static_cast<uint16_t>(std::stoi(params.at("mireds")));
        
        return sdkWrapper->SendColorTempCommand(nodeId, endpoint, colorTemp, transitionTime);
    }
    else if (command == "lock") {
        return sdkWrapper->SendDoorLockCommand(nodeId, endpoint, 0);
    }
    else if (command == "unlock") {
        return sdkWrapper->SendDoorLockCommand(nodeId, endpoint, 1);
    }
    else if (command == "open") {
        return sdkWrapper->SendWindowCoveringCommand(nodeId, endpoint, 0);
    }
    else if (command == "close") {
        return sdkWrapper->SendWindowCoveringCommand(nodeId, endpoint, 1);
    }
    else if (command == "stop") {
        return sdkWrapper->SendWindowCoveringCommand(nodeId, endpoint, 2);
    }
    else if (command == "setPosition") {
        uint8_t position = 50;
        if (params.count("position")) position = static_cast<uint8_t>(std::stoi(params.at("position")));
        return sdkWrapper->SendWindowCoveringCommand(nodeId, endpoint, 5, position);
    }
    else if (command == "setTargetTemp") {
        int16_t temp = 21;
        if (params.count("temperature")) temp = static_cast<int16_t>(std::stoi(params.at("temperature")));
        return sdkWrapper->SendThermostatCommand(nodeId, endpoint, temp);
    }
    
    Log(3, "Unknown command: " + command);
    return false;
}

bool MatterProtocol::SendGroupCommand(uint16_t groupId, const std::string& command,
                                       const std::map<std::string, std::string>& params) {
    if (groups.find(groupId) == groups.end()) {
        ReportError(-301, "Group not found: " + std::to_string(groupId));
        return false;
    }
    
    auto members = GetGroupMembers(groupId);
    bool allSuccess = true;
    for (const auto& deviceId : members) {
        if (!SendCommand(deviceId, command, params)) allSuccess = false;
    }
    return allSuccess;
}

// ===== GROUPS =====

bool MatterProtocol::CreateGroup(uint16_t groupId, const std::string& name) {
    std::lock_guard<std::mutex> lock(matterMutex);
    groups[groupId] = name;
    groupMembers[groupId] = {};
    return true;
}

bool MatterProtocol::DeleteGroup(uint16_t groupId) {
    std::lock_guard<std::mutex> lock(matterMutex);
    groups.erase(groupId);
    groupMembers.erase(groupId);
    return true;
}

bool MatterProtocol::AddToGroup(const std::string& deviceId, uint16_t groupId) {
    uint64_t nodeId;
    {
        std::lock_guard<std::mutex> lock(matterMutex);
        if (groups.find(groupId) == groups.end()) return false;
        auto it = matterNodes.find(deviceId);
        if (it == matterNodes.end()) return false;
        nodeId = it->second.NodeId;
        
        auto& members = groupMembers[groupId];
        if (std::find(members.begin(), members.end(), deviceId) == members.end()) {
            members.push_back(deviceId);
        }
    }
    return sdkWrapper->AddDeviceToGroup(nodeId, 1, groupId, groups[groupId]);
}

bool MatterProtocol::RemoveFromGroup(const std::string& deviceId, uint16_t groupId) {
    uint64_t nodeId;
    {
        std::lock_guard<std::mutex> lock(matterMutex);
        auto it = groupMembers.find(groupId);
        if (it == groupMembers.end()) return false;
        auto nodeIt = matterNodes.find(deviceId);
        if (nodeIt == matterNodes.end()) return false;
        nodeId = nodeIt->second.NodeId;
        
        auto& members = it->second;
        members.erase(std::remove(members.begin(), members.end(), deviceId), members.end());
    }
    return sdkWrapper->RemoveDeviceFromGroup(nodeId, 1, groupId);
}

std::vector<uint16_t> MatterProtocol::GetGroups() const {
    std::vector<uint16_t> result;
    std::lock_guard<std::mutex> lock(matterMutex);
    for (const auto& [id, name] : groups) result.push_back(id);
    return result;
}

std::vector<std::string> MatterProtocol::GetGroupMembers(uint16_t groupId) const {
    std::lock_guard<std::mutex> lock(matterMutex);
    auto it = groupMembers.find(groupId);
    return (it != groupMembers.end()) ? it->second : std::vector<std::string>{};
}

// ===== BINDING =====

bool MatterProtocol::BindDevices(const std::string& sourceId, const std::string& targetId) {
    uint64_t srcNodeId, dstNodeId;
    {
        std::lock_guard<std::mutex> lock(matterMutex);
        auto srcIt = matterNodes.find(sourceId);
        auto dstIt = matterNodes.find(targetId);
        if (srcIt == matterNodes.end() || dstIt == matterNodes.end()) return false;
        srcNodeId = srcIt->second.NodeId;
        dstNodeId = dstIt->second.NodeId;
        
        auto& targets = bindings[sourceId];
        if (std::find(targets.begin(), targets.end(), targetId) == targets.end()) {
            targets.push_back(targetId);
        }
    }
    return sdkWrapper->CreateBinding(srcNodeId, 1, dstNodeId, 1, MatterClusters::OnOff);
}

bool MatterProtocol::UnbindDevices(const std::string& sourceId, const std::string& targetId) {
    std::lock_guard<std::mutex> lock(matterMutex);
    auto it = bindings.find(sourceId);
    if (it == bindings.end()) return false;
    auto& targets = it->second;
    targets.erase(std::remove(targets.begin(), targets.end(), targetId), targets.end());
    return true;
}

std::vector<std::string> MatterProtocol::GetBindings(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(matterMutex);
    auto it = bindings.find(deviceId);
    return (it != bindings.end()) ? it->second : std::vector<std::string>{};
}

// ===== OTA =====

bool MatterProtocol::StartOTAUpdate(const std::string& deviceId, const std::string& firmwarePath) {
    uint64_t nodeId;
    {
        std::lock_guard<std::mutex> lock(matterMutex);
        auto it = matterNodes.find(deviceId);
        if (it == matterNodes.end()) return false;
        nodeId = it->second.NodeId;
        otaProgress[deviceId] = 0;
    }
    Log(2, "Starting OTA update for " + deviceId);
    return sdkWrapper->InitiateOTAUpdate(nodeId, firmwarePath);
}

int MatterProtocol::GetOTAProgress(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(matterMutex);
    auto it = otaProgress.find(deviceId);
    return (it != otaProgress.end()) ? it->second : -1;
}

bool MatterProtocol::CancelOTAUpdate(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(matterMutex);
    otaProgress.erase(deviceId);
    return true;
}

// ===== SECURITY =====

SmartHomeSecurityLevel MatterProtocol::GetSecurityLevel() const {
    return SmartHomeSecurityLevel::Certified;  // device attestation certificates
}
bool MatterProtocol::SetNetworkKey(const std::vector<uint8_t>& key) { return true; }

// ===== CONFIGURATION =====

bool MatterProtocol::LoadConfig(const std::string& path) { storagePath = path; return true; }
bool MatterProtocol::SaveConfig(const std::string& path) { return true; }

std::vector<SmartHomeDeviceCategory> MatterProtocol::GetSupportedDeviceCategories() const {
    return {
        SmartHomeDeviceCategory::Light, SmartHomeDeviceCategory::Switch,
        SmartHomeDeviceCategory::Plug, SmartHomeDeviceCategory::Thermostat,
        SmartHomeDeviceCategory::Lock, SmartHomeDeviceCategory::Blind,
        SmartHomeDeviceCategory::Fan, SmartHomeDeviceCategory::Sensor,
        SmartHomeDeviceCategory::Speaker
    };
}

// ===== FABRIC MANAGEMENT =====

bool MatterProtocol::AddFabric(const MatterFabric& fabric) {
    std::lock_guard<std::mutex> lock(matterMutex);
    for (const auto& f : fabrics) {
        if (f.FabricId == fabric.FabricId) return false;
    }
    fabrics.push_back(fabric);
    return true;
}

bool MatterProtocol::RemoveFabric(uint64_t fabricId) {
    std::lock_guard<std::mutex> lock(matterMutex);
    auto it = std::find_if(fabrics.begin(), fabrics.end(),
        [fabricId](const MatterFabric& f) { return f.FabricId == fabricId; });
    if (it != fabrics.end()) { fabrics.erase(it); return true; }
    return false;
}

std::vector<MatterFabric> MatterProtocol::GetFabrics() const {
    std::lock_guard<std::mutex> lock(matterMutex);
    return fabrics;
}

// ===== COMMISSIONING WINDOW =====

bool MatterProtocol::OpenCommissioningWindow(const std::string& deviceId, int timeoutSeconds) {
    Log(2, "Opening commissioning window for " + deviceId);
    return true;
}

bool MatterProtocol::CloseCommissioningWindow(const std::string& deviceId) {
    Log(2, "Closing commissioning window for " + deviceId);
    return true;
}

std::string MatterProtocol::GenerateSetupCode(const std::string& deviceId) const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 99999999);
    std::stringstream ss;
    ss << std::setw(11) << std::setfill('0') << dist(gen);
    return ss.str();
}

std::string MatterProtocol::GenerateQRCode(const std::string& deviceId) const {
    return "MT:Y.K90-SO040000000000";
}

// ===== ATTRIBUTE ACCESS =====

bool MatterProtocol::ReadAttribute(const std::string& deviceId, uint16_t endpoint,
                                    uint32_t clusterId, uint32_t attributeId,
                                    std::string& outValue) {
    uint64_t nodeId;
    {
        std::lock_guard<std::mutex> lock(matterMutex);
        auto it = matterNodes.find(deviceId);
        if (it == matterNodes.end()) return false;
        nodeId = it->second.NodeId;
    }
    
    bool success = false;
    sdkWrapper->ReadAttribute(nodeId, endpoint, clusterId, attributeId,
        [&success, &outValue](bool ok, const std::string& value) {
            success = ok;
            outValue = value;
        });
    return success;
}

bool MatterProtocol::WriteAttribute(const std::string& deviceId, uint16_t endpoint,
                                     uint32_t clusterId, uint32_t attributeId,
                                     const std::string& value) {
    uint64_t nodeId;
    {
        std::lock_guard<std::mutex> lock(matterMutex);
        auto it = matterNodes.find(deviceId);
        if (it == matterNodes.end()) return false;
        nodeId = it->second.NodeId;
    }
    return sdkWrapper->WriteAttribute(nodeId, endpoint, clusterId, attributeId, value);
}

bool MatterProtocol::SubscribeAttribute(const std::string& deviceId, uint16_t endpoint,
                                         uint32_t clusterId, uint32_t attributeId,
                                         OnAttributeChange callback) {
    uint64_t nodeId;
    {
        std::lock_guard<std::mutex> lock(matterMutex);
        auto it = matterNodes.find(deviceId);
        if (it == matterNodes.end()) return false;
        nodeId = it->second.NodeId;
    }
    
    std::string key = deviceId + "_" + std::to_string(endpoint) + "_" +
                      std::to_string(clusterId) + "_" + std::to_string(attributeId);
    attributeSubscriptions[key] = callback;
    
    return sdkWrapper->SubscribeAttribute(nodeId, endpoint, clusterId, attributeId, 10, 60,
        [callback, deviceId, endpoint, clusterId, attributeId](const std::string& value) {
            if (callback) callback(deviceId, endpoint, clusterId, attributeId, value);
        });
}

bool MatterProtocol::InvokeCommand(const std::string& deviceId, uint16_t endpoint,
                                    uint32_t clusterId, uint32_t commandId,
                                    const std::map<std::string, std::string>& fields) {
    auto params = fields;
    params["endpoint"] = std::to_string(endpoint);
    return SendCommand(deviceId, std::to_string(commandId), params);
}

// ===== NODE ACCESS =====

MatterNode MatterProtocol::GetMatterNode(const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock(matterMutex);
    auto it = matterNodes.find(deviceId);
    return (it != matterNodes.end()) ? it->second : MatterNode{};
}

std::vector<MatterNode> MatterProtocol::GetAllMatterNodes() const {
    std::vector<MatterNode> result;
    std::lock_guard<std::mutex> lock(matterMutex);
    for (const auto& [id, node] : matterNodes) result.push_back(node);
    return result;
}

// ===== THREAD INTEGRATION =====

bool MatterProtocol::SetThreadDataset(const std::vector<uint8_t>& dataset) {
    threadDataset = dataset;
    SetCapability(ProtocolCapability::BorderRouter);
    return true;
}

std::vector<uint8_t> MatterProtocol::GetThreadDataset() const { return threadDataset; }
bool MatterProtocol::IsThreadBorderRouter() const { return HasCapability(ProtocolCapability::BorderRouter); }

// ===== EVENT LOOP =====

void MatterProtocol::EventLoopThread() {
    Log(3, "Event loop thread started");
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    Log(3, "Event loop thread stopped");
}

// ===== HELPERS =====

SmartHomeDeviceInfo MatterProtocol::NodeToDeviceInfo(const MatterNode& node) const {
    SmartHomeDeviceInfo info;
    info.DeviceId = node.DeviceId;
    info.Name = node.ProductName.empty() ? ("Matter Device " + std::to_string(node.NodeId)) : node.ProductName;
    info.Manufacturer = node.VendorName;
    info.Model = node.ProductName;
    info.FirmwareVersion = node.SoftwareVersion;
    info.Protocol = SmartHomeProtocolType::Matter;
    info.Category = DetermineDeviceCategory(node.DeviceTypes);
    info.State = node.IsOnline ? SmartHomeDeviceState::Online : SmartHomeDeviceState::Offline;
    return info;
}

SmartHomeDeviceCategory MatterProtocol::DetermineDeviceCategory(
    const std::vector<uint32_t>& deviceTypes) const {
    
    for (uint32_t type : deviceTypes) {
        switch (type) {
            case MatterDeviceTypes::OnOffLight:
            case MatterDeviceTypes::DimmableLight:
            case MatterDeviceTypes::ColorTemperatureLight:
            case MatterDeviceTypes::ExtendedColorLight:
                return SmartHomeDeviceCategory::Light;
            case MatterDeviceTypes::OnOffPlug:
            case MatterDeviceTypes::DimmablePlug:
                return SmartHomeDeviceCategory::Plug;
            case MatterDeviceTypes::OnOffSwitch:
            case MatterDeviceTypes::DimmerSwitch:
            case MatterDeviceTypes::ColorDimmerSwitch:
                return SmartHomeDeviceCategory::Switch;
            case MatterDeviceTypes::Thermostat:
                return SmartHomeDeviceCategory::Thermostat;
            case MatterDeviceTypes::DoorLock:
                return SmartHomeDeviceCategory::Lock;
            case MatterDeviceTypes::WindowCovering:
                return SmartHomeDeviceCategory::Blind;
            case MatterDeviceTypes::Fan:
                return SmartHomeDeviceCategory::Fan;
            case MatterDeviceTypes::TemperatureSensor:
            case MatterDeviceTypes::HumiditySensor:
            case MatterDeviceTypes::OccupancySensor:
            case MatterDeviceTypes::ContactSensor:
            case MatterDeviceTypes::LightSensor:
            case MatterDeviceTypes::SmokeDetector:
                return SmartHomeDeviceCategory::Sensor;
            case MatterDeviceTypes::Speaker:
                return SmartHomeDeviceCategory::Speaker;
        }
    }
    return SmartHomeDeviceCategory::Unknown;
}

// ===== FACTORY FUNCTION =====

std::shared_ptr<ISmartHomeProtocol> CreateMatterProtocol() {
    return std::make_shared<MatterProtocol>();
}

} // namespace SmartHome
} // namespace UltraCanvas

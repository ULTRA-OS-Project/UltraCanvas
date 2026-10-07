// include/IODeviceManager/UltraCanvasIODevicePrinterIPPProtocol.h
// IPP, the Internet Printing Protocol, without the network.
//
// A driverless printer - IPP Everywhere, AirPrint, Mopria - is spoken to in
// IPP: a binary message (RFC 8010) POSTed over HTTP, answered by another. The
// printer describes itself in the same encoding, and accepts a document in a
// format it names, so nothing on this side needs a driver for it.
//
// Everything that can be decided without a printer on the other end lives
// here, so that a test can decide it: the message encoding in both
// directions, reading a printer's description into this module's vocabulary,
// turning print options into the attributes a job carries, and working out
// whether a document goes to the printer as it is or has to be drawn first.
// UltraCanvasIODevicePrinterIPP.cpp holds the part that needs a network.
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevicePrinterTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// THE ENCODING
// ============================================================================

// One byte on the wire, naming either the start of an attribute group
// (0x00-0x0F, the "delimiter" tags) or the type of a value (everything else).
enum class IppTag : uint8_t {
    // Delimiters: which group the attributes that follow belong to.
    OperationGroup = 0x01,
    JobGroup = 0x02,
    EndOfAttributes = 0x03,
    PrinterGroup = 0x04,
    UnsupportedGroup = 0x05,

    // Out-of-band values: a value that says why there is no value.
    Unsupported = 0x10,
    Unknown = 0x12,
    NoValue = 0x13,

    // Integers.
    Integer = 0x21,
    Boolean = 0x22,
    Enum = 0x23,

    // Octet strings and structured values.
    OctetString = 0x30,
    DateTime = 0x31,
    Resolution = 0x32,
    RangeOfInteger = 0x33,
    BeginCollection = 0x34,
    TextWithLanguage = 0x35,
    NameWithLanguage = 0x36,
    EndCollection = 0x37,

    // Character strings.
    Text = 0x41,
    Name = 0x42,
    Keyword = 0x44,
    Uri = 0x45,
    UriScheme = 0x46,
    Charset = 0x47,
    NaturalLanguage = 0x48,
    MimeMediaType = 0x49,
    MemberAttrName = 0x4A
};

struct IppAttribute;

// One value of an attribute. Which fields mean something depends on the tag;
// the rest stay at their defaults.
struct IppValue {
    IppTag tag = IppTag::NoValue;

    // Integer, Enum, Boolean (0 or 1), the lower bound of a RangeOfInteger,
    // the cross-feed resolution of a Resolution.
    int32_t integer = 0;

    // The upper bound of a RangeOfInteger, the feed resolution of a
    // Resolution.
    int32_t upper = 0;

    // A Resolution's units: 3 is dots per inch, 4 dots per centimetre.
    uint8_t units = 3;

    // Every string type, as UTF-8 - and the raw bytes of an OctetString or
    // DateTime, which are not text but have nowhere better to go. For the
    // two ...WithLanguage types this is the text alone; the language is
    // dropped on reading and sent as "en" on writing.
    std::string text;

    // The members of a collection, in order.
    std::vector<IppAttribute> members;

    static IppValue Integer(int32_t value);
    static IppValue Enum(int32_t value);
    static IppValue Boolean(bool value);
    static IppValue Keyword(const std::string& value);
    static IppValue Name(const std::string& value);
    static IppValue Text(const std::string& value);
    static IppValue Uri(const std::string& value);
    static IppValue Charset(const std::string& value);
    static IppValue Language(const std::string& value);
    static IppValue MimeType(const std::string& value);
    static IppValue Range(int32_t lower, int32_t upperBound);
    static IppValue Dpi(int32_t crossFeed, int32_t feed);
    static IppValue Collection(std::vector<IppAttribute> members);

    // Whether the value is one of the string types, and so has `text`.
    bool IsString() const;
};

// A named attribute and its values. "1setOf" attributes have several values;
// most have one.
struct IppAttribute {
    std::string name;
    std::vector<IppValue> values;

    IppAttribute() = default;
    IppAttribute(std::string attributeName, IppValue value);
    IppAttribute(std::string attributeName, std::vector<IppValue> attributeValues);

    // The first value, or null when there are none.
    const IppValue* First() const;

    // The text of every string value, in order.
    std::vector<std::string> Strings() const;

    // The first value's text, or empty.
    std::string String() const;

    // The first value's integer, or `fallback` when there is no integer-typed
    // first value.
    int32_t IntegerOr(int32_t fallback) const;

    // The first value as a boolean, or `fallback`.
    bool BooleanOr(bool fallback) const;

    // Whether any string value equals `text`, ignoring ASCII case - keywords
    // and MIME types are case-insensitive, and printers are not consistent.
    bool Contains(const std::string& text) const;

    // Whether any integer value equals `value`.
    bool ContainsInteger(int32_t value) const;

    // A member of a collection value, or null.
    const IppAttribute* Member(const std::string& memberName,
                               size_t valueIndex = 0) const;
};

// The attributes that follow one delimiter tag.
struct IppGroup {
    IppTag tag = IppTag::OperationGroup;
    std::vector<IppAttribute> attributes;

    const IppAttribute* Find(const std::string& name) const;

    // Appends, or replaces an attribute of the same name - a request may not
    // carry one name twice, and a caller's explicit value should win over a
    // derived one.
    void Set(const std::string& name, IppValue value);
    void Set(const std::string& name, std::vector<IppValue> values);
};

// A request or a response.
struct IppMessage {
    uint8_t versionMajor = 2;
    uint8_t versionMinor = 0;

    // The operation-id in a request, the status-code in a response.
    uint16_t code = 0;
    uint32_t requestId = 1;

    std::vector<IppGroup> groups;

    IppGroup& AddGroup(IppTag tag);

    // The first group with this tag, or null.
    const IppGroup* FindGroup(IppTag tag) const;
    IppGroup* FindGroup(IppTag tag);

    // Every group with this tag. Get-Jobs answers with one job group per job.
    std::vector<const IppGroup*> FindGroups(IppTag tag) const;

    // An attribute of the first group with this tag, or null.
    const IppAttribute* Find(IppTag groupTag, const std::string& name) const;
};

// The message on the wire, ending with the end-of-attributes tag. A Print-Job
// request's document follows it directly, in the same HTTP body.
std::vector<uint8_t> EncodeIppMessage(const IppMessage& message);

// Reads a message. `outConsumed` receives how many bytes it occupied, which
// is where any document data after it begins. On failure `outError` says
// what was wrong, in terms a person reading a log could act on.
//
// Every length is checked against what is actually there before it is used:
// the bytes come from a device on the network, and a malformed or hostile
// reply has to be refused rather than read past.
bool DecodeIppMessage(const uint8_t* data, size_t size, IppMessage& out,
                      size_t* outConsumed = nullptr,
                      std::string* outError = nullptr);

// ============================================================================
// OPERATIONS AND STATUS CODES
// ============================================================================

enum class IppOperation : uint16_t {
    PrintJob = 0x0002,
    ValidateJob = 0x0004,
    CancelJob = 0x0008,
    GetJobAttributes = 0x0009,
    GetJobs = 0x000A,
    GetPrinterAttributes = 0x000B
};

// Status codes the backend treats specially. The rest are reported by name.
constexpr uint16_t kIppStatusOk = 0x0000;
constexpr uint16_t kIppStatusNotFound = 0x0406;
constexpr uint16_t kIppStatusVersionNotSupported = 0x0503;
constexpr uint16_t kIppStatusTemporaryError = 0x0505;
constexpr uint16_t kIppStatusBusy = 0x0507;

// Whether a refusal means "not now" rather than "no": the printer is busy
// with another job, or has a passing problem. A job refused this way is worth
// sending again after a pause; one refused for anything else is not.
inline bool IppStatusIsRetryable(uint16_t status) {
    return status == kIppStatusBusy || status == kIppStatusTemporaryError;
}

// 0x0000-0x00FF are all success. Several of them mean "done, but I changed
// something" - a printer substituting an option it does not support is the
// ordinary case, not a failure - so the class is tested, not the members.
inline bool IppStatusSucceeded(uint16_t status) { return status < 0x0100; }

// The keyword RFC 8011 gives a status code ("client-error-not-found"), or a
// hex number for one it does not name.
std::string IppStatusToString(uint16_t status);

// This module's result code for a failed status.
IODeviceResultCode IppStatusToResultCode(uint16_t status);

// A request with the operation attributes every operation starts with, in
// the order RFC 8011 requires: attributes-charset, attributes-natural-
// language, then the target printer-uri, then who is asking.
IppMessage MakeIppRequest(IppOperation operation, uint32_t requestId,
                          const std::string& printerUri,
                          const std::string& userName);

// ============================================================================
// ADDRESSES
// ============================================================================

// The http:// or https:// URL an ipp:// or ipps:// URI is POSTed to. IPP's
// default port is 631 for both, where HTTP's would be 80 and 443, so a URI
// without one has 631 written in. An http(s) URL is returned unchanged, and
// anything else as an empty string.
std::string IppHttpUrlFor(const std::string& printerUri);

// A printer address as a person wrote it, as the ipp:// or ipps:// URI the
// printer expects in printer-uri. "http://host:631/ipp/print" becomes
// "ipp://host:631/ipp/print"; an http(s) URL with no port keeps the one its
// scheme implies (80, 443), since that is where the printer was said to be.
// Surrounding spaces and trailing slashes go. Empty for anything that is not
// an ipp, ipps, http or https address.
std::string IppNormalizePrinterUri(const std::string& address);

// The IPP printer URIs worth asking a Windows print queue's printer at, most
// likely first, given the queue's port. Windows keeps no printer URI for a
// queue, only a port, so this is the best guess the port allows:
//
//   http://10.0.0.5:631/ipp/print    an IPP port: that address, as IPP
//   IP_10.0.0.5, 10.0.0.5_1          a Standard TCP/IP port named after its
//                                    address: IPP Everywhere's /ipp/print on
//                                    631, then /ipp and the root, where older
//                                    printers answer
//
// `hostAddress` is the address a Standard TCP/IP port is configured with,
// when the caller could read it (the port monitor's "HostAddress"); it wins
// over whatever the port is called, since a port can be named anything. A
// port with no address in it - USB001, LPT1:, FILE:, a WSD-... port - and no
// host address gives none.
std::vector<std::string> IppUrisForWindowsPort(const std::string& portName,
                                               const std::string& hostAddress);

// A host in one spelling, so that two spellings of one address compare
// equal: lower case, no DNS root dot ("printer.local."), no IPv6 brackets.
std::string IppNormalizeHost(const std::string& host);

// The host of a URI ("http://10.0.0.5:3911/" gives "10.0.0.5"), normalized as
// IppNormalizeHost does. Empty when it is not a URI.
std::string IppUriHost(const std::string& uri);

// One Windows device node, as the spooler backend reads it through SetupAPI.
// Only what finding a WSD printer's address takes.
struct IppWindowsDeviceNode {
    std::string containerId;                 // DEVPKEY_Device_ContainerId
    std::string friendlyName;                // DEVPKEY_Device_FriendlyName
    bool isPrintQueue = false;               // device class PrintQueue
    std::vector<std::string> ipAddresses;    // PKEY_PNPX_IpAddress
    std::vector<std::string> xAddrs;         // PKEY_PNPX_XAddrs: WS-Discovery URLs
    std::string location;                    // DEVPKEY_Device_LocationInfo
};

// The network addresses of the printer behind Windows queue `queueName`.
//
// A WSD port carries no address the spooler gives out, but Plug and Play
// puts the queue's device node and the WSD device it prints to in one
// *container* - the box on the network - and the WSD device node has the
// printer's addresses: PnP-X's IpAddress, or failing that, the host of its
// WS-Discovery URLs or of its location. So the queue's container is looked
// up by its name, and the addresses read off the other nodes in it. IPv4
// first, each once.
//
// A queue in no container, or in the computer's own ({00000000-0000-0000-
// ffff-ffffffffffff}, which every built-in device shares), gives none.
std::vector<std::string> IppHostsForWindowsQueue(const std::string& queueName,
                                                 const std::vector<IppWindowsDeviceNode>& nodes);

// Whether a printer found under DNS-SD - at `mdnsHost`, answering at
// `mdnsIp` - is one a Windows queue already reaches, given the hosts the
// queues print to. The Windows counterpart of IppCupsQueueReachesPrinter: the
// queue is the system's own way to the printer, so the IPP backend leaves it
// to the spooler rather than listing it twice.
bool IppPrinterIsWindowsQueue(const std::vector<std::string>& queueHosts,
                              const std::string& mdnsHost, const std::string& mdnsIp);

// The printer URI a DNS-SD advertisement describes. `rp` in the TXT record is
// the resource path ("ipp/print"); absent, the printer is at the root, which
// is what the Bonjour printing specification says an absent `rp` means.
std::string IppUriFromMdns(const std::string& host, int port,
                           const std::vector<std::string>& txt, bool useTls);

// The value of `key` in a TXT record given as "key=value" strings, compared
// without regard to ASCII case, as DNS-SD keys are. Empty when absent.
std::string IppTxtValue(const std::vector<std::string>& txt,
                        const std::string& key);

// The instance name - "Office Printer" - out of the DNS-SD name the mDNS
// plugin reports as an entry's `dn`. Every backend of that plugin reports the
// *full* service name there ("Office Printer._ipp._tcp.local"), and they
// differ in escaping: Avahi and Win32 hand it back readable, Bonjour in DNS
// presentation form ("Office\032Printer._ipp._tcp.local."). So the service
// type and domain are cut at the last "._ipp._tcp" or "._ipps._tcp", and DNS
// escapes are undone (DnsSdInstanceName() does the work, shared with eSCL).
// A name with no service type in it is taken to be the instance already.
std::string IppInstanceFromServiceName(const std::string& serviceName);

// The device id a printer is registered under: "urn:uuid:<uuid>" when its
// UUID is known - the form its own printer-uuid attribute takes, so the id
// is the same however the printer was found - and "ipp:<uri>" otherwise.
std::string IppDeviceIdFor(const std::string& uuid, const std::string& printerUri);

// Whether a CUPS queue whose device-uri is `cupsDeviceUri` already reaches
// the printer that was discovered under DNS-SD name `serviceName` (as the
// mDNS plugin reports it - see IppInstanceFromServiceName), with UUID `uuid`,
// at `printerUri`.
//
// CUPS finds driverless printers itself and offers each as a queue, so on a
// machine running CUPS the same printer would otherwise be listed twice. The
// registry cannot collapse the two by device id, because the ids do not
// agree: libcups does not return printer-uuid among a destination's options
// (checked against CUPS 2.4.7, for its discovered queues and configured ones
// alike), so the CUPS backend names every queue "cups:<name>". The match is
// made on what a queue's device-uri does carry instead:
//
//   dnssd://Office%20Printer._ipp._tcp.local/?uuid=...   the UUID
//   ipps://Office%20Printer._ipps._tcp.local/            the instance name
//   ipp://192.168.1.20:631/ipp/print                     host, port and path
//
// The instance name is compared after undoing both kinds of escaping it can
// arrive in: percent-encoding in the URI, and DNS escapes (`\032`, `\.`) in
// the name Bonjour hands back.
bool IppCupsQueueReachesPrinter(const std::string& cupsDeviceUri,
                                const std::string& serviceName,
                                const std::string& uuid,
                                const std::string& printerUri);

// ============================================================================
// WHAT A PRINTER SAYS ABOUT ITSELF
// ============================================================================

// What a printer can do, read from a Get-Printer-Attributes response's
// printer group. Sizes come from media-supported, whose PWG self-describing
// names ("iso_a4_210x297mm") carry their own dimensions, so a size is
// recognised whatever else the printer calls it.
IOPrinterCapabilities IppCapabilitiesFromAttributes(const IppGroup& printer);

// State, reasons, whether it is taking jobs, and its supplies.
IOPrinterStatus IppStatusFromAttributes(const IppGroup& printer);

// Supply levels. From the marker-* attributes when the printer has them, and
// otherwise from printer-supply (PWG 5100.13), which IPP Everywhere printers
// report instead.
std::vector<IOSupplyLevel> IppSuppliesFromAttributes(const IppGroup& printer);

// One job, from a Get-Jobs or Get-Job-Attributes response's job group.
IOPrintJobStatus IppJobStatusFromAttributes(const IppGroup& job);

// The size, in hundredths of a millimetre, that a PWG self-describing media
// name states: "iso_a4_210x297mm" is 21000 x 29700, "na_letter_8.5x11in" is
// 21590 x 27940. Zero dimensions for a name that states none.
//
// Parsed in integer arithmetic, not through a float: a media name is a file
// format number, and the float parsers read it by the user's locale.
IOPaperDimensions IppMediaDimensionsFromPwgName(const std::string& name);

// What the printer takes in, and what this side needs to know to draw pages
// for it. Not part of IOPrinterCapabilities because none of it is a choice a
// user makes: it decides how a job is sent, not what is printed.
struct IppDocumentSupport {
    std::vector<std::string> documentFormats;       // document-format-supported
    std::vector<std::string> rasterTypes;           // pwg-raster-document-type-supported
    std::vector<IOResolution> rasterResolutions;    // pwg-raster-document-resolution-supported
    std::string rasterSheetBack = "normal";         // pwg-raster-document-sheet-back
    bool pageRanges = false;                        // page-ranges-supported

    // The margin the printer can print within on every medium it takes: the
    // largest value each media-*-margin-supported lists, which is how CUPS
    // lays out a page for a driverless printer. A PWG raster page is printed
    // as it is, edge to edge, so anything drawn inside this is lost to the
    // printer's unprintable border. A printer that lists no margins is given
    // CUPS's default: 12.7 mm top and bottom, 6.35 mm at the sides.
    IOPageMargins margins{635, 1270, 635, 1270};

    // Every edge lists a zero margin, so the printer can print to the edge.
    bool borderless = false;

    bool Accepts(const std::string& mimeType) const;
};

IppDocumentSupport IppDocumentSupportFromAttributes(const IppGroup& printer);

// The margins a page is drawn within: on each edge, the larger of what the
// job asks for and what the printer needs - or what the job asks for alone
// when it asks for borderless printing on a printer that can do it.
IOPageMargins IppDrawingMargins(const IppDocumentSupport& printer, const IOPageSetup& page);

// ============================================================================
// WHAT A JOB CARRIES
// ============================================================================

// The IPP media-type keyword for a media type, or empty when IPP has none
// that means the same thing - in which case none is sent, rather than a
// near miss the printer would act on.
std::string IppMediaTypeKeyword(IOMediaType type);

// The job-template attributes for `options`, added to the job group of a
// Print-Job request.
//
// `pageRange` is sent only when non-empty, and should be empty when the pages
// were already selected before the document was made - a range applied twice
// prints the wrong pages. `drawnHere` says the document is PWG raster this
// side drew: orientation and resolution are then already in the pixels, and
// asking the printer to apply them again would rotate the page twice.
//
// Anything in options.backendOptions is added last and replaces a derived
// attribute of the same name. Its value is sent as an integer when it is all
// digits, a boolean when it is "true" or "false", and a keyword otherwise.
void AddIppJobTemplate(IppGroup& job, const IOPrintOptions& options,
                       const std::vector<int>& pageRange, bool drawnHere);

// ============================================================================
// SENDING A DOCUMENT AS IT IS, OR DRAWING IT
// ============================================================================

// The type a job of pages that draw themselves (IOPrintJob::pages) is planned
// as. No printer names it, so such a job is always drawn here, as PWG raster.
extern const char* const kIppDrawnPagesType;

// The MIME type of what a job holds: its declared type, or one inferred from
// the file name; kIppDrawnPagesType for a job that is only pages. Empty when
// nothing says.
std::string IppJobDocumentType(const IOPrintJob& job);

struct IppDocumentPlan {
    // True: send the job's own bytes. False: draw its pages here and send
    // them as PWG raster.
    bool passThrough = false;

    // What the Print-Job request declares as document-format.
    std::string documentFormat;

    // Whether the printer should apply the job's page range. False when the
    // pages are selected here, and when there is nothing to select.
    bool sendPageRange = false;
};

// Decides how a job of type `documentType` reaches this printer.
//
// A document the printer names in document-format-supported is sent as it
// is - a PDF to a printer that renders PDF is printed by the printer, which
// knows its own engine best. Plain text is the exception: it is always drawn
// here, because what a printer's own text path does with UTF-8 is anybody's
// guess, and a page this side draws is the same page on every printer.
// Anything else this side can draw - text, and the images the page sources
// decode - becomes PWG raster, which every IPP Everywhere printer must
// accept.
//
// Refused, by name, rather than half-printed: a PDF to a printer that takes
// none (nothing here can paginate one yet), a page range on a document the
// printer cannot select pages from, and a printer that takes neither the
// document nor PWG raster.
IODeviceResult PlanIppDocument(const std::string& documentType,
                               const std::vector<int>& pageRange,
                               const IppDocumentSupport& printer,
                               IppDocumentPlan& out);

// The PWG raster type to draw in: "srgb_8" or "sgray_8", whichever the
// printer takes that best matches the colour mode asked for. Empty when it
// takes neither.
std::string ChoosePwgRasterType(const std::vector<std::string>& rasterTypes,
                                IOPrinterColorMode colorMode);

// The resolution to draw at, from the ones the printer takes for PWG raster.
//
// An explicit resolution the printer supports is used as asked. Otherwise
// Draft takes the lowest, Normal the one nearest 300 dpi, and High and Photo
// the highest up to 600 dpi. That ceiling is deliberate: a page is drawn and
// held in memory whole, and an A4 page at 1200 dpi is 400 MB of RGB for
// detail no text or photograph printed from here would show.
IOResolution ChoosePwgRasterResolution(const std::vector<IOResolution>& supported,
                                       IOPrintQuality quality,
                                       IOResolution requested);

}  // namespace UltraCanvas

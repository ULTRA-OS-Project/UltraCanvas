// include/IODeviceManager/UltraCanvasIODeviceScannerESCLProtocol.h
// eSCL, the driverless scanning protocol - the parts that are pure data.
//
// eSCL (Apple calls it AirScan, the Mopria Alliance calls it Mopria Scan) is
// what a modern network scanner speaks when nobody has installed a driver for
// it. It is plain HTTP and XML, four calls:
//
//   GET    {base}/ScannerCapabilities   what the scanner can do
//   POST   {base}/ScanJobs              start a job; the reply's Location
//                                       header names it
//   GET    {job}/NextDocument           the next page, or 404 when there are
//                                       no more
//   DELETE {job}                        cancel
//
// **That 404 is the protocol's way of saying the feeder is empty**, and it is
// how a multi-page run ends normally. It maps exactly onto this module's
// existing rule: DoScanPage() returns DeviceNotFound and ScanPages() treats
// it as the end of the run rather than a failure - but only once a page has
// been produced, so a 404 on the very first page stays the error it is.
//
// Everything in this header is a pure function over strings and structs: no
// sockets, no HTTP, no device. That is deliberate - the fiddly parts of eSCL
// are parsing a capability document and getting the units right, and neither
// should need a scanner on the network to test.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODeviceScannerTypes.h"
#include "UltraCanvasIODeviceTypes.h"

#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// UNITS
// ============================================================================

// eSCL measures scan regions in **three-hundredths of an inch** - the
// ContentRegionUnits value is literally "escl:ThreeHundredthsOfInches" - while
// this module measures everything in hundredths of a millimetre. An inch is
// 2540 hundredths of a millimetre, so the ratio is 2540/300.
//
// Both directions round to nearest rather than truncating: a scan area is
// derived from a paper size and then handed back, and truncation twice turns
// A4 into something a millimetre short.
int EsclUnitsToHundredthsMM(int units);
int HundredthsMMToEsclUnits(int hundredthsMM);

// ============================================================================
// COLOUR MODES
// ============================================================================

// eSCL names a colour mode and its depth together - "RGB24", "Grayscale8",
// "BlackAndWhite1" - so the bit depth comes back alongside the mode.
ScanColorMode EsclColorModeFromName(const std::string& name, int* outBitsPerSample = nullptr);

// The name to ask for. Empty for a mode eSCL has no word for.
std::string EsclColorModeToName(ScanColorMode mode);

// ============================================================================
// CAPABILITIES
// ============================================================================

// What a scanner said about itself, beyond what ScanCapabilities can hold.
struct EsclScannerDescription {
    std::string makeAndModel;
    std::string serialNumber;
    std::string version;

    ScanCapabilities capabilities;

    // Which document formats it will return. eSCL scanners must offer
    // image/jpeg; many also offer application/pdf and image/png. Kept
    // separately because ScanCapabilities has no format field - a backend
    // hands back decoded raster, so the wire format is its own business.
    std::vector<std::string> documentFormats;

    bool HasSource(ScanSource source) const;
};

// Reads a ScannerCapabilities document.
//
// The elements are namespace-prefixed - `scan:ScannerCapabilities`,
// `pwg:MakeAndModel` - and different vendors prefix differently, so lookups
// match on the local name after the colon rather than on the whole thing.
// tinyxml2 does not strip prefixes for us.
IODeviceResult ParseEsclCapabilities(const std::string& xml,
                                     EsclScannerDescription& outDescription);

// ============================================================================
// SCAN SETTINGS
// ============================================================================

// Builds the ScanSettings document that starts a job.
//
// `documentFormat` is the MIME type to ask the scanner for; pass one the
// scanner listed. The region is omitted entirely when the configuration names
// no area, which asks the scanner for its own default - better than guessing
// a size, since a guess that exceeds the bed is an error on some firmware.
std::string BuildEsclScanSettings(const ScanConfiguration& configuration,
                                  const std::string& documentFormat);

// ============================================================================
// URLS
// ============================================================================

// The job URL from a POST /ScanJobs reply.
//
// The Location header is allowed to be absolute or an absolute path, and
// firmware differs, so both are handled. An empty result means the scanner
// said nothing usable and the job cannot be collected.
std::string ResolveEsclJobUrl(const std::string& baseUrl,
                              const std::string& location);

// The base URL for a scanner found over mDNS.
//
// The TXT records carry `rs=` - the resource path, almost always "eSCL" but
// not required to be - so it is read rather than assumed. `txt` holds entries
// as "key=value", which is how the mDNS plugin reports them.
std::string EsclBaseUrlFromMdns(const std::string& host, int port,
                                const std::vector<std::string>& txtRecords,
                                bool useTls);

// One TXT record's value, or empty. Keys are matched case-insensitively
// because the DNS-SD convention is that they are case-insensitive.
std::string EsclTxtValue(const std::vector<std::string>& txtRecords,
                         const std::string& key);

}  // namespace UltraCanvas

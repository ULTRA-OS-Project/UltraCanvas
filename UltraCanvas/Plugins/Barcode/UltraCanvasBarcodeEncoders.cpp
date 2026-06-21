// Plugins/Barcode/UltraCanvasBarcodeEncoders.cpp
// Encoder factory + symbology metadata. Per-symbology encoders live in
// Encoders/<Symbology>Encoder.cpp.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "Plugins/Barcode/UltraCanvasBarcodeEncoders.h"
#include <memory>

namespace UltraCanvas {

    // Each encoder file exposes a single factory function with this signature.
    std::unique_ptr<IBarcodeEncoder> CreateCode39Encoder(const BarcodeEncoderOptions&, bool extended);
    std::unique_ptr<IBarcodeEncoder> CreateCode93Encoder(const BarcodeEncoderOptions&);
    std::unique_ptr<IBarcodeEncoder> CreateCode128Encoder(const BarcodeEncoderOptions&,
                                                          BarcodeSymbology mode);
    std::unique_ptr<IBarcodeEncoder> CreateEANUPCEncoder(const BarcodeEncoderOptions&,
                                                         BarcodeSymbology mode);
    std::unique_ptr<IBarcodeEncoder> CreateITFEncoder(const BarcodeEncoderOptions&,
                                                      BarcodeSymbology mode);
    std::unique_ptr<IBarcodeEncoder> CreateStandard25Encoder(const BarcodeEncoderOptions&);
    std::unique_ptr<IBarcodeEncoder> CreateCodabarEncoder(const BarcodeEncoderOptions&);
    std::unique_ptr<IBarcodeEncoder> CreateMSIEncoder(const BarcodeEncoderOptions&);
    std::unique_ptr<IBarcodeEncoder> CreatePharmacodeEncoder(const BarcodeEncoderOptions&);

    std::unique_ptr<IBarcodeEncoder> CreateBarcodeEncoder(BarcodeSymbology symbology,
                                                          const BarcodeEncoderOptions& options) {
        switch (symbology) {
            case BarcodeSymbology::Code39:
                return CreateCode39Encoder(options, false);
            case BarcodeSymbology::Code39Extended:
                return CreateCode39Encoder(options, true);
            case BarcodeSymbology::Code93:
                return CreateCode93Encoder(options);
            case BarcodeSymbology::Code128:
            case BarcodeSymbology::Code128A:
            case BarcodeSymbology::Code128B:
            case BarcodeSymbology::Code128C:
            case BarcodeSymbology::GS1_128:
                return CreateCode128Encoder(options, symbology);
            case BarcodeSymbology::EAN13:
            case BarcodeSymbology::EAN8:
            case BarcodeSymbology::UPCA:
            case BarcodeSymbology::UPCE:
            case BarcodeSymbology::ISBN:
                return CreateEANUPCEncoder(options, symbology);
            case BarcodeSymbology::ITF:
            case BarcodeSymbology::ITF14:
                return CreateITFEncoder(options, symbology);
            case BarcodeSymbology::Standard25:
                return CreateStandard25Encoder(options);
            case BarcodeSymbology::Codabar:
                return CreateCodabarEncoder(options);
            case BarcodeSymbology::MSIPlessey:
                return CreateMSIEncoder(options);
            case BarcodeSymbology::Pharmacode:
                return CreatePharmacodeEncoder(options);
        }
        return nullptr;
    }

    BarcodePattern EncodeBarcode(BarcodeSymbology symbology,
                                  const std::string& data,
                                  const BarcodeEncoderOptions& options) {
        auto encoder = CreateBarcodeEncoder(symbology, options);
        if (!encoder) {
            BarcodePattern bad;
            bad.valid = false;
            bad.errorMessage = "Unknown symbology";
            return bad;
        }
        return encoder->Encode(data);
    }

    const char* GetBarcodeSymbologyName(BarcodeSymbology symbology) {
        switch (symbology) {
            case BarcodeSymbology::Code39:         return "Code 39";
            case BarcodeSymbology::Code39Extended: return "Code 39 Extended";
            case BarcodeSymbology::Code93:         return "Code 93";
            case BarcodeSymbology::Code128:        return "Code 128";
            case BarcodeSymbology::Code128A:       return "Code 128-A";
            case BarcodeSymbology::Code128B:       return "Code 128-B";
            case BarcodeSymbology::Code128C:       return "Code 128-C";
            case BarcodeSymbology::GS1_128:        return "GS1-128";
            case BarcodeSymbology::EAN13:          return "EAN-13";
            case BarcodeSymbology::EAN8:           return "EAN-8";
            case BarcodeSymbology::UPCA:           return "UPC-A";
            case BarcodeSymbology::UPCE:           return "UPC-E";
            case BarcodeSymbology::ISBN:           return "ISBN-13";
            case BarcodeSymbology::ITF:            return "ITF (Interleaved 2 of 5)";
            case BarcodeSymbology::ITF14:          return "ITF-14";
            case BarcodeSymbology::Standard25:     return "Standard 2 of 5";
            case BarcodeSymbology::Codabar:        return "Codabar";
            case BarcodeSymbology::MSIPlessey:     return "MSI Plessey";
            case BarcodeSymbology::Pharmacode:     return "Pharmacode";
        }
        return "Unknown";
    }

} // namespace UltraCanvas

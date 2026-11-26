// Tests/Fuzz/PptReaderFuzz.cpp
// Fuzz target: the PowerPoint 97-2003 reader (UltraCanvasPresentationPpt.h).
// A .ppt is somebody else's file - a mail attachment, a download - and its
// records are lengths and offsets the reader must never trust.
//
// The input is the "PowerPoint Document" stream: the target puts it into a
// compound file of its own (UCCompoundFileWriter), without a Current User
// stream, so the reader finds the newest edit by walking the stream, and
// mutations land on the records rather than on the container. An input that
// is a compound file already is read as one too. The seeds in corpus/ppt are
// the streams of Tests/fixtures/*.ppt and of a small deck written by hand.
//
// Built with the UltraCanvas library, so it runs as a deterministic smoke
// test (FuzzSmokeMain.cpp) from the main build; see Tests/Fuzz/README.md.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasPresentationPpt.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace UltraCanvas;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::vector<uint8_t> input(data, data + size);
    PptxPresentation deck;
    std::string error;
    if (UCCompoundFileReader::HasSignature(input)) {
        ReadPptPresentationFromMemory(input, deck, error, true);
        return 0;
    }
    UCCompoundFileWriter writer;
    writer.AddStream("PowerPoint Document", std::move(input));
    ReadPptPresentationFromMemory(writer.Build(), deck, error, true);
    // What was read is the model's: its outline is made from it as an
    // application would.
    (void)PptxPresentationToRichDocument(deck);
    return 0;
}

// Apps/UltraAIApp/UltraAILocalModelText.h
// The words the local-model pages and dialogs share for one model: its
// size, what it serves, how it fits this computer.
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraAI Module
#pragma once

#include "UltraAILocalRuntime.h"

#include <cstdint>
#include <string>

namespace UltraAIApp {
namespace LocalModelText {

// "2,3 GB", "466 MB", "12 KB"; a dash for 0 (not on record).
std::string Bytes(uint64_t bytes);
// "1.2M", "45k", "812": a download or like count.
std::string Count(uint64_t count);
// "Chat", "Embeddings", "Speech to text" for a capability name.
std::string CapabilityLabel(const std::string& capability);
// "CPU only", "Vulkan (any GPU)", ... for an accelerator id.
std::string AcceleratorLabel(const std::string& id);
// "GPU", "CPU", "Too large", a dash - and the sentence behind each.
std::string FitLabel(UltraAI::ModelFit fit);
std::string FitTooltip(UltraAI::ModelFit fit);

} // namespace LocalModelText
} // namespace UltraAIApp

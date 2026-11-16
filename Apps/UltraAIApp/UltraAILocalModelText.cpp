// Apps/UltraAIApp/UltraAILocalModelText.cpp
// The words for one local model (UltraAILocalModelText.h).
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraAI Module

#include "UltraAILocalModelText.h"

#include "UltraLocalisation/UltraLocalisation.h"

#include <cstdio>

namespace UltraAIApp {
namespace LocalModelText {

using namespace UltraCanvas;
using UltraAI::ModelFit;

std::string Bytes(uint64_t bytes) {
    if (!bytes) return "—";
    char buffer[32];
    if (bytes >= 1024ull * 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));   // locale-ok: shown to the user
    } else if (bytes >= 1024ull * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024));   // locale-ok: shown to the user
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f KB", static_cast<double>(bytes) / 1024.0);   // locale-ok: shown to the user
    }
    return buffer;
}

std::string Count(uint64_t count) {
    char buffer[32];
    if (count >= 1000000) {
        std::snprintf(buffer, sizeof(buffer), "%.1fM", static_cast<double>(count) / 1000000.0);   // locale-ok: shown to the user
    } else if (count >= 1000) {
        std::snprintf(buffer, sizeof(buffer), "%.0fk", static_cast<double>(count) / 1000.0);   // locale-ok: shown to the user
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(count));
    }
    return buffer;
}

std::string CapabilityLabel(const std::string& capability) {
    if (capability == "textllm")      return Tr("Chat");
    if (capability == "embeddings")   return Tr("Embeddings");
    if (capability == "speechtotext") return Tr("Speech to text");
    return capability;
}

std::string AcceleratorLabel(const std::string& id) {
    if (id == "cpu")    return Tr("CPU only");
    if (id == "vulkan") return Tr("Vulkan (any GPU)");
    if (id == "metal")  return Tr("Metal (Apple GPU)");
    if (id == "cuda12") return Tr("CUDA 12 (NVIDIA)");
    if (id == "rocm")   return Tr("ROCm (AMD)");
    return id;
}

std::string FitLabel(ModelFit fit) {
    switch (fit) {
        case ModelFit::Recommended: return "GPU";
        case ModelFit::RunsOnCpu:   return "CPU";
        case ModelFit::TooLarge:    return Tr("Too large");
        case ModelFit::Unknown:     return "—";
    }
    return "—";
}

std::string FitTooltip(ModelFit fit) {
    switch (fit) {
        case ModelFit::Recommended: return Tr("Fits this computer's GPU memory: runs on the GPU");
        case ModelFit::RunsOnCpu:   return Tr("Fits system memory: runs on the CPU, or partly on the GPU");
        case ModelFit::TooLarge:    return Tr("Does not fit this computer's memory");
        case ModelFit::Unknown:     return Tr("No memory requirement on record");
    }
    return {};
}

} // namespace LocalModelText
} // namespace UltraAIApp

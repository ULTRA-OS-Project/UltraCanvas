// UltraAI/runtime/src/LocalRuntimeBuiltinData.cpp
// The gallery index and the model catalogue shipped with the framework, so
// a machine that has never been online knows what exists. RefreshGallery()
// replaces them with the published copies when those verify.
//
// The engine bundles are llama.cpp's own release archives for build b11669
// (https://github.com/ggml-org/llama.cpp/releases/tag/b11669), hashed on
// 2026-10-20. The UltraCanvas-built gallery of the design proposal (§5:
// our names, signed, whisper.cpp and Piper built by our CI) replaces this
// list when it exists; the format does not change.
//
// The catalogue's files are Hugging Face "resolve" URLs. A file whose
// sha256 is empty here is verified against the hash Hugging Face reports
// for it (the LFS oid from the tree API) when it is installed, and the
// hash is recorded in the model's model.json. Pinning the hashes here is
// the curation step the proposal describes (§6.3); sizes are approximate
// until then and are corrected from the host at install time.
// Version: 0.2.0 - the built-in topics
// Version: 0.1.0
// Last Modified: 2026-11-06
// Author: UltraAI Module

#include "LocalRuntimeInternal.h"

namespace UltraAI {
namespace local_detail {

const char* DefaultGalleryUrl() {
    return "https://github.com/ULTRA-OS-Project/UltraCanvas-AI-Engines/releases/latest/download/index.json";
}

const char* BuiltinGalleryJson() {
    return R"json({
  "formatVersion": 1,
  "published": "2026-10-20",
  "engines": [
    {
      "id": "llama-server",
      "version": "b11669",
      "license": "MIT",
      "executable": "llama-server",
      "serves": ["textllm", "embeddings"],
      "defaultArgs": ["--jinja"],
      "bundles": [
        {"platform": "linux-x64",   "accel": "cpu",    "url": "https://github.com/ggml-org/llama.cpp/releases/download/b11669/llama-b11669-bin-ubuntu-x64.tar.gz",        "sha256": "cf07c1c112d709bd67e522017471de57ed0ec8bea031d732c5422351c07f65ac", "size": 17851611},
        {"platform": "linux-x64",   "accel": "vulkan", "url": "https://github.com/ggml-org/llama.cpp/releases/download/b11669/llama-b11669-bin-ubuntu-vulkan-x64.tar.gz", "sha256": "e8709e5ba6c1b093dea7f3de57f4aba842f9a188fba6b465782d65d6fcfa4d9f", "size": 30497889},
        {"platform": "linux-arm64", "accel": "cpu",    "url": "https://github.com/ggml-org/llama.cpp/releases/download/b11669/llama-b11669-bin-ubuntu-arm64.tar.gz",        "sha256": "9ac6fda2058d35677608a06f751a50122ddfa4ce0ddb7f084466b8e79b6d4a99", "size": 13900160},
        {"platform": "linux-arm64", "accel": "vulkan", "url": "https://github.com/ggml-org/llama.cpp/releases/download/b11669/llama-b11669-bin-ubuntu-vulkan-arm64.tar.gz", "sha256": "d283522e479c0d532e3a04d715dd852ffb2d95925caa6d41ef2ba53a65e46a51", "size": 24187561},
        {"platform": "macos-arm64", "accel": "metal",  "url": "https://github.com/ggml-org/llama.cpp/releases/download/b11669/llama-b11669-bin-macos-arm64.tar.gz",        "sha256": "dbed55493d72bc1ba2babc8770d669c2289b5bc4ee9fb2a4dfc158d45b7d13fd", "size": 12191582, "minOs": "13.3"},
        {"platform": "macos-x64",   "accel": "cpu",    "url": "https://github.com/ggml-org/llama.cpp/releases/download/b11669/llama-b11669-bin-macos-x64.tar.gz",          "sha256": "cab28238d78158109b48deb27b05d50e0dcef56275d2e76da9ca8281526c7532", "size": 11677404, "minOs": "13.3"},
        {"platform": "win-x64",     "accel": "cpu",    "url": "https://github.com/ggml-org/llama.cpp/releases/download/b11669/llama-b11669-bin-win-cpu-x64.zip",            "sha256": "37b3efcf962847ca869745fd4ccf7f5e7e8dacfeff9fa3523b4ca8cdc66d632b", "size": 19537886},
        {"platform": "win-x64",     "accel": "vulkan", "url": "https://github.com/ggml-org/llama.cpp/releases/download/b11669/llama-b11669-bin-win-vulkan-x64.zip",         "sha256": "2c07574e7bde71321cb785282b20cc821e951132149eeceeea32ddebdbf0598e", "size": 32182350},
        {"platform": "win-arm64",   "accel": "cpu",    "url": "https://github.com/ggml-org/llama.cpp/releases/download/b11669/llama-b11669-bin-win-cpu-arm64.zip",          "sha256": "9cff39ffed17edcd03a7d876073f74d7b48cd43a1e5eafdd59ee094185ad871b", "size": 12442177},
        {"platform": "win-arm64",   "accel": "vulkan", "url": "https://github.com/ggml-org/llama.cpp/releases/download/b11669/llama-b11669-bin-win-vulkan-arm64.zip",       "sha256": "963d3b185362cadca3de643409f916a5ce95dd0f7fd8617ba9ee5d8833ac1ef3", "size": 24817965}
      ]
    },
    {
      "id": "whisper-server",
      "version": "v1.9.0",
      "license": "MIT",
      "executable": "whisper-server",
      "serves": ["speechtotext"],
      "defaultArgs": [],
      "bundles": [
        {"platform": "linux-x64",   "accel": "cpu", "url": "https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.0/whisper-bin-ubuntu-x64.tar.gz",   "sha256": "478ccaeb4fac4c57aa841763473760d7636e68aabedc9efc853db0b60d3e6150", "size": 13194601},
        {"platform": "linux-arm64", "accel": "cpu", "url": "https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.0/whisper-bin-ubuntu-arm64.tar.gz", "sha256": "4cff5dca013b99573d61fc7b654f376eb87d05a2fdb1ec22bd270cf434ccf1f2", "size": 12641400},
        {"platform": "win-x64",     "accel": "cpu", "url": "https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.0/whisper-bin-x64.zip",            "sha256": "00c4304b6be363a224a4b69829df49009f74131df8c3ce6a5878b89a11cd26ef", "size": 5410599}
      ]
    }
  ]
})json";
}

// The file names and sizes were checked against the Hugging Face repositories
// on 2026-10-21 (sizes to the repositories' figure where it was published, the
// nearest 10 MB otherwise); the hashes are still taken from the tree API at
// install time.
const char* BuiltinCatalogueJson() {
    return R"json({
  "formatVersion": 1,
  "published": "2026-10-21",
  "models": [
    {
      "id": "qwen3-4b-q4",
      "displayName": "Qwen3 4B (Q4_K_M)",
      "description": "A capable general chat model that answers in many languages, follows instructions and calls tools. The recommended first model for a computer with 8 GB or more.",
      "capability": "textllm",
      "engine": "llama-server",
      "license": "Apache-2.0",
      "files": [{"name": "Qwen3-4B-Q4_K_M.gguf", "url": "https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/main/Qwen3-4B-Q4_K_M.gguf", "size": 2497000000}],
      "requires": {"memoryBytes": 4000000000, "gpuMemoryBytes": 3500000000},
      "features": {"contextWindow": 32768, "tools": true},
      "engineArgs": ["--ctx-size", "8192"],
      "tags": ["chat", "tools", "multilingual", "recommended"]
    },
    {
      "id": "qwen3-1.7b-q8",
      "displayName": "Qwen3 1.7B (Q8_0)",
      "description": "A small, fast chat model for a laptop or a computer without a GPU. Good for short answers, summaries and translation of simple text.",
      "capability": "textllm",
      "engine": "llama-server",
      "license": "Apache-2.0",
      "files": [{"name": "Qwen3-1.7B-Q8_0.gguf", "url": "https://huggingface.co/Qwen/Qwen3-1.7B-GGUF/resolve/main/Qwen3-1.7B-Q8_0.gguf", "size": 2170000000}],
      "requires": {"memoryBytes": 3000000000, "gpuMemoryBytes": 2600000000},
      "features": {"contextWindow": 32768, "tools": true},
      "engineArgs": ["--ctx-size", "8192"],
      "tags": ["chat", "small", "multilingual"]
    },
    {
      "id": "qwen3-8b-q4",
      "displayName": "Qwen3 8B (Q4_K_M)",
      "description": "A stronger chat model for a computer with 16 GB of memory or a GPU with 8 GB. Better reasoning and writing than the 4B model.",
      "capability": "textllm",
      "engine": "llama-server",
      "license": "Apache-2.0",
      "files": [{"name": "Qwen3-8B-Q4_K_M.gguf", "url": "https://huggingface.co/Qwen/Qwen3-8B-GGUF/resolve/main/Qwen3-8B-Q4_K_M.gguf", "size": 5027784384}],
      "requires": {"memoryBytes": 7500000000, "gpuMemoryBytes": 6500000000},
      "features": {"contextWindow": 32768, "tools": true},
      "engineArgs": ["--ctx-size", "8192"],
      "tags": ["chat", "tools", "multilingual"]
    },
    {
      "id": "gemma-3-4b-it-q4",
      "displayName": "Gemma 3 4B Instruct (Q4_K_M, vision)",
      "description": "A chat model that also looks at images. Google's Gemma terms apply.",
      "capability": "textllm",
      "engine": "llama-server",
      "license": "Gemma Terms of Use",
      "files": [
        {"name": "gemma-3-4b-it-Q4_K_M.gguf", "url": "https://huggingface.co/ggml-org/gemma-3-4b-it-GGUF/resolve/main/gemma-3-4b-it-Q4_K_M.gguf", "size": 2490000000},
        {"name": "mmproj-model-f16.gguf", "url": "https://huggingface.co/ggml-org/gemma-3-4b-it-GGUF/resolve/main/mmproj-model-f16.gguf", "size": 851000000}
      ],
      "requires": {"memoryBytes": 5000000000, "gpuMemoryBytes": 4500000000},
      "features": {"contextWindow": 32768, "vision": true},
      "engineArgs": ["--ctx-size", "8192"],
      "tags": ["chat", "vision"]
    },
    {
      "id": "qwen3-embedding-0.6b-q8",
      "displayName": "Qwen3 Embedding 0.6B (Q8_0)",
      "description": "Text embeddings for search and similarity, in many languages. 1024 dimensions.",
      "capability": "embeddings",
      "engine": "llama-server",
      "license": "Apache-2.0",
      "files": [{"name": "Qwen3-Embedding-0.6B-Q8_0.gguf", "url": "https://huggingface.co/Qwen/Qwen3-Embedding-0.6B-GGUF/resolve/main/Qwen3-Embedding-0.6B-Q8_0.gguf", "size": 640000000}],
      "requires": {"memoryBytes": 1500000000, "gpuMemoryBytes": 1000000000},
      "features": {"contextWindow": 32768, "embeddingDimensions": 1024},
      "engineArgs": ["--ctx-size", "8192", "--pooling", "last"],
      "tags": ["embeddings", "multilingual", "recommended"]
    },
    {
      "id": "nomic-embed-text-v1.5-q8",
      "displayName": "Nomic Embed Text v1.5 (Q8_0)",
      "description": "A small, fast English embedding model. 768 dimensions, truncatable.",
      "capability": "embeddings",
      "engine": "llama-server",
      "license": "Apache-2.0",
      "files": [{"name": "nomic-embed-text-v1.5.Q8_0.gguf", "url": "https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.Q8_0.gguf", "size": 146000000}],
      "requires": {"memoryBytes": 600000000, "gpuMemoryBytes": 300000000},
      "features": {"contextWindow": 8192, "embeddingDimensions": 768},
      "engineArgs": ["--ctx-size", "8192", "--pooling", "mean"],
      "tags": ["embeddings", "small"]
    },
    {
      "id": "whisper-small",
      "displayName": "Whisper Small",
      "description": "Speech to text in about 100 languages, for recordings and the sound of videos. A good balance of accuracy and speed on a computer without a GPU.",
      "capability": "speechtotext",
      "engine": "whisper-server",
      "license": "MIT",
      "files": [{"name": "ggml-small.bin", "url": "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin", "size": 488636416}],
      "requires": {"memoryBytes": 1000000000},
      "tags": ["speech", "multilingual", "recommended"]
    },
    {
      "id": "whisper-base",
      "displayName": "Whisper Base",
      "description": "Small and fast speech to text in about 100 languages. Less accurate than Whisper Small; for an older or smaller computer.",
      "capability": "speechtotext",
      "engine": "whisper-server",
      "license": "MIT",
      "files": [{"name": "ggml-base.bin", "url": "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin", "size": 148897792}],
      "requires": {"memoryBytes": 500000000},
      "tags": ["speech", "multilingual", "small"]
    },
    {
      "id": "whisper-large-v3-turbo-q5",
      "displayName": "Whisper Large v3 Turbo (Q5_0)",
      "description": "The most accurate local speech to text, in about 100 languages. Slower: best on a computer with 8 cores or more.",
      "capability": "speechtotext",
      "engine": "whisper-server",
      "license": "MIT",
      "files": [{"name": "ggml-large-v3-turbo-q5_0.bin", "url": "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin", "size": 573571072}],
      "requires": {"memoryBytes": 1600000000},
      "tags": ["speech", "multilingual"]
    }
  ]
})json";
}

// The topics shipped with the framework: the same document the model server
// (Server/UltraAIModelServer) is seeded with - its tests compare the two.
const char* BuiltinTopicsJson() {
    return R"json({
  "formatVersion": 1,
  "published": "2026-11-06",
  "topics": [
    {
      "id": "chat",
      "name": "General chat",
      "description": "Conversations, questions and answers, writing help.",
      "capability": "textllm",
      "models": [{"id": "qwen3-4b-q4", "default": true}, {"id": "qwen3-8b-q4"}, {"id": "qwen3-1.7b-q8"}, {"id": "gemma-3-4b-it-q4"}]
    },
    {
      "id": "code",
      "name": "Programming",
      "description": "Writing, explaining and fixing source code.",
      "capability": "textllm",
      "models": [{"id": "qwen3-8b-q4", "default": true}, {"id": "qwen3-4b-q4"}]
    },
    {
      "id": "translation",
      "name": "Translation",
      "description": "Translating text between languages.",
      "capability": "textllm",
      "models": [{"id": "qwen3-4b-q4", "default": true}, {"id": "qwen3-8b-q4"}, {"id": "qwen3-1.7b-q8"}]
    },
    {
      "id": "summary",
      "name": "Summaries",
      "description": "Shortening documents, mails and web pages.",
      "capability": "textllm",
      "models": [{"id": "qwen3-1.7b-q8", "default": true}, {"id": "qwen3-4b-q4"}]
    },
    {
      "id": "vision",
      "name": "Pictures and documents",
      "description": "Describing pictures and reading scanned pages.",
      "capability": "textllm",
      "models": [{"id": "gemma-3-4b-it-q4", "default": true}]
    },
    {
      "id": "search",
      "name": "Search and similarity",
      "description": "Embeddings for finding related text.",
      "capability": "embeddings",
      "models": [{"id": "qwen3-embedding-0.6b-q8", "default": true}, {"id": "nomic-embed-text-v1.5-q8"}]
    },
    {
      "id": "transcription",
      "name": "Transcription",
      "description": "Turning recordings and the sound of videos into text.",
      "capability": "speechtotext",
      "models": [{"id": "whisper-small", "default": true}, {"id": "whisper-base"}, {"id": "whisper-large-v3-turbo-q5"}]
    }
  ]
})json";
}

} // namespace local_detail
} // namespace UltraAI

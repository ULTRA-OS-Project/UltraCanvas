// Plugins/Models/STL/UltraCanvasSTLLoader.h
// Self-contained STL (stereolithography) mesh loader and saver
// Supports both ASCII and binary STL, in both directions (load + save)
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_STL_LOADER_H
#define ULTRACANVAS_STL_LOADER_H

#include "UltraCanvas3DTypes.h"
#include <string>
#include <vector>
#include <cstdint>

namespace UltraCanvas {

// STL binary/ASCII output selection for saving.
    enum class STLFormat {
        Auto,    // Choose based on file: defaults to Binary (compact, lossless)
        ASCII,
        Binary
    };

// Self-contained STL reader/writer. No external dependencies.
// All methods are static; the mesh is the unit of exchange.
    class UltraCanvasSTLLoader {
    public:
        // ----- Loading -----

        // Load an STL file (auto-detects ASCII vs binary). Returns false on error;
        // a human-readable reason is written to outError when non-null.
        static bool Load(const std::string& filePath, Mesh3D& outMesh,
                         std::string* outError = nullptr);

        // Load from an in-memory STL byte buffer (auto-detects ASCII vs binary).
        static bool LoadFromMemory(const std::vector<uint8_t>& data, Mesh3D& outMesh,
                                   std::string* outError = nullptr);

        // ----- Saving -----

        // Save a mesh to an STL file. STLFormat::Auto writes binary STL.
        static bool Save(const std::string& filePath, const Mesh3D& mesh,
                         STLFormat format = STLFormat::Auto,
                         std::string* outError = nullptr);

        // ----- Inspection -----

        // True if the first bytes of the buffer look like binary STL (based on the
        // canonical "84 + 50 * triangleCount == size" heuristic).
        static bool LooksLikeBinary(const std::vector<uint8_t>& data);

        // Lightweight extension check ("stl", with or without a leading dot).
        static bool HasSTLExtension(const std::string& filePath);

    private:
        static bool ParseAscii(const std::string& text, Mesh3D& outMesh, std::string* outError);
        static bool ParseBinary(const std::vector<uint8_t>& data, Mesh3D& outMesh, std::string* outError);
        static bool WriteAscii(const std::string& filePath, const Mesh3D& mesh, std::string* outError);
        static bool WriteBinary(const std::string& filePath, const Mesh3D& mesh, std::string* outError);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_STL_LOADER_H

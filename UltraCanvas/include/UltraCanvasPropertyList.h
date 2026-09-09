// include/UltraCanvasPropertyList.h
// Apple property lists, in both encodings a Mac disk holds them in: the XML
// form and the binary "bplist00" form.
//
// This is deliberately not a general plist library. What the framework needs
// from a plist is a handful of named values out of its top-level dictionary
// — the name, icon and executable of an application bundle, the address in a
// ".webloc" shortcut — so that is what it offers: the top level, flattened
// to text. Nested dictionaries and arrays are skipped rather than
// half-modelled; a caller that needs them should say so and this can grow.
//
// No Apple API: the files are parsed directly, so a Mac disk mounted on
// ULTRA OS, Linux or Windows reads the same as it does on macOS.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace UltraCanvas {

    class UCPropertyList {
    public:
        // Read a property list file. False when it cannot be read or is
        // neither encoding — so a file that merely ends in ".plist" is never
        // mistaken for one.
        static bool Read(const std::string& path, UCPropertyList& out);
        // The same from bytes already in hand (a plist inside a bundle read
        // over the network, or one embedded in another file).
        static bool ReadBytes(const uint8_t* data, size_t size,
                              UCPropertyList& out);

        bool Has(const std::string& key) const;
        // Every scalar of the top-level dictionary, as text: strings as they
        // stand, integers in decimal, booleans as "true" / "false", reals in
        // the shortest form that round-trips, dates and data left as the raw
        // text the file carries.
        std::string GetString(const std::string& key,
                              const std::string& fallback = {}) const;
        bool GetBool(const std::string& key, bool fallback = false) const;
        long long GetInteger(const std::string& key, long long fallback = 0) const;

        const std::map<std::string, std::string>& Values() const { return values; }
        bool Empty() const { return values.empty(); }

    private:
        std::map<std::string, std::string> values;
    };

} // namespace UltraCanvas

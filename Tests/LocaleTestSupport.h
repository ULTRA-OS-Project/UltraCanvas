// Tests/LocaleTestSupport.h
// Running a check under a comma-decimal locale, for the tests that hold a file
// format to its own decimal point.
//
// There are two locale switches and they reach different halves of the defect
// class, which is why both are set together here:
//
//   * the C locale (setlocale) is what strtod, atof, snprintf and
//     std::to_string read. The Linux backend sets it from the environment at
//     start-up, because XIM needs LC_CTYPE for UTF-8 input, so this half is
//     the framework's default state on a localised desktop.
//   * the C++ global locale (std::locale::global) is what a stream copies when
//     it is constructed, and therefore what every `<<` and `>>` renders
//     through. setlocale does NOT set it: a test that moves only the C locale
//     leaves `stream << 1.5` writing `1.5` whatever the writer does, and the
//     whole stream half of the problem stays invisible. A host application
//     sets it with std::locale::global(std::locale("")) when it honours the
//     desktop's language, and it then applies to every writer in the process.
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include <clocale>
#include <exception>
#include <locale>
#include <string>

namespace UltraCanvasTest {

// Both switches, for the length of a scope, and back as they were after.
class InLocale {
public:
    explicit InLocale(const char* name) : saved_(SavedCLocale()) {
        std::setlocale(LC_ALL, name);
        try {
            std::locale::global(std::locale(name));
            global_ = true;
        } catch (const std::exception&) {
            global_ = false;   // the C half alone; the stream half is untested
        }
    }
    ~InLocale() {
        if (global_) std::locale::global(savedGlobal_);
        std::setlocale(LC_ALL, saved_.c_str());
    }
    // False where the name is a C locale the machine has but not a C++ one.
    // A check that is only about streams should report a skip rather than
    // pass, because without this half it would pass on any writer at all.
    bool HasGlobal() const { return global_; }

    InLocale(const InLocale&) = delete;
    InLocale& operator=(const InLocale&) = delete;

private:
    static std::string SavedCLocale() {
        const char* current = std::setlocale(LC_ALL, nullptr);
        return current ? current : "C";
    }

    std::string saved_;
    std::locale savedGlobal_;
    bool global_ = false;
};

// A locale installed on this machine whose decimal point is a comma, or
// nullptr where there is none - a bare CI runner often has only "C", and a
// test that finds none must report a skip rather than pass silently.
inline const char* CommaDecimalLocale() {
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8",
                             "fr_FR.utf8", "German_Germany.1252", "German"}) {
        if (std::setlocale(LC_ALL, name)) {
            const bool comma = std::localeconv()->decimal_point[0] == ',';
            std::setlocale(LC_ALL, "C");
            if (comma) return name;
        }
    }
    std::setlocale(LC_ALL, "C");
    return nullptr;
}

} // namespace UltraCanvasTest

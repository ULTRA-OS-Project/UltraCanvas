# scripts/macos-min-version.sh - sourced, not run.
#
# Reading the oldest macOS a Mach-O binary runs on, shared by package-macos.sh
# (which checks every binary it bundles) and scripts/homebrew-rebuild-newer-kegs.sh
# (which checks the Homebrew libraries before the build uses them). Written for
# the Bash 3.2 that macOS ships as /bin/bash.

# The minimum macOS the Mach-O file $1 declares (LC_BUILD_VERSION's minos;
# LC_VERSION_MIN_MACOSX in old binaries), or nothing. awk reads to the end
# rather than exiting at the match: an early exit can kill otool with SIGPIPE
# mid-output, which pipefail and set -e turn into the end of the run.
macho_min_macos() {
    { otool -l "$1" 2>/dev/null || true; } | awk '
        found                       { next }
        /cmd LC_BUILD_VERSION/      { build = 1; next }
        /cmd LC_VERSION_MIN_MACOSX/ { legacy = 1; next }
        build  && $1 == "minos"     { print $2; found = 1 }
        legacy && $1 == "version"   { print $2; found = 1 }
    '
}

# True when the dotted version $1 is newer than $2 (15.1 > 15, 26.0 > 15.6).
version_newer() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        na = split(a, x, "."); nb = split(b, y, ".")
        n = (na > nb) ? na : nb
        for (i = 1; i <= n; i++) {
            xi = (i <= na) ? x[i] + 0 : 0
            yi = (i <= nb) ? y[i] + 0 : 0
            if (xi > yi) exit 0
            if (xi < yi) exit 1
        }
        exit 1
    }'
}

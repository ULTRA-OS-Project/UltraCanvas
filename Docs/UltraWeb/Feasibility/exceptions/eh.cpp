// C++ exceptions in a wasm32-wasip1 guest: a library throw (std::stoi) and
// a throw of our own, both caught. Built twice by setup.sh, once per
// exception encoding, and run on wasmtime and WAMR by run.sh.
#include <cstdio>
#include <stdexcept>
#include <string>
int parse(const std::string& s) { return std::stoi(s); }   // throws std::invalid_argument
int main() {
    try { parse("not a number"); std::puts("no throw?"); }
    catch (const std::invalid_argument& e) { std::printf("caught: %s\n", e.what()); }
    try { throw std::runtime_error("custom"); } catch (const std::exception& e) { std::printf("caught: %s\n", e.what()); }
    return 0;
}

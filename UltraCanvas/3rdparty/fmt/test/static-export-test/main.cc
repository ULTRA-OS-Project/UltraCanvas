#include <iostream>
#include <string>

extern std::string foo();

int main() { std::cerr << foo() << std::endl; }

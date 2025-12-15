#include <iostream>
#include <isobus/isobus.hpp>

int main() {
    isobus::Isobus bus;
    std::cout << "isobus version: " << bus.version() << std::endl;
    return 0;
}

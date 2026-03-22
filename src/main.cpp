#include <iostream>
#include "AeroBoar.h"

int main() {
    try {
        AeroBoar aero_boar;
        return aero_boar.fly();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return -1;
    }
    return 0;
}
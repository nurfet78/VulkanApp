// main.cpp
#include "meadow_app.h"
#include <iostream>

int main() {
    try {
        MeadowApp app;
        app.Run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
}
#include "config/config.h"

#include <iostream>

int main() {
    if (Config::Default::RECOGNITION_THRESHOLD <= 0.0f ||
        Config::Default::RECOGNITION_THRESHOLD > 1.0f) {
        std::cerr << "Invalid recognition threshold: "
                  << Config::Default::RECOGNITION_THRESHOLD << std::endl;
        return 1;
    }

    if (Config::Camera::WIDTH <= 0 || Config::Camera::HEIGHT <= 0) {
        std::cerr << "Invalid camera resolution: "
                  << Config::Camera::WIDTH << "x" << Config::Camera::HEIGHT << std::endl;
        return 1;
    }

    if (Config::Default::DUPLICATE_CHECK_INTERVAL < 0) {
        std::cerr << "Invalid duplicate check interval: "
                  << Config::Default::DUPLICATE_CHECK_INTERVAL << std::endl;
        return 1;
    }

    std::cout << "Config smoke test passed" << std::endl;
    return 0;
}

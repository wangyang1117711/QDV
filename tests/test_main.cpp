#include "catch2/catch2_minimal.hpp"
#include <iostream>

int main() {
    std::cout << "\n===== QDV Unit Tests =====\n" << std::endl;

    int total = 0;
    for (const auto& r : testResults()) {
        total++;
        if (r.passed) {
            std::cout << "  [PASS] " << r.testName << std::endl;
            passedCount()++;
        } else {
            std::cout << "  [FAIL] " << r.testName << std::endl;
            if (!r.message.empty()) {
                std::cout << "         " << r.message << std::endl;
            }
            failedCount()++;
        }
    }

    std::cout << "\n----------------------------" << std::endl;
    std::cout << "Total: " << total
              << " | Passed: " << passedCount()
              << " | Failed: " << failedCount() << std::endl;

    return failedCount() > 0 ? 1 : 0;
}
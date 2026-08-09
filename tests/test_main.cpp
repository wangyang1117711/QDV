#include "catch2/catch2_minimal.hpp"
#include <iostream>
#include <QApplication>
#include <QTimer>
#include <exception>

int main(int argc, char* argv[]) {
    // 创建 QApplication（UI 测试需要 QApplication 而非 QCoreApplication）
    // 全局唯一实例，各测试文件中的 ensureApp() 通过 qApp 复用此实例
    QApplication app(argc, argv);

    std::cout << "\n===== QDV Unit Tests =====\n" << std::endl;

    int total = 0;
    int passed = 0;
    int failed = 0;

    for (auto& r : testResults()) {
        total++;
        // 每次执行前重置状态
        r.passed = true;
        r.message.clear();

        // 真正执行测试函数
        if (r.testFunc) {
            std::cout << "  [RUN] " << r.testName << " ..." << std::endl << std::flush;
            try {
                r.testFunc();
            } catch (const std::exception& e) {
                r.passed = false;
                r.message = std::string("未捕获异常: ") + e.what();
            } catch (...) {
                r.passed = false;
                r.message = "未捕获的未知异常";
            }
            // 处理待处理事件，防止事件累积
            QCoreApplication::processEvents();
        }

        if (r.passed) {
            std::cout << "  [PASS] " << r.testName << std::endl;
            passed++;
        } else {
            std::cout << "  [FAIL] " << r.testName << std::endl;
            if (!r.message.empty()) {
                std::cout << "         " << r.message << std::endl;
            }
            failed++;
        }
    }

    std::cout << "\n----------------------------" << std::endl;
    std::cout << "Total: " << total
              << " | Passed: " << passed
              << " | Failed: " << failed << std::endl;

    return failed > 0 ? 1 : 0;
}

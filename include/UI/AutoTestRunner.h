#ifndef AUTOTESTRUNNER_H
#define AUTOTESTRUNNER_H

#include <QString>
#include <QStringList>

class QApplication;

/// 端到端自动化测试工具（独立于 main.cpp 生产逻辑）
/// 通过命令行参数触发：--auto-test <png> [--add-op <type>...]
class AutoTestRunner {
public:
    /// 运行自动化测试并返回退出码。
    /// 返回 0 表示成功，非零表示失败。
    /// 返回 -1 表示没有触发测试（autoTestPng 为空）。
    static int run(QApplication& app,
                   const QString& autoTestPng,
                   const QStringList& addOps);
};

#endif // AUTOTESTRUNNER_H
#ifndef QDV_PATH_VALIDATOR_H
#define QDV_PATH_VALIDATOR_H

// =====================================================================
// S6 修复（安全要求）：路径校验工具类
// ---------------------------------------------------------------------
// 评估报告指出多个用户可控路径输入点（方案文件加载、图片导入、DLL 加载、
// 导出路径等）未做 cleanPath 清理与白名单校验，存在路径穿越风险
// （如 ../../etc/passwd）。
//
// 本工具类提供统一的路径校验接口，供 PluginManager / SchemeView /
// CameraView / main.cpp 等路径处理点调用。
//
// 设计原则：
//   1. sanitize() 仅做语法层清理（cleanPath + 绝对化），不访问文件系统
//   2. isWithinAllowedDir() 做语义层校验，确保清理后的路径落在允许的根目录内
//   3. 所有比较使用规范化路径（canonicalPath 优先，不存在时用 absoluteFilePath）
//   4. 边界匹配：/allowed/root 匹配 /allowed/root/file，但不匹配 /allowed/root-evil/file
// =====================================================================

#include <QString>
#include <QStringList>
#include <QDir>
#include <QFileInfo>

namespace QDV {

class PathValidator {
public:
    /// 清理路径：移除 "."/".."、重复分隔符，转为绝对路径。
    /// 空路径或包含 null 字节的路径返回空串（拒绝）。
    /// 不访问文件系统，仅做字符串层规范化。
    static QString sanitize(const QString& path) {
        if (path.isEmpty()) {
            return QString();
        }
        // 拒绝包含 null 字节的路径（防止截断攻击）
        if (path.contains(QChar::Null)) {
            return QString();
        }
        // QDir::cleanPath 解析 "." / ".." / 重复分隔符 / 尾部分隔符
        QString cleaned = QDir::cleanPath(path);
        // 转为绝对路径（基于进程 CWD），便于后续白名单比对
        return QFileInfo(cleaned).absoluteFilePath();
    }

    /// 检查路径是否在允许的根目录内（防止路径穿越）。
    /// 比较前会对 path 和 allowedRoots 都做 sanitize。
    /// 若文件/目录已存在，优先使用 canonicalPath 以解析符号链接。
    /// 匹配规则：路径等于某根目录，或以 "<root>/" 开头（边界匹配）。
    static bool isWithinAllowedDir(const QString& path, const QStringList& allowedRoots) {
        if (path.isEmpty() || allowedRoots.isEmpty()) {
            return false;
        }

        // 对待校验路径做规范化：存在则用 canonicalPath（解析符号链接），否则用 absoluteFilePath
        QFileInfo pathInfo(path);
        QString normalizedPath = pathInfo.canonicalFilePath();
        if (normalizedPath.isEmpty()) {
            // 文件/目录不存在（如保存新文件场景），退回 sanitize 结果
            normalizedPath = sanitize(path);
            if (normalizedPath.isEmpty()) {
                return false;
            }
        }
        normalizedPath = QDir::cleanPath(normalizedPath);

        for (const QString& root : allowedRoots) {
            if (root.isEmpty()) {
                continue;
            }
            // 规范化白名单根目录
            QFileInfo rootInfo(root);
            QString normalizedRoot = rootInfo.canonicalFilePath();
            if (normalizedRoot.isEmpty()) {
                normalizedRoot = sanitize(root);
                if (normalizedRoot.isEmpty()) {
                    continue;
                }
            }
            normalizedRoot = QDir::cleanPath(normalizedRoot);

            // 精确匹配（路径本身就是根目录）
            if (normalizedPath == normalizedRoot) {
                return true;
            }
            // 前缀匹配：必须以 "<root>/" 开头，防止 /allowed 匹配 /allowed-evil
            if (normalizedPath.startsWith(normalizedRoot + QLatin1Char('/'))) {
                return true;
            }
        }
        return false;
    }

    /// 便捷组合：先 sanitize 再校验白名单。
    /// 返回 sanitize 后的路径（校验通过）或空串（校验失败）。
    /// 调用方可据此判断是否继续处理路径。
    static QString sanitizeAndVerify(const QString& path, const QStringList& allowedRoots) {
        QString cleaned = sanitize(path);
        if (cleaned.isEmpty()) {
            return QString();
        }
        if (!isWithinAllowedDir(cleaned, allowedRoots)) {
            return QString();
        }
        return cleaned;
    }
};

} // namespace QDV

#endif // QDV_PATH_VALIDATOR_H

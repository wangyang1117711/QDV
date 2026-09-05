// =====================================================================
// test_annotation_session_undo.cpp — 撤销/重做 + 脏状态 单元测试
//
// 覆盖（对齐 tasks.md Task 4 验证）：
//   1. 增标注 → 撤销 → 重做（几何与坐标完整恢复）
//   2. 改标注 → 撤销（恢复旧值）
//   3. 删标注 → 撤销（恢复完整列表与类别）
//   4. 删标签 → 撤销（恢复标签列表与标注类别映射）
//   5. 脏状态生命周期：变更置脏、保存清零、打开工程清零
//
// 运行：cmake --build build -j 8 后执行 build/test_annotation_session_undo.exe
// =====================================================================
#include "AnnotationSession.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

static int g_pass = 0;
static int g_fail = 0;
#define CHECK(cond) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; qWarning().noquote() << "  FAIL:" << #cond << "@ line" << __LINE__; } \
} while (0)

// 构造最小 qdvann 工程：1 图 + 2 标签（cat=0 / dog=1）
static QString makeProject(const QString& path)
{
    QJsonObject root;
    root["version"] = "1.0";
    root["app"] = "QDV Annotator";
    root["taskType"] = "detection";
    root["labels"] = QJsonArray({ "cat", "dog" });
    root["labelColors"] = QJsonArray({ "#FFB74D", "#7986CB" });

    QJsonObject img;
    img["id"] = "img1";
    img["path"] = "dummy1.png";
    img["fileName"] = "dummy1.png";
    img["width"] = 100;
    img["height"] = 100;
    img["subset"] = "unassigned";
    img["annotations"] = QJsonArray();
    img["classLabels"] = QJsonArray();
    QJsonArray imgs; imgs.append(img);
    root["images"] = imgs;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return path;
}

static QVariantMap rectAnn(int labelId, int x, int y, int w, int h)
{
    QVariantMap a;
    a["shape"] = "rect";
    a["labelId"] = labelId;
    a["x"] = x; a["y"] = y; a["w"] = w; a["h"] = h;
    return a;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;

    // ---- 用例 1：增 → 撤销 → 重做 ----
    {
        QString proj = makeProject(tmp.filePath("t1.qdvann"));
        AnnotationSession s;
        CHECK(s.openProject(proj));
        CHECK(!s.dirty());            // 打开工程后不脏
        CHECK(!s.canUndo());

        s.addAnnotation(rectAnn(0, 1, 2, 3, 4));
        CHECK(s.canUndo());
        CHECK(!s.canRedo());
        CHECK(s.dirty());
        CHECK(s.currentAnnotations().size() == 1);

        s.undo();
        CHECK(s.currentAnnotations().size() == 0);
        CHECK(s.canRedo());
        CHECK(s.dirty());             // undo 后相对磁盘仍是“已更改”

        s.redo();
        CHECK(s.currentAnnotations().size() == 1);
        const QVariantMap a = s.currentAnnotations().at(0).toMap();
        CHECK(a.value("x").toInt() == 1 && a.value("y").toInt() == 2
              && a.value("w").toInt() == 3 && a.value("h").toInt() == 4
              && a.value("labelId").toInt() == 0);
    }

    // ---- 用例 2：改 → 撤销 ----
    {
        QString proj = makeProject(tmp.filePath("t2.qdvann"));
        AnnotationSession s;
        CHECK(s.openProject(proj));
        s.addAnnotation(rectAnn(0, 10, 10, 20, 20));
        QVariantMap a = s.currentAnnotations().at(0).toMap();
        a["x"] = 99;
        s.updateAnnotation(0, a);
        CHECK(s.currentAnnotations().at(0).toMap().value("x").toInt() == 99);
        s.undo();
        CHECK(s.currentAnnotations().at(0).toMap().value("x").toInt() == 10);
        CHECK(s.currentAnnotations().at(0).toMap().value("y").toInt() == 10);
    }

    // ---- 用例 3：删 → 撤销 ----
    {
        QString proj = makeProject(tmp.filePath("t3.qdvann"));
        AnnotationSession s;
        CHECK(s.openProject(proj));
        s.addAnnotation(rectAnn(0, 5, 5, 5, 5));
        s.addAnnotation(rectAnn(1, 8, 8, 8, 8));
        CHECK(s.currentAnnotations().size() == 2);
        s.removeAnnotation(0);
        CHECK(s.currentAnnotations().size() == 1);
        s.undo();
        CHECK(s.currentAnnotations().size() == 2);
        CHECK(s.currentAnnotations().at(0).toMap().value("labelId").toInt() == 0);
        CHECK(s.currentAnnotations().at(1).toMap().value("labelId").toInt() == 1);
    }

    // ---- 用例 4：删标签 → 撤销恢复标签列表与标注映射 ----
    {
        QString proj = makeProject(tmp.filePath("t4.qdvann"));
        AnnotationSession s;
        CHECK(s.openProject(proj));
        s.addAnnotation(rectAnn(0, 1, 1, 2, 2));   // cat(0)
        s.addAnnotation(rectAnn(1, 3, 3, 2, 2));   // dog(1)
        s.removeLabel(0);                          // 删 cat：标注0→-1，标注1 labelId 1→0
        CHECK(s.labels().size() == 1);
        CHECK(s.currentAnnotations().at(0).toMap().value("labelId").toInt() == -1);
        CHECK(s.currentAnnotations().at(1).toMap().value("labelId").toInt() == 0);
        s.undo();                                  // 恢复 cat/dog 及映射
        CHECK(s.labels().size() == 2);
        CHECK(s.labels().at(0) == "cat" && s.labels().at(1) == "dog");
        CHECK(s.currentAnnotations().at(0).toMap().value("labelId").toInt() == 0);
        CHECK(s.currentAnnotations().at(1).toMap().value("labelId").toInt() == 1);
    }

    // ---- 用例 5：脏状态生命周期（变更置脏 / 保存清零 / 重开清零）----
    {
        QString proj = makeProject(tmp.filePath("t5.qdvann"));
        AnnotationSession s;
        CHECK(s.openProject(proj));
        s.addAnnotation(rectAnn(0, 1, 1, 1, 1));
        CHECK(s.dirty());
        CHECK(s.saveProject(tmp.filePath("t5_saved.qdvann")));
        CHECK(!s.dirty());            // 保存成功后清零
        CHECK(s.canUndo());           // 同会话内撤销栈保留
        CHECK(s.openProject(proj));   // 重新打开工程
        CHECK(!s.dirty());
        CHECK(!s.canUndo());          // 撤销历史清空
        CHECK(!s.canRedo());
    }

    qInfo().noquote() << QString("==== test_annotation_session_undo: %1 passed, %2 failed ====")
                             .arg(g_pass).arg(g_fail);
    return g_fail == 0 ? 0 : 1;
}

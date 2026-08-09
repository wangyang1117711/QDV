#include "../catch2/catch2_minimal.hpp"
#include "TrainingInference/TrainingProject.h"
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

TEST_CASE("TrainingProject fromJson only keeps core data", "[trainingproject:filter]")
{
    TrainingProject project;

    // 设置完整项目数据（包含应被过滤的非核心信息）
    project.setName("测试项目");
    project.setDescription("这是一个测试项目");

    QList<ImageEntrySnapshot> images;
    ImageEntrySnapshot img1;
    img1.filePath = "/path/to/image1.png";
    img1.originalPath = "/path/to/image1.png";
    img1.fileName = "image1.png";
    img1.width = 1920;
    img1.height = 1080;
    img1.channels = 3;
    img1.format = "PNG";
    img1.fileSize = 1234567;
    img1.isAnnotated = true;
    img1.label = "类别A";
    img1.isSelected = false;
    images.append(img1);
    project.setImagesSnapshot(images);

    QJsonObject categories;
    categories["version"] = "1.0";
    QJsonArray catArr;
    QJsonObject cat1;
    cat1["id"] = "cat_1_abc12345";
    cat1["name"] = "类别A";
    cat1["color"] = "#FF5733";
    catArr.append(cat1);
    categories["categories"] = catArr;
    categories["nextId"] = 2;
    project.setCategoriesSnapshot(categories);

    TrainingParamsSnapshot params;
    params.modelType = "resnet50";
    params.numEpochs = 30;
    params.batchSize = 16;
    params.learningRate = 0.0005;
    params.valSplit = 0.15;

    TrainingStateSnapshot state;
    state.hasTrained = true;
    state.onnxPath = "models/test.onnx";
    state.lastMetrics = QVariantMap{{"accuracy", 0.99}};

    project.setTrainingSnapshot(params, state);
    project.appendHistory("save", "保存到 test.qdvproj");

    // 序列化（toJson 仍保留完整信息）
    QJsonObject json = project.toJson();

    // 反序列化（fromJson 仅保留核心信息）
    TrainingProject project2;
    QString errMsg;
    bool ok = project2.fromJson(json, &errMsg);

    REQUIRE(ok);

    // 核心信息 a：图像列表应完整保留
    auto images2 = project2.imagesSnapshot();
    REQUIRE_EQUAL(images2.size(), 1);
    CHECK(images2[0].filePath == "/path/to/image1.png");
    CHECK(images2[0].width == 1920);
    CHECK(images2[0].label == "类别A");

    // 核心信息 b：类别管理应完整保留
    auto cats2 = project2.categoriesSnapshot();
    CHECK(cats2["categories"].toArray().size() == 1);

    // 核心信息 c：模型选择/训练配置参数应保留
    auto params2 = project2.trainingParams();
    CHECK(params2.modelType == "resnet50");
    CHECK(params2.numEpochs == 30);
    CHECK(params2.batchSize == 16);
    CHECK(params2.learningRate == 0.0005);

    // 非核心信息：项目名称保留，描述不保留
    CHECK(project2.name() == "测试项目");
    CHECK(project2.description().isEmpty());

    // 非核心信息：训练历史状态应被重置
    auto state2 = project2.trainingState();
    CHECK(state2.hasTrained == false);
    CHECK(state2.onnxPath.isEmpty());
    CHECK(state2.lastMetrics.isEmpty());

    // 非核心信息：修改历史应清空
    CHECK(project2.historyJson().isEmpty());
}

TEST_CASE("TrainingProject version validation", "[trainingproject:version]")
{
    TrainingProject project;
    
    QJsonObject json;
    json["format"] = "qdv-training-project";
    json["version"] = "2.0";  // 不兼容的版本
    json["project"] = QJsonObject{{"name", "test"}};
    
    QString errMsg;
    bool ok = project.fromJson(json, &errMsg);
    
    REQUIRE(!ok);
    CHECK(errMsg.contains("版本不兼容"));
}

TEST_CASE("TrainingProject format validation", "[trainingproject:format]")
{
    TrainingProject project;
    
    QJsonObject json;
    json["format"] = "wrong-format";
    json["version"] = "1.0";
    
    QString errMsg;
    bool ok = project.fromJson(json, &errMsg);
    
    REQUIRE(!ok);
    CHECK(errMsg.contains("无效的项目文件格式"));
}

TEST_CASE("TrainingProject history limit", "[trainingproject:history]")
{
    TrainingProject project;
    
    // 添加超过上限的历史记录
    for (int i = 0; i < 60; ++i) {
        project.appendHistory("save", QString("save #%1").arg(i));
    }
    
    QJsonArray history = project.historyJson();
    
    // 验证历史记录不超过上限（MAX_HISTORY_ENTRIES = 50）
    CHECK(history.size() <= 50);
    CHECK(history.size() == 50);
}

TEST_CASE("TrainingProject reset", "[trainingproject:reset]")
{
    TrainingProject project;
    
    project.setName("测试");
    project.setDescription("描述");
    project.markDirty();
    
    QList<ImageEntrySnapshot> images;
    ImageEntrySnapshot img;
    img.filePath = "/test.png";
    images.append(img);
    project.setImagesSnapshot(images);
    
    project.reset();
    
    CHECK(project.name().isEmpty());
    CHECK(project.description().isEmpty());
    CHECK(!project.isDirty());
    CHECK(project.imagesSnapshot().isEmpty());
}

TEST_CASE("TrainingProject dirty flag", "[trainingproject:dirty]")
{
    TrainingProject project;
    
    CHECK(!project.isDirty());
    
    project.markDirty();
    CHECK(project.isDirty());
    
    project.markClean();
    CHECK(!project.isDirty());
}

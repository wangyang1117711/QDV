#include "../catch2/catch2_minimal.hpp"
#include "TrainingInference/ImagePreprocessor.h"
#include "TrainingInference/PreprocessDialog.h"
#include <QImage>
#include <QColor>

static QImage createTestImage(int w, int h, QColor color) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(color);
    return img;
}

TEST_CASE("ImagePreprocessor singleton", "[preprocess]") {
    auto* p1 = ImagePreprocessor::instance();
    auto* p2 = ImagePreprocessor::instance();
    REQUIRE(p1 != nullptr);
    REQUIRE(p1 == p2);
}

TEST_CASE("ImagePreprocessor process default params identity", "[preprocess]") {
    auto* preprocessor = ImagePreprocessor::instance();
    QImage input = createTestImage(64, 64, QColor(128, 128, 128));
    PreprocessParams params;
    QImage output = preprocessor->process(input, params);
    REQUIRE_EQUAL(output.width(), 64);
    REQUIRE_EQUAL(output.height(), 64);
    QColor c = output.pixelColor(32, 32);
    CHECK(c.red() >= 120);
    CHECK(c.red() <= 140);
}

TEST_CASE("ImagePreprocessor process brightness +100", "[preprocess]") {
    auto* preprocessor = ImagePreprocessor::instance();
    QImage input = createTestImage(64, 64, QColor(100, 100, 100));
    PreprocessParams params;
    params.brightness = 100;
    QImage output = preprocessor->process(input, params);
    REQUIRE_EQUAL(output.width(), 64);
    QColor c = output.pixelColor(32, 32);
    CHECK(c.red() >= 190);
}

TEST_CASE("ImagePreprocessor process brightness -100", "[preprocess]") {
    auto* preprocessor = ImagePreprocessor::instance();
    QImage input = createTestImage(64, 64, QColor(200, 200, 200));
    PreprocessParams params;
    params.brightness = -100;
    QImage output = preprocessor->process(input, params);
    QColor c = output.pixelColor(32, 32);
    CHECK(c.red() <= 110);
}

TEST_CASE("ImagePreprocessor process contrast 2.0", "[preprocess]") {
    auto* preprocessor = ImagePreprocessor::instance();
    QImage input = createTestImage(64, 64, QColor(180, 100, 50));
    PreprocessParams params;
    params.contrast = 2.0;
    QImage output = preprocessor->process(input, params);
    REQUIRE_EQUAL(output.width(), 64);
    QColor c = output.pixelColor(32, 32);
    CHECK(c.red() >= 230);
}

TEST_CASE("ImagePreprocessor process contrast 0.5", "[preprocess]") {
    auto* preprocessor = ImagePreprocessor::instance();
    QImage input = createTestImage(64, 64, QColor(200, 50, 50));
    PreprocessParams params;
    params.contrast = 0.5;
    QImage output = preprocessor->process(input, params);
    QColor c = output.pixelColor(32, 32);
    CHECK(c.red() <= 170);
}

TEST_CASE("ImagePreprocessor process with empty QImage", "[preprocess]") {
    auto* preprocessor = ImagePreprocessor::instance();
    QImage empty;
    PreprocessParams params;
    QImage output = preprocessor->process(empty, params);
    REQUIRE(output.isNull());
}

TEST_CASE("ImagePreprocessor process gamma 0.5", "[preprocess]") {
    auto* preprocessor = ImagePreprocessor::instance();
    QImage input = createTestImage(64, 64, QColor(160, 160, 160));
    PreprocessParams params;
    params.gamma = 0.5;
    QImage output = preprocessor->process(input, params);
    QColor c = output.pixelColor(32, 32);
    CHECK(c.red() >= 60);
}

TEST_CASE("ImagePreprocessor process gamma 2.0", "[preprocess]") {
    auto* preprocessor = ImagePreprocessor::instance();
    QImage input = createTestImage(64, 64, QColor(160, 160, 160));
    PreprocessParams params;
    params.gamma = 2.0;
    QImage output = preprocessor->process(input, params);
    QColor c = output.pixelColor(32, 32);
    CHECK(c.red() <= 100);
}

TEST_CASE("ImagePreprocessor process sharpness 0.0 no op", "[preprocess]") {
    auto* preprocessor = ImagePreprocessor::instance();
    QImage input = createTestImage(64, 64, QColor(128, 128, 128));
    PreprocessParams params;
    params.sharpness = 0.0;
    QImage output = preprocessor->process(input, params);
    REQUIRE_EQUAL(output.width(), 64);
    QColor c = output.pixelColor(32, 32);
    CHECK(c.red() >= 120);
}

TEST_CASE("ImagePreprocessor process sharpness 2.0", "[preprocess]") {
    auto* preprocessor = ImagePreprocessor::instance();
    QImage input = createTestImage(64, 64, QColor(128, 128, 128));
    PreprocessParams params;
    params.sharpness = 2.0;
    QImage output = preprocessor->process(input, params);
    REQUIRE_EQUAL(output.width(), 64);
}
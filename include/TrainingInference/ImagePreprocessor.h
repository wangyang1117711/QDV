#pragma once

#include <QImage>

struct PreprocessParams;

class ImagePreprocessor {
public:
    static ImagePreprocessor* instance();

    QImage process(const QImage& source, const PreprocessParams& params) const;

private:
    ImagePreprocessor() = default;
    static ImagePreprocessor* s_instance;
};
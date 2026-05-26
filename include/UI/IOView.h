#ifndef IO_VIEW_H
#define IO_VIEW_H

#include <QWidget>

class IOView : public QWidget {
    Q_OBJECT
    
public:
    explicit IOView(QWidget* parent = nullptr);
    ~IOView();
    
private:
    void setupUI();
};

#endif // IO_VIEW_H
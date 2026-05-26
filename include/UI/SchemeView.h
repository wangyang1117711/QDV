#ifndef SCHEME_VIEW_H
#define SCHEME_VIEW_H

#include <QWidget>
#include <QMap>

class QTreeWidget;

struct SchemeData {
    QString name;
    QString version;
    QString author;
    QString description;
    QList<QStringList> tools;
};

class SchemeView : public QWidget {
    Q_OBJECT

public:
    explicit SchemeView(QWidget* parent = nullptr);
    ~SchemeView();

signals:
    void schemeCountChanged(int count);

private slots:
    void onNewScheme();
    void onSaveScheme();
    void onLoadScheme();

private:
    void setupUI();
    void saveSchemesToFile(const QString& filePath);
    void loadSchemesFromFile(const QString& filePath);
    void refreshSchemeTree();

    QMap<QString, SchemeData> m_schemeMap;
    QTreeWidget* m_schemeTree;
};

#endif // SCHEME_VIEW_H
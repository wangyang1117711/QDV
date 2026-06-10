#include <QCoreApplication>
#include <QSettings>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QMessageAuthenticationCode>
#include <QDebug>

QByteArray generateSalt() {
    QByteArray salt(16, '\0');
    for (int i = 0; i < 16; ++i) {
        salt[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return salt;
}

QString hashPassword(const QString& password, const QByteArray& salt) {
    const int iterations = 100000;
    QByteArray derived = QMessageAuthenticationCode::hash(
        password.toUtf8(), salt, QCryptographicHash::Sha256
    );
    for (int i = 1; i < iterations; ++i) {
        derived = QMessageAuthenticationCode::hash(
            derived, salt, QCryptographicHash::Sha256
        );
    }
    return QString::fromLatin1(salt.toHex() + ":" + derived.toHex());
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("奇测科技");
    QCoreApplication::setApplicationName("QDetectVision");

    QSettings settings;

    // Clear old users
    settings.remove("users");
    
    // Create qc user
    QByteArray salt = generateSalt();
    QString hash = hashPassword("qc", salt);
    
    settings.beginWriteArray("users", 1);
    settings.setArrayIndex(0);
    settings.setValue("name", "qc");
    settings.setValue("hash", hash);
    settings.endArray();
    
    settings.setValue("setup/initialized", true);
    settings.sync();
    
    // Verify
    int size = settings.beginReadArray("users");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        qDebug() << "User:" << settings.value("name").toString() 
                 << "Hash:" << settings.value("hash").toString();
    }
    settings.endArray();
    
    qDebug() << "User qc created successfully";
    return 0;
}
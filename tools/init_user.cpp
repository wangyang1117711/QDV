#include <QCoreApplication>
#include "Core/AuthService.h"
#include "Core/Logger.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    AuthService* auth = AuthService::instance();
    
    if (auth->isFirstRun()) {
        qDebug() << "Creating admin user...";
        auth->createUser("admin", "Admin@123", true);
        qDebug() << "User 'admin' created";
    } else {
        qDebug() << "Setup already completed";
        if (auth->login("admin", "Admin@123")) {
            qDebug() << "Login successful!";
            QString token = auth->registerToken("admin");
            qDebug() << "Token registered:" << token;
        } else {
            qDebug() << "Login failed!";
        }
    }
    
    return 0;
}
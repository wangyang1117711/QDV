#ifndef LOGIN_VIEW_H
#define LOGIN_VIEW_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>

class LoginView : public QWidget {
    Q_OBJECT
    
public:
    explicit LoginView(QWidget* parent = nullptr);
    ~LoginView();
    
signals:
    void loginSuccess(const QString& username);
    void loginFailed(const QString& error);
    
private slots:
    void onLoginClicked();
    
private:
    QLineEdit* m_usernameEdit;
    QLineEdit* m_passwordEdit;
    QPushButton* m_loginButton;
    QCheckBox* m_rememberCheckBox;
    QLabel* m_errorLabel;
};

#endif // LOGIN_VIEW_H
#include "LoginView.h"
#include "AuthService.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QCheckBox>
#include <QLabel>
#include <QKeyEvent>
#include <QSettings>
#include <QTimer>
#include <QDateTime>
#include <QByteArray>

LoginView::LoginView(QWidget* parent) : QWidget(parent) {
    setWindowTitle("奇测视觉检测系统 - 登录");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(32, 32, 32, 32);
    mainLayout->setSpacing(24);

    QWidget* logoWidget = new QWidget();
    logoWidget->setFixedHeight(80);
    QHBoxLayout* logoLayout = new QHBoxLayout(logoWidget);
    logoLayout->setContentsMargins(0, 0, 0, 0);
    logoLayout->setSpacing(12);

    QLabel* logoIcon = new QLabel();
    logoIcon->setStyleSheet("color: #660874; font-size: 36px;");
    logoIcon->setText("QD");

    QWidget* titleWidget = new QWidget();
    QVBoxLayout* titleLayout = new QVBoxLayout(titleWidget);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(4);

    QLabel* titleLabel = new QLabel("奇测科技");
    titleLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #e0e0e0;");

    QLabel* subtitleLabel = new QLabel("智能视觉检测系统");
    subtitleLabel->setStyleSheet("font-size: 13px; color: #888;");

    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(subtitleLabel);

    logoLayout->addWidget(logoIcon);
    logoLayout->addWidget(titleWidget);
    mainLayout->addWidget(logoWidget);

    QFormLayout* formLayout = new QFormLayout();
    formLayout->setSpacing(16);

    QLabel* usernameLabel = new QLabel("用户名:");
    usernameLabel->setStyleSheet("color: #ccc; font-weight: bold; font-size: 14px;");

    m_usernameEdit = new QLineEdit();
    m_usernameEdit->setPlaceholderText("请输入用户名");
    m_usernameEdit->setStyleSheet(R"(
        QLineEdit {
            border: 1px solid #555;
            border-radius: 4px;
            padding: 10px 12px;
            font-size: 16px;
            background: #2d2d2d;
            color: #e0e0e0;
            min-height: 24px;
        }
        QLineEdit:focus {
            border-color: #660874;
            border-width: 2px;
            padding: 9px 11px;
        }
    )");
    formLayout->addRow(usernameLabel, m_usernameEdit);

    QLabel* passwordLabel = new QLabel("密码:");
    passwordLabel->setStyleSheet("color: #ccc; font-weight: bold; font-size: 14px;");

    m_passwordEdit = new QLineEdit();
    m_passwordEdit->setPlaceholderText("请输入密码");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setStyleSheet(R"(
        QLineEdit {
            border: 1px solid #555;
            border-radius: 4px;
            padding: 10px 12px;
            font-size: 16px;
            background: #2d2d2d;
            color: #e0e0e0;
            min-height: 24px;
        }
        QLineEdit:focus {
            border-color: #660874;
            border-width: 2px;
            padding: 9px 11px;
        }
    )");
    formLayout->addRow(passwordLabel, m_passwordEdit);

    mainLayout->addLayout(formLayout);

    QCheckBox* rememberCheckBox = new QCheckBox("记住我");
    rememberCheckBox->setStyleSheet("color: #aaa; font-size: 13px;");
    mainLayout->addWidget(rememberCheckBox);
    m_rememberCheckBox = rememberCheckBox;

    QSettings settings;
    bool saved = settings.value("login/remember", false).toBool();
    if (saved) {
        m_rememberCheckBox->setChecked(true);
        QString savedUser = settings.value("login/username").toString();
        QString savedToken = settings.value("login/token").toString();
        qint64 expirySecs = settings.value("login/tokenExpiry", 0).toLongLong();
        QDateTime expiry = QDateTime::fromSecsSinceEpoch(expirySecs);

        if (!savedUser.isEmpty() && !savedToken.isEmpty()
            && QDateTime::currentDateTime() < expiry) {
            m_usernameEdit->setText(savedUser);
            m_rememberCheckBox->setChecked(true);
            QTimer::singleShot(100, this, [this, savedUser, savedToken]() {
                if (AuthService::instance()->loginWithToken(savedUser, savedToken)) {
                    return;
                }
                QSettings s;
                s.remove("login/remember");
                s.remove("login/username");
                s.remove("login/token");
                s.remove("login/tokenExpiry");
            });
        } else {
            settings.remove("login/remember");
            settings.remove("login/username");
            settings.remove("login/token");
            settings.remove("login/tokenExpiry");
        }
    }

    m_errorLabel = new QLabel();
    m_errorLabel->setStyleSheet("color: #F44336; font-size: 12px; padding: 0;");
    m_errorLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_errorLabel);

    m_loginButton = new QPushButton("登录");
    m_loginButton->setStyleSheet(R"(
        QPushButton {
            background-color: #660874;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 10px 0;
            font-size: 15px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #7d1a8f;
        }
        QPushButton:pressed {
            background-color: #4f0062;
        }
        QPushButton:disabled {
            background-color: #555;
        }
    )");
    connect(m_loginButton, &QPushButton::clicked, this, &LoginView::onLoginClicked);
    mainLayout->addWidget(m_loginButton);

    connect(m_passwordEdit, &QLineEdit::returnPressed, m_loginButton, &QPushButton::click);

    setLayout(mainLayout);
    setFixedSize(400, 420);
}

LoginView::~LoginView() {
}

void LoginView::onLoginClicked() {
    QString username = m_usernameEdit->text().trimmed();
    QString password = m_passwordEdit->text();

    m_errorLabel->clear();

    if (username.isEmpty()) {
        m_errorLabel->setText("请输入用户名");
        m_usernameEdit->setFocus();
        return;
    }

    if (password.isEmpty()) {
        m_errorLabel->setText("请输入密码");
        m_passwordEdit->setFocus();
        return;
    }

    if (AuthService::instance()->login(username, password)) {
        m_errorLabel->clear();

        QSettings settings;
        if (m_rememberCheckBox->isChecked()) {
            QString token = AuthService::instance()->registerToken(username);
            settings.setValue("login/remember", true);
            settings.setValue("login/username", username);
            settings.setValue("login/token", token);
            settings.setValue("login/tokenExpiry",
                QDateTime::currentDateTime().addDays(30).toSecsSinceEpoch());
        } else {
            settings.remove("login/remember");
            settings.remove("login/username");
            settings.remove("login/token");
            settings.remove("login/tokenExpiry");
        }

        emit loginSuccess(username);
    } else {
        m_errorLabel->setText("用户名或密码错误");
        m_passwordEdit->selectAll();
        m_passwordEdit->setFocus();
        emit loginFailed("用户名或密码错误");
    }
}
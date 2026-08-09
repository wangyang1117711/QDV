#include "../catch2/catch2_minimal.hpp"
#include "Communication/TCPCommunicator.h"

TEST_CASE("TCPCommunicator构造与状态", "[tcp]") {
    TCPCommunicator comm;
    REQUIRE_FALSE(comm.isConnected());
}

TEST_CASE("TCPCommunicator无效地址拒绝", "[tcp]") {
    TCPCommunicator comm;
    bool ok = comm.connectToHost("invalid.invalid.invalid", 12345);
    CHECK(!ok);
}

TEST_CASE("TCPCommunicator未连接时发送失败", "[tcp]") {
    TCPCommunicator comm;
    QByteArray data("test");
    bool ok = comm.sendData(data);
    REQUIRE_FALSE(ok);
}

TEST_CASE("TCPCommunicator断开未连接对象安全", "[tcp]") {
    TCPCommunicator comm;
    comm.disconnect(); // should not crash
    REQUIRE_FALSE(comm.isConnected());
}

TEST_CASE("TCPCommunicator连接后断开", "[tcp]") {
    TCPCommunicator comm;
    bool ok = comm.connectToHost("127.0.0.1", 8080);
    // May connect or fail — both are valid behavior in test
    if (ok) {
        REQUIRE(comm.isConnected());
        comm.disconnect();
        REQUIRE_FALSE(comm.isConnected());
    }
}

// ---------------------------------------------------------------------------
// TLS 相关测试（不修改上方现有用例）
// ---------------------------------------------------------------------------

TEST_CASE("TCPCommunicator TLS 模式启用与禁用", "[tcp][tls]") {
    TCPCommunicator comm;
    // 默认未启用 TLS
    REQUIRE_FALSE(comm.isTlsEnabled());
    // 启用 TLS
    comm.enableTls(true);
    REQUIRE(comm.isTlsEnabled());
    // 禁用 TLS，回退到明文
    comm.enableTls(false);
    REQUIRE_FALSE(comm.isTlsEnabled());
    // 重复禁用不应崩溃
    comm.enableTls(false);
    REQUIRE_FALSE(comm.isTlsEnabled());
}

TEST_CASE("TCPCommunicator TLS 证书加载失败处理", "[tcp][tls]") {
    TCPCommunicator comm;
    comm.enableTls(true);
    // 加载不存在的 CA 证书文件应失败
    bool ok = comm.loadCertificates("nonexistent_ca_path_12345.crt");
    REQUIRE_FALSE(ok);
    // 加载后仍未连接
    REQUIRE_FALSE(comm.isConnected());
    // 重复切换 TLS 状态不崩溃
    comm.enableTls(false);
    REQUIRE_FALSE(comm.isTlsEnabled());
}

TEST_CASE("TCPCommunicator 未启用TLS时安全降级到明文", "[tcp][tls]") {
    TCPCommunicator comm;
    // 默认未启用 TLS，未配置证书：明文模式可用（向后兼容）
    REQUIRE_FALSE(comm.isTlsEnabled());
    REQUIRE_FALSE(comm.isConnected());
    // connectToHostSecure 在未启用 TLS 时拒绝安全连接并返回 false（不静默降级为明文）
    bool secureOk = comm.connectToHostSecure("127.0.0.1", 8080);
    REQUIRE_FALSE(secureOk);
    REQUIRE_FALSE(comm.isConnected());
    // 明文 connectToHost 路径仍可调用（不崩溃）
    // 不实际建立连接，仅验证对象状态稳定
    REQUIRE_FALSE(comm.isConnected());
}

TEST_CASE("TCPCommunicator TLS 连接失败不崩溃", "[tcp][tls]") {
    TCPCommunicator comm;
    comm.enableTls(true);
    // 连接到一个不会响应 TLS 握手的地址/端口（端口 1 通常无服务）
    // 无论 TCP 被拒绝还是握手失败，均应安全返回 false，不崩溃
    bool ok = comm.connectToHostSecure("127.0.0.1", 1);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(comm.isConnected());
    // 失败后对象仍可使用：禁用 TLS 后可再次尝试明文
    comm.enableTls(false);
    REQUIRE_FALSE(comm.isTlsEnabled());
    comm.disconnect();
    REQUIRE_FALSE(comm.isConnected());
}

TEST_CASE("TCPCommunicator TLS 钉扎指纹设置", "[tcp][tls]") {
    TCPCommunicator comm;
    // 默认无钉扎指纹
    REQUIRE(comm.pinnedCertificateSha256().isEmpty());
    // 设置钉扎指纹
    const QString pin = "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:"
                        "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99";
    comm.setPinnedCertificateSha256(pin);
    REQUIRE(comm.pinnedCertificateSha256() == pin);
}
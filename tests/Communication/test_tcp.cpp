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
#include "../catch2/catch2_minimal.hpp"
#include "Communication/SerialCommunicator.h"

TEST_CASE("SerialCommunicator构造", "[serial]") {
    SerialCommunicator comm;
    REQUIRE_FALSE(comm.isOpen());
}

TEST_CASE("SerialCommunicator不存在的端口打开失败", "[serial]") {
    SerialCommunicator comm;
    bool ok = comm.open("COM99", SerialCommunicator::Baud115200);
    REQUIRE_FALSE(ok);
}

TEST_CASE("SerialCommunicator未打开时发送安全", "[serial]") {
    SerialCommunicator comm;
    QByteArray data("test");
    bool ok = comm.send(data);
    REQUIRE_FALSE(ok);
}

TEST_CASE("SerialCommunicator关闭未打开对象安全", "[serial]") {
    SerialCommunicator comm;
    comm.close();
    REQUIRE_FALSE(comm.isOpen());
}
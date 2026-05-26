#include <catch2/catch_all.hpp>
#include "SerialCommunicator.h"
#include "IOController.h"
#include "TCPCommunicator.h"

TEST_CASE("SerialCommunicator", "[Communication]") {
    SECTION("Configuration with valid parameters") {
        SerialCommunicator serial;
        bool result = serial.configure("COM1", 115200, 8, 1, 0);
        REQUIRE(result == true);
        REQUIRE(serial.isOpen() == true);
        serial.close();
    }

    SECTION("Configuration with invalid data bits") {
        SerialCommunicator serial;
        bool result = serial.configure("COM1", 115200, 12, 1, 0);
        REQUIRE(result == false);
        REQUIRE(serial.isOpen() == false);
    }

    SECTION("Configuration with invalid stop bits") {
        SerialCommunicator serial;
        bool result = serial.configure("COM1", 115200, 8, 3, 0);
        REQUIRE(result == false);
    }

    SECTION("Configuration with invalid parity") {
        SerialCommunicator serial;
        bool result = serial.configure("COM1", 115200, 8, 1, 5);
        REQUIRE(result == false);
    }

    SECTION("Send when not open fails") {
        SerialCommunicator serial;
        bool result = serial.send("test data");
        REQUIRE(result == false);
    }

    SECTION("Send when open succeeds") {
        SerialCommunicator serial;
        serial.open("COM1");
        REQUIRE(serial.isOpen() == true);

        bool result = serial.send(QByteArray("hello"));
        REQUIRE(result == true);
        serial.close();
    }

    SECTION("Close connection") {
        SerialCommunicator serial;
        serial.open("COM1");
        REQUIRE(serial.isOpen() == true);

        serial.close();
        REQUIRE(serial.isOpen() == false);
    }
}

TEST_CASE("IOController", "[Communication]") {
    SECTION("Initialize with valid line count") {
        IOController io;
        bool result = io.initialize(8);
        REQUIRE(result == true);
        REQUIRE(io.totalLines() == 8);
    }

    SECTION("Initialize with invalid line count") {
        IOController io;
        bool result = io.initialize(0);
        REQUIRE(result == false);

        result = io.initialize(50);
        REQUIRE(result == false);
    }

    SECTION("Set line mode") {
        IOController io;
        io.initialize(8);

        bool result = io.setLineMode(0, IOController::OutputMode);
        REQUIRE(result == true);
        REQUIRE(io.lineMode(0) == IOController::OutputMode);
    }

    SECTION("Set output on input line fails") {
        IOController io;
        io.initialize(8);

        bool result = io.setOutput(0, true);
        REQUIRE(result == false);
    }

    SECTION("Set output on output line succeeds") {
        IOController io;
        io.initialize(8);
        io.setLineMode(3, IOController::OutputMode);

        bool result = io.setOutput(3, true);
        REQUIRE(result == true);
    }

    SECTION("Read input state") {
        IOController io;
        io.initialize(8);

        bool state;
        bool result = io.readInput(2, state);
        REQUIRE(result == true);
        REQUIRE(state == false);
    }

    SECTION("Set pulse output") {
        IOController io;
        io.initialize(8);
        io.setLineMode(1, IOController::PulseOutput);

        bool result = io.sendPulse(1, 500);
        REQUIRE(result == true);
    }

    SECTION("Send pulse on non-pulse line fails") {
        IOController io;
        io.initialize(8);

        bool result = io.sendPulse(0, 500);
        REQUIRE(result == false);
    }

    SECTION("Send pulse with invalid duration") {
        IOController io;
        io.initialize(8);
        io.setLineMode(0, IOController::PulseOutput);

        bool result = io.sendPulse(0, -10);
        REQUIRE(result == false);

        result = io.sendPulse(0, 20000);
        REQUIRE(result == false);
    }

    SECTION("Read all inputs") {
        IOController io;
        io.initialize(4);

        QMap<int, bool> inputs = io.readAllInputs();
        REQUIRE(inputs.size() == 4);
    }

    SECTION("Shutdown clears state") {
        IOController io;
        io.initialize(8);
        io.shutdown();

        REQUIRE(io.totalLines() == 0);
    }
}

TEST_CASE("TCPCommunicator", "[Communication]") {
    SECTION("Connection status") {
        TCPCommunicator tcp;
        REQUIRE(tcp.isConnected() == false);

        bool result = tcp.connectToServer("127.0.0.1", 8080);
        REQUIRE(result == false);
        REQUIRE(tcp.isConnected() == false);
    }

    SECTION("Send data") {
        TCPCommunicator tcp;
        bool result = tcp.sendData("test message");
        REQUIRE(result == false);
    }
}
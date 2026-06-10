#include "../catch2/catch2_minimal.hpp"
#include "Communication/IOController.h"
#include <QSignalSpy>

TEST_CASE("IOController construction and init", "[io]") {
    IOController ctrl;
    ctrl.initialize(8);
    REQUIRE_EQUAL(ctrl.totalLines(), 8);
}

TEST_CASE("IOController input simulation", "[io]") {
    IOController ctrl;
    ctrl.initialize(4);
    bool state = false;
    bool ok = ctrl.readInput(0, state);
    CHECK(ok);
    bool bad = ctrl.readInput(10, state);
    REQUIRE_FALSE(bad);
}

TEST_CASE("IOController trigger detection", "[io]") {
    IOController ctrl;
    ctrl.initialize(4);
    QSignalSpy spy(&ctrl, &IOController::inputChanged);
    bool state = false;
    ctrl.readInput(1, state);
    CHECK(spy.count() >= 1);
}

TEST_CASE("IOController status query", "[io]") {
    IOController ctrl;
    ctrl.initialize(4);
    auto modes = ctrl.allLineModes();
    REQUIRE_EQUAL(modes.size(), 4);
}

TEST_CASE("IOController setOutput signal", "[io]") {
    IOController ctrl;
    ctrl.initialize(4);
    QSignalSpy spy(&ctrl, &IOController::outputChanged);
    ctrl.setOutput(0, true);
    CHECK(spy.count() >= 1);
}

TEST_CASE("IOController readAllInputs", "[io]") {
    IOController ctrl;
    ctrl.initialize(4);
    auto inputs = ctrl.readAllInputs();
    REQUIRE_EQUAL(inputs.size(), 4);
}

TEST_CASE("IOController sendPulse", "[io]") {
    IOController ctrl;
    ctrl.initialize(4);
    ctrl.sendPulse(0, 100);
    SUCCEED("sendPulse called");
}
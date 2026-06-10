#include "../catch2/catch2_minimal.hpp"
#include "Core/BranchNode.h"
#include "Core/VisionTool.h"
#include <QJsonObject>

TEST_CASE("BranchNode default construction", "[branch]") {
    BranchNode node;
    REQUIRE(node.id.isEmpty());
    REQUIRE(node.sourceToolId.isEmpty());
    REQUIRE_FALSE(node.isValid());
}

TEST_CASE("BranchNode with source and operator", "[branch]") {
    BranchNode node;
    node.sourceToolId = "tool_1";
    node.conditionOp = "==";
    node.conditionValue = true;
    REQUIRE(node.isValid());
}

TEST_CASE("BranchNode with true/false branches", "[branch]") {
    BranchNode node;
    node.sourceToolId = "tool_1";
    node.conditionOp = ">";
    node.conditionValue = 0.75;
    node.trueBranchToolIds.append("tool_2");
    node.falseBranchToolIds.append("tool_3");

    REQUIRE(node.isValid());
    REQUIRE_EQUAL(node.trueBranchToolIds.size(), 1);
    REQUIRE_EQUAL(node.falseBranchToolIds.size(), 1);
}

TEST_CASE("BranchNode serialize and deserialize", "[branch]") {
    BranchNode node;
    node.id = "branch_1";
    node.sourceToolId = "tool_1";
    node.conditionOp = "==";
    node.conditionValue = true;
    node.trueBranchToolIds.append("tool_2");

    QJsonObject json = node.serialize();
    REQUIRE_EQUAL(json["id"].toString(), "branch_1");
    REQUIRE_EQUAL(json["source"].toString(), "tool_1");
    REQUIRE_EQUAL(json["trueBranch"].toArray().size(), 1);

    BranchNode restored;
    restored.deserialize(json);
    REQUIRE_EQUAL(restored.id, "branch_1");
    REQUIRE(restored.isValid());
}

TEST_CASE("BranchNode evaluate equality", "[branch]") {
    BranchNode node;
    node.sourceToolId = "tool_1";
    node.conditionOp = "==";
    node.conditionValue = true;

    ToolResult passResult;
    passResult.ok = true;
    REQUIRE(node.evaluate(passResult));

    ToolResult failResult;
    failResult.ok = false;
    REQUIRE_FALSE(node.evaluate(failResult));
}

TEST_CASE("BranchNode evaluate greater than", "[branch]") {
    BranchNode node;
    node.sourceToolId = "tool_1";
    node.conditionOp = ">";
    node.conditionValue = 0.5;

    ToolResult highResult;
    highResult.score = 0.9;
    REQUIRE(node.evaluate(highResult));

    ToolResult lowResult;
    lowResult.score = 0.3;
    REQUIRE_FALSE(node.evaluate(lowResult));
}

TEST_CASE("BranchNode evaluate less than", "[branch]") {
    BranchNode node;
    node.sourceToolId = "tool_1";
    node.conditionOp = "<";
    node.conditionValue = 0.5;

    ToolResult lowResult;
    lowResult.score = 0.3;
    REQUIRE(node.evaluate(lowResult));

    ToolResult highResult;
    highResult.score = 0.9;
    REQUIRE_FALSE(node.evaluate(highResult));
}

TEST_CASE("BranchNode evaluate empty content", "[branch]") {
    BranchNode node;
    node.conditionOp = "unknown";
    ToolResult result;
    REQUIRE_FALSE(node.evaluate(result));
}
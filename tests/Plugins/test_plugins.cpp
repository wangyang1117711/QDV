#include "../catch2/catch2_minimal.hpp"
#include "Plugins/PluginManager.h"
#include "Plugins/IPlugin.h"

TEST_CASE("PluginManager singleton", "[plugin]") {
    auto* p1 = PluginManager::instance();
    auto* p2 = PluginManager::instance();
    REQUIRE(p1 != nullptr);
    REQUIRE(p1 == p2);
}

TEST_CASE("PluginManager loadPlugin non-existent returns false", "[plugin]") {
    auto* mgr = PluginManager::instance();
    bool loaded = mgr->loadPlugin("no_such_file.dll");
    REQUIRE_FALSE(loaded);
    REQUIRE(mgr->getPlugin("nonexistent_plugin") == nullptr);
}

TEST_CASE("PluginManager unloadPlugins without crash", "[plugin]") {
    auto* mgr = PluginManager::instance();
    mgr->unloadPlugins();
    REQUIRE(mgr->loadedPlugins().size() == 0);
}

TEST_CASE("PluginManager empty plugin directory load", "[plugin]") {
    auto* mgr = PluginManager::instance();
    mgr->unloadPlugins();
    bool loaded = mgr->loadPlugins("nonexistent_directory_12345");
    REQUIRE_FALSE(loaded);
}

TEST_CASE("PluginManager getPlugin for non-existent name", "[plugin]") {
    auto* mgr = PluginManager::instance();
    REQUIRE(mgr->getPlugin("this_plugin_does_not_exist") == nullptr);
}
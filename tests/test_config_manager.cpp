#include <gtest/gtest.h>
#include "stereo_camera/data/ConfigManager.h"
#include "stereo_camera/common/Parameter.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

class ConfigManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/stereo_camera_test_config";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
};

TEST_F(ConfigManagerTest, SaveAndLoad) {
    auto param_mgr = std::make_shared<stereo_camera::ParameterManager>();
    stereo_camera::Parameter p;
    p.name = "fps";
    p.type = stereo_camera::ParameterType::Integer;
    p.value.int_val = 30;
    p.is_readonly = false;
    p.is_available = true;
    param_mgr->define(p);

    stereo_camera::ConfigManager cfg_mgr(test_dir_);
    cfg_mgr.set_parameter_manager(param_mgr);
    EXPECT_TRUE(cfg_mgr.save("test_config.json"));

    auto param_mgr2 = std::make_shared<stereo_camera::ParameterManager>();
    stereo_camera::ConfigManager cfg_mgr2(test_dir_);
    cfg_mgr2.set_parameter_manager(param_mgr2);
    EXPECT_TRUE(cfg_mgr2.load("test_config.json"));

    auto loaded = param_mgr2->get("fps");
    EXPECT_EQ(loaded.value.int_val, 30);
}

TEST_F(ConfigManagerTest, LoadNonexistentReturnsFalse) {
    stereo_camera::ConfigManager cfg_mgr(test_dir_);
    EXPECT_FALSE(cfg_mgr.load("nonexistent.json"));
}

TEST_F(ConfigManagerTest, SyncWritesParameters) {
    auto param_mgr = std::make_shared<stereo_camera::ParameterManager>();
    stereo_camera::Parameter p;
    p.name = "gain";
    p.type = stereo_camera::ParameterType::Float;
    p.value.float_val = 1.5;
    p.is_readonly = false;
    p.is_available = true;
    param_mgr->define(p);

    stereo_camera::ConfigManager cfg_mgr(test_dir_);
    cfg_mgr.set_parameter_manager(param_mgr);
    EXPECT_TRUE(cfg_mgr.sync());

    EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/parameters.json"));
}

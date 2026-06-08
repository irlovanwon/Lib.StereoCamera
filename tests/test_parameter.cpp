#include <gtest/gtest.h>
#include "stereo_camera/common/Parameter.h"

TEST(ParameterTest, DefineAndGet) {
    stereo_camera::ParameterManager mgr;
    stereo_camera::Parameter p;
    p.name = "fps";
    p.type = stereo_camera::ParameterType::Integer;
    p.value.int_val = 30;
    p.is_readonly = false;
    p.is_available = true;

    mgr.define(p);
    auto result = mgr.get("fps");
    EXPECT_EQ(result.name, "fps");
    EXPECT_EQ(result.value.int_val, 30);
    EXPECT_FALSE(result.is_readonly);
    EXPECT_TRUE(result.is_available);
}

TEST(ParameterTest, SetModifiesValue) {
    stereo_camera::ParameterManager mgr;
    stereo_camera::Parameter p;
    p.name = "gain";
    p.type = stereo_camera::ParameterType::Float;
    p.value.float_val = 1.0;
    p.is_readonly = false;
    p.is_available = true;
    mgr.define(p);

    stereo_camera::ParameterValue new_val;
    new_val.float_val = 2.5;
    EXPECT_TRUE(mgr.set("gain", new_val));
    EXPECT_DOUBLE_EQ(mgr.get("gain").value.float_val, 2.5);
}

TEST(ParameterTest, SetReadOnlyFails) {
    stereo_camera::ParameterManager mgr;
    stereo_camera::Parameter p;
    p.name = "readonly_param";
    p.type = stereo_camera::ParameterType::Integer;
    p.is_readonly = true;
    p.is_available = true;
    mgr.define(p);

    stereo_camera::ParameterValue val;
    val.int_val = 42;
    EXPECT_FALSE(mgr.set("readonly_param", val));
}

TEST(ParameterTest, SetUnavailableFails) {
    stereo_camera::ParameterManager mgr;
    stereo_camera::Parameter p;
    p.name = "unavail";
    p.type = stereo_camera::ParameterType::Integer;
    p.is_readonly = false;
    p.is_available = false;
    mgr.define(p);

    stereo_camera::ParameterValue val;
    val.int_val = 1;
    EXPECT_FALSE(mgr.set("unavail", val));
}

TEST(ParameterTest, ListReturnsAll) {
    stereo_camera::ParameterManager mgr;
    stereo_camera::Parameter p1;
    p1.name = "a";
    p1.type = stereo_camera::ParameterType::Integer;
    p1.is_readonly = false;
    p1.is_available = true;
    mgr.define(p1);

    stereo_camera::Parameter p2;
    p2.name = "b";
    p2.type = stereo_camera::ParameterType::Float;
    p2.is_readonly = false;
    p2.is_available = true;
    mgr.define(p2);

    EXPECT_EQ(mgr.list().size(), 2u);
}

TEST(ParameterTest, JsonRoundTrip) {
    stereo_camera::ParameterManager mgr;
    stereo_camera::Parameter p;
    p.name = "fps";
    p.type = stereo_camera::ParameterType::Integer;
    p.value.int_val = 30;
    p.is_readonly = false;
    p.is_available = true;
    mgr.define(p);

    auto j = mgr.to_json();
    stereo_camera::ParameterManager mgr2;
    mgr2.from_json(j);
    EXPECT_EQ(mgr2.get("fps").value.int_val, 30);
}

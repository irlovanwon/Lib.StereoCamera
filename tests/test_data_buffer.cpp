#include <gtest/gtest.h>
#include "stereo_camera/data/DataBuffer.h"
#include <memory>

TEST(DataBufferTest, PushAndGetLatest) {
    stereo_camera::DataBuffer buf(10);
    buf.create_slot("cam1", stereo_camera::DataType::StereoImage);

    auto bundle = std::make_shared<stereo_camera::DataBundle>();
    bundle->type = stereo_camera::DataType::StereoImage;
    bundle->timestamp = stereo_camera::Timestamp::now();
    bundle->payload = {1, 2, 3};

    buf.push("cam1", bundle);
    EXPECT_EQ(buf.slot_size("cam1", stereo_camera::DataType::StereoImage), 1u);
    EXPECT_EQ(buf.slot_size("cam1", stereo_camera::DataType::DepthMap), 0u);
}

TEST(DataBufferTest, SharedPtrZeroCopy) {
    stereo_camera::DataBuffer buf(10);
    buf.create_slot("cam1", stereo_camera::DataType::StereoImage);

    auto bundle = std::make_shared<stereo_camera::DataBundle>();
    bundle->type = stereo_camera::DataType::StereoImage;
    bundle->payload = {10, 20, 30};

    buf.push("cam1", bundle);

    auto latest = buf.get_latest("cam1", stereo_camera::DataType::StereoImage);
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->payload.size(), 3u);
    EXPECT_EQ(latest->payload[0], 10);

    EXPECT_EQ(latest.get(), bundle.get());
}

TEST(DataBufferTest, MaxFramesEnforced) {
    stereo_camera::DataBuffer buf(2);
    buf.create_slot("cam1", stereo_camera::DataType::StereoImage);

    for (int i = 0; i < 5; ++i) {
        auto bundle = std::make_shared<stereo_camera::DataBundle>();
        bundle->type = stereo_camera::DataType::StereoImage;
        bundle->timestamp = {i, 0};
        bundle->payload = {static_cast<uint8_t>(i)};
        buf.push("cam1", bundle);
    }

    EXPECT_EQ(buf.slot_size("cam1", stereo_camera::DataType::StereoImage), 2u);

    auto latest = buf.get_latest("cam1", stereo_camera::DataType::StereoImage);
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->timestamp.sec, 4);
}

TEST(DataBufferTest, GetLatestN) {
    stereo_camera::DataBuffer buf(10);
    buf.create_slot("cam1", stereo_camera::DataType::DepthMap);

    for (int i = 0; i < 5; ++i) {
        auto bundle = std::make_shared<stereo_camera::DataBundle>();
        bundle->type = stereo_camera::DataType::DepthMap;
        bundle->timestamp = {i, 0};
        buf.push("cam1", bundle);
    }

    auto latest = buf.get_latest_n("cam1", stereo_camera::DataType::DepthMap, 2);
    EXPECT_EQ(latest.size(), 2u);
    EXPECT_EQ(latest[0]->timestamp.sec, 3);
    EXPECT_EQ(latest[1]->timestamp.sec, 4);
}

TEST(DataBufferTest, PerSDKIsolation) {
    stereo_camera::DataBuffer buf(10);
    buf.create_slot("cam1", stereo_camera::DataType::StereoImage);
    buf.create_slot("cam2", stereo_camera::DataType::StereoImage);

    auto b1 = std::make_shared<stereo_camera::DataBundle>();
    b1->type = stereo_camera::DataType::StereoImage;
    b1->timestamp = {100, 0};
    buf.push("cam1", b1);

    auto b2 = std::make_shared<stereo_camera::DataBundle>();
    b2->type = stereo_camera::DataType::StereoImage;
    b2->timestamp = {200, 0};
    buf.push("cam2", b2);

    EXPECT_EQ(buf.slot_size("cam1", stereo_camera::DataType::StereoImage), 1u);
    EXPECT_EQ(buf.slot_size("cam2", stereo_camera::DataType::StereoImage), 1u);

    auto latest1 = buf.get_latest("cam1", stereo_camera::DataType::StereoImage);
    auto latest2 = buf.get_latest("cam2", stereo_camera::DataType::StereoImage);
    ASSERT_NE(latest1, nullptr);
    ASSERT_NE(latest2, nullptr);
    EXPECT_EQ(latest1->timestamp.sec, 100);
    EXPECT_EQ(latest2->timestamp.sec, 200);
}

TEST(DataBufferTest, RemoveSlot) {
    stereo_camera::DataBuffer buf(10);
    buf.create_slot("cam1", stereo_camera::DataType::StereoImage);

    auto bundle = std::make_shared<stereo_camera::DataBundle>();
    bundle->type = stereo_camera::DataType::StereoImage;
    buf.push("cam1", bundle);

    EXPECT_EQ(buf.slot_size("cam1", stereo_camera::DataType::StereoImage), 1u);
    buf.remove_slot("cam1", stereo_camera::DataType::StereoImage);
    EXPECT_EQ(buf.slot_size("cam1", stereo_camera::DataType::StereoImage), 0u);
}

TEST(DataBufferTest, RemoveAllSlots) {
    stereo_camera::DataBuffer buf(10);
    buf.create_slot("cam1", stereo_camera::DataType::StereoImage);
    buf.create_slot("cam1", stereo_camera::DataType::DepthMap);
    buf.create_slot("cam2", stereo_camera::DataType::StereoImage);

    EXPECT_EQ(buf.active_slots().size(), 3u);
    buf.remove_all_slots("cam1");
    auto slots = buf.active_slots();
    EXPECT_EQ(slots.size(), 1u);
    EXPECT_EQ(slots[0].camera_id, "cam2");
}

TEST(DataBufferTest, Clear) {
    stereo_camera::DataBuffer buf(10);
    buf.create_slot("cam1", stereo_camera::DataType::PointCloud);

    auto bundle = std::make_shared<stereo_camera::DataBundle>();
    bundle->type = stereo_camera::DataType::PointCloud;
    buf.push("cam1", bundle);
    EXPECT_EQ(buf.slot_size("cam1", stereo_camera::DataType::PointCloud), 1u);

    buf.clear();
    EXPECT_EQ(buf.active_slots().size(), 0u);
}

TEST(DataBufferTest, AutoCreateSlotOnPush) {
    stereo_camera::DataBuffer buf(10);

    auto bundle = std::make_shared<stereo_camera::DataBundle>();
    bundle->type = stereo_camera::DataType::IMU;
    bundle->payload = {42};

    buf.push("cam3", bundle);

    auto latest = buf.get_latest("cam3", stereo_camera::DataType::IMU);
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->payload[0], 42);
}

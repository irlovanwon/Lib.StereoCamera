cmake_minimum_required(VERSION 3.16)
project(StereoCamera VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(TURBOJPEG REQUIRED libturbojpeg)

set(LIB_SOURCES
    src/common/Parameter.cpp
    src/common/Response.cpp
    src/common/Logger.cpp
    src/api/CameraSDKClient.cpp
    src/api/ClientHandler.cpp
    src/api/AdminServer.cpp
    src/data/DataBuffer.cpp
    src/data/DataStorage.cpp
    src/data/ConfigManager.cpp
    src/data/SDKSlotManager.cpp
    src/data/DataPublisher.cpp
    src/data/LoopbackSubscriber.cpp
    src/data/WSServer.cpp
    src/vision/DisparityMap.cpp
    src/vision/DepthMap.cpp
    src/vision/PointCloud.cpp
    src/calibration/Calibration2D.cpp
    src/calibration/Calibration3D.cpp
)

add_library(stereo_camera_lib STATIC ${LIB_SOURCES})
target_include_directories(stereo_camera_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${ZMQ_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${WEBP_INCLUDE_DIRS}
)
target_link_libraries(stereo_camera_lib PUBLIC
    -lturbojpeg
    ${ZMQ_LIBRARIES}
    ${CURL_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${WEBP_LIBRARIES}
    nlohmann_json::nlohmann_json
    pthread
)
target_compile_options(stereo_camera_lib PRIVATE -Wall -Wextra -Wpedantic)

add_executable(stereo_camera_node src/main.cpp)
target_link_libraries(stereo_camera_node PRIVATE stereo_camera_lib)

enable_testing()
find_package(GTest REQUIRED)

add_executable(stereo_camera_tests
    tests/test_parameter.cpp
    tests/test_data_buffer.cpp
    tests/test_config_manager.cpp
)
target_link_libraries(stereo_camera_tests PRIVATE stereo_camera_lib GTest::gtest GTest::gtest_main)
add_test(NAME stereo_camera_tests COMMAND stereo_camera_tests)

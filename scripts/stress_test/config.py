import os

WSS_URI = os.environ.get("WSS_URI", "wss://127.0.0.1:9444")
HTTPS_HOST = os.environ.get("HTTPS_HOST", "127.0.0.1")
HTTPS_PORT = int(os.environ.get("HTTPS_PORT", "9443"))
CAMERA_ID = os.environ.get("CAMERA_ID", "cam1")

ZMQ_PUB_ENDPOINTS = {
    "stereo_image": os.environ.get("ZMQ_PUB_STEREO_IMAGE", "ipc:///tmp/sc_pub_stereo_image"),
    "depth_map": os.environ.get("ZMQ_PUB_DEPTH_MAP", "ipc:///tmp/sc_pub_depth_map"),
    "point_cloud": os.environ.get("ZMQ_PUB_POINT_CLOUD", "ipc:///tmp/sc_pub_point_cloud"),
    "imu": os.environ.get("ZMQ_PUB_IMU", "ipc:///tmp/sc_pub_imu"),
    "magnetometer": os.environ.get("ZMQ_PUB_MAGNETOMETER", "ipc:///tmp/sc_pub_magnetometer"),
    "barometer": os.environ.get("ZMQ_PUB_BAROMETER", "ipc:///tmp/sc_pub_barometer"),
}

DATA_TYPES = ["StereoImage", "DepthMap", "PointCloud", "IMU", "Magnetometer", "Barometer"]
TOPICS = [f"{CAMERA_ID}/{dt}" for dt in DATA_TYPES]

TEST_DURATION_SUSTAINED = int(os.environ.get("TEST_DURATION_SUSTAINED", "600"))
TEST_DURATION_QUICK = int(os.environ.get("TEST_DURATION_QUICK", "60"))

REPORT_DIR = os.environ.get("REPORT_DIR", "/tmp/stress_test_reports")

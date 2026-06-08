#include "stereo_camera/data/DataStorage.h"
#include "stereo_camera/common/Logger.h"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <nlohmann/json.hpp>

namespace stereo_camera {

namespace fs = std::filesystem;

FileStorage::FileStorage(const std::string& base_dir) : base_dir_(base_dir) {
    fs::create_directories(base_dir_);
}

bool FileStorage::save(const std::string& path, const DataBundle& bundle) {
    std::string full_path = base_dir_ + "/" + path;
    fs::create_directories(fs::path(full_path).parent_path());

    nlohmann::json j;
    j["timestamp_sec"] = bundle.timestamp.sec;
    j["timestamp_nsec"] = bundle.timestamp.nsec;
    j["data_type"] = bundle.type;
    j["payload_size"] = bundle.payload.size();

    std::ofstream file(full_path, std::ios::binary);
    if (!file) {
        Logger::instance().error("FileStorage", "Failed to open: " + full_path);
        return false;
    }

    auto json_str = j.dump();
    uint32_t json_size = json_str.size();
    file.write(reinterpret_cast<const char*>(&json_size), sizeof(json_size));
    file.write(json_str.data(), json_size);
    file.write(reinterpret_cast<const char*>(bundle.payload.data()), bundle.payload.size());
    return true;
}

bool FileStorage::load(const std::string& path, DataBundle& bundle) {
    std::string full_path = base_dir_ + "/" + path;
    std::ifstream file(full_path, std::ios::binary);
    if (!file) return false;

    uint32_t json_size;
    file.read(reinterpret_cast<char*>(&json_size), sizeof(json_size));
    std::string json_str(json_size, '\0');
    file.read(json_str.data(), json_size);

    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) return false;

    bundle.timestamp.sec = j["timestamp_sec"].get<int64_t>();
    bundle.timestamp.nsec = j["timestamp_nsec"].get<int64_t>();
    bundle.type = j["data_type"].get<DataType>();

    auto payload_size = j["payload_size"].get<size_t>();
    bundle.payload.resize(payload_size);
    file.read(reinterpret_cast<char*>(bundle.payload.data()), payload_size);
    return true;
}

std::vector<std::string> FileStorage::list(const std::string& prefix) const {
    std::vector<std::string> result;
    if (!fs::exists(base_dir_)) return result;

    for (const auto& entry : fs::recursive_directory_iterator(base_dir_)) {
        if (entry.is_regular_file()) {
            auto rel = fs::relative(entry.path(), base_dir_).string();
            if (rel.find(prefix) == 0) {
                result.push_back(rel);
            }
        }
    }
    return result;
}

} // namespace stereo_camera

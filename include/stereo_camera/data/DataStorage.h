#pragma once

#include "stereo_camera/common/Types.h"
#include <string>
#include <vector>

namespace stereo_camera {

class DataStorage {
public:
    virtual ~DataStorage() = default;

    virtual bool save(const std::string& path, const DataBundle& bundle) = 0;
    virtual bool load(const std::string& path, DataBundle& bundle) = 0;
    virtual std::vector<std::string> list(const std::string& prefix) const = 0;
};

class FileStorage : public DataStorage {
public:
    explicit FileStorage(const std::string& base_dir);

    bool save(const std::string& path, const DataBundle& bundle) override;
    bool load(const std::string& path, DataBundle& bundle) override;
    std::vector<std::string> list(const std::string& prefix) const override;

private:
    std::string base_dir_;
};

} // namespace stereo_camera

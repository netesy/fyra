#pragma once
#include <string>
#include <vector>

namespace target {
namespace artifact {

class APKPackager {
public:
    static bool package(const std::string& outputPath, const std::string& assetsPath, const std::string& libPath, const std::string& manifestPath);
};

}
}

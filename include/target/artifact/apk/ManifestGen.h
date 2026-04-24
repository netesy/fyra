#pragma once
#include <string>

namespace codegen {
namespace target {
namespace artifact {

class ManifestGen {
public:
    static std::string generate(const std::string& packageName, const std::string& appName);
};

}
}
}

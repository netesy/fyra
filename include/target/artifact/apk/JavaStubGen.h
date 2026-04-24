#pragma once
#include <string>

namespace codegen {
namespace target {
namespace artifact {

class JavaStubGen {
public:
    static std::string generate(const std::string& packageName, const std::string& className);
};

}
}
}

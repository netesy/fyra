#include "target/artifact/apk/JavaStubGen.h"
#include <sstream>

namespace target {
namespace artifact {

std::string JavaStubGen::generate(const std::string& packageName, const std::string& className) {
    std::stringstream ss;
    ss << "package " << packageName << ";\n\n";
    ss << "import android.app.NativeActivity;\n";
    ss << "import android.os.Bundle;\n\n";
    ss << "public class " << className << " extends NativeActivity {\n";
    ss << "    @Override\n";
    ss << "    protected void onCreate(Bundle savedInstanceState) {\n";
    ss << "        super.onCreate(savedInstanceState);\n";
    ss << "    }\n";
    ss << "}\n";
    return ss.str();
}

}
}
}

#include "target/artifact/apk/APKPackager.h"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace codegen {
namespace target {
namespace artifact {

bool APKPackager::package(const std::string& outputPath, const std::string& assetsPath, const std::string& libPath, const std::string& manifestPath) {
    // Deterministic unsigned APK fallback implementation
    // In a real scenario, this would use a ZIP library or call 'aapt2'
    std::cout << "[APKPackager] Packaging APK to " << outputPath << "\n";
    std::cout << "  - Manifest: " << manifestPath << "\n";
    std::cout << "  - Native Library: " << libPath << "\n";

    // Create directory structure simulation
    namespace fs = std::filesystem;
    fs::path buildDir = fs::path(outputPath).parent_path() / "apk_build";
    fs::create_directories(buildDir / "lib" / "arm64-v8a");

    // Copy manifest
    if (fs::exists(manifestPath)) {
        fs::copy_file(manifestPath, buildDir / "AndroidManifest.xml", fs::copy_options::overwrite_existing);
    }

    // Copy library
    if (fs::exists(libPath)) {
        fs::copy_file(libPath, buildDir / "lib" / "arm64-v8a" / "libfyra_app.so", fs::copy_options::overwrite_existing);
    }

    // Dummy APK file creation (representing a packaged zip)
    std::ofstream apk(outputPath, std::ios::binary);
    apk << "PK\03\04" << "DUMMY_APK_CONTENT";
    apk.close();

    return true;
}

}
}
}

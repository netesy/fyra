#include "target/artifact/apk/ManifestGen.h"
#include <sstream>

namespace codegen {
namespace target {
namespace artifact {

std::string ManifestGen::generate(const std::string& packageName, const std::string& appName) {
    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    ss << "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n";
    ss << "    package=\"" << packageName << "\"\n";
    ss << "    android:versionCode=\"1\"\n";
    ss << "    android:versionName=\"1.0\">\n";
    ss << "    <uses-sdk android:minSdkVersion=\"21\" android:targetSdkVersion=\"30\" />\n";
    ss << "    <application android:label=\"" << appName << "\" android:hasCode=\"false\">\n";
    ss << "        <activity android:name=\"android.app.NativeActivity\"\n";
    ss << "            android:label=\"" << appName << "\"\n";
    ss << "            android:configChanges=\"orientation|keyboardHidden\">\n";
    ss << "            <meta-data android:name=\"android.app.lib_name\" android:value=\"fyra_app\" />\n";
    ss << "            <intent-filter>\n";
    ss << "                <action android:name=\"android.intent.action.MAIN\" />\n";
    ss << "                <category android:name=\"android.intent.category.LAUNCHER\" />\n";
    ss << "            </intent-filter>\n";
    ss << "        </activity>\n";
    ss << "    </application>\n";
    ss << "</manifest>\n";
    return ss.str();
}

}
}
}

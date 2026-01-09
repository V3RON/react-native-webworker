require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

Pod::Spec.new do |s|
  s.name         = "Webworker"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => "https://github.com/V3RON/react-native-webworker.git", :tag => "#{s.version}" }

  # Include iOS binding AND shared C++ core
  s.source_files = [
    "ios/**/*.{h,m,mm}",
    "cpp/**/*.{h,cpp}"
  ]

  s.private_header_files = [
    "ios/**/*.h",
    "cpp/**/*.h"
  ]

  # Enable C++17 for Hermes/JSI support and add include paths
  s.pod_target_xcconfig = {
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++17",
    "HEADER_SEARCH_PATHS" => "\"$(PODS_ROOT)/hermes-engine/destroot/include\" \"$(PODS_TARGET_SRCROOT)/cpp\""
  }

  # Add cpp directory to header search paths
  s.xcconfig = {
    "HEADER_SEARCH_PATHS" => "\"$(PODS_TARGET_SRCROOT)/cpp\""
  }

  install_modules_dependencies(s)
end

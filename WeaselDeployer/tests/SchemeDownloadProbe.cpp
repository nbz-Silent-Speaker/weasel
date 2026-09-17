#include "WanxiangSchemeManager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

std::filesystem::path test_user_directory;

namespace {
std::wstring Widen(const std::string& value) {
  return {value.begin(), value.end()};
}

std::string Read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 6)
      throw std::runtime_error("expected root, tag, URL, SHA-256 and size");
    test_user_directory = std::filesystem::u8path(argv[1]);
    std::filesystem::remove_all(test_user_directory);
    std::filesystem::create_directories(test_user_directory);
    {
      std::ofstream custom(test_user_directory / "custom_phrase.txt",
                           std::ios::binary);
      custom << "keep user phrase";
    }
    WanxiangUpdateManager::SchemeRelease release;
    release.tag = Widen(argv[2]);
    release.url = Widen(argv[3]);
    release.sha256 = Widen(argv[4]);
    release.size = std::stoull(argv[5]);
    release.from_cnb = true;
    WanxiangSchemeManager manager(test_user_directory);
    std::wstring error;
    if (!manager.PrepareAndInstall(release, &error)) {
      std::wcerr << error << L'\n';
      return 1;
    }
    if (Read(test_user_directory / "version.txt").find(argv[2] + 1) ==
            std::string::npos ||
        Read(test_user_directory / "custom_phrase.txt") != "keep user phrase") {
      throw std::runtime_error("installed package failed validation");
    }
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(test_user_directory)) {
      if (entry.path().extension() == ".gram")
        throw std::runtime_error("grammar model was present in scheme package");
    }
    if (!manager.Rollback(&error))
      throw std::runtime_error("download probe rollback failed");
    std::filesystem::remove_all(test_user_directory);
    std::cout << "CNB scheme download, verification and extraction passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

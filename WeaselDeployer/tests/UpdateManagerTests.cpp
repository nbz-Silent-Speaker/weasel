#include "WanxiangUpdateManager.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

std::filesystem::path test_user_directory;

namespace {
void Require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::string Asset(const std::string& algorithm,
                  const std::string& digest,
                  const std::string& size) {
  return "<html>{\"id\":\"model\","
         "\"path\":\"/amzxyz/rime-wanxiang/-/releases/download/model/"
         "wanxiang-lts-zh-hans.gram\","
         "\"name\":\"wanxiang-lts-zh-hans.gram\","
         "\"hashAlgo\":\"" +
         algorithm + "\",\"hashValue\":\"" + digest +
         "\",\"sizeInByte\":" + size + ",\"author\":{}}</html>";
}
}  // namespace

int main() {
  try {
    const std::string uppercase_digest(64, 'A');
    WanxiangUpdateManager::ModelRelease release;
    Require(WanxiangUpdateManager::ParseModelRelease(
                Asset("sha256", uppercase_digest, "420343852"), &release),
            "valid CNB model metadata was rejected");
    Require(release.sha256 == std::wstring(64, L'a'),
            "model checksum was not normalized");
    Require(release.size == 420343852, "model size was not parsed exactly");
    Require(!WanxiangUpdateManager::ParseModelRelease(
                Asset("md5", uppercase_digest, "420343852"), &release),
            "non-SHA256 metadata was accepted");
    Require(!WanxiangUpdateManager::ParseModelRelease(
                Asset("sha256", std::string(63, 'a'), "420343852"), &release),
            "short checksum was accepted");
    Require(!WanxiangUpdateManager::ParseModelRelease(
                Asset("sha256", uppercase_digest, "0"), &release),
            "zero-sized model was accepted");
    auto wrong_path = Asset("sha256", uppercase_digest, "420343852");
    const std::string expected_name = "wanxiang-lts-zh-hans.gram";
    const auto name = wrong_path.find(expected_name);
    wrong_path.replace(name, expected_name.size(), "untrusted-model-file.gram");
    Require(!WanxiangUpdateManager::ParseModelRelease(wrong_path, &release),
            "unexpected model path was accepted");
    std::cout << "Update manager tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

#include "WanxiangUpdateManager.h"

#include <filesystem>
#include <fstream>
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

int main(int argc, char** argv) {
  try {
    test_user_directory =
        std::filesystem::temp_directory_path() / "weasel-update-manager-test";
    std::filesystem::create_directories(test_user_directory);
    {
      std::ofstream version(test_user_directory / "version.txt",
                            std::ios::binary | std::ios::trunc);
      version << "17.10.0\n";
    }
    std::wstring tag;
    Require(WanxiangUpdateManager::ParseReleaseAddress(
                L"https://github.com/amzxyz/rime-wanxiang/releases/tag/"
                L"v18.0.0",
                &tag) &&
                tag == L"v18.0.0",
            "GitHub release redirect was not recognized");
    Require(WanxiangUpdateManager::ParseReleaseAddress(
                L"https://cnb.cool/amzxyz/rime-wanxiang/-/releases/tag/"
                L"v17.10.3?tab=assets",
                &tag) &&
                tag == L"v17.10.3",
            "CNB release redirect was not recognized");
    Require(WanxiangUpdateManager::ParseReleaseAddress(
                L"/amzxyz/rime-wanxiang/releases/tag/v18.0.0", &tag) &&
                tag == L"v18.0.0",
            "relative GitHub release redirect was not recognized");
    Require(!WanxiangUpdateManager::ParseReleaseAddress(
                L"https://example.com/releases/tag/v99.0.0", &tag),
            "untrusted release host was accepted");
    Require(!WanxiangUpdateManager::ParseReleaseAddress(
                L"https://github.com/amzxyz/rime-wanxiang/releases/tag/"
                L"dict-nightly",
                &tag),
            "non-version release tag was accepted");
    Require(WanxiangUpdateManager::IsNewerVersion(L"v18.0.0", L"17.10.0"),
            "major version update was not detected");
    Require(WanxiangUpdateManager::IsNewerVersion(L"v17.10.3", L"17.10.0"),
            "patch version update was not detected");
    Require(!WanxiangUpdateManager::IsNewerVersion(L"v17.10.0", L"17.10.0"),
            "equal version was reported as newer");

    const std::string cnb_releases =
        R"([{"tag_name":"v17.10.3","draft":false,"prerelease":false,"assets":[{"name":"rime-wanxiang-lite.zip","hash_algo":"sha256","hash_value":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","size":100}]},{"tag_name":"v99.0.0-beta","draft":false,"prerelease":true,"assets":[{"name":"rime-wanxiang-lite.zip","hash_algo":"sha256","hash_value":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","size":200}]},{"tag_name":"v18.0.0","draft":false,"prerelease":false,"assets":[{"name":"rime-wanxiang-lite.zip","hash_algo":"sha256","hash_value":"F1C6E330C822B667F39D0EE596FA6069550BD2FAC7E6CB5E227510C0A25314C3","size":32212731}]}])";
    WanxiangUpdateManager::SchemeRelease scheme_release;
    Require(WanxiangUpdateManager::ParseCnbSchemeReleases(cnb_releases,
                                                          &scheme_release),
            "valid CNB release list was rejected");
    Require(scheme_release.tag == L"v18.0.0" && scheme_release.from_cnb &&
                scheme_release.size == 32212731 &&
                scheme_release.sha256 ==
                    L"f1c6e330c822b667f39d0ee596fa6069550bd2fac7e6cb5e227510c0a"
                    L"25314c3",
            "CNB parser did not select the highest stable verified release");
    const std::string github_releases =
        R"([{"tag_name":"v18.0.0","draft":false,"prerelease":false,"assets":[{"name":"rime-wanxiang-lite.zip","digest":"sha256:e530647357b02dcc84b8b1e0ae7c12c792a4f55f4c7b28f8a5c1aa981d999709","size":32212622}]}])";
    Require(WanxiangUpdateManager::ParseGithubSchemeReleases(github_releases,
                                                             &scheme_release) &&
                !scheme_release.from_cnb && scheme_release.tag == L"v18.0.0",
            "valid GitHub fallback metadata was rejected");

    WanxiangUpdateManager::SaveLastRelease(L"v18.0.0");
    WanxiangUpdateManager::Result cached;
    Require(WanxiangUpdateManager::LoadCachedResult(&cached),
            "scheme cache depended on model metadata");
    Require(cached.scheme_update_available && cached.update_available &&
                !cached.model_update_available,
            "cached scheme update was discarded without model metadata");

    const std::string uppercase_digest(64, 'A');
    WanxiangUpdateManager::ModelRelease release;
    Require(WanxiangUpdateManager::ParseModelRelease(
                Asset("sha256", uppercase_digest, "420343852"), &release),
            "valid CNB model metadata was rejected");
    Require(release.sha256 == std::wstring(64, L'a'),
            "model checksum was not normalized");
    Require(release.size == 420343852, "model size was not parsed exactly");
    WanxiangUpdateManager::SaveLastModel(release);
    WanxiangUpdateManager::SaveLastRelease(L"v18.0.1");
    std::wstring stale_sha256;
    unsigned long long stale_size = 0;
    Require(!WanxiangUpdateManager::LoadLastModelMetadata(&stale_sha256,
                                                          &stale_size),
            "a fresh scheme check retained stale model metadata");
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
    if (argc > 1 && std::string(argv[1]) == "--network") {
      const auto result = WanxiangUpdateManager::CheckNow();
      Require(result.success, "live release check failed");
      Require(result.latest_scheme_from_cnb,
              "live release check did not prefer CNB");
      Require(!result.latest_scheme_url.empty() &&
                  result.latest_scheme_size != 0 &&
                  result.latest_scheme_sha256.size() == 64,
              "live scheme metadata was incomplete");
      Require(WanxiangUpdateManager::IsNewerVersion(
                  result.latest_tag, WanxiangUpdateManager::kInstalledVersion),
              "live release check did not find a newer stable version");
      std::wcout << L"Live release check found " << result.latest_tag << L'\n';
    }
    std::cout << "Update manager tests passed\n";
    std::filesystem::remove_all(test_user_directory);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

#include "WanxiangModelManager.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

std::filesystem::path test_user_directory;

namespace {
void Require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

void Write(const std::filesystem::path& path, char value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  const std::string bytes(WanxiangModelManager::kExpectedSize, value);
  output.write(bytes.data(), bytes.size());
  Require(output.good(), "fixture write failed");
}

char FirstByte(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  char byte = 0;
  input.get(byte);
  Require(input.good(), "installed file read failed");
  return byte;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 3)
    return 2;
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  try {
    const std::filesystem::path root(argv[1]);
    test_user_directory = root / L"user";
    const auto staging = std::filesystem::path(argv[2]) / L"model.part";
    const auto model = test_user_directory / L"wanxiang-lts-zh-hans.gram";
    std::wstring error;

    // Exercise the production install/commit implementation with a small,
    // pinned fixture; only artifact constants and external IPC are substituted.
    {
      WanxiangModelManager manager;
      Require(manager.CachePath().wstring().find(
                  test_user_directory.wstring()) == std::wstring::npos,
              "download cache must be independent of the Rime user directory");
      Write(staging, 'Z');
      manager.job_path_ = staging;
      Require(manager.CompleteDownload(&error),
              "completed download could not be verified");
      Require(!std::filesystem::exists(model),
              "completing a download installed the model early");
      Require(manager.GetProgress().state ==
                  WanxiangModelManager::State::Transferred,
              "verified download was not reported as ready to install");
      Require(manager.CompleteAndInstall(&error), "fresh model install failed");
      Require(FirstByte(model) == 'Z', "wrong installed content");
      Require(manager.Commit(&error), "model commit failed");
      Require(!std::filesystem::exists(staging),
              "committed cache was not cleaned");
      Require(!std::filesystem::exists(manager.BackupPath()), "backup leaked");
    }
    {
      WanxiangModelManager manager;
      Write(model, 'A');
      Write(staging, 'Z');
      manager.job_path_ = staging;
      manager.ready_to_install_ = true;
      Require(manager.CompleteAndInstall(&error), "replacement failed");
      Require(manager.Rollback(&error), "deployment rollback failed");
      Require(FirstByte(model) == 'A', "rollback lost the original model");
      Require(std::filesystem::exists(staging),
              "rollback discarded retry cache");
      Require(manager.GetProgress().state ==
                  WanxiangModelManager::State::Transferred,
              "rollback did not preserve a reusable verified download");
    }
    {
      WanxiangModelManager manager;
      Write(staging, 'X');
      manager.job_path_ = staging;
      manager.ready_to_install_ = true;
      Require(!manager.CompleteAndInstall(&error),
              "corrupt cache was installed");
      Require(FirstByte(model) == 'A',
              "checksum failure changed original model");
    }
    {
      WanxiangModelManager manager;
      Write(staging, 'Z');
      std::ofstream(model, std::ios::binary) << "custom model";
      manager.job_path_ = staging;
      manager.ready_to_install_ = true;
      Require(!manager.CompleteAndInstall(&error),
              "custom model was overwritten");
      Require(FirstByte(model) == 'c', "custom model content changed");
      Require(std::filesystem::exists(staging),
              "cache lost after install failure");
      std::filesystem::remove(model);
      Require(manager.Start(&error), "verified cache retry failed");
      Require(!manager.job_, "cache retry started another download");
      Require(manager.CompleteAndInstall(&error), "cache reinstall failed");
      Require(manager.Commit(&error), "cache reinstall commit failed");
    }
    {
      const auto legacy_cache = test_user_directory / L".weasel-packages" /
                                L"wanxiang-lts-zh-hans.gram.part";
      {
        WanxiangModelManager manager;
        Write(legacy_cache, 'Z');
        manager.job_path_ = legacy_cache;
        manager.ready_to_install_ = true;
        Require(manager.CompleteAndInstall(&error),
                "cleanup fixture install failed");
        HANDLE lock = ::CreateFileW(legacy_cache.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(lock != INVALID_HANDLE_VALUE, "cache lock failed");
        const bool committed = manager.Commit(&error);
        ::CloseHandle(lock);
        Require(committed, "cleanup failure reversed successful deployment");
        Require(std::filesystem::exists(legacy_cache),
                "locked cache disappeared");
      }
      WanxiangModelManager manager;
      Require(!std::filesystem::exists(legacy_cache),
              "pending cleanup did not retry");
      Require(FirstByte(model) == 'Z', "cache cleanup changed installed model");
    }
    {
      WanxiangModelManager manager;
      Require(manager.RemoveInstalled(&error), "remove model failed");
      // Simulate process exit after replacement but before transaction commit.
      Write(model, 'X');
    }
    {
      WanxiangModelManager manager;
      Require(FirstByte(model) == 'Z',
              "interrupted transaction recovery failed");
    }
    {
      WanxiangModelManager manager;
      Require(manager.RemoveInstalled(&error), "committed remove failed");
      Require(manager.Commit(&error), "remove commit failed");
      Require(!std::filesystem::exists(model),
              "committed removal left the model installed");
      Require(manager.GetProgress().state ==
                  WanxiangModelManager::State::NotInstalled,
              "committed removal kept an installed model state");
    }
    std::cout << "Model transaction tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  ::CoUninitialize();
  return 0;
}

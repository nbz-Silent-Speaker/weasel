#pragma once

class UIStyleSettings;
namespace weasel {
enum class ColorSchemeTarget;
}

class Configurator {
 public:
  explicit Configurator();

  void Initialize();
  int Run(bool installing);
  int ConfigureColorScheme(weasel::ColorSchemeTarget target);
  int UpdateWorkspace(bool report_errors = false);
  int DictManagement();
  int SyncUserData();
};

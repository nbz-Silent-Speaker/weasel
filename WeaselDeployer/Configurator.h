#pragma once

class UIStyleSettings;
namespace weasel {
enum class ColorSchemeTarget;
}
namespace settings_navigation {
enum class Page;
}

class Configurator {
 public:
  explicit Configurator();

  void Initialize();
  int Run(bool installing);
  int ConfigureColorScheme(weasel::ColorSchemeTarget target);
  int ConfigureFonts();
  int ConfigureStatusIcons();
  int UpdateWorkspace(bool report_errors = false);
  int DictManagement();
  int SyncUserData();

 private:
  int ConfigureSettings(settings_navigation::Page initial_page);
};

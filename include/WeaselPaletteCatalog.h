#pragma once

#include <rime_api.h>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace weasel {

enum class PaletteTheme { Unspecified, Light, Dark };

struct PaletteScheme {
  // The editor identity includes the source; the persisted scheme ID does not.
  std::string color_scheme_id;
  std::string name;
  std::string author;
  bool custom = false;
  std::string variant;
  PaletteTheme theme = PaletteTheme::Unspecified;
};

struct PaletteGroup {
  std::string name;
  std::string light;
  std::string dark;
  bool custom = false;
};

inline bool PaletteMatchesTheme(PaletteTheme theme, bool dark) {
  return theme == PaletteTheme::Unspecified ||
         theme == (dark ? PaletteTheme::Dark : PaletteTheme::Light);
}

std::string PaletteId(const std::string& selection);
std::string PaletteSource(const std::string& selection);
const char* AppearanceColorKey(size_t index);
std::string PaletteValue(RimeApi* api,
                         RimeConfig* config,
                         const std::string& path);

// Read only raw files. Keep separate source trees even when their IDs match.
class PaletteCatalog {
 public:
  PaletteCatalog() = default;
  ~PaletteCatalog();
  PaletteCatalog(const PaletteCatalog&) = delete;
  PaletteCatalog& operator=(const PaletteCatalog&) = delete;

  void Clear();
  bool LoadFiles(RimeApi* api,
                 const std::filesystem::path& shared,
                 const std::filesystem::path& user);
  bool Load(RimeApi* api,
            const std::string& shared,
            const std::string& user,
            const std::string& custom);
  void ReadStyle(RimeConfig* config);

  const std::vector<PaletteScheme>& schemes() const { return schemes_; }
  const std::vector<PaletteGroup>& groups() const { return groups_; }
  RimeConfig* Config(const std::string& selection);
  const PaletteScheme* Find(const std::string& selection) const;
  std::string Resolve(const std::string& id, const std::string& source) const;
  std::string ExplicitSelection(RimeConfig* config, size_t index) const;

 private:
  static bool ValidId(const std::string& id);
  void MergeMap(RimeConfig* from,
                const std::string& path,
                RimeConfig* to,
                const std::string& destination);
  void ApplyPatches(RimeConfig* target, const std::string& table);
  void Build();

  RimeApi* api_ = nullptr;
  RimeConfig base_{};
  RimeConfig custom_{};
  RimeConfig raw_custom_{};
  std::vector<PaletteScheme> schemes_;
  std::vector<PaletteGroup> groups_;
};

}  // namespace weasel

#include "../SettingsColor.h"
#include <cstdlib>
#include <iostream>

using namespace settings_color;
void Check(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
int main() {
  for (auto input :
       {L"#0A9DA1", L"0a9da1", L"rgb(10,157,161)", L"rgba(10,157,161,1)",
        L"#0A9DA1FF", L"0xA19D0A", L"0xFFA19D0A"})
    Check(Parse(input) == std::optional<Color>(Color{}),
          "Equivalent input formats differ");
  Check(Parse(L"#f00") == std::optional<Color>(Color{255, 0, 0}), "short HEX");
  Check(Parse(L"rgb(100%,0%,0%)") == std::optional<Color>(Color{255, 0, 0}),
        "RGB percent");
  Check(Parse(L"hsl(120,100%,50%)") == std::optional<Color>(Color{0, 255, 0}),
        "HSL green");
  Check(Parse(L"hsv(240,100%,100%)") == std::optional<Color>(Color{0, 0, 255}),
        "HSV blue");
  Check(
      Parse(L"cmyk(100%,0%,0%,0%)") == std::optional<Color>(Color{0, 255, 255}),
      "CMYK cyan");
  for (auto invalid :
       {L"", L"#", L"rgb()", L"#gg0000", L"rgb(256,0,0)", L"rgb(-1,0,0)",
        L"rgba(0,0,0,0.5)", L"0x80000000", L"#ffffff00", L"rgb(1,2,3,)",
        L"rgb(1,2)", L"rgb(1,2,3)junk", L"hsv(361,100,100)", L"cmyk(0,0,0,101)",
        L"hsl(20,120%,50%)"})
    Check(!Parse(invalid), "Invalid/incomplete input accepted");
  Check(!DarkText({}), "Default accent must use white text");
  Check(DarkText({255, 255, 255}), "White accent must use dark text");
  Check(!DarkText({0, 0, 0}), "Black accent must use white text");
  for (int r = 0; r <= 255; r += 17)
    for (int g = 0; g <= 255; g += 17)
      for (int b = 0; b <= 255; b += 17) {
        Color c{r, g, b};
        Check(Parse(Hex(c)) == std::optional<Color>(c), "HEX round trip");
        for (Model model : {Model::Hsv, Model::Hsl, Model::Cmyk})
          Check(FromValues(model, Values(c, model)) == std::optional<Color>(c),
                "Model round trip");
      }
  Check(FromValues(Model::Cmyk, {0, 0, 0, 100}) ==
            std::optional<Color>(Color{0, 0, 0}),
        "CMYK black singularity");
  std::cout << "Settings colors: parse validation, contrast, 4096 RGB samples "
               "x 4 round trips passed.\n";
}

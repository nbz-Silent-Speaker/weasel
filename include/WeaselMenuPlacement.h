#pragma once

#include <windows.h>
#include <shellapi.h>
#include <algorithm>

namespace weasel {

struct MenuPlacement {
  POINT anchor;
  UINT flags;
};

// Use signed screen coordinates: secondary monitors can have negative origins.
inline MenuPlacement PlaceMenu(POINT point, const RECT& work, int edge = -1) {
  MenuPlacement result{point, TPM_RIGHTBUTTON};
  result.anchor.x = (std::max)(work.left, (std::min)(point.x, work.right - 1));
  result.anchor.y = (std::max)(work.top, (std::min)(point.y, work.bottom - 1));
  const bool align_right =
      edge == ABE_RIGHT ||
      (edge != ABE_LEFT && point.x - work.left > work.right - point.x);
  result.flags |= align_right ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
  result.flags |=
      edge == ABE_BOTTOM ||
              (edge != ABE_TOP && point.y - work.top > work.bottom - point.y)
          ? TPM_BOTTOMALIGN
          : TPM_TOPALIGN;
  result.flags |=
      edge == ABE_LEFT || edge == ABE_RIGHT ? TPM_HORIZONTAL : TPM_VERTICAL;
  return result;
}

inline UINT TrackTrayMenu(HMENU menu,
                          POINT point,
                          HWND owner,
                          UINT flags,
                          const RECT* icon = nullptr) {
  MONITORINFO info{sizeof(info)};
  HMONITOR monitor = ::MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
  if (!::GetMonitorInfoW(monitor, &info))
    return ::TrackPopupMenuEx(menu, flags, point.x, point.y, owner, nullptr);
  int edge = -1;
  APPBARDATA bar{sizeof(bar)};
  if (::SHAppBarMessage(ABM_GETTASKBARPOS, &bar) &&
      ::MonitorFromRect(&bar.rc, MONITOR_DEFAULTTONULL) == monitor &&
      ::PtInRect(&bar.rc, point)) {
    edge = static_cast<int>(bar.uEdge);
  } else {
    // Works for secondary taskbars too. With auto-hide, fall back to the
    // trigger position and let USER32 fit the popup to this monitor.
    if (point.x < info.rcWork.left)
      edge = ABE_LEFT;
    else if (point.x >= info.rcWork.right)
      edge = ABE_RIGHT;
    else if (point.y < info.rcWork.top)
      edge = ABE_TOP;
    else if (point.y >= info.rcWork.bottom)
      edge = ABE_BOTTOM;
  }
  const auto placement = PlaceMenu(point, info.rcWork, edge);
  // Anchor the root popup only. USER32 fits submenus to the available space;
  // MFT_RIGHTORDER is RTL language layout, not a taskbar placement control.
  TPMPARAMS params{sizeof(params)};
  if (icon)
    params.rcExclude = *icon;
  return ::TrackPopupMenuEx(menu, flags | placement.flags, placement.anchor.x,
                            placement.anchor.y, owner,
                            icon ? &params : nullptr);
}

}  // namespace weasel

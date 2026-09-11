#pragma once

#include <windows.h>
#include <shellapi.h>
#include <algorithm>

namespace weasel {

struct MenuPlacement {
  POINT anchor;
  UINT flags;
  bool cascade_left;
};

// Use signed screen coordinates: secondary monitors can have negative origins.
inline MenuPlacement PlaceMenu(POINT point, const RECT& work, int edge = -1) {
  MenuPlacement result{point, TPM_RIGHTBUTTON, false};
  result.anchor.x = (std::max)(work.left, (std::min)(point.x, work.right - 1));
  result.anchor.y = (std::max)(work.top, (std::min)(point.y, work.bottom - 1));
  result.cascade_left =
      edge == ABE_RIGHT ||
      (edge != ABE_LEFT && point.x - work.left > work.right - point.x);
  result.flags |= result.cascade_left ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
  result.flags |=
      edge == ABE_BOTTOM ||
              (edge != ABE_TOP && point.y - work.top > work.bottom - point.y)
          ? TPM_BOTTOMALIGN
          : TPM_TOPALIGN;
  result.flags |=
      edge == ABE_LEFT || edge == ABE_RIGHT ? TPM_HORIZONTAL : TPM_VERTICAL;
  return result;
}

inline void SetMenuCascade(HMENU menu, bool left) {
  for (int i = 0; i < ::GetMenuItemCount(menu); ++i) {
    MENUITEMINFOW item{sizeof(item)};
    item.fMask = MIIM_FTYPE | MIIM_SUBMENU;
    if (!::GetMenuItemInfoW(menu, i, TRUE, &item) || !item.hSubMenu)
      continue;
    item.fType =
        left ? item.fType | MFT_RIGHTORDER : item.fType & ~MFT_RIGHTORDER;
    ::SetMenuItemInfoW(menu, i, TRUE, &item);
    SetMenuCascade(item.hSubMenu, left);
  }
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
  SetMenuCascade(menu, placement.cascade_left);
  TPMPARAMS params{sizeof(params)};
  if (icon)
    params.rcExclude = *icon;
  return ::TrackPopupMenuEx(menu, flags | placement.flags, placement.anchor.x,
                            placement.anchor.y, owner,
                            icon ? &params : nullptr);
}

}  // namespace weasel

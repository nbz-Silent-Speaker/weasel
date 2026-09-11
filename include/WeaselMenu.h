#pragma once

#include <Windows.h>
#include <ctfutb.h>
#include <new>
#include <vector>

namespace weasel {

inline bool SetMenuCommandChecked(HMENU menu, UINT command, bool checked) {
  const int count = ::GetMenuItemCount(menu);
  for (int i = 0; i < count; ++i) {
    if (::GetMenuItemID(menu, i) == command)
      return ::CheckMenuItem(
                 menu, i,
                 MF_BYPOSITION | (checked ? MF_CHECKED : MF_UNCHECKED)) !=
             static_cast<DWORD>(-1);
    if (HMENU child = ::GetSubMenu(menu, i)) {
      if (SetMenuCommandChecked(child, command, checked))
        return true;
    }
  }
  return false;
}

// Both language-bar menu entry points retain nested groups and checked state.
inline HRESULT CopyMenuToTfMenu(HMENU menu, ITfMenu* target) {
  if (!target)
    return E_POINTER;
  const int count = ::GetMenuItemCount(menu);
  if (count < 0)
    return E_INVALIDARG;
  try {
    for (int i = 0; i < count; ++i) {
      MENUITEMINFOW item = {};
      item.cbSize = sizeof(item);
      item.fMask =
          MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_SUBMENU | MIIM_STATE;
      if (!::GetMenuItemInfoW(menu, i, TRUE, &item))
        return E_FAIL;
      DWORD flags = 0;
      if (item.fType & MFT_SEPARATOR)
        flags |= TF_LBMENUF_SEPARATOR;
      if ((item.fType & MFT_RADIOCHECK) && (item.fState & MFS_CHECKED))
        flags |= TF_LBMENUF_RADIOCHECKED;
      if (item.fState & MFS_CHECKED)
        flags |= TF_LBMENUF_CHECKED;
      if (item.fState & MFS_DISABLED)
        flags |= TF_LBMENUF_GRAYED;
      if (item.hSubMenu)
        flags |= TF_LBMENUF_SUBMENU;
      std::vector<wchar_t> text(static_cast<size_t>(item.cch) + 1);
      item.dwTypeData = text.data();
      item.cch = static_cast<UINT>(text.size());
      if (!::GetMenuItemInfoW(menu, i, TRUE, &item))
        return E_FAIL;
      ITfMenu* child = nullptr;
      const HRESULT added = target->AddMenuItem(
          item.wID, flags, nullptr, nullptr,
          flags & TF_LBMENUF_SEPARATOR ? nullptr : text.data(),
          flags & TF_LBMENUF_SEPARATOR ? 0 : item.cch,
          item.hSubMenu ? &child : nullptr);
      if (FAILED(added)) {
        if (child)
          child->Release();
        return added;
      }
      if (item.hSubMenu) {
        if (!child)
          return E_UNEXPECTED;
        const HRESULT copied = CopyMenuToTfMenu(item.hSubMenu, child);
        child->Release();
        if (FAILED(copied))
          return copied;
      }
    }
  } catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
  }
  return S_OK;
}

}  // namespace weasel

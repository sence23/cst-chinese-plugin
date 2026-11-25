#include "nativeui.h"
#include "catalog.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>

namespace {
bool classIs(HWND window, const wchar_t *expected)
{
    wchar_t name[64] = {};
    return GetClassNameW(window, name, 64) && lstrcmpiW(name, expected) == 0;
}

bool sameThread(HWND window)
{
    return GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId();
}

void replaceCaption(HWND window)
{
    int length = GetWindowTextLengthW(window);
    if (!length || length > 8192)
        return;
    std::vector<wchar_t> text(length + 1);
    const int read = GetWindowTextW(window, text.data(), length + 1);
    const auto old = QString::fromWCharArray(text.data(), read);
    auto value = uiText(old);
    if (!value.isNull() && value != old && IsWindow(window) && sameThread(window))
        SetWindowTextW(window, reinterpret_cast<const wchar_t *>(value.utf16()));
}

BOOL CALLBACK translateChild(HWND window, LPARAM)
{
    if (!sameThread(window))
        return TRUE;
    if (classIs(window, L"#32770")) {
        replaceCaption(window);
        return TRUE;
    }
    HWND parent = GetParent(window);
    while (parent && !classIs(parent, L"#32770")) {
        wchar_t name[64] = {};
        GetClassNameW(parent, name, 64);
        if (classIs(parent,L"Edit") || classIs(parent,L"ComboBox") ||
            classIs(parent,L"ComboBoxEx32") || classIs(parent,L"SysListView32") ||
            classIs(parent,L"SysTreeView32") || _wcsnicmp(name,L"RichEdit",8) == 0)
            return TRUE;
        parent = GetParent(parent);
    }
    if (!parent || !sameThread(parent))
        return TRUE;
    const auto style = GetWindowLongPtrW(window, GWL_STYLE);
    if (classIs(window, L"Static")) {
        auto kind = style & SS_TYPEMASK;
        if (kind == SS_LEFT || kind == SS_CENTER || kind == SS_RIGHT ||
            kind == SS_SIMPLE || kind == SS_LEFTNOWORDWRAP)
            replaceCaption(window);
    } else if (classIs(window, L"Button") && (style & BS_TYPEMASK) != BS_OWNERDRAW &&
               !(style & (BS_BITMAP | BS_ICON))) {
        replaceCaption(window);
    }
    return TRUE;
}

BOOL CALLBACK translateDialog(HWND window, LPARAM)
{
    if (sameThread(window) && classIs(window, L"#32770"))
        replaceCaption(window);
    if (IsWindow(window))
        EnumChildWindows(window, translateChild, 0);
    return TRUE;
}
}

void translateThreadDialogs()
{
    // Win32 dialogs inside this Qt host only. No global hook or cross-process window writes.
    EnumWindows(translateDialog, 0);
}

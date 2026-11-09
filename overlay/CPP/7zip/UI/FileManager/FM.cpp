// FM.cpp

#include "StdAfx.h"

#include "../../../Common/MyWindows.h"

#if defined(__MINGW32__) || defined(__MINGW64__)
#include <shlwapi.h>
#else
#include <Shlwapi.h>
#endif

#include "../../../../C/Compiler.h"
#include "../../../../C/Alloc.h"
#ifdef _WIN32
#include "../../../../C/DllSecur.h"
#endif

#include "../../../Common/StringConvert.h"
#include "../../../Common/StringToInt.h"

#include "../../../Windows/ErrorMsg.h"
#include "../../../Windows/MemoryLock.h"
#include "../../../Windows/NtCheck.h"
#include "../../../Windows/System.h"

#ifndef UNDER_CE
#include "../../../Windows/SecurityUtils.h"
#endif

#include "../GUI/ExtractRes.h"

#include "resource.h"

#include "App.h"
#include "FormatUtils.h"
#include "LangUtils.h"
#include "MyLoadMenu.h"
#include "Panel.h"
#include "RegistryUtils.h"
#include "StringUtils.h"
#include "ViewSettings.h"

using namespace NWindows;
using namespace NFile;
using namespace NFind;

// #define MAX_LOADSTRING 100

extern
bool g_DisableUserQuestions;
bool g_DisableUserQuestions;

extern
bool g_RAM_Size_Defined;
bool g_RAM_Size_Defined;

extern
bool g_LargePagesMode;
bool g_LargePagesMode = false;
// static bool g_OpenArchive = false;

#ifdef UNDER_CE
static bool g_Maximized = false;
#endif

extern
size_t g_RAM_Size;
size_t g_RAM_Size;

#ifdef _WIN32
extern
HINSTANCE g_hInstance;
HINSTANCE g_hInstance;
#endif

HWND g_HWND;

static UString g_MainPath;
static UString g_ArcFormat;

// HRESULT LoadGlobalCodecs();
void FreeGlobalCodecs();

#ifndef UNDER_CE

#ifdef Z7_USE_DYN_ComCtl32Version
Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION

DWORD g_ComCtl32Version;

static DWORD GetDllVersion(LPCTSTR dllName)
{
  DWORD dwVersion = 0;
  const HMODULE hmodule = LoadLibrary(dllName);
  if (hmodule)
  {
    const
     DLLGETVERSIONPROC f_DllGetVersion = Z7_GET_PROC_ADDRESS(
     DLLGETVERSIONPROC, hmodule,
    "DllGetVersion");
    if (f_DllGetVersion)
    {
      DLLVERSIONINFO dvi;
      ZeroMemory(&dvi, sizeof(dvi));
      dvi.cbSize = sizeof(dvi);
      const HRESULT hr = f_DllGetVersion(&dvi);
      if (SUCCEEDED(hr))
        dwVersion = (DWORD)MAKELONG(dvi.dwMinorVersion, dvi.dwMajorVersion);
    }
    FreeLibrary(hmodule);
  }
  return dwVersion;
}

#endif
#endif

bool g_IsSmallScreen = false;

extern
bool g_LVN_ITEMACTIVATE_Support;
bool g_LVN_ITEMACTIVATE_Support = true;
// LVN_ITEMACTIVATE replaces both NM_DBLCLK & NM_RETURN
// Windows 2000
// NT/98 + IE 3 (g_ComCtl32Version >= 4.70)


static const int kNumDefaultPanels = 1;
static const int kSplitterWidth = 4;
static const int kSplitterRateMax = 1 << 16;
static const int kPanelSizeMin = 120;


class CSplitterPos
{
  int _ratio; // 10000 is max
  int _pos;
  int _fullWidth;
  void SetRatioFromPos(HWND hWnd)
    { _ratio = (_pos + kSplitterWidth / 2) * kSplitterRateMax /
        MyMax(GetWidth(hWnd), 1); }
public:
  int GetPos() const
    { return _pos; }
  int GetWidth(HWND hWnd) const
  {
    RECT rect;
    ::GetClientRect(hWnd, &rect);
    return rect.right;
  }
  void SetRatio(HWND hWnd, int aRatio)
  {
    _ratio = aRatio;
    SetPosFromRatio(hWnd);
  }
  void SetPosPure(HWND hWnd, int pos)
  {
    int posMax = GetWidth(hWnd) - kSplitterWidth;
    if (posMax < kPanelSizeMin * 2)
      pos = posMax / 2;
    else
    {
      if (pos > posMax - kPanelSizeMin)
        pos = posMax - kPanelSizeMin;
      else if (pos < kPanelSizeMin)
        pos = kPanelSizeMin;
    }
    _pos = pos;
  }
  void SetPos(HWND hWnd, int pos)
  {
    _fullWidth = GetWidth(hWnd);
    SetPosPure(hWnd, pos);
    SetRatioFromPos(hWnd);
  }
  void SetPosFromRatio(HWND hWnd)
  {
    int fullWidth = GetWidth(hWnd);
    if (_fullWidth != fullWidth && fullWidth != 0)
    {
      _fullWidth = fullWidth;
      SetPosPure(hWnd, GetWidth(hWnd) * _ratio / kSplitterRateMax - kSplitterWidth / 2);
    }
  }
};

static bool g_CanChangeSplitter = false;
static UInt32 g_SplitterPos = 0;
static CSplitterPos g_Splitter;
static bool g_PanelsInfoDefined = false;
static bool g_WindowWasCreated = false;

static int g_StartCaptureMousePos;
static int g_StartCaptureSplitterPos;

CApp g_App;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// 7-Zip Modern ---------------------------------------------------------------
// macOS-style window chrome. The standard caption is removed by collapsing
// the non-client area in WM_NCCALCSIZE, and the top strip of the client area
// is painted by hand: traffic-light window buttons, the menu bar as clickable
// labels and a centred title. Dragging, double click maximising, Aero Snap
// and the system menu keep working through the HTCAPTION hit-test code.
// ------------------------------------------------------------------------------

#ifndef SM_CXPADDEDBORDER // available since Vista; the SDK target is older
#define SM_CXPADDEDBORDER 92
#endif

struct CModernChrome
{
  bool Active;
  bool MouseInside;
  int HotLight;  // window button under the mouse (0..2), -1 when none
  int HotMenu;   // menu label under the mouse, -1 when none
  int OpenMenu;  // popup currently being tracked, -1 when none
};

static CModernChrome g_Chrome = { false, false, -1, -1, -1 };

static RECT Chrome_LightRect(int i)
{
  RECT r;
  r.left = NModernUI::LightLeft() +
      i * (NModernUI::LightSize() + NModernUI::LightGap());
  r.top = (NModernUI::CaptionHeight() - NModernUI::LightSize()) / 2;
  r.right = r.left + NModernUI::LightSize();
  r.bottom = r.top + NModernUI::LightSize();
  return r;
}

static RECT Chrome_CaptionRect(HWND hWnd)
{
  RECT rc;
  ::GetClientRect(hWnd, &rc);
  rc.bottom = rc.top + NModernUI::CaptionHeight();
  return rc;
}

static int Chrome_MenuLeft()
{
  return NModernUI::LightLeft() +
      3 * (NModernUI::LightSize() + NModernUI::LightGap()) +
      NModernUI::Scale(10);
}

// The menu resource marks shortcuts as "文件(&F)"; the in-window menu bar
// shows clean labels instead: "文件". "&&" survives as a literal '&'.
static void Chrome_MenuLabel(const wchar_t *src, wchar_t *dst, unsigned cap)
{
  unsigned o = 0;
  for (unsigned i = 0; src[i] && o + 1 < cap; i++)
  {
    if (src[i] == L'&')
    {
      if (src[i + 1] == L'&')
      {
        dst[o++] = L'&';
        i++;
        continue;
      }
      // "(&X)" / "[&X]": drop the whole bracket group with the shortcut
      if (i > 0 && (src[i - 1] == L'(' || src[i - 1] == L'['))
      {
        const wchar_t close = (src[i - 1] == L'(') ? L')' : L']';
        if (src[i + 2] == close)
        {
          i += 2;
          o--; // remove the opening bracket written in the previous pass
          continue;
        }
      }
      continue; // bare accelerator marker
    }
    dst[o++] = src[i];
  }
  dst[o] = 0;
}

// The same width measurement drives painting and hit testing, so the two can
// never drift apart.
static int Chrome_MenuItemWidth(HWND hWnd, int index)
{
  wchar_t buf[128];
  buf[0] = 0;
  const HMENU menu = ::GetMenu(hWnd);
  if (!menu)
    return 0;
  ::GetMenuStringW(menu, index, buf, 128, MF_BYPOSITION);
  wchar_t label[128];
  Chrome_MenuLabel(buf, label, 128);
  HDC dc = ::GetDC(hWnd);
  SIZE sz;
  sz.cx = 0;
  HGDIOBJ old = ::SelectObject(dc, NModernUI::Font_Bar());
  ::GetTextExtentPoint32W(dc, label, lstrlenW(label), &sz);
  ::SelectObject(dc, old);
  ::ReleaseDC(hWnd, dc);
  return sz.cx + NModernUI::Scale(16);
}

static int Chrome_MenuAt(HWND hWnd, int x, int y)
{
  if (y < 0 || y >= NModernUI::CaptionHeight())
    return -1;
  const HMENU menu = ::GetMenu(hWnd);
  if (!menu)
    return -1;
  int cx = Chrome_MenuLeft();
  const int count = ::GetMenuItemCount(menu);
  for (int i = 0; i < count; i++)
  {
    const int w = Chrome_MenuItemWidth(hWnd, i);
    if (x >= cx && x < cx + w)
      return i;
    cx += w;
  }
  return -1;
}

static int Chrome_LightAt(int x, int y)
{
  for (int i = 0; i < 3; i++)
  {
    const RECT r = Chrome_LightRect(i);
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
      return i;
  }
  return -1;
}

static void Chrome_Invalidate(HWND hWnd)
{
  const RECT rc = Chrome_CaptionRect(hWnd);
  ::InvalidateRect(hWnd, &rc, FALSE);
}

static void Chrome_TrackMouse(HWND hWnd)
{
  TRACKMOUSEEVENT tme;
  tme.cbSize = sizeof(tme);
  tme.dwFlags = TME_LEAVE;
  tme.hwndTrack = hWnd;
  tme.dwHoverTime = 0;
  ::TrackMouseEvent(&tme);
}

static void Chrome_OpenMenu(HWND hWnd, int index)
{
  const HMENU menu = ::GetMenu(hWnd);
  if (!menu)
    return;
  const HMENU sub = ::GetSubMenu(menu, index);
  if (!sub)
    return;
  int x = Chrome_MenuLeft();
  for (int i = 0; i < index; i++)
    x += Chrome_MenuItemWidth(hWnd, i);
  POINT pt = { x, NModernUI::CaptionHeight() - NModernUI::Scale(2) };
  ::ClientToScreen(hWnd, &pt);

  ::SetForegroundWindow(hWnd);
  g_Chrome.OpenMenu = index;
  g_Chrome.HotMenu = index;
  Chrome_Invalidate(hWnd);

  // The chosen command comes back as an ordinary WM_COMMAND; the flag has to
  // be reset no matter how the popup was dismissed.
  ::TrackPopupMenuEx(sub, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON,
      pt.x, pt.y, hWnd, NULL);

  g_Chrome.OpenMenu = -1;
  g_Chrome.HotMenu = -1;
  Chrome_Invalidate(hWnd);
}

// The window title carries the full path; the caption shows the leaf like the
// Finder does.
static void Chrome_TitleText(HWND hWnd, wchar_t *buf, unsigned cap)
{
  wchar_t full[1024];
  full[0] = 0;
  ::GetWindowTextW(hWnd, full, 1024);
  int end = lstrlenW(full);
  while (end > 0 && (full[end - 1] == L'\\' || full[end - 1] == L'/'))
    end--;
  int start = end;
  while (start > 0 && full[start - 1] != L'\\' && full[start - 1] != L'/')
    start--;
  if (end <= start)
  {
    lstrcpynW(buf, full, (int)cap);
    return;
  }
  unsigned n = (unsigned)(end - start);
  if (n > cap - 1)
    n = cap - 1;
  lstrcpynW(buf, full + start, (int)(n + 1));
}

static COLORREF Chrome_Darken(COLORREF c, double f)
{
  return RGB((int)(GetRValue(c) * f), (int)(GetGValue(c) * f),
      (int)(GetBValue(c) * f));
}

static void Chrome_DrawLightGlyph(HDC dc, const RECT &r, int light, COLORREF col)
{
  HPEN pen = ::CreatePen(PS_SOLID, NModernUI::Scale(1), col);
  HGDIOBJ oldPen = ::SelectObject(dc, pen);
  const int cx = (r.left + r.right) / 2;
  const int cy = (r.top + r.bottom) / 2;
  const int g = NModernUI::Scale(3); // glyph radius
  if (light == 0) // close: two diagonals
  {
    ::MoveToEx(dc, cx - g, cy - g, NULL);
    ::LineTo(dc, cx + g, cy + g);
    ::MoveToEx(dc, cx + g, cy - g, NULL);
    ::LineTo(dc, cx - g, cy + g);
  }
  else if (light == 1) // minimise: a single dash
  {
    ::MoveToEx(dc, cx - g, cy, NULL);
    ::LineTo(dc, cx + g, cy);
  }
  else // maximise: two triangles pointing outward on the diagonal
  {
    const POINT tri1[3] = { { cx - g, cy + g / 2 }, { cx - g / 2, cy + g }, { cx - g / 2, cy + g / 2 } };
    const POINT tri2[3] = { { cx + g, cy - g / 2 }, { cx + g / 2, cy - g }, { cx + g / 2, cy - g / 2 } };
    HBRUSH br = ::CreateSolidBrush(col);
    HGDIOBJ oldBr = ::SelectObject(dc, br);
    ::Polygon(dc, tri1, 3);
    ::Polygon(dc, tri2, 3);
    ::SelectObject(dc, oldBr);
    ::DeleteObject(br);
  }
  ::SelectObject(dc, oldPen);
  ::DeleteObject(pen);
}

static void Chrome_Paint(HWND hWnd, HDC dc)
{
  const RECT band = Chrome_CaptionRect(hWnd);
  NModernUI::FillSolid(dc, band, NModernUI::Col_Band());

  // traffic lights
  for (int i = 0; i < 3; i++)
  {
    COLORREF col;
    if (i == 0)
      col = NModernUI::Col_LightClose();
    else if (i == 1)
      col = NModernUI::Col_LightMin();
    else
      col = NModernUI::Col_LightMax();
    if (!g_Chrome.Active)
      col = NModernUI::Col_LightOff();
    const RECT r = Chrome_LightRect(i);
    HBRUSH br = ::CreateSolidBrush(col);
    HGDIOBJ oldBr = ::SelectObject(dc, br);
    HGDIOBJ oldPen = ::SelectObject(dc, ::GetStockObject(NULL_PEN));
    ::Ellipse(dc, r.left, r.top, r.right, r.bottom);
    ::SelectObject(dc, oldPen);
    ::SelectObject(dc, oldBr);
    ::DeleteObject(br);
    // the glyphs only show while the pointer hovers, like on macOS
    if (g_Chrome.Active && g_Chrome.HotLight == i)
      Chrome_DrawLightGlyph(dc, r, i, Chrome_Darken(col, 0.45));
  }

  // menu bar
  const HMENU menu = ::GetMenu(hWnd);
  if (menu)
  {
    int x = Chrome_MenuLeft();
    const int count = ::GetMenuItemCount(menu);
    for (int i = 0; i < count; i++)
    {
      wchar_t buf[128];
      buf[0] = 0;
      ::GetMenuStringW(menu, i, buf, 128, MF_BYPOSITION);
      wchar_t label[128];
      Chrome_MenuLabel(buf, label, 128);
      const int w = Chrome_MenuItemWidth(hWnd, i);
      RECT ir = band;
      ir.left = x;
      ir.right = x + w;
      const bool lit = (i == g_Chrome.HotMenu) || (i == g_Chrome.OpenMenu);
      if (lit)
        NModernUI::FillRound(dc, ir, NModernUI::Scale(6),
            NModernUI::Col_Accent());
      NModernUI::DrawTextIn(dc, ir, label,
          lit ? RGB(0xFF, 0xFF, 0xFF) : NModernUI::Col_Ink(),
          NModernUI::Font_Bar(),
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      x += w;
    }
  }

  // centred title
  wchar_t title[128];
  Chrome_TitleText(hWnd, title, 128);
  RECT tr = band;
  tr.left = Chrome_MenuLeft();
  NModernUI::DrawTextIn(dc, tr, title,
      g_Chrome.Active ? NModernUI::Col_Ink() : NModernUI::Col_InkDim(),
      NModernUI::Font(100, true),
      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static const wchar_t * const kWindowClass = L"7-Zip::FM";

#ifdef UNDER_CE
#define WS_OVERLAPPEDWINDOW ( \
  WS_OVERLAPPED   | \
  WS_CAPTION      | \
  WS_SYSMENU      | \
  WS_THICKFRAME   | \
  WS_MINIMIZEBOX  | \
  WS_MAXIMIZEBOX)
#endif


/*
typedef HRESULT (WINAPI *Func_SetWindowTheme)(
  HWND    hwnd,
  LPCWSTR pszSubAppName,
  LPCWSTR pszSubIdList
);

typedef BOOL (WINAPI *Func_AllowDarkModeForWindow)(
  HWND a_HWND, BOOL a_Allow);

enum PreferredAppMode
{
  Default,
  AllowDark,
  ForceDark,
  ForceLight,
  Max
};
// ordinal 135, in 1903
typedef BOOL (WINAPI *Func_SetPreferredAppMode)(PreferredAppMode appMode);

typedef HRESULT (WINAPI *Func_DwmSetWindowAttribute)(
       HWND    hwnd,
       DWORD   dwAttribute,
       LPCVOID pvAttribute,
       DWORD   cbAttribute
);
*/

static BOOL InitInstance(int nCmdShow)
{
  CWindow wnd;

  // LoadString(hInstance, IDS_CLASS, windowClass, MAX_LOADSTRING);

  UString title ("7-Zip Modern"); // LangString(IDS_APP_TITLE, 0x03000000);

  /*
  //If it is already running, then focus on the window
  hWnd = FindWindow(windowClass, title);
  if (hWnd)
  {
    SetForegroundWindow ((HWND) (((DWORD)hWnd) | 0x01));
    return 0;
  }
  */

  WNDCLASSW wc;

  // wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.style = 0;
  wc.lpfnWndProc = (WNDPROC) WndProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = g_hInstance;
  wc.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON));

  // wc.hCursor = LoadCursor (NULL, IDC_ARROW);
  wc.hCursor = ::LoadCursor(NULL, IDC_SIZEWE);
  // wc.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
  wc.hbrBackground = ::CreateSolidBrush(NModernUI::Col_Hairline());

  wc.lpszMenuName =
    #ifdef UNDER_CE
    0
    #else
    MAKEINTRESOURCEW(IDM_MENU)
    #endif
    ;

  wc.lpszClassName = kWindowClass;

  if (MyRegisterClass(&wc) == 0)
    return FALSE;

  // RECT rect;
  // GetClientRect(hWnd, &rect);

  DWORD style = WS_OVERLAPPEDWINDOW;
  // DWORD style = 0;
  
  CWindowInfo info;
  info.maximized = false;
  int x, y, xSize, ySize;
  x = y = xSize = ySize = CW_USEDEFAULT;
  bool windowPosIsRead;
  info.Read(windowPosIsRead, g_PanelsInfoDefined);

  if (windowPosIsRead)
  {
    x = info.rect.left;
    y = info.rect.top;
    
    xSize = RECT_SIZE_X(info.rect);
    ySize = RECT_SIZE_Y(info.rect);
  }


  if (g_PanelsInfoDefined)
  {
    g_SplitterPos = info.splitterPos;
    if (info.numPanels < 1 || info.numPanels > 2)
      info.numPanels = kNumDefaultPanels;
    if (info.currentPanel >= 2)
      info.currentPanel = 0;
  }
  else
  {
    info.numPanels = kNumDefaultPanels;
    info.currentPanel = 0;
  }

  g_App.NumPanels = info.numPanels;
  g_App.LastFocusedPanel = info.currentPanel;

  if (!wnd.Create(kWindowClass, title, style,
    x, y, xSize, ySize, NULL, NULL, g_hInstance, NULL))
    return FALSE;

  /*
  // doesn't work
  {
    const HMODULE hmodule = LoadLibrary("UxTheme.dll");
    if (hmodule)
    {
      {
        const
          Func_AllowDarkModeForWindow f = Z7_GET_PROC_ADDRESS(
          Func_AllowDarkModeForWindow, hmodule,
          MAKEINTRESOURCEA(133));
        if (f)
        {
          BOOL res = f((HWND)wnd, TRUE);
          res = res;
        }
      }
      {
        const
          Func_SetPreferredAppMode f = Z7_GET_PROC_ADDRESS(
          Func_SetPreferredAppMode, hmodule,
          MAKEINTRESOURCEA(135));
        if (f)
        {
          f(ForceDark);
        }
      }
      {
        const
          Func_SetWindowTheme f = Z7_GET_PROC_ADDRESS(
          Func_SetWindowTheme, hmodule,
          "SetWindowTheme");
        if (f)
        {
          // HRESULT hres = f((HWND)wnd, L"DarkMode_Explorer", NULL);
          HRESULT hres = f((HWND)wnd, L"Explorer", NULL);
          hres = hres;
        }
      }
      FreeLibrary(hmodule);
    }
  }
  {
    const HMODULE hmodule = LoadLibrary("Dwmapi.dll");
    if (hmodule)
    {
      const
        Func_DwmSetWindowAttribute f = Z7_GET_PROC_ADDRESS(
        Func_DwmSetWindowAttribute, hmodule,
        "DwmSetWindowAttribute");
      if (f)
      {
        #ifndef Z7_WIN_DWMWA_USE_IMMERSIVE_DARK_MODE
        #define Z7_WIN_DWMWA_USE_IMMERSIVE_DARK_MODE 20
        #endif
        BOOL value = TRUE;
        f((HWND)wnd, Z7_WIN_DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
      }
      FreeLibrary(hmodule);
    }
  }
  */

  if (nCmdShow == SW_SHOWNORMAL ||
      nCmdShow == SW_SHOW
      #ifndef UNDER_CE
      || nCmdShow == SW_SHOWDEFAULT
      #endif
      )
  {
    if (info.maximized)
      nCmdShow = SW_SHOWMAXIMIZED;
    else
      nCmdShow = SW_SHOWNORMAL;
  }

#ifdef UNDER_CE
  if (nCmdShow == SW_SHOWMAXIMIZED)
    g_Maximized = true;
#endif

  #ifndef UNDER_CE
  WINDOWPLACEMENT placement;
  placement.length = sizeof(placement);
  if (wnd.GetPlacement(&placement))
  {
    if (windowPosIsRead)
      placement.rcNormalPosition = info.rect;
    placement.showCmd = (UINT)nCmdShow;
    wnd.SetPlacement(&placement);
  }
  else
  #endif
    wnd.Show(nCmdShow);

  return TRUE;
}

/*
static void GetCommands(const UString &aCommandLine, UString &aCommands)
{
  UString aProgramName;
  aCommands.Empty();
  bool aQuoteMode = false;
  for (int i = 0; i < aCommandLine.Length(); i++)
  {
    wchar_t aChar = aCommandLine[i];
    if (aChar == L'\"')
      aQuoteMode = !aQuoteMode;
    else if (aChar == L' ' && !aQuoteMode)
    {
      if (!aQuoteMode)
      {
        i++;
        break;
      }
    }
    else
      aProgramName += aChar;
  }
  aCommands = aCommandLine.Ptr(i);
}
*/

#if defined(_WIN32) && !defined(_WIN64) && !defined(UNDER_CE)

extern
bool g_Is_Wow64;
bool g_Is_Wow64;

typedef BOOL (WINAPI *Func_IsWow64Process)(HANDLE, PBOOL);
Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION

static void Set_Wow64()
{
  g_Is_Wow64 = false;
  const
  Func_IsWow64Process fn = Z7_GET_PROC_ADDRESS(
  Func_IsWow64Process, GetModuleHandleA("kernel32.dll"),
      "IsWow64Process");
  if (fn)
  {
    BOOL isWow;
    if (fn(GetCurrentProcess(), &isWow))
      g_Is_Wow64 = (isWow != FALSE);
  }
}

#endif

#if _MSC_VER > 1400 /* && _MSC_VER <= 1900 */
  // GetVersion was declared deprecated
  #pragma warning(disable : 4996)
#endif
#ifdef __clang__
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

bool IsLargePageSupported();
bool IsLargePageSupported()
{
  #ifdef _WIN64
  return true;
  #else

  DWORD v = GetVersion();
  // low byte is major version:
  // next byte is minor version:
  v = ((v & 0xff) << 8) | ((v >> 8) & 0xFF);
  return (v > 0x501);
  // if ((Byte)v < 5) return false;
  // if ((Byte)v > 5) return true;
  // return ((Byte)(v >> 8) > 1);
  /* large pages work in 5.1 (XP-32bit) if it's (g_Is_Wow64) mode;
     but here we don't enable them in (XP-32bit). */
  #endif
}

#ifndef UNDER_CE

static void SetMemoryLock()
{
  if (!IsLargePageSupported())
    return;
  // if (ReadLockMemoryAdd())
  NSecurity::AddLockMemoryPrivilege();

  if (ReadLockMemoryEnable())
  if (NSecurity::Get_LargePages_RiskLevel() == 0)
  {
    // note: child processes can inherit that Privilege
    g_LargePagesMode = NSecurity::EnablePrivilege_LockMemory();
  }
}

extern
bool g_SymLink_Supported;
bool g_SymLink_Supported = false;

static void Set_SymLink_Supported()
{
  // g_SymLink_Supported = false;
  const DWORD v = GetVersion();
  // low byte is major version:
  if ((Byte)v < 6)
    return;
  g_SymLink_Supported = true;
  // if (g_SymLink_Supported)
  {
    NSecurity::EnablePrivilege_SymLink();
  }
}

#endif

/*
static const int kNumSwitches = 1;

namespace NKey {
enum Enum
{
  kOpenArachive = 0
};

}

static const CSwitchForm kSwitchForms[kNumSwitches] =
  {
    { L"SOA", NSwitchType::kSimple, false },
  };
*/

// int APIENTRY WinMain2(HINSTANCE hInstance, HINSTANCE /* hPrevInstance */, LPSTR /* lpCmdLine */, int /* nCmdShow */);

static void ErrorMessage(const wchar_t *s)
{
  MessageBoxW(NULL, s, L"7-Zip", MB_ICONERROR);
}

static void ErrorMessage(const char *s)
{
  ErrorMessage(GetUnicodeString(s));
}


#if defined(_UNICODE) && !defined(_WIN64) && !defined(UNDER_CE)
#define NT_CHECK_FAIL_ACTION ErrorMessage("Unsupported Windows version"); return 1;
#endif

static int WINAPI WinMain2(int nCmdShow)
{
  g_RAM_Size_Defined = NSystem::GetRamSize(g_RAM_Size);

  #ifdef _WIN32

  /*
  #ifndef _WIN64
  #ifndef UNDER_CE
  {
    HMODULE hMod = GetModuleHandle("Kernel32.dll");
    if (hMod)
    {
      typedef BOOL (WINAPI *PSETDEP)(DWORD);
      #define MY_PROCESS_DEP_ENABLE 1
      PSETDEP procSet = (PSETDEP)GetProcAddress(hMod,"SetProcessDEPPolicy");
      if (procSet)
        procSet(MY_PROCESS_DEP_ENABLE);

      typedef BOOL (WINAPI *HSI)(HANDLE, HEAP_INFORMATION_CLASS ,PVOID, SIZE_T);
      HSI hsi = (HSI)GetProcAddress(hMod, "HeapSetInformation");
      #define MY_HeapEnableTerminationOnCorruption ((HEAP_INFORMATION_CLASS)1)
      if (hsi)
        hsi(NULL, MY_HeapEnableTerminationOnCorruption, NULL, 0);
    }
  }
  #endif
  #endif
  */

  NT_CHECK
  #ifdef Z7_LARGE_PAGES
  z7_LargePage_Set(0, 0, 0);
  #endif

  #endif

  #ifdef Z7_LANG
  LoadLangOneTime();
  #endif

  InitCommonControls();

#ifdef Z7_USE_DYN_ComCtl32Version
  g_ComCtl32Version = ::GetDllVersion(TEXT("comctl32.dll"));
  g_LVN_ITEMACTIVATE_Support = (g_ComCtl32Version >= MAKELONG(71, 4));
#endif

  #if defined(_WIN32) && !defined(_WIN64) && !defined(UNDER_CE)
  Set_Wow64();
  #endif


  g_IsSmallScreen = !NWindows::NControl::IsDialogSizeOK(200, 200);

  // OleInitialize is required for drag and drop.
  #ifndef UNDER_CE
  OleInitialize(NULL);
  #endif
  // Maybe needs CoInitializeEx also ?
  // NCOM::CComInitializer comInitializer;

  UString commandsString;
  // MessageBoxW(NULL, GetCommandLineW(), L"", 0);

  #ifdef UNDER_CE
  commandsString = GetCommandLineW();
  #else
  UString programString;
  SplitStringToTwoStrings(GetCommandLineW(), programString, commandsString);
  #endif

  commandsString.Trim();
  UString paramString, tailString;
  SplitStringToTwoStrings(commandsString, paramString, tailString);
  paramString.Trim();
  tailString.Trim();
  if (tailString.IsPrefixedBy("-t"))
    g_ArcFormat = tailString.Ptr(2);

  /*
  UStringVector switches;
  for (;;)
  {
    if (tailString.IsEmpty())
      break;
    UString s1, s2;
    SplitStringToTwoStrings(tailString, s1, s2);
    if (s2.IsEmpty())
    {
      tailString.Trim();
      switches.Add(tailString);
      break;
    }
    s1.Trim();
    switches.Add(s1);
    tailString = s2;
  }

  FOR_VECTOR(i, switches)
  {
    const UString &sw = switches[i];
    if (sw.IsPrefixedBy(L"-t"))
      g_ArcFormat = sw.Ptr(2);
    //
    else if (sw.IsPrefixedBy(L"-stp"))
    {
      const wchar_t *end;
      UInt32 val = ConvertStringToUInt32(sw.Ptr(4), &end);
      if (*end != 0)
        throw 111;
      g_TypeParseLevel = val;
    }
    else
    //
      throw 112;
  }
  */

  if (!paramString.IsEmpty())
  {
    g_MainPath = paramString;
    // return WinMain2(hInstance, hPrevInstance, lpCmdLine, nCmdShow);

    // MessageBoxW(NULL, paramString, L"", 0);
  }
  /*
  UStringVector commandStrings;
  NCommandLineParser::SplitCommandLine(GetCommandLineW(), commandStrings);
  NCommandLineParser::CParser parser(kNumSwitches);
  try
  {
    parser.ParseStrings(kSwitchForms, commandStrings);
    const UStringVector &nonSwitchStrings = parser.NonSwitchStrings;
    if (nonSwitchStrings.Size() > 1)
    {
      g_MainPath = nonSwitchStrings[1];
      // g_OpenArchive = parser[NKey::kOpenArachive].ThereIs;
      CFileInfoW fileInfo;
      if (FindFile(g_MainPath, fileInfo))
      {
        if (!fileInfo.IsDir())
          g_OpenArchive = true;
      }
    }
  }
  catch(...) { }
  */


  #if defined(_WIN32) && !defined(UNDER_CE)
  SetMemoryLock();
  Set_SymLink_Supported();
  #endif

  g_App.ReloadLangItems();

  MSG msg;
  if (!InitInstance (nCmdShow))
    return FALSE;

  // we will load Global_Codecs at first use instead.
  /*
  OutputDebugStringW(L"Before LoadGlobalCodecs");
  LoadGlobalCodecs();
  OutputDebugStringW(L"After LoadGlobalCodecs");
  */

  #ifndef _UNICODE
  if (g_IsNT)
  {
    HACCEL hAccels = LoadAcceleratorsW(g_hInstance, MAKEINTRESOURCEW(IDR_ACCELERATOR1));
    while (GetMessageW(&msg, NULL, 0, 0))
    {
      if (TranslateAcceleratorW(g_HWND, hAccels, &msg) == 0)
      {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
  }
  else
  #endif
  {
    HACCEL hAccels = LoadAccelerators(g_hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR1));
    while (GetMessage(&msg, NULL, 0, 0))
    {
      if (TranslateAccelerator(g_HWND, hAccels, &msg) == 0)
      {
        // if (g_Hwnd != NULL || !IsDialogMessage(g_Hwnd, &msg))
        // if (!IsDialogMessage(g_Hwnd, &msg))
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }
  }

  // Destructor of g_CodecsReleaser can release DLLs.
  // But we suppose that it's better to release DLLs here (before destructor).
  FreeGlobalCodecs();

  g_HWND = NULL;
  #ifndef UNDER_CE
  OleUninitialize();
  #endif
  return (int)msg.wParam;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /* hPrevInstance */,
    #ifdef UNDER_CE
    LPWSTR
    #else
    LPSTR
    #endif
    /* lpCmdLine */, int nCmdShow)
{
  g_hInstance = hInstance;

  try
  {
    try
    {
      #ifdef _WIN32
      My_SetDefaultDllDirectories();
      #endif
      return WinMain2(nCmdShow);
    }
    catch (...)
    {
      g_ExitEventLauncher.Exit(true);
      throw;
    }
  }
  catch(const CNewException &)
  {
    ErrorMessage(LangString(IDS_MEM_ERROR));
    return 1;
  }
  catch(const UString &s)
  {
    ErrorMessage(s);
    return 1;
  }
  catch(const AString &s)
  {
    ErrorMessage(s.Ptr());
    return 1;
  }
  catch(const wchar_t *s)
  {
    ErrorMessage(s);
    return 1;
  }
  catch(const char *s)
  {
    ErrorMessage(s);
    return 1;
  }
  catch(int v)
  {
    AString e ("Error: ");
    e.Add_UInt32((unsigned)v);
    ErrorMessage(e);
    return 1;
  }
  catch(...)
  {
    ErrorMessage("Unknown error");
    return 1;
  }
}

static void SaveWindowInfo(HWND aWnd)
{
  CWindowInfo info;

  #ifdef UNDER_CE
  
  if (!::GetWindowRect(aWnd, &info.rect))
    return;
  info.maximized = g_Maximized;
  
  #else
  
  WINDOWPLACEMENT placement;
  placement.length = sizeof(placement);
  if (!::GetWindowPlacement(aWnd, &placement))
    return;
  info.rect = placement.rcNormalPosition;
  info.maximized = BOOLToBool(::IsZoomed(aWnd));
  
  #endif
  
  info.numPanels = g_App.NumPanels;
  info.currentPanel = g_App.LastFocusedPanel;
  info.splitterPos = (unsigned)g_Splitter.GetPos();

  info.Save();
}

static void ExecuteCommand(UINT commandID)
{
  CPanel::CDisableTimerProcessing disableTimerProcessing1(g_App.Panels[0]);
  CPanel::CDisableTimerProcessing disableTimerProcessing2(g_App.Panels[1]);

  switch (commandID)
  {
    case kMenuCmdID_Toolbar_Add: g_App.AddToArchive(); break;
    case kMenuCmdID_Toolbar_Extract: g_App.ExtractArchives(); break;
    case kMenuCmdID_Toolbar_Test: g_App.TestArchives(); break;
  }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
    // 7-Zip Modern: custom macOS-style frame ------------------------------
    case WM_NCCALCSIZE:
      // Collapse the non-client area: the client covers the whole window and
      // the caption strip is an ordinary part of the client. When maximised
      // the window rect extends past the monitor by the frame size, so trim
      // it back in.
      if (wParam)
      {
        RECT &rc = *(RECT *)lParam;
        if (::IsZoomed(hWnd))
        {
          const int fx = ::GetSystemMetrics(SM_CXSIZEFRAME) +
              ::GetSystemMetrics(SM_CXPADDEDBORDER);
          const int fy = ::GetSystemMetrics(SM_CYSIZEFRAME) +
              ::GetSystemMetrics(SM_CXPADDEDBORDER);
          rc.left += fx;
          rc.top += fy;
          rc.right -= fx;
          rc.bottom -= fy;
        }
        return 0;
      }
      break;

    case WM_NCHITTEST:
    {
      const POINT pt = { (int)(short)LOWORD(lParam),
                         (int)(short)HIWORD(lParam) };
      RECT wr;
      ::GetWindowRect(hWnd, &wr);
      if (!::IsZoomed(hWnd))
      {
        // the invisible resize border lives inside our client area now
        const int m = NModernUI::Scale(6);
        const bool l = pt.x < wr.left + m;
        const bool r = pt.x >= wr.right - m;
        const bool t = pt.y < wr.top + m;
        const bool b = pt.y >= wr.bottom - m;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT;
        if (b && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (b) return HTBOTTOM;
      }
      POINT cp = pt;
      ::ScreenToClient(hWnd, &cp);
      if (cp.y < NModernUI::CaptionHeight() &&
          Chrome_LightAt(cp.x, cp.y) < 0 &&
          Chrome_MenuAt(hWnd, cp.x, cp.y) < 0)
        return HTCAPTION; // drag, double click maximise, system menu, snap
      return HTCLIENT;
    }

    case WM_SETCURSOR:
      // The window class carries the horizontal resize cursor for the panel
      // splitter; the caption strip gets a normal arrow instead.
      if (LOWORD(lParam) == HTCLIENT)
      {
        const DWORD pos = ::GetMessagePos();
        POINT pt = { (int)(short)LOWORD(pos), (int)(short)HIWORD(pos) };
        ::ScreenToClient(hWnd, &pt);
        if (pt.y < NModernUI::CaptionHeight())
        {
          ::SetCursor(::LoadCursor(NULL, IDC_ARROW));
          return TRUE;
        }
      }
      break;

    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC dc = ::BeginPaint(hWnd, &ps);
      if (dc)
      {
        Chrome_Paint(hWnd, dc);
        ::EndPaint(hWnd, &ps);
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      g_Chrome.MouseInside = false;
      g_Chrome.HotLight = -1;
      g_Chrome.HotMenu = -1;
      Chrome_Invalidate(hWnd);
      break;

    case WM_COMMAND:
    {
      unsigned wmId    = LOWORD(wParam);
      unsigned wmEvent = HIWORD(wParam);
      if ((HWND) lParam != NULL && wmEvent != 0)
        break;
      if (wmId >= kMenuCmdID_Toolbar_Start && wmId < kMenuCmdID_Toolbar_End)
      {
        ExecuteCommand(wmId);
        return 0;
      }
      if (OnMenuCommand(hWnd, wmId))
        return 0;
      break;
    }
    case WM_INITMENUPOPUP:
      OnMenuActivating(hWnd, HMENU(wParam), LOWORD(lParam));
      break;

    /*
    It doesn't help
    case WM_EXITMENULOOP:
      {
        OnMenuUnActivating(hWnd);
        break;
      }
    case WM_UNINITMENUPOPUP:
      OnMenuUnActivating(hWnd, HMENU(wParam), lParam);
      break;
    */

    case WM_CREATE:
    {
      g_HWND = hWnd;
      /*
      INITCOMMONCONTROLSEX icex;
      icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
      icex.dwICC  = ICC_BAR_CLASSES;
      InitCommonControlsEx(&icex);
      
      // Toolbar buttons used to create the first 4 buttons.
      TBBUTTON tbb [ ] =
      {
        // {0, 0, TBSTATE_ENABLED, BTNS_SEP, 0L, 0},
        // {VIEW_PARENTFOLDER, kParentFolderID, TBSTATE_ENABLED, BTNS_BUTTON, 0L, 0},
          // {0, 0, TBSTATE_ENABLED, BTNS_SEP, 0L, 0},
        {VIEW_NEWFOLDER, ID_FILE_CREATEFOLDER, TBSTATE_ENABLED, BTNS_BUTTON, 0L, 0},
      };
      
      int baseID = 100;
      NWindows::NControl::CToolBar aToolBar;
      aToolBar.Attach(::CreateToolbarEx (hWnd,
        WS_CHILD | WS_BORDER | WS_VISIBLE | TBSTYLE_TOOLTIPS, //  | TBSTYLE_FLAT
        baseID + 2, 11,
        (HINSTANCE)HINST_COMMCTRL, IDB_VIEW_SMALL_COLOR,
        (LPCTBBUTTON)&tbb, Z7_ARRAY_SIZE(tbb),
        0, 0, 100, 30, sizeof (TBBUTTON)));
      */
      // HCURSOR cursor = ::LoadCursor(0, IDC_SIZEWE);
      // ::SetCursor(cursor);

      if (g_PanelsInfoDefined)
        g_Splitter.SetPos(hWnd, (int)g_SplitterPos);
      else
      {
        g_Splitter.SetRatio(hWnd, kSplitterRateMax / 2);
        g_SplitterPos = (unsigned)g_Splitter.GetPos();
      }

      RECT rect;
      ::GetClientRect(hWnd, &rect);
      const int xSize = rect.right;
      int xSizes[2];
      xSizes[0] = g_Splitter.GetPos();
      xSizes[1] = xSize - kSplitterWidth - xSizes[0];
      if (xSizes[1] < 0)
        xSizes[1] = 0;

      g_App.CreateDragTarget();
      
      COpenResult openRes;
      bool needOpenArc = false;

      UString fullPath = g_MainPath;
      if (!fullPath.IsEmpty() /* && g_OpenArchive */)
      {
        if (!NFile::NName::IsAbsolutePath(fullPath))
        {
          FString fullPathF;
          if (NFile::NName::GetFullPath(us2fs(fullPath), fullPathF))
            fullPath = fs2us(fullPathF);
        }
        if (NFile::NFind::DoesFileExist_FollowLink(us2fs(fullPath)))
          needOpenArc = true;
      }
      
      HRESULT res = g_App.Create(hWnd, fullPath, g_ArcFormat, xSizes,
          needOpenArc,
          openRes);

      if (res == E_ABORT)
        return -1;
      
      if ((needOpenArc && !openRes.ArchiveIsOpened) || res != S_OK)
      {
        UString m ("Error");
        if (res == S_FALSE || res == S_OK)
        {
          m = MyFormatNew(openRes.Encrypted ?
                IDS_CANT_OPEN_ENCRYPTED_ARCHIVE :
                IDS_CANT_OPEN_ARCHIVE,
              fullPath);
        }
        else if (res != S_OK)
          m = HResultToMessage(res);
        if (!openRes.ErrorMessage.IsEmpty())
        {
          m.Add_LF();
          m += openRes.ErrorMessage;
        }
        ErrorMessage(m);
        return -1;
      }

      g_WindowWasCreated = true;
      
      // g_SplitterPos = 0;

      // ::DragAcceptFiles(hWnd, TRUE);
      RegisterDragDrop(hWnd, g_App._dropTarget);

      break;
    }

    case WM_CLOSE:
    {
      // why do we use WA_INACTIVE here ?
      SendMessage(hWnd, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), (LPARAM)hWnd);
      g_ExitEventLauncher.Exit(false);
      // ::DragAcceptFiles(hWnd, FALSE);
      RevokeDragDrop(hWnd);
      g_App._dropTarget.Release();

      if (g_WindowWasCreated)
        g_App.Save();
    
      g_App.ReleaseApp();
      
      if (g_WindowWasCreated)
        SaveWindowInfo(hWnd);

      g_ExitEventLauncher.Exit(true);
      // default DefWindowProc will call DestroyWindow / WM_DESTROY
      break;
    }

    case WM_DESTROY:
    {
      PostQuitMessage(0);
      break;
    }
    
    // case WM_MOVE: break;
    
    case WM_LBUTTONDOWN:
    {
      // caption strip first: traffic lights and the in-window menu bar
      const int capX = (int)(short)LOWORD(lParam);
      const int capY = (int)(short)HIWORD(lParam);
      if (capY < NModernUI::CaptionHeight())
      {
        const int light = Chrome_LightAt(capX, capY);
        if (light >= 0)
        {
          if (light == 0)
            ::PostMessage(hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
          else if (light == 1)
            ::PostMessage(hWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
          else
            ::PostMessage(hWnd, WM_SYSCOMMAND,
                ::IsZoomed(hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
          return 0;
        }
        const int menuIndex = Chrome_MenuAt(hWnd, capX, capY);
        if (menuIndex >= 0)
        {
          Chrome_OpenMenu(hWnd, menuIndex);
          return 0;
        }
      }
      g_StartCaptureMousePos = LOWORD(lParam);
      g_StartCaptureSplitterPos = g_Splitter.GetPos();
      ::SetCapture(hWnd);
      break;
    }
    
    case WM_LBUTTONUP:
    {
      ::ReleaseCapture();
      break;
    }
    
    case WM_MOUSEMOVE:
    {
      // hover states in the caption strip (window buttons and menu labels)
      {
        const int x = (int)(short)LOWORD(lParam);
        const int y = (int)(short)HIWORD(lParam);
        const int light = (y < NModernUI::CaptionHeight())
            ? Chrome_LightAt(x, y) : -1;
        const int menuIndex = (light < 0)
            ? Chrome_MenuAt(hWnd, x, y) : -1;
        if (light != g_Chrome.HotLight || menuIndex != g_Chrome.HotMenu)
        {
          g_Chrome.HotLight = light;
          g_Chrome.HotMenu = menuIndex;
          Chrome_Invalidate(hWnd);
        }
        if (!g_Chrome.MouseInside)
        {
          g_Chrome.MouseInside = true;
          Chrome_TrackMouse(hWnd);
        }
      }
      if ((wParam & MK_LBUTTON) != 0 && ::GetCapture() == hWnd)
      {
        g_Splitter.SetPos(hWnd, g_StartCaptureSplitterPos +
            (short)LOWORD(lParam) - g_StartCaptureMousePos);
        g_App.MoveSubWindows();
      }
      break;
    }

    case WM_SIZE:
    {
      if (g_CanChangeSplitter)
        g_Splitter.SetPosFromRatio(hWnd);
      else
      {
        g_Splitter.SetPos(hWnd, (int)g_SplitterPos );
        g_CanChangeSplitter = true;
      }
      
#ifdef UNDER_CE
      g_Maximized = (wParam == SIZE_MAXIMIZED) || (wParam == SIZE_MAXSHOW);
#endif

      g_App.MoveSubWindows();
      /*
      int xSize = LOWORD(lParam);
      int ySize = HIWORD(lParam);
      // int xSplitter = 2;
      int xWidth = g_SplitPos;
      // int xSplitPos = xWidth;
      g_Panel[0]._listView.MoveWindow(0, 0, xWidth, ySize);
      g_Panel[1]._listView.MoveWindow(xSize - xWidth, 0, xWidth, ySize);
      */
      return 0;
      // break;
    }
    
    case WM_SETFOCUS:
      // g_App.SetFocus(g_App.LastFocusedPanel);
      g_App.SetFocusToLastItem();
      break;

    case WM_ACTIVATE:
      // Modern menus hide their accelerator keys until Alt is held down.
      // The flag lives in the per window UI state, which the system resets
      // whenever the window is deactivated, so it is re-armed on activation.
      if (LOWORD(wParam) != WA_INACTIVE)
        ::SendMessageW(hWnd, WM_CHANGEUISTATE,
            MAKEWPARAM(UIS_SET, UISF_HIDEACCEL), 0);
      g_Chrome.Active = (LOWORD(wParam) != WA_INACTIVE);
      Chrome_Invalidate(hWnd);
      break;

    case WM_NCACTIVATE:
      g_Chrome.Active = (wParam != 0);
      Chrome_Invalidate(hWnd);
      break;
    
    /*
    case kLangWasChangedMessage:
      MyLoadMenu();
      return 0;
    */
      
    /*
    case WM_SETTINGCHANGE:
      break;
    */
    
    case WM_NOTIFY:
    {
      g_App.OnNotify((int)wParam, (LPNMHDR)lParam);
      break;
    }
    
    /*
    case WM_DROPFILES:
    {
      g_App.GetFocusedPanel().CompressDropFiles((HDROP)wParam);
      return 0 ;
    }
    */
  }
  #ifndef _UNICODE
  if (g_IsNT)
    return DefWindowProcW(hWnd, message, wParam, lParam);
  else
  #endif
    return DefWindowProc(hWnd, message, wParam, lParam);

}

void CApp::MoveSubWindows()
{
  HWND hWnd = _window;
  RECT rect;
  if (!hWnd)
    return;
  ::GetClientRect(hWnd, &rect);
  int xSize = rect.right;
  if (xSize == 0)
    return;
  int headerSize = NModernUI::CaptionHeight();

  #ifdef UNDER_CE
  _commandBar.AutoSize();
  {
    _commandBar.Show(true); // maybe we need it for
    headerSize += _commandBar.Height();
  }
  #endif

  if (_toolBar.IsCreated())
  {
    _toolBar.SetTop(NModernUI::CaptionHeight());
    _toolBar.AutoSize();
    headerSize += _toolBar.GetBarHeight();
  }
  
  int ySize = MyMax((int)(rect.bottom - headerSize), 0);
  
  if (NumPanels > 1)
  {
    Panels[0].Move(0, headerSize, g_Splitter.GetPos(), ySize);
    int xWidth1 = g_Splitter.GetPos() + kSplitterWidth;
    Panels[1].Move(xWidth1, headerSize, xSize - xWidth1, ySize);
  }
  else
  {
    /*
    int otherPanel = 1 - LastFocusedPanel;
    if (PanelsCreated[otherPanel])
      Panels[otherPanel].Move(0, headerSize, 0, ySize);
    */
    Panels[LastFocusedPanel].Move(0, headerSize, xSize, ySize);
  }
}

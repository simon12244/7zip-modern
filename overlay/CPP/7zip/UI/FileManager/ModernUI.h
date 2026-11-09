// ModernUI.h
//
// 7-Zip Modern - shared design tokens plus the custom flat toolbar.
//
// The stock 7-Zip file manager is built from stock comctl32 controls: a
// 24px toolbar with text labels, 3D list headers and sunken status parts.
// This module replaces the visual layer only - every command, shortcut and
// behaviour stays untouched.
//
// Tokens and helpers are header-only (inline) so that no extra translation
// unit is needed; the toolbar implementation lives in App.cpp.

#ifndef ZIP7_INC_MODERN_UI_H
#define ZIP7_INC_MODERN_UI_H

#include "../../../Common/MyString.h"
#include "../../../Common/MyVector.h"
#include "../../../Windows/Window.h"

#define Z7_MODERN_BAR_CLASS TEXT("7zModernToolBar")

namespace NModernUI
{
  // ---------------------------------------------------------------- palette
  // A calm neutral canvas with a single blue accent. All values are tuned
  // for the light theme; dark surfaces only differ in the list/bar colour.

  inline COLORREF Col_Bg()           { return RGB(0xFF, 0xFF, 0xFF); }
  inline COLORREF Col_Bar()          { return RGB(0xFF, 0xFF, 0xFF); }
  inline COLORREF Col_Band()         { return RGB(0xF7, 0xF8, 0xFA); }
  inline COLORREF Col_Hairline()     { return RGB(0xE4, 0xE7, 0xEB); }
  inline COLORREF Col_HairlineSoft() { return RGB(0xEE, 0xF0, 0xF3); }
  inline COLORREF Col_Ink()          { return RGB(0x1F, 0x23, 0x28); }
  inline COLORREF Col_InkDim()       { return RGB(0x6B, 0x72, 0x80); }
  inline COLORREF Col_InkFaint()     { return RGB(0x9A, 0xA1, 0xAC); }
  inline COLORREF Col_Hover()        { return RGB(0xF1, 0xF3, 0xF6); }
  inline COLORREF Col_Pressed()      { return RGB(0xE4, 0xE7, 0xEC); }
  inline COLORREF Col_Accent()       { return RGB(0x25, 0x63, 0xEB); }
  inline COLORREF Col_Zebra()        { return RGB(0xFA, 0xFB, 0xFC); }
  inline COLORREF Col_Field()        { return RGB(0xFF, 0xFF, 0xFF); }

  // ---------------------------------------------------------------- metrics

  // 7-Zip lays the file manager out with fixed 96-dpi metrics (it does not
  // re-scale its fonts per monitor), so the command bar follows the same
  // rule. Deriving our own scale factor from the monitor DPI would make the
  // bar visibly oversized compared to the list and the menu.
  inline int Scale(int value) { return value; }

  // Text however is a different story: the stock controls take their row and
  // band heights from their own font, and 7-Zip hands the list a font built
  // from the system metrics. Drawing our labels at a literal 96 dpi on a
  // high-dpi monitor makes them half the size of everything around them, so
  // the font cache below resolves the real vertical resolution.
  inline int Dpi()
  {
    static int g_Dpi = 0;
    if (g_Dpi == 0)
    {
      HDC dc = ::GetDC(NULL);
      g_Dpi = dc ? ::GetDeviceCaps(dc, LOGPIXELSY) : 96;
      if (dc)
        ::ReleaseDC(NULL, dc);
      if (g_Dpi < 96)
        g_Dpi = 96;
    }
    return g_Dpi;
  }

  inline int Pt(int pt10)
  {
    return -::MulDiv(pt10, Dpi(), 720);
  }

  // ---------------------------------------------------------------- fonts

  // pt10 == font size in tenths of a point (105 == 10.5pt).
  // Fonts are cached: the process keeps them for its whole lifetime.
  inline HFONT Font(int pt10, bool bold)
  {
    enum { kSlots = 24 };
    static HFONT g_Fonts[kSlots];
    static int   g_Heights[kSlots];

    const int height = Pt(pt10);
    const int slot = (bold ? 12 : 0) + (pt10 % 12);

    if (g_Fonts[slot] && g_Heights[slot] == height)
      return g_Fonts[slot];

    if (g_Fonts[slot])
      ::DeleteObject(g_Fonts[slot]);

    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = height;
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    lstrcpyW(lf.lfFaceName, L"Segoe UI");

    g_Fonts[slot] = ::CreateFontIndirectW(&lf);
    g_Heights[slot] = height;
    return g_Fonts[slot];
  }

  inline HFONT Font_List()    { return Font(105, false); }
  inline HFONT Font_Header()  { return Font(100, false); }
  inline HFONT Font_Bar()     { return Font(90, false); }

  // ---------------------------------------------------------------- painting

  inline void FillSolid(HDC dc, const RECT &r, COLORREF color)
  {
    HBRUSH br = ::CreateSolidBrush(color);
    ::FillRect(dc, &r, br);
    ::DeleteObject(br);
  }

  inline void FillSolid(HDC dc, int l, int t, int r, int b, COLORREF color)
  {
    RECT rc;
    rc.left = l; rc.top = t; rc.right = r; rc.bottom = b;
    FillSolid(dc, rc, color);
  }

  inline void FillRound(HDC dc, const RECT &r, int radius, COLORREF color)
  {
    HBRUSH br = ::CreateSolidBrush(color);
    HGDIOBJ oldBr = ::SelectObject(dc, br);
    HGDIOBJ oldPen = ::SelectObject(dc, ::GetStockObject(NULL_PEN));
    ::RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    ::SelectObject(dc, oldPen);
    ::SelectObject(dc, oldBr);
    ::DeleteObject(br);
  }

  inline void DrawTextIn(HDC dc, const RECT &r, LPCWSTR text, COLORREF color,
      HFONT font, UINT flags)
  {
    HGDIOBJ oldFont = ::SelectObject(dc, font);
    const int oldMode = ::SetBkMode(dc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(dc, color);
    RECT rc = r;
    ::DrawTextW(dc, text, -1, &rc, flags);
    ::SetTextColor(dc, oldColor);
    ::SetBkMode(dc, oldMode);
    ::SelectObject(dc, oldFont);
  }

  // Rounded corners for a stock child control: the corners simply become
  // transparent, revealing whatever the parent painted behind.
  inline void RoundCorners(HWND hwnd, int radius)
  {
    RECT rc;
    ::GetWindowRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
      return;
    HRGN rgn = ::CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius);
    ::SetWindowRgn(hwnd, rgn, TRUE); // the system owns rgn from now on
  }
}

// --------------------------------------------------------------------------
// CModernToolBar - a flat, icon-only command bar.
//
// Deliberately not a comctl32 toolbar: that control cannot render rounded
// hover states, vertical rhythm or an icon-only layout at modern sizes.
// Commands are posted to the parent as ordinary WM_COMMAND messages, so the
// existing dispatch in FM.cpp keeps working unchanged.
// --------------------------------------------------------------------------

class CModernToolBar
{
  Z7_CLASS_NO_COPY(CModernToolBar)
public:
  struct CItem
  {
    int Cmd;
    HBITMAP Bmp;
    int Bw;
    int Bh;
    UString Tip;
    bool GroupEnd;
    bool Right;
    RECT Rect;
  };

  CModernToolBar();
  ~CModernToolBar();

  bool Create(HWND parent);
  void Destroy();

  void Attach(HWND hwnd) { _hwnd = hwnd; }
  operator HWND() const { return _hwnd; }
  bool IsCreated() const { return _hwnd != NULL; }

  void AddItem(int cmd, UINT bmpResID, const UString &tip, bool groupEnd, bool right);

  void ClearItems();

  void SetShowText(bool showText) { _showText = showText; }
  void SetIconSize(int size) { _iconSize = size; }
  void AutoSize();
  int GetBarHeight() const { return _barHeight; }

  LRESULT OnMsg(UINT msg, WPARAM wParam, LPARAM lParam);

public:
  // DEBUG
  int _dbgImages;
  int _dbgInit;
  int _dbgIcons[8];
private:

private:
  void Layout(int width);
  void Paint(HDC dc);
  void DrawItemIcon(HDC dc, const CItem &item, int x, int y);
  int HitTest(int x, int y) const;
  void UpdateTip(int index);

  HWND _hwnd;
  HWND _parent;
  int _iconSize;
  int _barHeight;
  int _hot;
  int _pressed;
  bool _showText;
  HWND _tip;
  UString _tipText;
  int _tipIndex;
  CObjectVector<CItem> _items;
};

#endif

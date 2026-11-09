// App.cpp

#include "StdAfx.h"

#include "resource.h"
#include "OverwriteDialogRes.h"

#include "../../../Windows/FileName.h"
#include "../../../Windows/PropVariantConv.h"

/*
#include "Windows/COM.h"
#include "Windows/Error.h"
#include "Windows/FileDir.h"

#include "Windows/PropVariant.h"
#include "Windows/Thread.h"
*/

#include "App.h"
#include "CopyDialog.h"
#include "ExtractCallback.h"
#include "FormatUtils.h"
#include "IFolder.h"
#include "LangUtils.h"
#include "MyLoadMenu.h"
#include "RegistryUtils.h"
#include "ViewSettings.h"

#include "PropertyNameRes.h"

using namespace NWindows;
using namespace NFile;
using namespace NDir;
using namespace NFind;
using namespace NName;

extern HINSTANCE g_hInstance;

#define kTempDirPrefix FTEXT("7zE")

void CPanelCallbackImp::OnTab()
{
  if (g_App.NumPanels != 1)
    _app->Panels[1 - _index].SetFocusToList();
  _app->RefreshTitle();
}

void CPanelCallbackImp::SetFocusToPath(unsigned index)
{
  unsigned newPanelIndex = index;
  if (g_App.NumPanels == 1)
    newPanelIndex = g_App.LastFocusedPanel;
  _app->RefreshTitle();
  _app->Panels[newPanelIndex]._headerComboBox.SetFocus();
  _app->Panels[newPanelIndex]._headerComboBox.ShowDropDown();
}


void CPanelCallbackImp::OnCopy(bool move, bool copyToSame) { _app->OnCopy(move, copyToSame, _index); }
void CPanelCallbackImp::OnSetSameFolder() { _app->OnSetSameFolder(_index); }
void CPanelCallbackImp::OnSetSubFolder()  { _app->OnSetSubFolder(_index); }
void CPanelCallbackImp::PanelWasFocused() { _app->SetFocusedPanel(_index); _app->RefreshTitlePanel(_index); }
void CPanelCallbackImp::DragBegin() { _app->DragBegin(_index); }
void CPanelCallbackImp::DragEnd() { _app->DragEnd(); }
void CPanelCallbackImp::RefreshTitle(bool always) { _app->RefreshTitlePanel(_index, always); }

void CApp::ReloadLangItems()
{
  LangString(IDS_N_SELECTED_ITEMS, LangString_N_SELECTED_ITEMS);
}

void CApp::SetListSettings()
{
  CFmSettings st;
  st.Load();

  ShowSystemMenu = st.ShowSystemMenu;

  // 7-Zip Modern: double buffering removes the flicker while scrolling/resizing
  DWORD extendedStyle = LVS_EX_HEADERDRAGDROP | LVS_EX_DOUBLEBUFFER;
  if (st.FullRow)
    extendedStyle |= LVS_EX_FULLROWSELECT;
  if (st.ShowGrid)
    extendedStyle |= LVS_EX_GRIDLINES;
  
  if (st.SingleClick)
  {
    extendedStyle |= LVS_EX_ONECLICKACTIVATE | LVS_EX_TRACKSELECT;
    /*
    if (ReadUnderline())
      extendedStyle |= LVS_EX_UNDERLINEHOT;
    */
  }

  for (unsigned i = 0; i < kNumPanelsMax; i++)
  {
    CPanel &panel = Panels[i];
    panel._mySelectMode = st.AlternativeSelection;
    panel._showDots = st.ShowDots;
    panel._showRealFileIcons = st.ShowRealFileIcons;
    panel._exStyle = extendedStyle;

    LONG_PTR style = panel._listView.GetStyle();
    if (st.AlternativeSelection)
      style |= LVS_SINGLESEL;
    else
      style &= ~(LONG_PTR)(DWORD)LVS_SINGLESEL;
    panel._listView.SetStyle(style);
    panel.SetExtendedStyle();
  }
}

#ifndef ILC_COLOR32
#define ILC_COLOR32 0x0020
#endif

HRESULT CApp::CreateOnePanel(unsigned panelIndex, const UString &mainPath, const UString &arcFormat,
    bool needOpenArc,
    COpenResult &openRes)
{
  if (Panels[panelIndex].PanelCreated)
    return S_OK;
  
  m_PanelCallbackImp[panelIndex].Init(this, panelIndex);
  
  UString path;
  if (mainPath.IsEmpty())
  {
    if (!::ReadPanelPath(panelIndex, path))
      path.Empty();
  }
  else
    path = mainPath;
  
  const unsigned id = 1000 + 100 * panelIndex; // check it

  return Panels[panelIndex].Create(_window, _window,
      id, path, arcFormat, &m_PanelCallbackImp[panelIndex], &AppState,
      needOpenArc,
      openRes);
}


// ---------------------------------------------------------------- modern toolbar
//
// The stock comctl32 toolbar cannot express what the redesign needs (icon
// only layout, rounded hover, hairline separators), so the command bar is a
// small custom window. It only forwards WM_COMMAND to the main window, which
// means every existing menu/toolbar handler keeps working as-is.

static LRESULT CALLBACK ModernBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  CModernToolBar *bar = (CModernToolBar *)::GetWindowLongPtr(hwnd, GWLP_USERDATA);
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
    bar = (CModernToolBar *)cs->lpCreateParams;
    ::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)bar);
    if (bar)
      bar->Attach(hwnd);
  }
  if (bar)
    return bar->OnMsg(msg, wParam, lParam);
  return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

// TransparentBlt lives in msimg32.dll, which the project does not link.
// Resolving it lazily keeps the build untouched.
typedef BOOL (WINAPI *FTransparentBlt)(HDC, int, int, int, int,
    HDC, int, int, int, int, UINT);

static FTransparentBlt GetTransparentBlt()
{
  static FTransparentBlt fn = NULL;
  static bool tried = false;
  if (!tried)
  {
    tried = true;
    HMODULE dll = ::LoadLibraryW(L"msimg32.dll");
    if (dll)
      fn = (FTransparentBlt)(void *)::GetProcAddress(dll, "TransparentBlt");
  }
  return fn;
}

CModernToolBar::CModernToolBar():
    _hwnd(NULL),
    _parent(NULL),
    _iconSize(24),
    _barHeight(0),
    _hot(-1),
    _pressed(-1),
    _showText(false),
    _tip(NULL),
    _tipIndex(-1)
{
}

CModernToolBar::~CModernToolBar()
{
  Destroy();
}

void CModernToolBar::ClearItems()
{
  for (unsigned i = 0; i < _items.Size(); i++)
    if (_items[i].Bmp)
      ::DeleteObject(_items[i].Bmp);
  _items.Clear();
  _hot = _pressed = -1;
}

void CModernToolBar::Destroy()
{
  if (_tip)
  {
    ::DestroyWindow(_tip);
    _tip = NULL;
  }
  ClearItems();
  if (_hwnd)
  {
    ::DestroyWindow(_hwnd);
    _hwnd = NULL;
  }
}

bool CModernToolBar::Create(HWND parent)
{
  static bool g_Registered = false;
  if (!g_Registered)
  {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ModernBarProc;
    wc.hInstance = g_hInstance;
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = Z7_MODERN_BAR_CLASS;
    if (!::RegisterClassW(&wc))
      return false;
    g_Registered = true;
  }

  _parent = parent;
  _hwnd = ::CreateWindowExW(0, Z7_MODERN_BAR_CLASS, L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
      0, 0, 0, 0, parent, NULL, g_hInstance, this);
  if (!_hwnd)
    return false;

  _tip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
      WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
      0, 0, 0, 0, _hwnd, NULL, g_hInstance, NULL);
  if (_tip)
  {
    TOOLINFOW ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    ti.hwnd = _hwnd;
    ti.uId = 1;
    ti.lpszText = (LPWSTR)L"";
    ::SendMessageW(_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    ::SendMessageW(_tip, TTM_SETMAXTIPWIDTH, 0, 400);
  }

  return true;
}

void CModernToolBar::AddItem(int cmd, UINT bmpResID, const UString &tip, bool groupEnd, bool right)
{
  CItem item;
  item.Cmd = cmd;
  item.Tip = tip;
  item.GroupEnd = groupEnd;
  item.Right = right;
  item.Bmp = ::LoadBitmap(g_hInstance, MAKEINTRESOURCE(bmpResID));
  item.Bw = _iconSize;
  item.Bh = _iconSize;
  memset(&item.Rect, 0, sizeof(item.Rect));
  if (!item.Bmp)
  {
    item.Bw = item.Bh = 0;
  }
  else
  {
    BITMAP bm;
    if (::GetObject(item.Bmp, sizeof(bm), &bm))
    {
      item.Bw = bm.bmWidth;
      item.Bh = bm.bmHeight;
    }
  }
  _items.Add(item);
}

void CModernToolBar::DrawItemIcon(HDC dc, const CItem &item, int x, int y)
{
  if (!item.Bmp)
    return;
  HDC mdc = ::CreateCompatibleDC(dc);
  if (!mdc)
    return;
  HGDIOBJ old = ::SelectObject(mdc, item.Bmp);
  FTransparentBlt fn = GetTransparentBlt();
  if (fn)
    fn(dc, x, y, item.Bw, item.Bh, mdc, 0, 0, item.Bw, item.Bh, RGB(255, 0, 255));
  else
    ::BitBlt(dc, x, y, item.Bw, item.Bh, mdc, 0, 0, SRCCOPY);
  ::SelectObject(mdc, old);
  ::DeleteDC(mdc);
}

void CModernToolBar::AutoSize()
{
  if (!_hwnd)
    return;
  RECT rc;
  ::GetClientRect(_parent, &rc);
  Layout(rc.right);
  ::SetWindowPos(_hwnd, NULL, 0, 0, rc.right, _barHeight,
      SWP_NOZORDER | SWP_NOACTIVATE);
}

void CModernToolBar::Layout(int width)
{
  const int padX    = NModernUI::Scale(10);
  const int gap     = NModernUI::Scale(2);
  const int groupGap = NModernUI::Scale(9);
  const int iconW   = _iconSize;
  const int btnH    = iconW + NModernUI::Scale(_showText ? 30 : 12);

  _barHeight = btnH + NModernUI::Scale(10);

  HDC dc = ::GetDC(_hwnd);
  HFONT font = NModernUI::Font_Bar();
  HGDIOBJ oldFont = dc ? ::SelectObject(dc, font) : NULL;

  int x = padX;
  const int y = NModernUI::Scale(5);

  for (unsigned i = 0; i < _items.Size(); i++)
  {
    CItem &item = _items[i];
    if (item.Right)
      continue;

    int w = _showText ? NModernUI::Scale(58) : NModernUI::Scale(44);
    if (_showText && dc && !item.Tip.IsEmpty())
    {
      SIZE sz;
      int textW = 0;
      if (::GetTextExtentPoint32W(dc, item.Tip, item.Tip.Len(), &sz))
        textW = sz.cx;
      if (textW + NModernUI::Scale(16) > w)
        w = textW + NModernUI::Scale(16);
    }
    else
    {
      w = MyMax(w, iconW + NModernUI::Scale(16));
    }

    item.Rect.left = x;
    item.Rect.top = y;
    item.Rect.right = x + w;
    item.Rect.bottom = y + btnH;

    x += w + gap;
    if (item.GroupEnd)
      x += groupGap;
  }

  if (oldFont && dc)
  {
    ::SelectObject(dc, oldFont);
    ::ReleaseDC(_hwnd, dc);
  }

  // right aligned commands
  int rx = width - padX;
  for (int i = (int)_items.Size() - 1; i >= 0; i--)
  {
    CItem &item = _items[i];
    if (!item.Right)
      continue;
    int w = _showText ? NModernUI::Scale(58) : NModernUI::Scale(44);
    w = MyMax(w, iconW + NModernUI::Scale(16));
    rx -= w;
    item.Rect.left = rx;
    item.Rect.top = y;
    item.Rect.right = rx + w;
    item.Rect.bottom = y + btnH;
    rx -= gap;
  }
}

int CModernToolBar::HitTest(int px, int py) const
{
  for (unsigned i = 0; i < _items.Size(); i++)
  {
    const RECT &r = _items[i].Rect;
    if (px >= r.left && px < r.right && py >= r.top && py < r.bottom)
      return (int)i;
  }
  return -1;
}

void CModernToolBar::Paint(HDC dc)
{
  RECT rc;
  ::GetClientRect(_hwnd, &rc);
  NModernUI::FillSolid(dc, rc, NModernUI::Col_Bar());

  const int radius = NModernUI::Scale(8);

  for (unsigned i = 0; i < _items.Size(); i++)
  {
    const CItem &item = _items[i];
    if ((int)i == _pressed)
      NModernUI::FillRound(dc, item.Rect, radius, NModernUI::Col_Pressed());
    else if ((int)i == _hot)
      NModernUI::FillRound(dc, item.Rect, radius, NModernUI::Col_Hover());
  }

  const int sepTop = NModernUI::Scale(12);
  const int sepBottom = NModernUI::Scale(28);
  for (unsigned i = 0; i < _items.Size(); i++)
  {
    const CItem &item = _items[i];
    if (!item.GroupEnd || item.Right || i + 1 >= _items.Size())
      continue;
    const int sx = item.Rect.right + NModernUI::Scale(5);
    NModernUI::FillSolid(dc, sx, sepTop, sx + 1, sepBottom, NModernUI::Col_Hairline());
  }

  const int iconTop = NModernUI::Scale(6);
  HFONT font = NModernUI::Font_Bar();

  for (unsigned i = 0; i < _items.Size(); i++)
  {
    const CItem &item = _items[i];
    if (item.Bmp)
    {
      const int ix = item.Rect.left + ((item.Rect.right - item.Rect.left) - item.Bw) / 2;
      const int iy = item.Rect.top + iconTop;
      DrawItemIcon(dc, item, ix, iy);
    }
    if (_showText && !item.Tip.IsEmpty())
    {
      RECT tr = item.Rect;
      tr.top = item.Rect.top + iconTop + _iconSize + NModernUI::Scale(2);
      NModernUI::DrawTextIn(dc, tr, item.Tip, NModernUI::Col_InkDim(), font,
          DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
  }

  // hairline that separates the bar from the panels
  NModernUI::FillSolid(dc, rc.left, rc.bottom - 1, rc.right, rc.bottom,
      NModernUI::Col_Hairline());
}

void CModernToolBar::UpdateTip(int index)
{
  if (!_tip)
    return;
  if (index == _tipIndex)
    return;
  _tipIndex = index;

  TOOLINFOW ti;
  memset(&ti, 0, sizeof(ti));
  ti.cbSize = sizeof(ti);
  ti.hwnd = _hwnd;
  ti.uId = 1;

  if (index < 0 || (unsigned)index >= _items.Size() || _items[index].Tip.IsEmpty())
  {
    ::SendMessageW(_tip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
    return;
  }

  _tipText = _items[index].Tip;
  ti.lpszText = (LPWSTR)(LPCWSTR)_tipText;
  ::SendMessageW(_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);

  RECT r = _items[index].Rect;
  POINT pt;
  pt.x = r.left;
  pt.y = r.bottom + NModernUI::Scale(6);
  ::ClientToScreen(_hwnd, &pt);
  ::SendMessageW(_tip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x, pt.y));
  ::SendMessageW(_tip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
}

LRESULT CModernToolBar::OnMsg(UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_ERASEBKGND:
      return 1;

    case WM_SIZE:
      Layout(LOWORD(lParam));
      ::InvalidateRect(_hwnd, NULL, FALSE);
      return 0;

    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC dc = ::BeginPaint(_hwnd, &ps);
      Paint(dc);
      ::EndPaint(_hwnd, &ps);
      return 0;
    }

    case WM_MOUSEMOVE:
    {
      const int x = (int)(short)LOWORD(lParam);
      const int y = (int)(short)HIWORD(lParam);
      const int index = HitTest(x, y);
      if (index != _hot)
      {
        _hot = index;
        UpdateTip(index);
        ::InvalidateRect(_hwnd, NULL, FALSE);
      }
      TRACKMOUSEEVENT tme;
      tme.cbSize = sizeof(tme);
      tme.dwFlags = TME_LEAVE;
      tme.hwndTrack = _hwnd;
      tme.dwHoverTime = 0;
      ::TrackMouseEvent(&tme);
      return 0;
    }

    case WM_MOUSELEAVE:
      _hot = -1;
      UpdateTip(-1);
      ::InvalidateRect(_hwnd, NULL, FALSE);
      return 0;

    case WM_LBUTTONDOWN:
    {
      const int index = HitTest((int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
      if (index >= 0)
      {
        _pressed = index;
        ::SetCapture(_hwnd);
        ::InvalidateRect(_hwnd, NULL, FALSE);
      }
      return 0;
    }

    case WM_LBUTTONUP:
    {
      const int index = HitTest((int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
      const int pressed = _pressed;
      _pressed = -1;
      if (::GetCapture() == _hwnd)
        ::ReleaseCapture();
      ::InvalidateRect(_hwnd, NULL, FALSE);
      if (index >= 0 && index == pressed && (unsigned)index < _items.Size())
      {
        UpdateTip(-1);
        ::SendMessage(_parent, WM_COMMAND,
            MAKEWPARAM(_items[index].Cmd, 0), 0);
      }
      return 0;
    }

    case WM_CAPTURECHANGED:
      if (_pressed >= 0)
      {
        _pressed = -1;
        ::InvalidateRect(_hwnd, NULL, FALSE);
      }
      return 0;
  }
  return ::DefWindowProc(_hwnd, msg, wParam, lParam);
}

void CApp::ReloadToolbars()
{
  _toolBar.Destroy();

  if (!ShowArchiveToolbar && !ShowStandardToolbar)
    return;

  if (!_toolBar.Create(_window))
    return;

  _toolBar.SetShowText(ShowButtonsLables);
  _toolBar.SetIconSize(LargeButtons ? 32 : 28);

  if (ShowArchiveToolbar)
  {
    _toolBar.AddItem(kMenuCmdID_Toolbar_Add, LargeButtons ? IDB_ADD : IDB_ADD2,
        LangString(IDS_ADD), false, false);
    _toolBar.AddItem(kMenuCmdID_Toolbar_Extract, LargeButtons ? IDB_EXTRACT : IDB_EXTRACT2,
        LangString(IDS_EXTRACT), false, false);
    _toolBar.AddItem(kMenuCmdID_Toolbar_Test, LargeButtons ? IDB_TEST : IDB_TEST2,
        LangString(IDS_TEST), true, false);
  }

  if (ShowStandardToolbar)
  {
    _toolBar.AddItem(IDM_COPY_TO, LargeButtons ? IDB_COPY : IDB_COPY2,
        LangString(IDS_BUTTON_COPY), false, false);
    _toolBar.AddItem(IDM_MOVE_TO, LargeButtons ? IDB_MOVE : IDB_MOVE2,
        LangString(IDS_BUTTON_MOVE), false, false);
    _toolBar.AddItem(IDM_DELETE, LargeButtons ? IDB_DELETE : IDB_DELETE2,
        LangString(IDS_BUTTON_DELETE), false, false);
    _toolBar.AddItem(IDM_PROPERTIES, LargeButtons ? IDB_INFO : IDB_INFO2,
        LangString(IDS_BUTTON_INFO), true, false);
  }

  _toolBar.AutoSize();
}

void CApp::SaveToolbarChanges()
{
  SaveToolbar();
  ReloadToolbars();
  MoveSubWindows();
}


HRESULT CApp::Create(HWND hwnd, const UString &mainPath, const UString &arcFormat, int xSizes[2], bool needOpenArc, COpenResult &openRes)
{
  _window.Attach(hwnd);

  #ifdef UNDER_CE
  _commandBar.Create(g_hInstance, hwnd, 1);
  #endif

  MyLoadMenu(false);  // needResetMenu
  
  #ifdef UNDER_CE
  _commandBar.AutoSize();
  #endif

  ReadToolbar();
  ReloadToolbars();

  unsigned i;
  for (i = 0; i < kNumPanelsMax; i++)
    Panels[i].PanelCreated = false;

  AppState.Read();
  
  SetListSettings();

  if (LastFocusedPanel >= kNumPanelsMax)
    LastFocusedPanel = 0;
  // ShowDeletedFiles = Read_ShowDeleted();

  CListMode listMode;
  listMode.Read();
  
  for (i = 0; i < kNumPanelsMax; i++)
  {
    CPanel &panel = Panels[i];
    panel._listViewMode = listMode.Panels[i];
    panel._xSize = xSizes[i];
    panel._flatModeForArc = ReadFlatView(i);
  }
  
  for (i = 0; i < kNumPanelsMax; i++)
  {
    unsigned panelIndex = i;
    if (needOpenArc && LastFocusedPanel == 1)
      panelIndex = 1 - i;

    bool isMainPanel = (panelIndex == LastFocusedPanel);

    if (NumPanels > 1 || isMainPanel)
    {
      if (NumPanels == 1)
        Panels[panelIndex]._xSize = xSizes[0] + xSizes[1];
      
      COpenResult openRes2;
      UString path;
      if (isMainPanel)
        path = mainPath;
      
      RINOK(CreateOnePanel(panelIndex, path, arcFormat,
          isMainPanel && needOpenArc,
          *(isMainPanel ? &openRes : &openRes2)))
      
      if (isMainPanel)
      {
        if (needOpenArc && !openRes.ArchiveIsOpened)
          return S_OK;
      }
    }
  }
  
  SetFocusedPanel(LastFocusedPanel);
  Panels[LastFocusedPanel].SetFocusToList();
  return S_OK;
}


HRESULT CApp::SwitchOnOffOnePanel()
{
  if (NumPanels == 1)
  {
    NumPanels++;
    COpenResult openRes;
    RINOK(CreateOnePanel(1 - LastFocusedPanel, UString(), UString(),
        false, // needOpenArc
        openRes))
    Panels[1 - LastFocusedPanel].Enable(true);
    Panels[1 - LastFocusedPanel].Show(SW_SHOWNORMAL);
  }
  else
  {
    NumPanels--;
    Panels[1 - LastFocusedPanel].Enable(false);
    Panels[1 - LastFocusedPanel].Show(SW_HIDE);
  }
  MoveSubWindows();
  return S_OK;
}

void CApp::Save()
{
  AppState.Save();
  CListMode listMode;
  
  for (unsigned i = 0; i < kNumPanelsMax; i++)
  {
    const CPanel &panel = Panels[i];
    UString path;
    if (panel._parentFolders.IsEmpty())
      path = panel._currentFolderPrefix;
    else
      path = panel._parentFolders[0].ParentFolderPath;
      // GetFolderPath(panel._parentFolders[0].ParentFolder);
    SavePanelPath(i, path);
    listMode.Panels[i] = panel.GetListViewMode();
    SaveFlatView(i, panel._flatModeForArc);
  }
  
  listMode.Save();
  // Save_ShowDeleted(ShowDeletedFiles);
}

void CApp::ReleaseApp()
{
  // 24.09: ReleasePanel() will stop panel timer processing.
  // but we want to stop timer processing for all panels
  // before ReleasePanel() calling.
  unsigned i;
  for (i = 0; i < kNumPanelsMax; i++)
    Panels[i].Disable_Processing_Timer_Notify_StatusBar();
  // It's for unloading COM dll's: don't change it.
  for (i = 0; i < kNumPanelsMax; i++)
    Panels[i].ReleasePanel();
}

// reduces path to part that exists on disk (or root prefix of path)
// output path is normalized (with WCHAR_PATH_SEPARATOR)
static void Reduce_Path_To_RealFileSystemPath(UString &path)
{
  unsigned prefixSize = GetRootPrefixSize(path);

  while (!path.IsEmpty())
  {
    if (NFind::DoesDirExist_FollowLink(us2fs(path)))
    {
      NName::NormalizeDirPathPrefix(path);
      break;
    }
    int pos = path.ReverseFind_PathSepar();
    if (pos < 0)
    {
      path.Empty();
      break;
    }
    path.DeleteFrom((unsigned)(pos + 1));
    if ((unsigned)pos + 1 == prefixSize)
      break;
    path.DeleteFrom((unsigned)pos);
  }
}

// returns: true, if such dir exists or is root
/*
static bool CheckFolderPath(const UString &path)
{
  UString pathReduced = path;
  Reduce_Path_To_RealFileSystemPath(pathReduced);
  return (pathReduced == path);
}
*/

extern UString ConvertSizeToString(UInt64 value);

static void AddSizeValue(UString &s, UInt64 size)
{
  s += MyFormatNew(IDS_FILE_SIZE, ConvertSizeToString(size));
}

static void AddValuePair1(UString &s, UINT resourceID, UInt64 size)
{
  AddLangString(s, resourceID);
  s += ": ";
  AddSizeValue(s, size);
  s.Add_LF();
}

void AddValuePair2(UString &s, UINT resourceID, UInt64 num, UInt64 size);
void AddValuePair2(UString &s, UINT resourceID, UInt64 num, UInt64 size)
{
  if (num == 0)
    return;
  AddLangString(s, resourceID);
  s += ": ";
  s += ConvertSizeToString(num);

  if (size != (UInt64)(Int64)-1)
  {
    s += "    ( ";
    AddSizeValue(s, size);
    s += " )";
  }
  s.Add_LF();
}

static void AddPropValueToSum(IFolderFolder *folder, UInt32 index, PROPID propID, UInt64 &sum)
{
  if (sum == (UInt64)(Int64)-1)
    return;
  NCOM::CPropVariant prop;
  folder->GetProperty(index, propID, &prop);
  UInt64 val = 0;
  if (ConvertPropVariantToUInt64(prop, val))
    sum += val;
  else
    sum = (UInt64)(Int64)-1;
}

UString CPanel::GetItemsInfoString(const CRecordVector<UInt32> &indices)
{
  UString info;
  UInt64 numDirs, numFiles, filesSize, foldersSize;
  numDirs = numFiles = filesSize = foldersSize = 0;
  
  unsigned i;
  for (i = 0; i < indices.Size(); i++)
  {
    const UInt32 index = indices[i];
    if (IsItem_Folder(index))
    {
      AddPropValueToSum(_folder, index, kpidSize, foldersSize);
      numDirs++;
    }
    else
    {
      AddPropValueToSum(_folder, index, kpidSize, filesSize);
      numFiles++;
    }
  }

  AddValuePair2(info, IDS_PROP_FOLDERS, numDirs, foldersSize);
  AddValuePair2(info, IDS_PROP_FILES, numFiles, filesSize);
  int numDefined = ((foldersSize != (UInt64)(Int64)-1) && foldersSize != 0) ? 1: 0;
  numDefined += ((filesSize != (UInt64)(Int64)-1) && filesSize != 0) ? 1: 0;
  if (numDefined == 2)
    AddValuePair1(info, IDS_PROP_SIZE, filesSize + foldersSize);
  
  info.Add_LF();
  info += _currentFolderPrefix;
  
  for (i = 0; i < indices.Size() && (int)i < (int)kCopyDialog_NumInfoLines - 6; i++)
  {
    info.Add_LF();
    info += "  ";
    const UInt32 index = indices[i];
    info += GetItemRelPath(index);
    if (IsItem_Folder(index))
      info.Add_PathSepar();
  }
  if (i != indices.Size())
  {
    info.Add_LF();
    info += "  ...";
  }
  return info;
}

bool IsCorrectFsName(const UString &name);



/* Returns true, if path is path that can be used as path for File System functions
*/

/*
static bool IsFsPath(const FString &path)
{
  if (!IsAbsolutePath(path))
    return false;
  unsigned prefixSize = GetRootPrefixSize(path);
}
*/

void CApp::OnCopy(bool move, bool copyToSame, unsigned srcPanelIndex)
{
  const unsigned destPanelIndex = (NumPanels <= 1) ? srcPanelIndex : (1 - srcPanelIndex);
  CPanel &srcPanel = Panels[srcPanelIndex];
  CPanel &destPanel = Panels[destPanelIndex];

  CPanel::CDisableTimerProcessing disableTimerProcessing1(destPanel);
  CPanel::CDisableTimerProcessing disableTimerProcessing2(srcPanel);

  if (move)
  {
    if (!srcPanel.CheckBeforeUpdate(IDS_MOVE))
      return;
  }
  else if (!srcPanel.DoesItSupportOperations())
  {
    srcPanel.MessageBox_Error_UnsupportOperation();
    return;
  }

  CRecordVector<UInt32> indices;
  UString destPath;
  bool useDestPanel = false;

  {
    if (copyToSame)
    {
      const int focusedItem = srcPanel._listView.GetFocusedItem();
      if (focusedItem < 0)
        return;
      const unsigned realIndex = srcPanel.GetRealItemIndex(focusedItem);
      if (realIndex == kParentIndex)
        return;
      indices.Add(realIndex);
      destPath = srcPanel.GetItemName(realIndex);
    }
    else
    {
      srcPanel.Get_ItemIndices_OperSmart(indices);
      if (indices.Size() == 0)
        return;
      destPath = destPanel.GetFsPath();
      if (NumPanels == 1)
        Reduce_Path_To_RealFileSystemPath(destPath);
    }
  }
  
  UStringVector copyFolders;
  ReadCopyHistory(copyFolders);
  
  const bool useFullItemPaths = srcPanel.Is_IO_FS_Folder(); // maybe we need flat also here ??

  {
    CCopyDialog copyDialog;

    copyDialog.Strings = copyFolders;
    copyDialog.Value = destPath;
    LangString(move ? IDS_MOVE : IDS_COPY, copyDialog.Title);
    LangString(move ? IDS_MOVE_TO : IDS_COPY_TO, copyDialog.Static);
    copyDialog.Info = srcPanel.GetItemsInfoString(indices);

    if (copyDialog.Create(srcPanel.GetParent()) != IDOK)
      return;

    destPath = copyDialog.Value;
  }

  {
    if (destPath.IsEmpty())
    {
      srcPanel.MessageBox_Error_UnsupportOperation();
      return;
    }

    UString correctName;
    if (!srcPanel.CorrectFsPath(destPath, correctName))
    {
      srcPanel.MessageBox_Error_HRESULT(E_INVALIDARG);
      return;
    }

    if (IsAbsolutePath(destPath))
      destPath.Empty();
    else
      destPath = srcPanel.GetFsPath();
    destPath += correctName;

    #if defined(_WIN32) && !defined(UNDER_CE)
    if (destPath.Len() != 0 && destPath[0] == '\\')
      if (destPath.Len() == 1 || destPath[1] != '\\')
      {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
      }
    #endif

    bool possibleToUseDestPanel = false;

    if (CompareFileNames(destPath, destPanel.GetFsPath()) == 0)
    {
      if (NumPanels == 1 || CompareFileNames(destPath, srcPanel.GetFsPath()) == 0)
      {
        srcPanel.MessageBox_Error(L"Cannot copy files onto itself");
        return;
      }

      if (destPanel.DoesItSupportOperations())
        possibleToUseDestPanel = true;
    }

    bool destIsFsPath = false;

    if (possibleToUseDestPanel)
    {
      if (destPanel.IsFSFolder() || destPanel.IsAltStreamsFolder())
        destIsFsPath = true;
      else if (destPanel.IsFSDrivesFolder() || destPanel.IsRootFolder())
      {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
      }
    }
    else
    {
      if (IsAltPathPrefix(us2fs(destPath)))
      {
        // we allow alt streams dest only to alt stream folder in second panel
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
        /*
        FString basePath = us2fs(destPath);
        basePath.DeleteBack();
        if (!DoesFileOrDirExist(basePath))
        {
          srcPanel.MessageBoxError2Lines(basePath, ERROR_FILE_NOT_FOUND); // GetLastError()
          return;
        }
        destIsFsPath = true;
        */
      }
      else
      {
        if (indices.Size() == 1 &&
          !destPath.IsEmpty() && !IS_PATH_SEPAR(destPath.Back()))
        {
          int pos = destPath.ReverseFind_PathSepar();
          if (pos < 0)
          {
            srcPanel.MessageBox_Error_UnsupportOperation();
            return;
          }
          {
            /*
            #ifdef _WIN32
            UString name = destPath.Ptr(pos + 1);
            if (name.Find(L':') >= 0)
            {
              srcPanel.MessageBox_Error_UnsupportOperation();
              return;
            }
            #endif
            */
            UString prefix = destPath.Left(pos + 1);
            if (!CreateComplexDir(us2fs(prefix)))
            {
              const HRESULT lastError = GetLastError_noZero_HRESULT();
              srcPanel.MessageBox_Error_2Lines_Message_HRESULT(prefix, lastError);
              return;
            }
          }
          // bool isFolder = srcPanael.IsItem_Folder(indices[0]);
        }
        else
        {
          NName::NormalizeDirPathPrefix(destPath);
          if (!CreateComplexDir(us2fs(destPath)))
          {
            const HRESULT lastError = GetLastError_noZero_HRESULT();
            srcPanel.MessageBox_Error_2Lines_Message_HRESULT(destPath, lastError);
            return;
          }
        }
        destIsFsPath = true;
      }
    }

    if (!destIsFsPath)
      useDestPanel = true;

    AddUniqueStringToHeadOfList(copyFolders, destPath);
    while (copyFolders.Size() > 20)
      copyFolders.DeleteBack();
    SaveCopyHistory(copyFolders);
  }

  bool useSrcPanel = !useDestPanel || !srcPanel.Is_IO_FS_Folder();

  bool useTemp = useSrcPanel && useDestPanel;
  if (useTemp && NumPanels == 1)
  {
    srcPanel.MessageBox_Error_UnsupportOperation();
    return;
  }
  
  CTempDir tempDirectory;
  FString tempDirPrefix;
  if (useTemp)
  {
    tempDirectory.Create(kTempDirPrefix);
    tempDirPrefix = tempDirectory.GetPath();
    NFile::NName::NormalizeDirPathPrefix(tempDirPrefix);
  }

  CSelectedState srcSelState;
  CSelectedState destSelState;
  srcPanel.SaveSelectedState(srcSelState);
  destPanel.SaveSelectedState(destSelState);

  CPanel::CDisableNotify disableNotify1(destPanel);
  CPanel::CDisableNotify disableNotify2(srcPanel);

  HRESULT result = S_OK;
  
  if (useSrcPanel)
  {
    CCopyToOptions options;
    // options.src_Is_IO_FS_Folder = useFullItemPaths;
    options.folder = useTemp ? fs2us(tempDirPrefix) : destPath;
    options.moveMode = move;
    options.includeAltStreams = true;
    options.replaceAltStreamChars = false;
    options.showErrorMessages = true;

    result = srcPanel.CopyTo(options, indices, NULL);
  }
  
  if (result == S_OK && useDestPanel)
  {
    UStringVector filePaths;
    UString folderPrefix;
    
    if (useTemp)
      folderPrefix = fs2us(tempDirPrefix);
    else
      folderPrefix = srcPanel.GetFsPath();
    
    filePaths.ClearAndReserve(indices.Size());
    
    FOR_VECTOR (i, indices)
    {
      UInt32 index = indices[i];
      UString s;
      if (useFullItemPaths)
        s = srcPanel.GetItemRelPath2(index);
      else
        s = srcPanel.GetItemName_for_Copy(index);
      filePaths.AddInReserved(s);
    }
    
    result = destPanel.CopyFrom(move, folderPrefix, filePaths, true, NULL);
  }
  
  if (result != S_OK)
  {
    // disableNotify1.Restore();
    // disableNotify2.Restore();
    // For Password:
    // srcPanel.SetFocusToList();
    // srcPanel.InvalidateList(NULL, true);

    if (result != E_ABORT)
      srcPanel.MessageBox_Error_HRESULT(result);
    // return;
  }

  RefreshTitleAlways();
  
  if (copyToSame || move)
  {
    srcPanel.RefreshListCtrl(srcSelState);
  }
  
  if (!copyToSame)
  {
    destPanel.RefreshListCtrl(destSelState);
    srcPanel.KillSelection();
  }

  disableNotify1.Restore();
  disableNotify2.Restore();
  srcPanel.SetFocusToList();
}

void CApp::OnSetSameFolder(unsigned srcPanelIndex)
{
  if (NumPanels <= 1)
    return;
  const CPanel &srcPanel = Panels[srcPanelIndex];
  CPanel &destPanel = Panels[1 - srcPanelIndex];
  destPanel.BindToPathAndRefresh(srcPanel._currentFolderPrefix);
}

void CApp::OnSetSubFolder(unsigned srcPanelIndex)
{
  if (NumPanels <= 1)
    return;
  const CPanel &srcPanel = Panels[srcPanelIndex];
  CPanel &destPanel = Panels[1 - srcPanelIndex];

  const int focusedItem = srcPanel._listView.GetFocusedItem();
  if (focusedItem < 0)
    return;
  const unsigned realIndex = srcPanel.GetRealItemIndex(focusedItem);
  if (!srcPanel.IsItem_Folder(realIndex))
    return;

  // destPanel.BindToFolder(srcPanel._currentFolderPrefix + srcPanel.GetItemName(realIndex) + WCHAR_PATH_SEPARATOR);

  CMyComPtr<IFolderFolder> newFolder;
  if (realIndex == kParentIndex)
  {
    if (srcPanel._folder->BindToParentFolder(&newFolder) != S_OK)
      return;
    if (!newFolder)
    {
      {
        const UString parentPrefix = srcPanel.GetParentDirPrefix();
        COpenResult openRes;
        destPanel.BindToPath(parentPrefix, UString(), openRes);
      }
      destPanel.RefreshListCtrl();
      return;
    }
  }
  else
  {
    if (srcPanel._folder->BindToFolder(realIndex, &newFolder) != S_OK)
      return;
  }

  if (!newFolder)
    return;

  destPanel.CloseOpenFolders();
  destPanel.SetNewFolder(newFolder);
  destPanel.RefreshListCtrl();
}

/*
int CApp::GetFocusedPanelIndex() const
{
  return LastFocusedPanel;
  HWND hwnd = ::GetFocus();
  for (;;)
  {
    if (hwnd == 0)
      return 0;
    for (unsigned i = 0; i < kNumPanelsMax; i++)
    {
      if (PanelsCreated[i] &&
          ((HWND)Panels[i] == hwnd || Panels[i]._listView == hwnd))
        return i;
    }
    hwnd = GetParent(hwnd);
  }
}
*/

static UString g_ToolTipBuffer;
static CSysString g_ToolTipBufferSys;

// Tooltip text for the remaining stock toolbars (panel header, dialogs).
// The modern command bar carries its own tooltips.
static void Modern_GetCommandTip(int cmd, UString &s)
{
  switch (cmd)
  {
    case kMenuCmdID_Toolbar_Add:     s = LangString(IDS_ADD); break;
    case kMenuCmdID_Toolbar_Extract: s = LangString(IDS_EXTRACT); break;
    case kMenuCmdID_Toolbar_Test:    s = LangString(IDS_TEST); break;
    case IDM_COPY_TO:                s = LangString(IDS_BUTTON_COPY); break;
    case IDM_MOVE_TO:                s = LangString(IDS_BUTTON_MOVE); break;
    case IDM_DELETE:                 s = LangString(IDS_BUTTON_DELETE); break;
    case IDM_PROPERTIES:             s = LangString(IDS_BUTTON_INFO); break;
    default: s.Empty();
  }
}

void CApp::OnNotify(int /* ctrlID */, LPNMHDR pnmh)
{
  {
    if (pnmh->code == TTN_GETDISPINFO)
    {
      LPNMTTDISPINFO info = (LPNMTTDISPINFO)pnmh;
      info->hinst = NULL;
      g_ToolTipBuffer.Empty();
      Modern_GetCommandTip((int)info->hdr.idFrom, g_ToolTipBuffer);
      g_ToolTipBufferSys = GetSystemString(g_ToolTipBuffer);
      info->lpszText = g_ToolTipBufferSys.Ptr_non_const();
      return;
    }
    #ifndef _UNICODE
    if (pnmh->code == TTN_GETDISPINFOW)
    {
      LPNMTTDISPINFOW info = (LPNMTTDISPINFOW)pnmh;
      info->hinst = NULL;
      g_ToolTipBuffer.Empty();
      Modern_GetCommandTip((int)info->hdr.idFrom, g_ToolTipBuffer);
      info->lpszText = g_ToolTipBuffer.Ptr_non_const();
      return;
    }
    #endif
  }
}

void CApp::RefreshTitle(bool always)
{
  UString path = GetFocusedPanel()._currentFolderPrefix;
  if (path.IsEmpty())
    path = "7-Zip Modern"; // LangString(IDS_APP_TITLE);
  if (!always && path == PrevTitle)
    return;
  PrevTitle = path;
  NWindows::MySetWindowText(_window, path);
}

void CApp::RefreshTitlePanel(unsigned panelIndex, bool always)
{
  if (panelIndex != GetFocusedPanelIndex())
    return;
  RefreshTitle(always);
}

static void AddUniqueStringToHead(UStringVector &list, const UString &s)
{
  for (unsigned i = 0; i < list.Size();)
    if (s.IsEqualTo_NoCase(list[i]))
      list.Delete(i);
    else
      i++;
  list.Insert(0, s);
}


void CFolderHistory::Normalize()
{
  const unsigned kMaxSize = 100;
  if (Strings.Size() > kMaxSize)
    Strings.DeleteFrom(kMaxSize);
}

void CFolderHistory::AddString(const UString &s)
{
  NSynchronization::CCriticalSectionLock lock(_criticalSection);
  AddUniqueStringToHead(Strings, s);
  Normalize();
}

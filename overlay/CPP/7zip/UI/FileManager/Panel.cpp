// Panel.cpp

#include "StdAfx.h"

#include <windowsx.h>

#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"

#include "../../../Windows/ErrorMsg.h"
#include "../../../Windows/FileName.h"
#include "../../../Windows/PropVariant.h"
#include "../../../Windows/Thread.h"

#include "../../PropID.h"

#include "resource.h"
#include "../GUI/ExtractRes.h"

#include "../Common/ArchiveName.h"
#include "../Common/CompressCall.h"
#include "../Common/ZipRegistry.h"

#include "../Agent/IFolderArchive.h"

#include "App.h"
#include "ExtractCallback.h"
#include "FSFolder.h"
#include "FormatUtils.h"
#include "LangUtils.h"
#include "Panel.h"
#include "RootFolder.h"

#include "PropertyNameRes.h"

using namespace NWindows;
using namespace NControl;

#ifndef _UNICODE
extern bool g_IsNT;
#endif

static const UINT_PTR kTimerID = 1;
static const UINT kTimerElapse = 1000;

static DWORD kStyles[4] = { LVS_ICON, LVS_SMALLICON, LVS_LIST, LVS_REPORT };

// 7-Zip Modern -------------------------------------------------------------
// One shared, DPI-aware list font. Derived from the shell message font so it
// always matches the language/theme of the OS, then scaled up slightly for
// comfortable row heights.
static HFONT Modern_ListFont()
{
  static HFONT font = NULL;
  static bool tried = false;
  if (tried)
    return font;
  tried = true;

  NONCLIENTMETRICSW ncm;
  ncm.cbSize = sizeof(ncm);
  if (!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    return NULL;

  LOGFONTW lf = ncm.lfMessageFont;
  if (lf.lfHeight < 0)
    lf.lfHeight = lf.lfHeight * 14 / 12;   // ~9pt -> ~10.5pt
  lf.lfQuality = CLEARTYPE_QUALITY;
  font = ::CreateFontIndirectW(&lf);
  return font;
}

static HFONT Modern_CreateListFont()
{
  return Modern_ListFont();
}

// static const int kCreateFolderID = 101;

extern HINSTANCE g_hInstance;

// 7-Zip Modern: chrome for the stock controls ------------------------------
//
// The path bar, its band and the status bar are stock comctl32 controls. They
// keep all their behaviour; only the painting is overridden so that they stop
// looking like Windows 95. Subclassing through GetProp/SetProp avoids adding
// a commctrl version dependency.

static const TCHAR * const kModernOldProc = TEXT("7zModernOldProc");
static HBRUSH g_ModernFieldBrush = NULL;

static LRESULT CALLBACK Modern_ChromeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  const WNDPROC oldProc = (WNDPROC)::GetProp(hwnd, kModernOldProc);
  if (!oldProc)
    return ::DefWindowProc(hwnd, msg, wParam, lParam);

  switch (msg)
  {
    case WM_ERASEBKGND:
    {
      RECT rc;
      ::GetClientRect(hwnd, &rc);
      NModernUI::FillSolid((HDC)wParam, rc, NModernUI::Col_Band());
      return 1;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
      HDC dc = (HDC)wParam;
      ::SetBkColor(dc, NModernUI::Col_Field());
      ::SetTextColor(dc, NModernUI::Col_Ink());
      if (!g_ModernFieldBrush)
        g_ModernFieldBrush = ::CreateSolidBrush(NModernUI::Col_Field());
      return (LRESULT)g_ModernFieldBrush;
    }

    case WM_CTLCOLORBTN:
    {
      if (!g_ModernFieldBrush)
        g_ModernFieldBrush = ::CreateSolidBrush(NModernUI::Col_Field());
      return (LRESULT)g_ModernFieldBrush;
    }

    case WM_NOTIFY:
    {
      LPNMHDR hdr = (LPNMHDR)lParam;
      if (hdr && hdr->code == NM_CUSTOMDRAW)
      {
        LPNMTBCUSTOMDRAW cd = (LPNMTBCUSTOMDRAW)lParam;
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
        {
          cd->clrBtnFace = NModernUI::Col_Band();
          cd->clrBtnHighlight = NModernUI::Col_Band();
          cd->clrMark = NModernUI::Col_Hairline();
          cd->clrText = NModernUI::Col_Ink();
          cd->clrHighlightHotTrack = NModernUI::Col_Hover();
          cd->nStringBkMode = TRANSPARENT;
          return CDRF_NEWFONT;
        }
      }
      break;
    }

    case WM_PAINT:
    {
      const LRESULT res = ::CallWindowProc(oldProc, hwnd, msg, wParam, lParam);
      HDC dc = ::GetDC(hwnd);
      if (dc)
      {
        RECT rc;
        ::GetClientRect(hwnd, &rc);
        NModernUI::FillSolid(dc, rc.left, rc.bottom - 1, rc.right, rc.bottom,
            NModernUI::Col_Hairline());
        ::ReleaseDC(hwnd, dc);
      }
      return res;
    }
  }

  return ::CallWindowProc(oldProc, hwnd, msg, wParam, lParam);
}

static void Modern_Subclass(HWND hwnd)
{
  if (!hwnd || ::GetProp(hwnd, kModernOldProc))
    return;
  const WNDPROC oldProc = (WNDPROC)::SetWindowLongPtr(hwnd, GWLP_WNDPROC,
      (LONG_PTR)Modern_ChromeProc);
  ::SetProp(hwnd, kModernOldProc, (HANDLE)oldProc);
}

// The status bar needs a proc of its own. comctl32 draws the stock panes with
// a raised bevel and there is no notification that lets us restyle them, so
// the parts are measured with SB_GETPARTS and painted from scratch. Every
// other message is forwarded, which keeps the stored text, the owner
// notifications and the auto-invalidation on SB_SETTEXT working.

static const TCHAR * const kModernStatusOldProc = TEXT("7zModernStatusOldProc");

static LRESULT CALLBACK Modern_StatusProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  const WNDPROC oldProc = (WNDPROC)::GetProp(hwnd, kModernStatusOldProc);
  if (!oldProc)
    return ::DefWindowProc(hwnd, msg, wParam, lParam);

  if (msg == WM_ERASEBKGND)
    return 1;

  if (msg == WM_PAINT)
  {
    PAINTSTRUCT ps;
    HDC dc = ::BeginPaint(hwnd, &ps);
    if (!dc)
      return 0;

    RECT rc;
    ::GetClientRect(hwnd, &rc);
    NModernUI::FillSolid(dc, rc, NModernUI::Col_Band());
    // a single hairline instead of the sunken top edge
    NModernUI::FillSolid(dc, rc.left, rc.top, rc.right, rc.top + 1,
        NModernUI::Col_Hairline());

    const int kMaxParts = 16;
    int edges[kMaxParts];
    ::ZeroMemory(edges, sizeof(edges));
    const int numParts = (int)::SendMessageW(hwnd, SB_GETPARTS, kMaxParts,
        (LPARAM)edges);

    // Reuse the control's font: the bar height was measured from it.
    HFONT font = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
    if (!font)
      font = NModernUI::Font_Bar();

    int left = rc.left;
    for (int i = 0; i < numParts && i < kMaxParts; i++)
    {
      int right = edges[i];
      if (right <= 0 || right > rc.right)
        right = rc.right;

      if (right > left)
      {
        WCHAR text[512];
        text[0] = 0;
        const LRESULT res = ::SendMessageW(hwnd, SB_GETTEXTW, (WPARAM)i,
            (LPARAM)text);
        // SBT_OWNERDRAW parts are not used by the file manager; skip them so
        // that their empty text does not wipe the band.
        if (LOWORD(res) != SBT_OWNERDRAW && text[0])
        {
          RECT tr = rc;
          tr.left = left + NModernUI::Scale(8);
          tr.right = right - NModernUI::Scale(8);
          ::IntersectRect(&tr, &tr, &rc);
          NModernUI::DrawTextIn(dc, tr, text, NModernUI::Col_InkDim(),
              font,
              DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
      }

      // hairline between adjacent parts, inset so it reads as a separator and
      // not as a box
      if (i + 1 < numParts && right < rc.right)
        NModernUI::FillSolid(dc, right - 1, rc.top + NModernUI::Scale(7),
            right, rc.bottom - NModernUI::Scale(7),
            NModernUI::Col_Hairline());

      left = right;
    }

    ::EndPaint(hwnd, &ps);
    return 0;
  }

  return ::CallWindowProc(oldProc, hwnd, msg, wParam, lParam);
}

static void Modern_StyleHeader(HWND rebar, HWND toolBar, HWND combo, HWND edit)
{
  if (rebar)
    Modern_Subclass(rebar);
  if (toolBar)
    Modern_Subclass(toolBar);
  if (combo)
  {
    ::SendMessageW(combo, WM_SETFONT, (WPARAM)NModernUI::Font_Header(), TRUE);
    NModernUI::RoundCorners(combo, 10);
  }
  if (edit)
    ::SendMessageW(edit, WM_SETFONT, (WPARAM)NModernUI::Font_Header(), TRUE);
}

static void Modern_RefreshHeader(HWND rebar)
{
  if (rebar)
    ::InvalidateRect(rebar, NULL, TRUE);
}

static void Modern_StyleStatusBar(HWND statusBar)
{
  if (!statusBar || ::GetProp(statusBar, kModernStatusOldProc))
    return;
  ::SendMessage(statusBar, SB_SETBKCOLOR, 0, (LPARAM)NModernUI::Col_Band());
  // The bar derives its height from this font, so it stays in proportion.
  ::SendMessageW(statusBar, WM_SETFONT, (WPARAM)NModernUI::Font_Bar(), TRUE);
  const WNDPROC oldProc = (WNDPROC)::SetWindowLongPtr(statusBar, GWLP_WNDPROC,
      (LONG_PTR)Modern_StatusProc);
  ::SetProp(statusBar, kModernStatusOldProc, (HANDLE)oldProc);
}

void CPanel::ReleasePanel()
{
  Disable_Processing_Timer_Notify_StatusBar();
  // It's for unloading COM dll's: don't change it.
  CloseOpenFolders();
  _sevenZipContextMenu.Release();
  _systemContextMenu.Release();
}

CPanel::~CPanel()
{
  CloseOpenFolders();
}

HWND CPanel::GetParent() const
{
  const HWND h = CWindow2::GetParent();
  return h ? h : _mainWindow;
}

#define kClassName L"7-Zip::Panel"


HRESULT CPanel::Create(HWND mainWindow, HWND parentWindow, UINT id,
    const UString &currentFolderPrefix,
    const UString &arcFormat,
    CPanelCallback *panelCallback, CAppState *appState,
    bool needOpenArc,
    COpenResult &openRes)
{
  _mainWindow = mainWindow;
  _processTimer = true;
  _processNotify = true;
  _processStatusBar = true;

  _panelCallback = panelCallback;
  _appState = appState;
  // _index = index;
  _baseID = id;
  _comboBoxID = _baseID + 3;
  _statusBarID = _comboBoxID + 1;

  UString cfp = currentFolderPrefix;

  if (!currentFolderPrefix.IsEmpty())
    if (currentFolderPrefix[0] == L'.')
    {
      FString cfpF;
      if (NFile::NDir::MyGetFullPathName(us2fs(currentFolderPrefix), cfpF))
        cfp = fs2us(cfpF);
    }

  RINOK(BindToPath(cfp, arcFormat, openRes))

  if (needOpenArc && !openRes.ArchiveIsOpened)
    return S_OK;

  if (!CreateEx(0, kClassName, NULL, WS_CHILD | WS_VISIBLE,
      0, 0, _xSize, 260,
      parentWindow, (HMENU)(UINT_PTR)id, g_hInstance))
    return E_FAIL;
  PanelCreated = true;

  return S_OK;
}

// extern UInt32 g_NumMessages;

LRESULT CPanel::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  // g_NumMessages++;
  switch (message)
  {
    case kShiftSelectMessage:
      OnShiftSelectMessage();
      return 0;
    case kReLoadMessage:
      RefreshListCtrl(_selectedState);
      return 0;
    case kSetFocusToListView:
      _listView.SetFocus();
      return 0;
    case kOpenItemChanged:
      return OnOpenItemChanged(lParam);
    case kRefresh_StatusBar:
      if (_processStatusBar)
        Refresh_StatusBar();
      return 0;
    #ifdef UNDER_CE
    case kRefresh_HeaderComboBox:
      LoadFullPathAndShow();
      return 0;
    #endif
    case WM_TIMER:
      OnTimer();
      return 0;
    case WM_CONTEXTMENU:
      if (OnContextMenu(HANDLE(wParam), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
        return 0;
      break;
    /*
    case WM_DROPFILES:
      CompressDropFiles(HDROP(wParam));
      return 0;
    */
  }
  return CWindow2::OnMessage(message, wParam, lParam);
}

LRESULT CMyListView::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  if (message == WM_CHAR)
  {
    UINT scanCode = (UINT)((lParam >> 16) & 0xFF);
    bool extended = ((lParam & 0x1000000) != 0);
    UINT virtualKey = MapVirtualKey(scanCode, 1);
    if (virtualKey == VK_MULTIPLY || virtualKey == VK_ADD ||
        virtualKey == VK_SUBTRACT)
      return 0;
    if ((wParam == '/' && extended)
        || wParam == '\\' || wParam == '/')
    {
      _panel->OpenDrivesFolder();
      return 0;
    }
  }
  else if (message == WM_SYSCHAR)
  {
    // For Alt+Enter Beep disabling
    UINT scanCode = (UINT)(lParam >> 16) & 0xFF;
    UINT virtualKey = MapVirtualKey(scanCode, 1);
    if (virtualKey == VK_RETURN || virtualKey == VK_MULTIPLY ||
        virtualKey == VK_ADD || virtualKey == VK_SUBTRACT)
      return 0;
  }
  /*
  else if (message == WM_SYSKEYDOWN)
  {
    // return 0;
  }
  */
  else if (message == WM_KEYDOWN)
  {
    bool alt = IsKeyDown(VK_MENU);
    bool ctrl = IsKeyDown(VK_CONTROL);
    bool shift = IsKeyDown(VK_SHIFT);
    switch (wParam)
    {
      /*
      case VK_RETURN:
      {
        if (shift && !alt && !ctrl)
        {
          _panel->OpenSelectedItems(false);
          return 0;
        }
        break;
      }
      */
      case VK_NEXT:
      {
        if (ctrl && !alt && !shift)
        {
          _panel->OpenFocusedItemAsInternal();
          return 0;
        }
        break;
      }
      case VK_PRIOR:
      if (ctrl && !alt && !shift)
      {
        _panel->OpenParentFolder();
        return 0;
      }
    }
  }
  #ifdef UNDER_CE
  else if (message == WM_KEYUP)
  {
    if (wParam == VK_F2) // it's VK_TSOFT2
    {
      // Activate Menu
      ::PostMessage(g_HWND, WM_SYSCOMMAND, SC_KEYMENU, 0);
      return 0;
    }
  }
  #endif
  else if (message == WM_SETFOCUS)
  {
    _panel->_lastFocusedIsList = true;
    _panel->_panelCallback->PanelWasFocused();
  }
  return CListView2::OnMessage(message, wParam, lParam);
}

/*
static LRESULT APIENTRY ComboBoxSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  CWindow tempDialog(hwnd);
  CMyComboBox *w = (CMyComboBox *)(tempDialog.GetUserDataLongPtr());
  if (w == NULL)
    return 0;
  return w->OnMessage(message, wParam, lParam);
}

LRESULT CMyComboBox::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  return CallWindowProc(_origWindowProc, *this, message, wParam, lParam);
}
*/

#ifndef UNDER_CE

static LRESULT APIENTRY ComboBoxEditSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  CWindow tempDialog(hwnd);
  CMyComboBoxEdit *w = (CMyComboBoxEdit *)(tempDialog.GetUserDataLongPtr());
  if (w == NULL)
    return 0;
  return w->OnMessage(message, wParam, lParam);
}

#endif

LRESULT CMyComboBoxEdit::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  // See MSDN / Subclassing a Combo Box / Creating a Combo-box Toolbar
  switch (message)
  {
    case WM_SYSKEYDOWN:
      switch (wParam)
      {
        case VK_F1:
        case VK_F2:
        {
          // check ALT
          if ((lParam & (1<<29)) == 0)
            break;
          bool alt = IsKeyDown(VK_MENU);
          bool ctrl = IsKeyDown(VK_CONTROL);
          bool shift = IsKeyDown(VK_SHIFT);
          if (alt && !ctrl && !shift)
          {
            _panel->_panelCallback->SetFocusToPath(wParam == VK_F1 ? 0 : 1);
            return 0;
          }
          break;
        }
      }
      break;
    case WM_KEYDOWN:
      switch (wParam)
      {
        case VK_TAB:
          // SendMessage(hwndMain, WM_ENTER, 0, 0);
          _panel->SetFocusToList();
          return 0;
        case VK_F9:
        {
          bool alt = IsKeyDown(VK_MENU);
          bool ctrl = IsKeyDown(VK_CONTROL);
          bool shift = IsKeyDown(VK_SHIFT);
          if (!alt && !ctrl && !shift)
          {
            g_App.SwitchOnOffOnePanel();
            return 0;
          }
          break;
        }
        case 'W':
        {
          bool ctrl = IsKeyDown(VK_CONTROL);
          if (ctrl)
          {
            PostMessage(g_HWND, WM_COMMAND, IDCLOSE, 0);
            return 0;
          }
          break;
        }
      }
      break;
    case WM_CHAR:
      switch (wParam)
      {
        case VK_TAB:
        case VK_ESCAPE:
          return 0;
      }
  }
  #ifndef _UNICODE
  if (g_IsNT)
    return CallWindowProcW(_origWindowProc, *this, message, wParam, lParam);
  else
  #endif
    return CallWindowProc(_origWindowProc, *this, message, wParam, lParam);
}


/*
  REBARBANDINFO in vista (_WIN32_WINNT >= 0x0600) has additional fields
  we want 2000/xp compatibility.
  so we must use reduced structure, if we compile with (_WIN32_WINNT >= 0x0600)
  Also there are additional fields, if (_WIN32_IE >= 0x0400).
    but (_WIN32_IE >= 0x0400) is expected.
  note:
  in x64 (64-bit):
  {
    (108 == REBARBANDINFO_V6_SIZE)
    (112 == sizeof(REBARBANDINFO) // for (_WIN32_WINNT <  0x0600)
    (128 == sizeof(REBARBANDINFO) // for (_WIN32_WINNT >= 0x0600)
    there is difference in sizes, because REBARBANDINFO size was
    not aligned for 8-bytes in (_WIN32_WINNT < 0x0600).
    We hope that WinVista+ support support both (108 and 112) sizes.
    But does WinXP-x64 support (108 == REBARBANDINFO_V6_SIZE)?
    {
         96   LPARAM  lParam;
        104   UINT    cxHeader;
      #if (_WIN32_WINNT >= 0x0600)
        108   RECT    rcChevronLocation;
        124   UINT    uChevronState;
      #endif
    }
*/

#if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0600) && defined(REBARBANDINFOA_V6_SIZE)
  #define my_compatib_REBARBANDINFO_size  REBARBANDINFO_V6_SIZE
#else
  #define my_compatib_REBARBANDINFO_size  sizeof(REBARBANDINFO)
#endif


bool CPanel::OnCreate(CREATESTRUCT * /* createStruct */)
{
  // _virtualMode = false;
  // _sortIndex = 0;
  _sortID = kpidName;
  _ascending = true;
  _lastFocusedIsList = true;

  DWORD style = WS_CHILD | WS_VISIBLE; //  | WS_BORDER ; // | LVS_SHAREIMAGELISTS; //  | LVS_SHOWSELALWAYS;

  style |= LVS_SHAREIMAGELISTS;
  // style  |= LVS_AUTOARRANGE;
  style |= WS_CLIPCHILDREN;
  style |= WS_CLIPSIBLINGS;

  const UInt32 kNumListModes = Z7_ARRAY_SIZE(kStyles);
  if (_listViewMode >= kNumListModes)
    _listViewMode = kNumListModes - 1;

  style |= kStyles[_listViewMode]
    | WS_TABSTOP
    | LVS_EDITLABELS;
  if (_mySelectMode)
    style |= LVS_SINGLESEL;

  /*
  if (_virtualMode)
    style |= LVS_OWNERDATA;
  */

  DWORD exStyle;
  exStyle = WS_EX_CLIENTEDGE;

  if (!_listView.CreateEx(exStyle, style, 0, 0, 116, 260,
      *this, (HMENU)(UINT_PTR)(_baseID + 1), g_hInstance, NULL))
    return false;

  _listView.SetUnicodeFormat();
  _listView._panel = this;
  _listView.SetWindowProc();

  _listView.SetImageList(Shell_Get_SysImageList_smallIcons(true), LVSIL_SMALL);
  _listView.SetImageList(Shell_Get_SysImageList_smallIcons(false), LVSIL_NORMAL);

  // 7-Zip Modern: step the list font up one size. The shell's message font is
  // the right family on every Windows version; going from ~9pt to ~10.5pt
  // gives the rows comfortable breathing room (the row height in report mode
  // follows the font metrics) which is the single biggest readability win.
  {
    HFONT listFont = Modern_CreateListFont();
    if (listFont)
    {
      ::SendMessage((HWND)_listView, WM_SETFONT, (WPARAM)listFont, TRUE);
      const HWND headerWnd = (HWND)::SendMessageW((HWND)_listView, LVM_GETHEADER, 0, 0);
      if (headerWnd)
        ::SendMessageW(headerWnd, WM_SETFONT, (WPARAM)NModernUI::Font_Header(), TRUE);
    }
  }

  // _exStyle |= LVS_EX_HEADERDRAGDROP;
  // DWORD extendedStyle = _listView.GetExtendedListViewStyle();
  // extendedStyle |= _exStyle;
  //  _listView.SetExtendedListViewStyle(extendedStyle);
  SetExtendedStyle();

  _listView.Show(SW_SHOW);
  _listView.InvalidateRect(NULL, true);
  _listView.Update();
  
  // Ensure that the common control DLL is loaded.
  INITCOMMONCONTROLSEX icex;

  icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icex.dwICC  = ICC_BAR_CLASSES;
  InitCommonControlsEx(&icex);

  const TBBUTTON tbb[] =
  {
    // {0, 0, TBSTATE_ENABLED, BTNS_SEP, 0L, 0},
    {0, kParentFolderID, TBSTATE_ENABLED, BTNS_BUTTON, { 0, 0 }, 0, 0 },
    // {0, 0, TBSTATE_ENABLED, BTNS_SEP, 0L, 0},
    // {VIEW_NEWFOLDER, kCreateFolderID, TBSTATE_ENABLED, BTNS_BUTTON, 0L, 0},
  };

#ifdef Z7_USE_DYN_ComCtl32Version
  if (g_ComCtl32Version >= MAKELONG(71, 4))
#endif
  {
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC  = ICC_COOL_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);
    
    // if there is no CCS_NOPARENTALIGN, there is space of some pixels after rebar (Incorrect GetWindowRect ?)

    // 7-Zip Modern: a borderless band instead of the classic 3D rebar.
    _headerReBar.Attach(::CreateWindowEx(WS_EX_TOOLWINDOW,
      REBARCLASSNAME,
      NULL, WS_VISIBLE | WS_CHILD |
      WS_CLIPCHILDREN | WS_CLIPSIBLINGS
      | CCS_NODIVIDER
      | CCS_NOPARENTALIGN
      | CCS_TOP
      | RBS_VARHEIGHT
      ,0,0,0,0, *this, NULL, g_hInstance, NULL));
  }

  DWORD toolbarStyle =  WS_CHILD | WS_VISIBLE ;
  if (_headerReBar)
  {
    toolbarStyle |= 0
      // | WS_CLIPCHILDREN
      // | WS_CLIPSIBLINGS

      | TBSTYLE_TOOLTIPS
      | CCS_NODIVIDER
      | CCS_NORESIZE
      | TBSTYLE_FLAT
      ;
  }

  _headerToolBar.Attach(::CreateToolbarEx ((*this), toolbarStyle,
      _baseID + 2, 11,
      (HINSTANCE)HINST_COMMCTRL,
      IDB_VIEW_SMALL_COLOR,
      (LPCTBBUTTON)&tbb, Z7_ARRAY_SIZE(tbb),
      0, 0, 0, 0, sizeof (TBBUTTON)));

  // 7-Zip Modern: swap the comctl32 "parent folder" artwork for our own glyph
  {
    HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 0);
    if (il)
    {
      HBITMAP bmp = ::LoadBitmap(g_hInstance, MAKEINTRESOURCE(IDB_UP));
      if (bmp)
      {
        ImageList_AddMasked(il, bmp, RGB(255, 0, 255));
        ::DeleteObject(bmp);
      }
      ::SendMessageW((HWND)_headerToolBar, TB_SETIMAGELIST, 0, (LPARAM)il);
    }
  }

  #ifndef UNDER_CE
  // Load ComboBoxEx class
  icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icex.dwICC = ICC_USEREX_CLASSES;
  InitCommonControlsEx(&icex);
  #endif
  
  _headerComboBox.CreateEx(0,
      #ifdef UNDER_CE
      WC_COMBOBOXW
      #else
      WC_COMBOBOXEXW
      #endif
      , NULL,
    WS_VISIBLE | WS_CHILD | CBS_DROPDOWN | CBS_AUTOHSCROLL,
      0, 0, 100, 620,
      (_headerReBar ? _headerToolBar : (HWND)*this),
      (HMENU)(UINT_PTR)(_comboBoxID),
      g_hInstance, NULL);

#ifndef UNDER_CE
  _headerComboBox.SetUnicodeFormat(true);
  _headerComboBox.SetImageList(Shell_Get_SysImageList_smallIcons(true));
  _headerComboBox.SetExtendedStyle(CBES_EX_PATHWORDBREAKPROC, CBES_EX_PATHWORDBREAKPROC);
  /*
  _headerComboBox.SetUserDataLongPtr(LONG_PTR(&_headerComboBox));
  _headerComboBox._panel = this;
  _headerComboBox._origWindowProc =
      (WNDPROC)_headerComboBox.SetLongPtr(GWLP_WNDPROC,
      LONG_PTR(ComboBoxSubclassProc));
  */
  _comboBoxEdit.Attach(_headerComboBox.GetEditControl());
  // _comboBoxEdit.SendMessage(CCM_SETUNICODEFORMAT, (WPARAM)(BOOL)TRUE, 0);
  _comboBoxEdit.SetUserDataLongPtr(LONG_PTR(&_comboBoxEdit));
  _comboBoxEdit._panel = this;
   #ifndef _UNICODE
   if (g_IsNT)
     _comboBoxEdit._origWindowProc =
      (WNDPROC)_comboBoxEdit.SetLongPtrW(GWLP_WNDPROC, LONG_PTR(ComboBoxEditSubclassProc));
   else
   #endif
     _comboBoxEdit._origWindowProc =
      (WNDPROC)_comboBoxEdit.SetLongPtr(GWLP_WNDPROC, LONG_PTR(ComboBoxEditSubclassProc));
#endif

  if (_headerReBar)
  {
    REBARINFO     rbi;
    rbi.cbSize = sizeof(REBARINFO);  // Required when using this struct.
    rbi.fMask  = 0;
    rbi.himl   = (HIMAGELIST)NULL;
    _headerReBar.SetBarInfo(&rbi);
    
    // Send the TB_BUTTONSTRUCTSIZE message, which is required for
    // backward compatibility.
    // _headerToolBar.SendMessage(TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    SIZE size;
    _headerToolBar.GetMaxSize(&size);
    
    REBARBANDINFO rbBand;
    memset(&rbBand, 0, sizeof(rbBand));
    // rbBand.cbSize = sizeof(rbBand);  // for debug
    // rbBand.cbSize = REBARBANDINFO_V3_SIZE; // for debug
    rbBand.cbSize = my_compatib_REBARBANDINFO_size;
    rbBand.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_COLORS;
    rbBand.clrFore = NModernUI::Col_Ink();
    rbBand.clrBack = NModernUI::Col_Band();
    rbBand.fStyle = RBBS_NOGRIPPER;
    rbBand.cxMinChild = (UINT)size.cx;
    rbBand.cyMinChild = (UINT)size.cy;
    rbBand.cyChild = (UINT)size.cy;
    rbBand.cx = (UINT)size.cx;
    rbBand.hwndChild  = _headerToolBar;
    _headerReBar.InsertBand(-1, &rbBand);

    RECT rc;
    ::GetWindowRect(_headerComboBox, &rc);
    rbBand.cxMinChild = 30;
    rbBand.cyMinChild = (UINT)(rc.bottom - rc.top);
    rbBand.cx = 1000;
    rbBand.hwndChild  = _headerComboBox;
    _headerReBar.InsertBand(-1, &rbBand);
    // _headerReBar.MaximizeBand(1, false);
  }

  Modern_StyleHeader(_headerReBar, _headerToolBar, _headerComboBox, _comboBoxEdit);

  _statusBar.Create(WS_CHILD | WS_VISIBLE, L"Status", (*this), _statusBarID);
  // _statusBar2.Create(WS_CHILD | WS_VISIBLE, L"Status", (*this), _statusBarID + 1);

  const int sizes[] = {220, 320, 420, -1};
  _statusBar.SetParts(4, sizes);
  Modern_StyleStatusBar(_statusBar);
  // _statusBar2.SetParts(5, sizes);

  /*
  RECT rect;
  GetClientRect(&rect);
  OnSize(0, RECT_SIZE_X(rect), RECT_SIZE_Y(rect));
  */

  SetTimer(kTimerID, kTimerElapse);

  // InitListCtrl();
  RefreshListCtrl();
  
  return true;
}

void CPanel::OnDestroy()
{
  SaveListViewInfo();
  CWindow2::OnDestroy();
}

void CPanel::ChangeWindowSize(int xSize, int ySize)
{
  if (!(HWND)*this)
    return;
  int kHeaderSize;
  int kStatusBarSize;
  // int kStatusBar2Size;
  RECT rect;
  if (_headerReBar)
    _headerReBar.GetWindowRect(&rect);
  else
    _headerToolBar.GetWindowRect(&rect);

  kHeaderSize = RECT_SIZE_Y(rect);

  _statusBar.GetWindowRect(&rect);
  kStatusBarSize = RECT_SIZE_Y(rect);
  
  // _statusBar2.GetWindowRect(&rect);
  // kStatusBar2Size = RECT_SIZE_Y(rect);
 
  int yListViewSize = MyMax(ySize - kHeaderSize - kStatusBarSize, 0);
  const int kStartXPos = 32;
  if (_headerReBar)
  {
  }
  else
  {
    _headerToolBar.Move(0, 0, xSize, 0);
    _headerComboBox.Move(kStartXPos, 2,
        MyMax(xSize - kStartXPos - 10, kStartXPos), 0);
  }
  _listView.Move(0, kHeaderSize, xSize, yListViewSize);
  _statusBar.Move(0, kHeaderSize + yListViewSize, xSize, kStatusBarSize);
  NModernUI::RoundCorners(_headerComboBox, 10);
  Modern_RefreshHeader(_headerReBar);
  // _statusBar2.MoveWindow(0, kHeaderSize + yListViewSize + kStatusBarSize, xSize, kStatusBar2Size);
  // _statusBar.MoveWindow(0, 100, xSize, kStatusBarSize);
  // _statusBar2.MoveWindow(0, 200, xSize, kStatusBar2Size);
}

bool CPanel::OnSize(WPARAM /* wParam */, int xSize, int ySize)
{
  if (!(HWND)*this)
    return true;
  if (_headerReBar)
    _headerReBar.Move(0, 0, xSize, 0);
  ChangeWindowSize(xSize, ySize);
  return true;
}

bool CPanel::OnNotifyReBar(LPNMHDR header, LRESULT & /* result */)
{
  switch (header->code)
  {
    case RBN_HEIGHTCHANGE:
    {
      RECT rect;
      GetWindowRect(&rect);
      ChangeWindowSize(RECT_SIZE_X(rect), RECT_SIZE_Y(rect));
      return false;
    }
  }
  return false;
}

/*
UInt32 g_OnNotify = 0;
UInt32 g_LVIF_TEXT = 0;
UInt32 g_Time = 0;

void Print_OnNotify(const char *name)
{
  char s[256];
  DWORD tim = GetTickCount();
  sprintf(s,
      "Time = %7u ms, Notify = %9u, TEXT = %9u, %s",
      tim - g_Time,
      g_OnNotify,
      g_LVIF_TEXT,
      name);
  g_Time = tim;
  OutputDebugStringA(s);
  g_OnNotify = 0;
  g_LVIF_TEXT = 0;
}
*/

bool CPanel::OnNotify(UINT /* controlID */, LPNMHDR header, LRESULT &result)
{
  /*
  g_OnNotify++;

  if (header->hwndFrom == _listView)
  {
    if (header->code == LVN_GETDISPINFOW)
    {
      LV_DISPINFOW *dispInfo = (LV_DISPINFOW *)header;
        if ((dispInfo->item.mask & LVIF_TEXT))
          g_LVIF_TEXT++;
    }
  }
  */

  if (!_processNotify)
    return false;

  if (header->hwndFrom == _headerComboBox)
    return OnNotifyComboBox(header, result);
  else if (header->hwndFrom == _headerReBar)
    return OnNotifyReBar(header, result);
  else if (header->hwndFrom == _listView)
    return OnNotifyList(header, result);
  else if (::GetParent(header->hwndFrom) == _listView)
  {
    // NMHDR:code is UINT
    // NM_RCLICK is unsigned in windows sdk
    // NM_RCLICK is int      in MinGW
    if (header->code == (UINT)NM_RCLICK)
      return OnRightClick((MY_NMLISTVIEW_NMITEMACTIVATE *)header, result);
  }
  return false;
}

bool CPanel::OnCommand(unsigned code, unsigned itemID, LPARAM lParam, LRESULT &result)
{
  if (itemID == kParentFolderID)
  {
    OpenParentFolder();
    result = 0;
    return true;
  }
  /*
  if (itemID == kCreateFolderID)
  {
    CreateFolder();
    result = 0;
    return true;
  }
  */
  if (itemID == _comboBoxID)
  {
    if (OnComboBoxCommand(code, lParam, result))
      return true;
  }
  return CWindow2::OnCommand(code, itemID, lParam, result);
}



/*
void CPanel::MessageBox_Info(LPCWSTR message, LPCWSTR caption) const
  { ::MessageBoxW((HWND)*this, message, caption, MB_OK); }
void CPanel::MessageBox_Warning(LPCWSTR message) const
  { ::MessageBoxW((HWND)*this, message, L"7-Zip", MB_OK | MB_ICONWARNING); }
*/

void CPanel::MessageBox_Error_Caption(LPCWSTR message, LPCWSTR caption) const
  { ::MessageBoxW((HWND)*this, message, caption, MB_OK | MB_ICONSTOP); }

void CPanel::MessageBox_Error(LPCWSTR message) const
  { MessageBox_Error_Caption(message, L"7-Zip"); }

static UString ErrorHResult_To_Message(HRESULT errorCode)
{
  if (errorCode == 0)
    errorCode = E_FAIL;
  return HResultToMessage(errorCode);
}

void CPanel::MessageBox_Error_HRESULT_Caption(HRESULT errorCode, LPCWSTR caption) const
{
  MessageBox_Error_Caption(ErrorHResult_To_Message(errorCode), caption);
}

void CPanel::MessageBox_Error_HRESULT(HRESULT errorCode) const
  { MessageBox_Error_HRESULT_Caption(errorCode, L"7-Zip"); }

void CPanel::MessageBox_Error_2Lines_Message_HRESULT(LPCWSTR message, HRESULT errorCode) const
{
  UString m = message;
  m.Add_LF();
  m += ErrorHResult_To_Message(errorCode);
  MessageBox_Error(m);
}

void CPanel::MessageBox_LastError(LPCWSTR caption) const
  { MessageBox_Error_HRESULT_Caption(GetLastError_noZero_HRESULT(), caption); }

void CPanel::MessageBox_LastError() const
  { MessageBox_LastError(L"7-Zip"); }

void CPanel::MessageBox_Error_LangID(UINT resourceID) const
  { MessageBox_Error(LangString(resourceID)); }

void CPanel::MessageBox_Error_UnsupportOperation() const
  { MessageBox_Error_LangID(IDS_OPERATION_IS_NOT_SUPPORTED); }




void CPanel::SetFocusToList()
{
  _listView.SetFocus();
  // SetCurrentPathText();
}

void CPanel::SetFocusToLastRememberedItem()
{
  if (_lastFocusedIsList)
    SetFocusToList();
  else
    _headerComboBox.SetFocus();
}

UString CPanel::GetFolderTypeID() const
{
  {
    NCOM::CPropVariant prop;
    if (_folder->GetFolderProperty(kpidType, &prop) == S_OK)
      if (prop.vt == VT_BSTR)
        return (const wchar_t *)prop.bstrVal;
  }
  return UString();
}

bool CPanel::IsFolderTypeEqTo(const char *s) const
{
  return StringsAreEqual_Ascii(GetFolderTypeID(), s);
}

bool CPanel::IsRootFolder() const { return IsFolderTypeEqTo("RootFolder"); }
bool CPanel::IsFSFolder() const { return IsFolderTypeEqTo("FSFolder"); }
bool CPanel::IsFSDrivesFolder() const { return IsFolderTypeEqTo("FSDrives"); }
bool CPanel::IsAltStreamsFolder() const { return IsFolderTypeEqTo("AltStreamsFolder"); }
bool CPanel::IsArcFolder() const
{
  return GetFolderTypeID().IsPrefixedBy_Ascii_NoCase("7-Zip");
}

bool CPanel::IsHashFolder() const
{
  if (_folder)
  {
    NCOM::CPropVariant prop;
    if (_folder->GetFolderProperty(kpidIsHash, &prop) == S_OK)
      if (prop.vt == VT_BOOL)
        return VARIANT_BOOLToBool(prop.boolVal);
  }
  return false;
}

UString CPanel::GetFsPath() const
{
  if (IsFSDrivesFolder() && !IsDeviceDrivesPrefix() && !IsSuperDrivesPrefix())
    return UString();
  return _currentFolderPrefix;
}

UString CPanel::GetDriveOrNetworkPrefix() const
{
  if (!IsFSFolder())
    return UString();
  UString drive = GetFsPath();
  drive.DeleteFrom(NFile::NName::GetRootPrefixSize(drive));
  return drive;
}

void CPanel::SetListViewMode(UInt32 index)
{
  if (index >= 4)
    return;
  _listViewMode = index;
  const LONG_PTR oldStyle = _listView.GetStyle();
  const DWORD newStyle = kStyles[index];

  // DWORD tickCount1 = GetTickCount();
  if ((oldStyle & LVS_TYPEMASK) != (LONG_PTR)newStyle)
    _listView.SetStyle((oldStyle & ~(LONG_PTR)(DWORD)LVS_TYPEMASK) | (LONG_PTR)newStyle);
  // RefreshListCtrlSaveFocused();
  /*
  DWORD tickCount2 = GetTickCount();
  char s[256];
  sprintf(s, "SetStyle = %5d",
      tickCount2 - tickCount1
      );
  OutputDebugStringA(s);
  */

}

void CPanel::ChangeFlatMode()
{
  _flatMode = !_flatMode;
  if (!_parentFolders.IsEmpty())
    _flatModeForArc = _flatMode;
  else
    _flatModeForDisk = _flatMode;
  RefreshListCtrl_SaveFocused();
}

/*
void CPanel::Change_ShowNtfsStrems_Mode()
{
  _showNtfsStrems_Mode = !_showNtfsStrems_Mode;
  if (!_parentFolders.IsEmpty())
    _showNtfsStrems_ModeForArc = _showNtfsStrems_Mode;
  else
    _showNtfsStrems_ModeForDisk = _showNtfsStrems_Mode;
  RefreshListCtrlSaveFocused();
}
*/

void CPanel::Post_Refresh_StatusBar()
{
  if (_processStatusBar)
    PostMsg(kRefresh_StatusBar);
}

void CPanel::AddToArchive()
{
  if (!Is_IO_FS_Folder())
  {
    MessageBox_Error_UnsupportOperation();
    return;
  }
  CRecordVector<UInt32> indices;
  Get_ItemIndices_Operated(indices);
  if (indices.Size() == 0)
  {
    MessageBox_Error_LangID(IDS_SELECT_FILES);
    return;
  }
  UString destCurDirPrefix = GetFsPath();
  if (IsFSDrivesFolder())
    destCurDirPrefix = ROOT_FS_FOLDER;
  UStringVector names;
  GetFilePaths(indices, names);
  UString baseName;
  const UString arcName = CreateArchiveName(names,
      false, // isHash
      NULL,  // CFileInfo *fi
      baseName);
  const HRESULT res = CompressFiles(destCurDirPrefix, arcName, L"",
      true,   // addExtension
      names,
      false,  // email
      true,   // showDialog
      false); // waitFinish
  if (res != S_OK)
  {
    if (destCurDirPrefix.Len() >= MAX_PATH)
      MessageBox_Error_LangID(IDS_MESSAGE_UNSUPPORTED_OPERATION_FOR_LONG_PATH_FOLDER);
  }
  // KillSelection();
}

// function from ContextMenu.cpp
UString GetSubFolderNameForExtract(const UString &arcPath);

static UString GetSubFolderNameForExtract2(const UString &arcPath)
{
  int slashPos = arcPath.ReverseFind_PathSepar();
  UString s;
  UString name = arcPath;
  if (slashPos >= 0)
  {
    s = arcPath.Left((unsigned)(slashPos + 1));
    name = arcPath.Ptr((unsigned)(slashPos + 1));
  }
  s += GetSubFolderNameForExtract(name);
  return s;
}


int CPanel::FindDir_InOperatedList(const CRecordVector<UInt32> &operatedIndices) const
{
  const bool *isDirVector = _isDirVector.ConstData();
  const UInt32 *indices = operatedIndices.ConstData();
  const unsigned numItems = operatedIndices.Size();
  for (unsigned i = 0; i < numItems; i++)
    if (isDirVector[indices[i]])
      return (int)i;
  return -1;
}


void CPanel::GetFilePaths(const CRecordVector<UInt32> &operatedIndices, UStringVector &paths) const
{
  paths.ClearAndReserve(operatedIndices.Size());
  UString path = GetFsPath();
  const unsigned prefixLen = path.Len();
  const UInt32 *indices = operatedIndices.ConstData();
  const unsigned numItems = operatedIndices.Size();
  // for (unsigned y = 0; y < 10000; y++, paths.Clear())
  for (unsigned i = 0; i < numItems; i++)
  {
    path.DeleteFrom(prefixLen);
    Add_ItemRelPath2_To_String(indices[i], path);
    // ODS_U(path)
    paths.AddInReserved(path);
  }
}


void CPanel::ExtractArchives()
{
  if (!_parentFolders.IsEmpty())
  {
    _panelCallback->OnCopy(false, false);
    return;
  }
  CRecordVector<UInt32> indices;
  Get_ItemIndices_Operated(indices);
  if (indices.IsEmpty() || FindDir_InOperatedList(indices) != -1)
  {
    MessageBox_Error_LangID(IDS_SELECT_FILES);
    return;
  }
  UStringVector paths;
  GetFilePaths(indices, paths);
  UString outFolder = GetFsPath();
  if (indices.Size() == 1)
    outFolder += GetSubFolderNameForExtract2(GetItemRelPath(indices[0]));
  else
    outFolder.Add_Char('*');
  outFolder.Add_PathSepar();
  
  CContextMenuInfo ci;
  ci.Load();

  ::ExtractArchives(paths, outFolder
      , true   // showDialog
      , false  // elimDup
      , ci.WriteZone
      );
}

/*
static void AddValuePair(UINT resourceID, UInt64 value, UString &s)
{
  AddLangString(s, resourceID);
  char sz[32];
  s += ": ";
  ConvertUInt64ToString(value, sz);
  s += sz;
  s.Add_LF();
}

// now we don't need CThreadTest, since now we call CopyTo for "test command

class CThreadTest: public CProgressThreadVirt
{
  HRESULT ProcessVirt();
public:
  CRecordVector<UInt32> Indices;
  CExtractCallbackImp *ExtractCallbackSpec;
  CMyComPtr<IFolderArchiveExtractCallback> ExtractCallback;
  CMyComPtr<IArchiveFolder> ArchiveFolder;
};

HRESULT CThreadTest::ProcessVirt()
{
  RINOK(ArchiveFolder->Extract(&Indices[0], Indices.Size(),
      true, // includeAltStreams
      false, // replaceAltStreamColon
      NExtract::NPathMode::kFullPathnames,
      NExtract::NOverwriteMode::kAskBefore,
      NULL, // path
      BoolToInt(true), // testMode
      ExtractCallback));
  if (ExtractCallbackSpec->IsOK())
  {
    UString s;
    AddValuePair(IDS_PROP_FOLDERS, ExtractCallbackSpec->NumFolders, s);
    AddValuePair(IDS_PROP_FILES, ExtractCallbackSpec->NumFiles, s);
    // AddValuePair(IDS_PROP_SIZE, ExtractCallbackSpec->UnpackSize, s);
    // AddSizePair(IDS_COMPRESSED_COLON, Stat.PackSize, s);
    s.Add_LF();
    AddLangString(s, IDS_MESSAGE_NO_ERRORS);
    FinalMessage.OkMessage.Message = s;
  }
  return S_OK;
}

static void AddSizePair(UInt32 langID, UInt64 value, UString &s)
{
  char sz[32];
  AddLangString(s, langID);
  s += L' ';
  ConvertUInt64ToString(value, sz);
  s += sz;
  ConvertUInt64ToString(value >> 20, sz);
  s += " (";
  s += sz;
  s += " MB)";
  s.Add_LF();
}
*/

void CPanel::TestArchives()
{
  CRecordVector<UInt32> indices;
  Get_ItemIndices_OperSmart(indices);
  CMyComPtr<IArchiveFolder> archiveFolder;
  _folder.QueryInterface(IID_IArchiveFolder, &archiveFolder);
  if (archiveFolder)
  {
    CCopyToOptions options;
    options.streamMode = true;
    options.showErrorMessages = true;
    options.testMode = true;

    UStringVector messages;
    HRESULT res = CopyTo(options, indices, &messages);
    if (res != S_OK)
    {
      if (res != E_ABORT)
        MessageBox_Error_HRESULT(res);
    }
    return;

    /*
    {
    CThreadTest extracter;

    extracter.ArchiveFolder = archiveFolder;
    extracter.ExtractCallbackSpec = new CExtractCallbackImp;
    extracter.ExtractCallback = extracter.ExtractCallbackSpec;
    extracter.ExtractCallbackSpec->ProgressDialog = &extracter.ProgressDialog;
    if (!_parentFolders.IsEmpty())
    {
      const CFolderLink &fl = _parentFolders.Back();
      extracter.ExtractCallbackSpec->PasswordIsDefined = fl.UsePassword;
      extracter.ExtractCallbackSpec->Password = fl.Password;
    }

    if (indices.IsEmpty())
      return;

    extracter.Indices = indices;
    
    const UString title = LangString(IDS_PROGRESS_TESTING);
    
    extracter.ProgressDialog.CompressingMode = false;
    extracter.ProgressDialog.MainWindow = GetParent();
    extracter.ProgressDialog.MainTitle = "7-Zip"; // LangString(IDS_APP_TITLE);
    extracter.ProgressDialog.MainAddTitle = title + L' ';
    
    extracter.ExtractCallbackSpec->OverwriteMode = NExtract::NOverwriteMode::kAskBefore;
    extracter.ExtractCallbackSpec->Init();
    
    if (extracter.Create(title, GetParent()) != S_OK)
      return;
    
    }
    RefreshTitleAlways();
    return;
    */
  }

  if (!IsFSFolder())
  {
    MessageBox_Error_UnsupportOperation();
    return;
  }
  UStringVector paths;
  GetFilePaths(indices, paths);
  if (paths.IsEmpty())
  {
    MessageBox_Error_LangID(IDS_SELECT_FILES);
    return;
  }
  ::TestArchives(paths);
}

// 7-Zip Modern: context menu verification harness.
// Calls the registered 7-Zip shell extension (IContextMenu) for a given file
// and dumps the resulting menu structure. Used to verify the top-level
// "extract here" quick action added by this project.

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
#include <string>

static const CLSID CLSID_7zip =
  { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00 } };

static void PrintW(const wchar_t *s, int depth)
{
  for (int i = 0; i < depth; i++) printf("  ");
  int len = (int)wcslen(s);
  int need = WideCharToMultiByte(CP_UTF8, 0, s, len, NULL, 0, NULL, NULL);
  std::string out(need, 0);
  WideCharToMultiByte(CP_UTF8, 0, s, len, &out[0], need, NULL, NULL);
  // strip mnemonic markers for readability
  std::string clean;
  for (size_t i = 0; i < out.size(); i++)
    if (out[i] != '&') clean += out[i];
  printf("%s\n", clean.c_str());
}

static void DumpMenu(HMENU hMenu, int depth)
{
  int count = GetMenuItemCount(hMenu);
  for (int i = 0; i < count; i++)
  {
    MENUITEMINFOW mii;
    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU | MIIM_ID;
    wchar_t buf[512];
    mii.dwTypeData = buf;
    mii.cch = 511;

    if (!GetMenuItemInfoW(hMenu, i, TRUE, &mii))
      continue;

    if (mii.fType & MFT_SEPARATOR)
    {
      for (int k = 0; k < depth; k++) printf("  ");
      printf("---\n");
      continue;
    }

    PrintW(buf, depth);
    if (mii.hSubMenu)
      DumpMenu(mii.hSubMenu, depth + 1);
  }
}

int wmain(int argc, wchar_t **argv)
{
  if (argc < 2)
  {
    printf("usage: test-menu <path-to-archive>\n");
    return 2;
  }

  CoInitialize(NULL);

  IContextMenu *pcm = NULL;
  HRESULT hr = CoCreateInstance(CLSID_7zip, NULL, CLSCTX_INPROC_SERVER,
      IID_IContextMenu, (void **)&pcm);
  if (FAILED(hr))
  {
    printf("CoCreateInstance failed: 0x%08X\n", (unsigned)hr);
    return 1;
  }

  IShellFolder *psfDesktop = NULL;
  SHGetDesktopFolder(&psfDesktop);

  PIDLIST_ABSOLUTE pidl = NULL;
  hr = psfDesktop->ParseDisplayName(NULL, NULL, argv[1], NULL, &pidl, NULL);
  if (FAILED(hr))
  {
    printf("ParseDisplayName failed: 0x%08X\n", (unsigned)hr);
    return 1;
  }

  IShellFolder *psfParent = NULL;
  PCUITEMID_CHILD pidlChild = NULL;
  SHBindToParent(pidl, IID_IShellFolder, (void **)&psfParent, &pidlChild);

  IDataObject *pdo = NULL;
  psfParent->GetUIObjectOf(NULL, 1, &pidlChild, IID_IDataObject, NULL, (void **)&pdo);

  IShellExtInit *psei = NULL;
  pcm->QueryInterface(IID_IShellExtInit, (void **)&psei);
  if (psei)
  {
    psei->Initialize(NULL, pdo, NULL);
    psei->Release();
  }

  HMENU hMenu = CreatePopupMenu();
  hr = pcm->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL);
  printf("QueryContextMenu: 0x%08X\n", (unsigned)hr);
  printf("=== context menu for: %ws ===\n", argv[1]);
  DumpMenu(hMenu, 0);

  DestroyMenu(hMenu);
  if (pdo) pdo->Release();
  if (psfParent) psfParent->Release();
  if (pidl) CoTaskMemFree(pidl);
  if (psfDesktop) psfDesktop->Release();
  pcm->Release();
  CoUninitialize();
  return 0;
}

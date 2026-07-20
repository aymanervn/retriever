#ifdef _WIN32
#include "svc.h"
#include <windows.h>
#include <shellapi.h>

#define WM_TRAY (WM_APP + 1)
#define WM_INDEX_DONE (WM_APP + 2)
#define WM_SVC_QUIT (WM_APP + 3)
#define TRAY_ID 1
#define CMD_CLOSE 1
#define CMD_EXIT 100

static index_t *g_ix;
static char g_drives[26];
static int g_ndr;
static HWND g_wnd, g_popup;
static volatile LONG g_done;
static double g_secs;
static int g_exit;
static const char *g_fail = "";

static void status_text(char *buf, size_t n)
{
    if (!g_done)
        snprintf(buf, n, "indexing...");
    else if (g_secs < 10)
        snprintf(buf, n, "finished indexing in %.1fs", g_secs);
    else
        snprintf(buf, n, "finished indexing in %.0fs", g_secs);
}

static void nid_init(NOTIFYICONDATAA *nid)
{
    memset(nid, 0, sizeof *nid);
    nid->cbSize = sizeof *nid;
    nid->hWnd = g_wnd;
    nid->uID = TRAY_ID;
}

static void tray_add(void)
{
    NOTIFYICONDATAA nid;
    nid_init(&nid);
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1));
    if (!nid.hIcon)
        nid.hIcon = LoadIconA(NULL, MAKEINTRESOURCEA(32512));
    snprintf(nid.szTip, sizeof nid.szTip, "retriever " RTV_VERSION " -- indexing...");
    Shell_NotifyIconA(NIM_ADD, &nid);
}

static void tray_update(int balloon, const char *text)
{
    NOTIFYICONDATAA nid;
    nid_init(&nid);
    nid.uFlags = NIF_TIP;
    snprintf(nid.szTip, sizeof nid.szTip, "retriever " RTV_VERSION " -- %s", text);
    if (balloon)
    {
        nid.uFlags |= NIF_INFO;
        snprintf(nid.szInfo, sizeof nid.szInfo, "%s", text);
        snprintf(nid.szInfoTitle, sizeof nid.szInfoTitle, "retriever");
        nid.dwInfoFlags = NIIF_INFO;
    }
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

static void tray_remove(void)
{
    NOTIFYICONDATAA nid;
    nid_init(&nid);
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

static void popup_toggle(void)
{
    if (g_popup)
    {
        DestroyWindow(g_popup);
        return;
    }
    char txt[64];
    status_text(txt, sizeof txt);
    int w = 240, h = 48;
    POINT pt;
    NOTIFYICONIDENTIFIER nii = {sizeof nii, g_wnd, TRAY_ID, {0}};
    RECT r;
    if (SUCCEEDED(Shell_NotifyIconGetRect(&nii, &r)))
    {
        pt.x = (r.left + r.right) / 2;
        pt.y = r.top;
    }
    else
        GetCursorPos(&pt);
    RECT wa = {0, 0, w, h};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int x = pt.x - w / 2;
    if (x + w > wa.right)
        x = wa.right - w;
    if (x < wa.left)
        x = wa.left;
    int y = pt.y - h - 8;
    if (y < wa.top)
        y = pt.y + 8;
    HINSTANCE inst = GetModuleHandleA(NULL);
    g_popup = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "rtv_popup", "",
                              WS_POPUP | WS_BORDER, x, y, w, h, g_wnd, NULL, inst, NULL);
    HWND btn = CreateWindowExA(0, "BUTTON", txt, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 6, 6,
                               w - 12, h - 12, g_popup, (HMENU)(INT_PTR)CMD_CLOSE, inst, NULL);
    SendMessageA(btn, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    ShowWindow(g_popup, SW_SHOW);
    SetForegroundWindow(g_popup);
}

static LRESULT CALLBACK popup_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_COMMAND:
        if (LOWORD(wp) == CMD_CLOSE)
            DestroyWindow(w);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE)
            DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        if (g_popup == w)
            g_popup = NULL;
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}

static LRESULT CALLBACK tray_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_TRAY:
        if (lp == WM_LBUTTONUP)
            popup_toggle();
        else if (lp == WM_RBUTTONUP)
        {
            char txt[64];
            status_text(txt, sizeof txt);
            HMENU menu = CreatePopupMenu();
            AppendMenuA(menu, MF_STRING | MF_DISABLED, 0, txt);
            AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuA(menu, MF_STRING, CMD_EXIT, "Exit retriever");
            POINT pt;
            GetCursorPos(&pt);
            /* foreground + WM_NULL dance, else the menu won't dismiss on
             * outside clicks (classic Shell_NotifyIcon quirk) */
            SetForegroundWindow(w);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, w, NULL);
            PostMessageA(w, WM_NULL, 0, 0);
            DestroyMenu(menu);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == CMD_EXIT)
            PostQuitMessage(0);
        return 0;
    case WM_INDEX_DONE:
    {
        char txt[64];
        status_text(txt, sizeof txt);
        tray_update(1, txt);
        return 0;
    }
    case WM_SVC_QUIT:
        tray_update(1, g_fail);
        g_exit = (int)wp;
        PostQuitMessage((int)wp);
        return 0;
    case WM_DESTROY:
        tray_remove();
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}

static void boot_fn(void *arg)
{
    (void)arg;
    int64_t t0 = plat_usec();
    int ok = 0;
    if (g_ndr)
    {
        for (int i = 0; i < g_ndr; i++)
            if (win_index_volume(g_ix, g_drives[i]) == 0)
                ok++;
    }
    else
        ok = win_all_volumes(g_ix);
    if (!ok)
    {
        g_fail = "no volumes indexed; exiting";
        PostMessageA(g_wnd, WM_SVC_QUIT, 1, 0);
        return;
    }
    g_secs = (double)(plat_usec() - t0) / 1e6;
    InterlockedExchange(&g_done, 1);
    PostMessageA(g_wnd, WM_INDEX_DONE, 0, 0);
    int rc = ipc_serve(g_ix);
    g_fail = "pipe server failed; exiting";
    PostMessageA(g_wnd, WM_SVC_QUIT, (WPARAM)rc, 0);
}

int tray_run(index_t *ix, const char *drives, int ndr)
{
    g_ix = ix;
    memcpy(g_drives, drives, sizeof g_drives);
    g_ndr = ndr;
    HINSTANCE inst = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0};
    wc.hInstance = inst;
    wc.lpfnWndProc = tray_proc;
    wc.lpszClassName = "rtv_tray";
    RegisterClassA(&wc);
    wc.lpfnWndProc = popup_proc;
    wc.lpszClassName = "rtv_popup";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);
    g_wnd = CreateWindowExA(0, "rtv_tray", "retriever", 0, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!g_wnd)
        return 1;
    tray_add();
    if (plat_thread(boot_fn, NULL))
    {
        tray_remove();
        return 1;
    }
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    DestroyWindow(g_wnd);
    return g_exit;
}
#else
typedef int rtv_tray_win_not_built;
#endif

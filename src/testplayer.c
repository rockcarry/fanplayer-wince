// 包含头文件
#include <windows.h>
#include "ffplayer.h"

char* UnicodeToUTF8(const wchar_t *str)
{
    char *result ;
    int   textlen;
    textlen = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
    result  = (char*) malloc((textlen+1) * sizeof(char));
    WideCharToMultiByte(CP_UTF8, 0, str, -1, result, textlen, NULL, NULL);
    return result;
}

#define PLAYER_WND_CLASS TEXT("TestPlayer")
#define PLAYER_WND_NAME  TEXT("TestPlayer")

static void *g_player = NULL;

static LRESULT CALLBACK PLAYER_WNDPROC(
    HWND hwnd,      /* handle to window */
    UINT uMsg,      /* message identifier */
    WPARAM wParam,  /* first message parameter */
    LPARAM lParam   /* second message parameter */
)
{
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        player_setrect(g_player, 0, 0, 0, (lParam >> 0) & 0xffff, (lParam >> 16) & 0xffff);
        break;
    case MSG_FANPLAYER:
        if (wParam == MSG_OPEN_DONE) {
            player_play(g_player);
        }
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return 0;
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPreInst, LPWSTR lpszCmdLine, int nCmdShow)
{
    WNDCLASS wc      = {0};
    HWND     hwnd    = NULL;
    MSG      msg     = {0};
    OPENFILENAME ofn = {0};
    char *futf8      = NULL;
    TCHAR fname[MAX_PATH] = TEXT("");

    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hwnd;
    ofn.lpstrFile    = fname;
    ofn.nMaxFile     = sizeof(fname);
    ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrTitle   = TEXT("open");
    if (!GetOpenFileName(&ofn)) {
        return 0;
    } else {
        futf8 = UnicodeToUTF8(fname);
    }

    wc.lpfnWndProc   = PLAYER_WNDPROC;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIcon  (NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = PLAYER_WND_CLASS;
    if (!RegisterClass(&wc)) return FALSE;

    hwnd = CreateWindow(PLAYER_WND_CLASS, PLAYER_WND_NAME, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 272,
        NULL, NULL, hInst, NULL);
    if (!hwnd) return FALSE;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // open player
    g_player = player_open(futf8, hwnd, NULL);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage (&msg);
    }

    // close player
    if (g_player) {
        player_close(g_player);
        g_player = NULL;
    }

    if (futf8) free(futf8);
    return 0;
}


/**
 * cards.c - 卡片控件引擎实现（GDI 自绘，无第三方依赖）
 */
#include "cards.h"
#include <windowsx.h>
#include <wchar.h>
#include <stdlib.h>

/* ---- 色板（见 docs/REDESIGN.md 2.1） ---- */
static const COLORREF CARD_BG        = RGB(255, 255, 255);
static const COLORREF CARD_BORDER    = RGB(228, 233, 239);
static const COLORREF CARD_BORDER_HV = RGB(14, 165, 233);   /* 悬停主色 */
static const COLORREF CARD_DANGER_HV = RGB(239, 68, 68);
static const COLORREF TEXT_PRIMARY   = RGB(15, 23, 42);
static const COLORREF TEXT_SECONDARY = RGB(100, 116, 139);
static const COLORREF ACCENT         = RGB(14, 165, 233);
static const COLORREF ACCENT_HOVER   = RGB(2, 132, 199);
static const COLORREF DANGER         = RGB(239, 68, 68);
static const COLORREF DANGER_HOVER   = RGB(185, 28, 28);

#define CARD_PAD       18
#define CARD_RADIUS    10
#define BTN_W          76
#define BTN_H          30

typedef struct _CARD_DATA {
    int   id;
    WCHAR title[64];
    WCHAR line1[128];
    WCHAR line2[128];
    WCHAR btn[32];
    BOOL  danger;
    BOOL  selected;
    BOOL  hover;
    BOOL  btnHover;
    BOOL  tracking;
} CARD_DATA;

static HFONT g_fontTitle = NULL;
static HFONT g_fontBody  = NULL;
static HFONT g_fontBtn   = NULL;

static void EnsureFonts(void) {
    if (g_fontTitle) return;
    g_fontTitle = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fontBody = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_fontBtn = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

static void NotifyParent(HWND hWnd, int code) {
    CARD_DATA* d = (CARD_DATA*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (d && IsWindow(GetParent(hWnd)))
        PostMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(d->id, code), (LPARAM)hWnd);
}

static void PaintCard(HWND hWnd, HDC hdc) {
    CARD_DATA* d = (CARD_DATA*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    RECT rc;
    if (!d) return;

    GetClientRect(hWnd, &rc);

    /* 背景（与父窗口底色一致） */
    SetBkMode(hdc, TRANSPARENT);

    /* 卡片圆角底 */
    {
        HBRUSH bg = CreateSolidBrush(CARD_BG);
        HBRUSH oldBg = SelectObject(hdc, bg);
        HPEN pen, oldPen;
        COLORREF border = d->selected ? ACCENT
            : (d->hover ? (d->danger ? CARD_DANGER_HV : CARD_BORDER_HV)
                        : CARD_BORDER);
        pen = CreatePen(PS_SOLID, (d->hover || d->selected) ? 2 : 1, border);
        oldPen = SelectObject(hdc, pen);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, CARD_RADIUS, CARD_RADIUS);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBg);
        DeleteObject(pen);
        DeleteObject(bg);
    }

    /* 文本 */
    {
        RECT rTitle = { CARD_PAD, CARD_PAD, rc.right - CARD_PAD, rc.top + 42 };
        RECT rL1    = { CARD_PAD, rc.top + 46, rc.right - CARD_PAD, rc.top + 68 };
        RECT rL2    = { CARD_PAD, rc.top + 70, rc.right - CARD_PAD, rc.top + 92 };

        SelectObject(hdc, g_fontTitle);
        SetTextColor(hdc, TEXT_PRIMARY);
        DrawTextW(hdc, d->title, -1, &rTitle, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(hdc, g_fontBody);
        SetTextColor(hdc, TEXT_SECONDARY);
        DrawTextW(hdc, d->line1, -1, &rL1, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawTextW(hdc, d->line2, -1, &rL2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    /* 动作按钮（右下角） */
    if (d->btn[0]) {
        RECT rcBtn = { rc.right - BTN_W - CARD_PAD, rc.bottom - BTN_H - CARD_PAD,
                       rc.right - CARD_PAD, rc.bottom - CARD_PAD };
        COLORREF fill = d->danger
            ? (d->btnHover ? DANGER_HOVER : DANGER)
            : (d->btnHover ? ACCENT_HOVER : ACCENT);
        HBRUSH bg = CreateSolidBrush(fill);
        HBRUSH oldBg = SelectObject(hdc, bg);
        HPEN pen = CreatePen(PS_SOLID, 1, fill);
        HPEN oldPen = SelectObject(hdc, pen);
        RoundRect(hdc, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 6, 6);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBg);
        DeleteObject(pen);
        DeleteObject(bg);

        SetTextColor(hdc, RGB(255, 255, 255));
        SelectObject(hdc, g_fontBtn);
        DrawTextW(hdc, d->btn, -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static BOOL InButton(HWND hWnd, int x, int y) {
    CARD_DATA* d = (CARD_DATA*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    RECT rc;
    if (!d || !d->btn[0]) return FALSE;
    GetClientRect(hWnd, &rc);
    return x >= rc.right - BTN_W - CARD_PAD && x <= rc.right - CARD_PAD &&
           y >= rc.bottom - BTN_H - CARD_PAD && y <= rc.bottom - CARD_PAD;
}

static LRESULT CALLBACK CardProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CARD_DATA* d = (CARD_DATA*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        d = (CARD_DATA*)calloc(1, sizeof(CARD_DATA));
        if (!d) return -1;
        d->id = (int)(INT_PTR)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)d);
        EnsureFonts();
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        PaintCard(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;   /* 全自绘，避免闪烁 */

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        BOOL overBtn = InButton(hWnd, pt.x, pt.y);
        if (!d->tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            d->tracking = TRUE;
        }
        if (!d->hover || d->btnHover != overBtn) {
            d->hover = TRUE;
            d->btnHover = overBtn;
            SetCursor(LoadCursor(NULL, IDC_HAND));
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (d) {
            d->tracking = FALSE;
            d->hover = FALSE;
            d->btnHover = FALSE;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (d->btn[0] && InButton(hWnd, pt.x, pt.y)) {
            NotifyParent(hWnd, BMN_BUTTON);
        } else {
            NotifyParent(hWnd, BMN_OPEN);
        }
        return 0;
    }

    case WM_DESTROY:
        if (d) { free(d); SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0); }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

BOOL BMCard_RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CardProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"BMCardClass";
    return RegisterClassExW(&wc) != 0;
}

HWND BMCard_Create(HWND parent, int id, int x, int y, int w, int h,
                   const WCHAR* title, const WCHAR* line1, const WCHAR* line2,
                   const WCHAR* btnText, BOOL danger) {
    HWND hWnd = CreateWindowExW(0, L"BMCardClass", NULL,
        WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
        (HMENU)(INT_PTR)(1000 + id), NULL, (LPVOID)(INT_PTR)id);
    if (!hWnd) return NULL;
    {
        CARD_DATA* d = (CARD_DATA*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (title)  lstrcpynW(d->title, title, 64);
        if (line1)  lstrcpynW(d->line1, line1, 128);
        if (line2)  lstrcpynW(d->line2, line2, 128);
        if (btnText) lstrcpynW(d->btn, btnText, 32);
        d->danger = danger;
    }
    return hWnd;
}

void BMCard_SetLine(HWND card, int idx, const WCHAR* text) {
    CARD_DATA* d = (CARD_DATA*)GetWindowLongPtrW(card, GWLP_USERDATA);
    if (!d || !text) return;
    if (idx == 1) lstrcpynW(d->line1, text, 128);
    else if (idx == 2) lstrcpynW(d->line2, text, 128);
    InvalidateRect(card, NULL, FALSE);
}

BOOL BMCard_IsSelected(HWND card) {
    CARD_DATA* d = (CARD_DATA*)GetWindowLongPtrW(card, GWLP_USERDATA);
    return d ? d->selected : FALSE;
}

void BMCard_SetSelected(HWND card, BOOL selected) {
    CARD_DATA* d = (CARD_DATA*)GetWindowLongPtrW(card, GWLP_USERDATA);
    if (!d) return;
    d->selected = selected;
    InvalidateRect(card, NULL, FALSE);
}

/* ---------------- 扁平按钮 ---------------- */

typedef struct _FLATBTN_DATA {
    int  id;
    BOOL primary;
    BOOL danger;
    BOOL hover;
    WCHAR text[48];
    BOOL tracking;
} FLATBTN_DATA;

static LRESULT CALLBACK FlatBtnProc2(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    FLATBTN_DATA* d = (FLATBTN_DATA*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        d = (FLATBTN_DATA*)calloc(1, sizeof(FLATBTN_DATA));
        if (!d) return -1;
        d->id = (int)(INT_PTR)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)d);
        EnsureFonts();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        SetBkMode(hdc, TRANSPARENT);
        {
            COLORREF fill = d->primary
                ? (d->hover ? ACCENT_HOVER : ACCENT)
                : (d->danger ? (d->hover ? DANGER_HOVER : DANGER) : CARD_BG);
            COLORREF border = d->danger && !d->primary ? DANGER : (d->primary ? fill : CARD_BORDER);
            COLORREF text = (d->primary || d->danger) ? RGB(255,255,255) : TEXT_PRIMARY;
            HBRUSH bg = CreateSolidBrush(fill);
            HPEN pen = CreatePen(PS_SOLID, 1, border);
            HBRUSH oldBg = SelectObject(hdc, bg);
            HPEN oldPen = SelectObject(hdc, pen);
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBg);
            DeleteObject(pen);
            DeleteObject(bg);
            SelectObject(hdc, g_fontBtn);
            SetTextColor(hdc, text);
            DrawTextW(hdc, d->text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        if (!d->tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            d->tracking = TRUE;
        }
        if (!d->hover) {
            d->hover = TRUE;
            SetCursor(LoadCursor(NULL, IDC_HAND));
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        d->tracking = FALSE;
        d->hover = FALSE;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
        PostMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(d->id, 0), (LPARAM)hWnd);
        return 0;
    case WM_DESTROY:
        free(d);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

HWND BMFlatButton_Create(HWND parent, int id, int x, int y, int w, int h,
                         const WCHAR* text, BOOL primary, BOOL danger) {
    HWND hWnd = CreateWindowExW(0, L"BMFlatBtnClass", NULL,
        WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
        (HMENU)(INT_PTR)(2000 + id), NULL, (LPVOID)(INT_PTR)id);
    if (!hWnd) return NULL;
    {
        FLATBTN_DATA* d = (FLATBTN_DATA*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        lstrcpynW(d->text, text ? text : L"", 48);
        d->primary = primary;
        d->danger = danger;
    }
    return hWnd;
}

BOOL BMFlatButton_RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = FlatBtnProc2;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"BMFlatBtnClass";
    return RegisterClassExW(&wc) != 0;
}

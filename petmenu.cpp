// 自定义右键菜单：简洁圆角卡片（无图标），悬停高亮；选中项通过 WM_PETMENU_CMD 回传
#include "common.h"

namespace {

const int MENU_W   = 170;
const int ITEM_H   = 42;
const int PAD_Y    = 10;
const int SEP_H    = 9;
const int kSepAfter = 2;      // 在第 2 项（"属性"）之后画分隔线

struct MenuItem { int id; const wchar_t* text; };

const MenuItem kItems[] = {
    { 1, L"聊天" },
    { 2, L"外观设置" },
    { 5, L"属性" },
    { 4, L"退出" },
};
const int kItemCount = (int)(sizeof(kItems) / sizeof(kItems[0]));

struct MenuState {
    int  hover = -1;
    int  press = -1;
    bool done  = false;
    HWND owner = NULL;
    Gdiplus::Bitmap* bmp = NULL;
};

int MenuHeight() { return PAD_Y * 2 + kItemCount * ITEM_H + SEP_H; }

int ItemTop(int i) { return PAD_Y + i * ITEM_H + (i > kSepAfter ? SEP_H : 0); }

int HitItem(int x, int y)
{
    if (x < 0 || x >= MENU_W || y < PAD_Y) return -1;
    for (int i = 0; i < kItemCount; i++) {
        int top = ItemTop(i);
        if (y >= top && y < top + ITEM_H) return i;
    }
    return -1;
}

void PaintMenu(HWND hwnd, MenuState* st)
{
    using namespace G;
    if (!st->bmp) st->bmp = new Bitmap(MENU_W, MenuHeight(), PixelFormat32bppPARGB);
    Graphics g(st->bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));

    int H = MenuHeight();

    // 外阴影
    GraphicsPath sp;
    AddRoundRectPath(sp, 4, 7, MENU_W - 8, H - 8, 18);
    g.FillPath(&SolidBrush(Color(80, 0, 0, 0)), &sp);

    GraphicsPath bgp;
    AddRoundRectPath(bgp, 0, 0, MENU_W, H, 18);
    g.FillPath(&SolidBrush(Color(255, 255, 255, 255)), &bgp);
    g.DrawPath(&Pen(Color(255, 232, 232, 232), 1.0f), &bgp);

    Font f(L"Microsoft YaHei UI", 14, FontStyleRegular, UnitPixel);
    SolidBrush tc(Color(255, 51, 51, 51));

    for (int i = 0; i < kItemCount; i++) {
        int top = ItemTop(i);
        bool hover = (st->hover == i), press = (st->press == i);
        if (hover || press) {
            GraphicsPath hp;
            AddRoundRectPath(hp, 8, (float)top + 3, MENU_W - 16, ITEM_H - 6, 10);
            g.FillPath(&SolidBrush(press ? Color(255, 225, 238, 255)
                                         : Color(255, 243, 249, 255)), &hp);
        }
        g.DrawString(kItems[i].text, -1, &f, PointF(20, top + (ITEM_H - 18) / 2.0f - 2), &tc);
        if (i == kSepAfter) {
            g.DrawLine(&Pen(Color(255, 238, 238, 238), 1.0f),
                16, top + ITEM_H + SEP_H / 2, MENU_W - 16, top + ITEM_H + SEP_H / 2);
        }
    }

    PresentLayered(hwnd, MENU_W, H, st->bmp);
}

LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MenuState* st = (MenuState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (st) PaintMenu(hwnd, st);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        if (!st) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = HitItem(pt.x, pt.y);
        if (h != st->hover) { st->hover = h; InvalidateRect(hwnd, NULL, FALSE); }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (st) { st->hover = -1; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    case WM_LBUTTONDOWN: {
        if (!st) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        st->press = HitItem(pt.x, pt.y);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!st) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int i = HitItem(pt.x, pt.y);
        st->done = true;
        if (i >= 0 && i == st->press && st->owner)
            PostMessageW(st->owner, WM_PETMENU_CMD, kItems[i].id, 0);
        ReleaseCapture();
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        if (st) { st->done = true; ReleaseCapture(); DestroyWindow(hwnd); }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && st) { st->done = true; ReleaseCapture(); DestroyWindow(hwnd); }
        return 0;
    case WM_PET_CLICKAWAY:
        // 点击菜单外任意区域：关闭菜单
        if (st) { st->done = true; ReleaseCapture(); DestroyWindow(hwnd); }
        return 0;
    case WM_CAPTURECHANGED:
        // 鼠标捕获被外部夺走（点击了别处）时关闭菜单
        if (st && !st->done) { st->done = true; DestroyWindow(hwnd); }
        return 0;
    case WM_DESTROY:
        if (st && st->bmp) { delete st->bmp; st->bmp = NULL; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

// 对外接口
void ShowPetMenu(HWND owner, POINT pt)
{
    MenuState st;
    st.owner = owner;

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = MenuProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"PetMenuClass";
        RegisterClassExW(&wc);
        s_registered = true;
    }

    int H = MenuHeight();
    // 以点击点所在显示器的工作区为准：桌宠被拖到副屏时，菜单也跟着出现在副屏
    RECT wa = MonitorWorkAreaNear(pt.x, pt.y);
    int x = pt.x, y = pt.y;
    if (x + MENU_W > wa.right - 4) x = wa.right - 4 - MENU_W;
    if (x < wa.left + 4) x = wa.left + 4;
    if (y + H > wa.bottom - 4) y = wa.bottom - 4 - H;
    if (y < wa.top + 4) y = wa.top + 4;

    SetPetFrozen(true);   // 菜单打开期间桌宠停止走动
    HWND menu = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PetMenuClass", L"", WS_POPUP,
        x, y, MENU_W, H, NULL, NULL, g_hInst, &st);
    if (!menu) { SetPetFrozen(false); return; }

    ShowWindow(menu, SW_SHOWNOACTIVATE);
    InvalidateRect(menu, NULL, FALSE);
    UpdateWindow(menu);
    SetCapture(menu);
    SetFocus(menu);
    SetClickAwayTarget(menu);   // 点击菜单外任意区域时关闭菜单

    MSG msg;
    while (IsWindow(menu) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    SetClickAwayTarget(NULL);
    SetPetFrozen(false);
}

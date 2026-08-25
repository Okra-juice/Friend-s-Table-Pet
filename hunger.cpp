// 桌宠属性窗口：显示当前的饥饿值（进度条 + 数字），点击任意位置关闭
#include "common.h"

namespace {

const int DLG_W = 260;
const int DLG_H = 214;

struct PropState {
    Gdiplus::Bitmap* bmp = NULL;
};

// 定位属性窗口：桌宠右侧（放不下则移到左侧），限制在桌宠所在显示器的工作区内；
// 桌宠被拖到副屏时属性窗口也跟过去
static void PositionProp(HWND hwnd)
{
    RECT pr, wa;
    if (g_hPetWnd && IsWindow(g_hPetWnd)) GetWindowRect(g_hPetWnd, &pr);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
    wa = MonitorWorkAreaNear((pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2);
    int x = pr.right + 14, y = pr.top;
    if (x + DLG_W > wa.right - 4) x = pr.left - DLG_W - 14;
    if (x < wa.left + 4) x = wa.left + 4;
    if (y + DLG_H > wa.bottom - 4) y = wa.bottom - DLG_H;
    if (y < wa.top + 4) y = wa.top + 4;
    SetWindowPos(hwnd, NULL, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void PaintDialog(HWND hwnd, PropState* st)
{
    using namespace G;
    if (!st->bmp) st->bmp = new Bitmap(DLG_W, DLG_H, PixelFormat32bppPARGB);
    Graphics g(st->bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));

    // 外阴影
    GraphicsPath sp;
    AddRoundRectPath(sp, 5, 9, DLG_W - 10, DLG_H - 10, 22);
    g.FillPath(&SolidBrush(Color(90, 0, 0, 0)), &sp);

    GraphicsPath bgp;
    AddRoundRectPath(bgp, 0, 0, DLG_W, DLG_H, 22);
    g.FillPath(&SolidBrush(Color(255, 255, 255, 255)), &bgp);
    g.DrawPath(&Pen(Color(255, 232, 232, 232), 1.0f), &bgp);

    g.DrawString(L"桌宠属性", -1,
        &Font(L"Microsoft YaHei UI", 20, FontStyleBold, UnitPixel), PointF(26, 16),
        &SolidBrush(Color(255, 51, 51, 51)));

    Font labF(L"Microsoft YaHei UI", 13, FontStyleBold, UnitPixel);
    g.DrawString(L"饥饿值", -1, &labF, PointF(26, 58),
                 &SolidBrush(Color(255, 70, 70, 70)));

    const float bx = 26, by = 88, bw = DLG_W - 52, bh = 16;
    GraphicsPath track;
    AddRoundRectPath(track, bx, by, bw, bh, 8);
    g.FillPath(&SolidBrush(Color(255, 230, 232, 238)), &track);

    // 填充（绿色 → 黄色 → 红色 随饥饿值变化）
    float ratio = (float)g_hunger / (float)kHungerMax;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio > 0.01f) {
        GraphicsPath fill;
        AddRoundRectPath(fill, bx, by, bw * ratio, bh, 8);
        Color c(255, 82, 196, 120);   // 绿色
        if (ratio < 0.5f) c = Color(255, 245, 166, 58);   // 黄色
        if (ratio < 0.25f) c = Color(255, 235, 87, 87);   // 红色
        g.FillPath(&SolidBrush(c), &fill);
    }

    std::wstring val = std::to_wstring(g_hunger) + L" / " + std::to_wstring(kHungerMax);
    StringFormat vsf;
    vsf.SetAlignment(StringAlignmentCenter);
    vsf.SetLineAlignment(StringAlignmentCenter);
    Font valF(L"Microsoft YaHei UI", 12, FontStyleBold, UnitPixel);
    g.DrawString(val.c_str(), -1, &valF, RectF(bx, by, bw, bh), &vsf,
                 &SolidBrush(Color(255, 255, 255, 255)));

    g.DrawString(L"心情值", -1, &labF, PointF(26, 122),
                 &SolidBrush(Color(255, 70, 70, 70)));

    const float mx = 26, my = 152, mw = DLG_W - 52, mh = 16;
    GraphicsPath mtrack;
    AddRoundRectPath(mtrack, mx, my, mw, mh, 8);
    g.FillPath(&SolidBrush(Color(255, 230, 232, 238)), &mtrack);

    // 填充（蓝色 → 橙色 → 红色 随心情值变化）
    float mratio = (float)g_mood / (float)kMoodMax;
    if (mratio < 0.0f) mratio = 0.0f;
    if (mratio > 1.0f) mratio = 1.0f;
    if (mratio > 0.01f) {
        GraphicsPath mfill;
        AddRoundRectPath(mfill, mx, my, mw * mratio, mh, 8);
        Color mc(255, 92, 124, 255);   // 蓝色
        if (mratio < 0.5f) mc = Color(255, 240, 160, 80);   // 橙色
        if (mratio < 0.25f) mc = Color(255, 235, 87, 87);   // 红色
        g.FillPath(&SolidBrush(mc), &mfill);
    }

    std::wstring mval = std::to_wstring(g_mood) + L" / " + std::to_wstring(kMoodMax);
    g.DrawString(mval.c_str(), -1, &valF, RectF(mx, my, mw, mh), &vsf,
                 &SolidBrush(Color(255, 255, 255, 255)));

    Font hintF(L"Microsoft YaHei UI", 11, FontStyleRegular, UnitPixel);
    g.DrawString(L"把文件拖给桌宠可以喂食，饥饿值会随时间缓慢下降", -1,
        &hintF, PointF(26, 182),
        &SolidBrush(Color(255, 160, 160, 160)));

    PresentLayered(hwnd, DLG_W, DLG_H, st->bmp);
}

LRESULT CALLBACK PropProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PropState* st = (PropState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        SetTimer(hwnd, 1, 120, NULL);   // 跟随桌宠移动（含跨屏）
        return 0;
    case WM_TIMER:
        if (wp == 1) PositionProp(hwnd);   // 桌宠被拖走/跨屏时属性窗口跟着走
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (st) PaintDialog(hwnd, st);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PET_CLICKAWAY:
        // 点击窗口外任意区域：关闭属性窗口
        DestroyWindow(hwnd);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        // 属性窗口只是展示信息，点一下即关闭
        DestroyWindow(hwnd);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (st && st->bmp) { delete st->bmp; st->bmp = NULL; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

// 对外接口
int ShowHungerDialog(HINSTANCE hInst)
{
    PropState st;

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = PropProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"PetHungerPropClass";
        RegisterClassExW(&wc);
        s_registered = true;
    }

    HWND dlg = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PetHungerPropClass", L"", WS_POPUP,
        0, 0, DLG_W, DLG_H, NULL, NULL, hInst, &st);
    if (!dlg) return 0;
    PositionProp(dlg);
    SetPetFrozen(true);   // 属性窗口打开期间桌宠停止走动
    ShowWindow(dlg, SW_SHOW);
    InvalidateRect(dlg, NULL, FALSE);
    UpdateWindow(dlg);
    SetForegroundWindow(dlg);
    SetFocus(dlg);
    SetClickAwayTarget(dlg);   // 点击窗口外任意区域时关闭

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    SetClickAwayTarget(NULL);
    SetPetFrozen(false);
    return 1;
}

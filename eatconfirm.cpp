// 吃文件确认对话框（气泡样式）：问句 + 小字提示 + 不同意/同意两个按钮
#include "common.h"

namespace {

const int DLG_W = 340;
const int DLG_H = 200;

const RECT kNoBtn    = { 76, 132, 186, 172 };   // 不同意
const RECT kYesBtn   = { 196, 132, 306, 172 };  // 同意
const RECT kCloseBtn = { 304, 12, 338, 46 };    // 关闭 ×（等同不同意）

struct ConfirmState {
    int  hover = 0;      // 1 不同意 2 同意 3 关闭
    int  press = 0;
    bool result = false;
    std::wstring question;   // 问句
    std::wstring hint;       // 小字提示
    std::wstring noText;     // 左侧按钮文字
    std::wstring yesText;    // 右侧按钮文字
    bool danger = false;     // 右侧按钮是否红色（危险操作）
    Gdiplus::Bitmap* bmp = NULL;
};

// 定位确认框：桌宠上方（像对话气泡，放不下则移到下方），
// 限制在桌宠所在显示器的工作区内；桌宠走动/被拖到副屏时也跟着走
static void PositionConfirm(HWND hwnd)
{
    RECT pr, wa;
    if (g_hPetWnd && IsWindow(g_hPetWnd)) GetWindowRect(g_hPetWnd, &pr);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
    wa = MonitorWorkAreaNear((pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2);
    int x = (pr.left + pr.right - DLG_W) / 2;
    int y = pr.top - DLG_H - 8;
    if (y < wa.top + 4) y = pr.bottom + 8;
    if (y + DLG_H > wa.bottom - 4) y = wa.bottom - 4 - DLG_H;
    if (x < wa.left + 4) x = wa.left + 4;
    if (x + DLG_W > wa.right - 4) x = wa.right - 4 - DLG_W;
    SetWindowPos(hwnd, NULL, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void DrawButton(Gdiplus::Graphics& g, ConfirmState* st, int id, const RECT& rc,
                const wchar_t* text, bool primary)
{
    using namespace G;
    float x = (float)rc.left, y = (float)rc.top;
    float w = (float)(rc.right - rc.left), h = (float)(rc.bottom - rc.top);
    GraphicsPath p;
    AddRoundRectPath(p, x, y, w, h, 8);
    bool hover = (st->hover == id), press = (st->press == id);
    Font f(L"Microsoft YaHei UI", 15, FontStyleBold, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    if (primary) {
        // 天蓝色主按钮；danger=true 时切换为红色（危险操作）
        Color c(255, 64, 158, 255);
        if (st->danger) c = Color(255, 225, 72, 72);
        if (press) c = st->danger ? Color(255, 200, 52, 52) : Color(255, 44, 138, 235);
        else if (hover) c = st->danger ? Color(255, 240, 110, 110) : Color(255, 100, 180, 255);
        g.FillPath(&SolidBrush(c), &p);
        g.DrawString(text, -1, &f, RectF(x, y, w, h), &sf,
                     &SolidBrush(Color(255, 255, 255, 255)));
    } else {
        g.FillPath(&SolidBrush(Color(255, 255, 255, 255)), &p);
        Pen pen(hover ? Color(255, 120, 132, 152) : Color(255, 200, 202, 208), 1.5f);
        g.DrawPath(&pen, &p);
        g.DrawString(text, -1, &f, RectF(x, y, w, h), &sf,
                     &SolidBrush(Color(255, 90, 90, 90)));
    }
}

void PaintDialog(HWND hwnd, ConfirmState* st)
{
    using namespace G;
    if (!st->bmp) st->bmp = new Bitmap(DLG_W, DLG_H, PixelFormat32bppPARGB);
    Graphics g(st->bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));

    // 外阴影
    GraphicsPath sp;
    AddRoundRectPath(sp, 5, 9, DLG_W - 10, DLG_H - 10, 24);
    g.FillPath(&SolidBrush(Color(90, 0, 0, 0)), &sp);

    GraphicsPath bgp;
    AddRoundRectPath(bgp, 0, 0, DLG_W, DLG_H, 24);
    g.FillPath(&SolidBrush(Color(255, 255, 255, 255)), &bgp);
    g.DrawPath(&Pen(Color(255, 232, 232, 232), 1.0f), &bgp);

    StringFormat qsf;
    qsf.SetAlignment(StringAlignmentCenter);
    g.DrawString(st->question.c_str(), -1,
        &Font(L"Microsoft YaHei UI", 18, FontStyleBold, UnitPixel),
        RectF(0, 32, DLG_W, 36), &qsf, &SolidBrush(Color(255, 51, 51, 51)));

    g.DrawString(st->hint.c_str(), -1,
        &Font(L"Microsoft YaHei UI", 11, FontStyleRegular, UnitPixel),
        RectF(0, 70, DLG_W, 22), &qsf, &SolidBrush(Color(255, 158, 158, 158)));

    DrawButton(g, st, 1, kNoBtn, st->noText.c_str(), false);
    DrawButton(g, st, 2, kYesBtn, st->yesText.c_str(), true);

    // 关闭 ×
    bool hover = (st->hover == 3), press = (st->press == 3);
    if (press || hover) {
        g.FillEllipse(&SolidBrush(press ? Color(80, 0, 0, 0) : Color(36, 0, 0, 0)),
            304, 12, 34, 34);
    }
    Font xf(L"Microsoft YaHei UI", 15, FontStyleRegular, UnitPixel);
    StringFormat xsf;
    xsf.SetAlignment(StringAlignmentCenter);
    xsf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"\u00D7", -1, &xf, RectF(302, 10, 38, 38), &xsf,
                 &SolidBrush(Color(255, 140, 140, 140)));

    PresentLayered(hwnd, DLG_W, DLG_H, st->bmp);
}

LRESULT CALLBACK ConfirmProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ConfirmState* st = (ConfirmState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        SetTimer(hwnd, 1, 120, NULL);   // 跟随桌宠移动（含跨屏）
        return 0;
    case WM_TIMER:
        if (wp == 1) PositionConfirm(hwnd);   // 桌宠走动/被拖走时确认框跟着走
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
        // 点击窗口外任意区域 = 不同意
        DestroyWindow(hwnd);
        return 0;
    case WM_MOUSEMOVE: {
        if (!st) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = 0;
        if (PtInRect(&kCloseBtn, pt)) h = 3;
        else if (PtInRect(&kNoBtn, pt)) h = 1;
        else if (PtInRect(&kYesBtn, pt)) h = 2;
        if (h != st->hover) { st->hover = h; InvalidateRect(hwnd, NULL, FALSE); }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (st) { st->hover = 0; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    case WM_LBUTTONDOWN: {
        if (!st) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = 0;
        if (PtInRect(&kCloseBtn, pt)) h = 3;
        else if (PtInRect(&kNoBtn, pt)) h = 1;
        else if (PtInRect(&kYesBtn, pt)) h = 2;
        st->press = h;
        if (h) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!st) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = 0;
        if (PtInRect(&kCloseBtn, pt)) h = 3;
        else if (PtInRect(&kNoBtn, pt)) h = 1;
        else if (PtInRect(&kYesBtn, pt)) h = 2;
        int p = st->press;
        st->press = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        if (p && p == h) {
            if (p == 2) st->result = true;   // 只有"同意"返回 true
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        if (wp == VK_RETURN && st) { st->result = true; DestroyWindow(hwnd); return 0; }
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
bool ConfirmDialogEx(HINSTANCE hInst, HWND owner, const std::wstring& question,
                     const std::wstring& hint, const std::wstring& noText,
                     const std::wstring& yesText, bool danger)
{
    ConfirmState st;
    st.question = question;
    st.hint = hint;
    st.noText = noText;
    st.yesText = yesText;
    st.danger = danger;

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ConfirmProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"PetEatConfirmClass";
        RegisterClassExW(&wc);
        s_registered = true;
    }

    HWND dlg = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PetEatConfirmClass", L"", WS_POPUP,
        0, 0, DLG_W, DLG_H, NULL, NULL, hInst, &st);
    if (!dlg) return false;
    PositionConfirm(dlg);

    ShowWindow(dlg, SW_SHOW);
    InvalidateRect(dlg, NULL, FALSE);
    UpdateWindow(dlg);
    SetForegroundWindow(dlg);
    SetFocus(dlg);
    SetClickAwayTarget(dlg);   // 点击窗口外任意区域 = 不同意

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    SetClickAwayTarget(NULL);
    return st.result;
}

bool ConfirmEatDialog(HINSTANCE hInst, HWND owner)
{
    return ConfirmDialogEx(hInst, owner, L"我可以吃掉这个文件吗？",
                           L"被吃掉的文件会被放入回收站",
                           L"不同意", L"同意", false);
}

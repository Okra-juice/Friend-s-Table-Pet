// 外观设置对话框：身体粗细 / 大小 / 身体大小 三个滑块 + 更换头像入口
// 滑块拖动即时生效（桌宠在背景持续渲染，实时预览）；关闭时保存配置
#include "common.h"

namespace {

const int DLG_W = 680;
const int DLG_H = 472;

const RECT kCloseBtn    = { 636, 14, 672, 50 };   // 关闭 ×
const RECT kCalmSlot    = { 20, 80, 116, 236 };   // 平静头像
const RECT kThinkSlot   = { 126, 80, 222, 236 };  // 思考头像
const RECT kMouthSlot   = { 232, 80, 328, 236 };  // 张嘴头像
const RECT kUnhappySlot = { 338, 80, 434, 236 };  // 不开心头像
const RECT kAngrySlot   = { 444, 80, 540, 236 };  // 生气头像
const RECT kHappySlot   = { 550, 80, 646, 236 };  // 开心头像
const RECT kResetBtn    = { 210, 414, 320, 456 }; // 恢复默认
const RECT kDoneBtn     = { 350, 414, 480, 456 }; // 完成

const RECT kTrackR  = { 162, 0, 380, 0 };        // 滑块轨道 x 范围（y 随行中心）
const int  kRowY[3] = { 258, 312, 366 };         // 三行滑块的纵向中心

struct SliderDef { const wchar_t* label; float* dst; float minV, maxV; };

SliderDef kSliders[3] = {
    { L"身体粗细", &g_bodyThickness, 0.6f, 1.6f },
    { L"大小",     &g_petScale,      0.8f, 1.4f },
    { L"身体大小", &g_bodyScale,      0.8f, 1.4f },
};

struct AppearState {
    int  hover = 0;          // 1 平静 5 思考 6 张嘴 7 不开心 8 生气 9 开心 2 重置 3 完成 4 关闭
    int  press = 0;
    int  dragSlider = -1;    // 正在拖动的滑块下标（-1 = 无）
    bool isDragging = false; // 拖动整个窗口
    POINT dragStart = { 0, 0 };
    POINT winStart  = { 0, 0 };
    Gdiplus::Bitmap* bmp = NULL;
};

int HitSlider(int x, int y)
{
    for (int i = 0; i < 3; i++) {
        if (std::abs(y - kRowY[i]) <= 26 &&
            x >= kTrackR.left - 18 && x <= kTrackR.right + 30)
            return i;
    }
    return -1;
}

int HitButton(int x, int y)
{
    POINT pt = { x, y };
    if (PtInRect(&kCloseBtn, pt)) return 4;
    if (PtInRect(&kCalmSlot, pt)) return 1;
    if (PtInRect(&kThinkSlot, pt)) return 5;
    if (PtInRect(&kMouthSlot, pt)) return 6;
    if (PtInRect(&kUnhappySlot, pt)) return 7;
    if (PtInRect(&kAngrySlot, pt)) return 8;
    if (PtInRect(&kHappySlot, pt)) return 9;
    if (PtInRect(&kResetBtn, pt)) return 2;
    if (PtInRect(&kDoneBtn, pt)) return 3;
    return 0;
}

// 选文件与拖拽共用：1 平静头像 2 思考头像 3 张嘴头像 4 不开心头像 5 生气头像 6 开心头像
void ApplySlotImage(HWND hwnd, int which, const std::wstring& path)
{
    G::Image* im = G::Image::FromFile(path.c_str());
    if (!im || im->GetLastStatus() != G::Ok) {
        if (im) delete im;
        MessageBoxW(hwnd,
            L"无法加载该图片，请换一张 PNG / JPG / BMP 图片试试。",
            L"桌宠", MB_ICONWARNING | MB_OK);
        return;
    }
    delete im;
    if (which == 1) ApplyHeadImage(path);
    else if (which == 2) ApplyThinkImage(path);
    else if (which == 3) ApplyMouthImage(path);
    else if (which == 4) ApplyUnhappyImage(path);
    else if (which == 5) ApplyAngryImage(path);
    else ApplyHappyImage(path);
    InvalidateRect(hwnd, NULL, FALSE);
}

// 槽位：1 平静头像 2 思考头像 3 张嘴头像 4 不开心头像 5 生气头像 6 开心头像
void ChooseSlotImage(HWND hwnd, int which)
{
    WCHAR buf[4096] = { 0 };
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 4096;
    ofn.lpstrFilter =
        L"图片文件 (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff)\0"
        L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0"
        L"所有文件 (*.*)\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn)) {
        ApplySlotImage(hwnd, which, ofn.lpstrFile);
    }
}

// 拖拽放图时定位目标槽位：1 平静 5 思考 6 张嘴 7 不开心 8 生气 9 开心；
// 没落在槽上就取水平中心最近的槽
int HitSlotForDrop(int x, int y)
{
    POINT pt = { x, y };
    if (PtInRect(&kCalmSlot, pt)) return 1;
    if (PtInRect(&kThinkSlot, pt)) return 5;
    if (PtInRect(&kMouthSlot, pt)) return 6;
    if (PtInRect(&kUnhappySlot, pt)) return 7;
    if (PtInRect(&kAngrySlot, pt)) return 8;
    if (PtInRect(&kHappySlot, pt)) return 9;
    const int centers[6][2] = { {1, 68}, {5, 174}, {6, 280}, {7, 386}, {8, 492}, {9, 598} };
    int best = 1, bestD = 0x7fffffff;
    for (auto& c : centers) {
        int d = std::abs(x - c[1]);
        if (d < bestD) { bestD = d; best = c[0]; }
    }
    return best;
}

void DrawImageSlot(Gdiplus::Graphics& g, AppearState* st, int id, const RECT& rc,
                   const wchar_t* label, Gdiplus::Image* img)
{
    using namespace G;
    float x = (float)rc.left, y = (float)rc.top;
    float w = (float)(rc.right - rc.left);
    bool hover = (st->hover == id), press = (st->press == id);

    GraphicsPath card;
    AddRoundRectPath(card, x, y, w, 100, 12);
    g.FillPath(&SolidBrush(Color(255, 250, 250, 250)), &card);
    g.DrawPath(&Pen(hover ? Color(255, 100, 170, 255) : Color(255, 228, 228, 228),
                    hover ? 1.8f : 1.2f), &card);
    if (img) {
        // 居中裁剪显示图片（保持正方形，槽位窄时缩小预览）
        float ps = w - 24.0f;
        if (ps > 84.0f) ps = 84.0f;
        float px = x + (w - ps) / 2.0f, py = y + 8.0f;
        GraphicsPath inner;
        AddRoundRectPath(inner, px, py, ps, ps, 9);
        g.SetClip(&inner);
        float iw = (float)img->GetWidth(), ih = (float)img->GetHeight();
        float s = std::min(iw, ih), sx = (iw - s) / 2, sy = (ih - s) / 2;
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(img, RectF(px, py, ps, ps), sx, sy, s, s, UnitPixel);
        g.ResetClip();
    } else {
        StringFormat csf;
        csf.SetAlignment(StringAlignmentCenter);
        csf.SetLineAlignment(StringAlignmentCenter);
        g.FillEllipse(&SolidBrush(Color(255, 235, 235, 235)),
            x + w / 2 - 20.0f, y + 34.0f, 40.0f, 40.0f);
        Font plusF(L"Microsoft YaHei UI", 26, FontStyleRegular, UnitPixel);
        g.DrawString(L"+", -1, &plusF, RectF(x + w / 2 - 20, y + 26, 40, 44), &csf,
                     &SolidBrush(Color(255, 176, 176, 176)));
    }

    StringFormat lsf;
    lsf.SetAlignment(StringAlignmentCenter);
    Font labF(L"Microsoft YaHei UI", 12, FontStyleBold, UnitPixel);
    g.DrawString(label, -1, &labF, RectF(x, y + 104, w, 20), &lsf,
                 &SolidBrush(Color(255, 80, 80, 80)));

    RECT btn = { (int)x + 6, (int)y + 128, (int)(x + w) - 6, (int)y + 154 };
    GraphicsPath bp;
    AddRoundRectPath(bp, (float)btn.left, (float)btn.top,
        (float)(btn.right - btn.left), (float)(btn.bottom - btn.top), 7);
    Color bc(255, 64, 158, 255);
    if (press) bc = Color(255, 44, 138, 235);
    else if (hover) bc = Color(255, 100, 180, 255);
    g.FillPath(&SolidBrush(bc), &bp);
    Font bf(L"Microsoft YaHei UI", 13, FontStyleBold, UnitPixel);
    g.DrawString(L"选择图片", -1, &bf,
        RectF((float)btn.left, (float)btn.top,
              (float)(btn.right - btn.left), (float)(btn.bottom - btn.top)),
        &lsf, &SolidBrush(Color(255, 255, 255, 255)));
}

float ValueToX(int i, float v)
{
    const SliderDef& s = kSliders[i];
    float t = (v - s.minV) / (s.maxV - s.minV);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return kTrackR.left + t * (float)(kTrackR.right - kTrackR.left);
}

void XToValue(int i, int x)
{
    const SliderDef& s = kSliders[i];
    float t = (x - kTrackR.left) / (float)(kTrackR.right - kTrackR.left);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    *s.dst = s.minV + t * (s.maxV - s.minV);
}

// primary：天蓝色主按钮，否则白底描边
void DrawButton(Gdiplus::Graphics& g, AppearState* st, int id, const RECT& rc,
                const wchar_t* text, bool primary)
{
    using namespace G;
    float x = (float)rc.left, y = (float)rc.top;
    float w = (float)(rc.right - rc.left), h = (float)(rc.bottom - rc.top);
    GraphicsPath p;
    AddRoundRectPath(p, x, y, w, h, 6);
    bool hover = (st->hover == id), press = (st->press == id);
    Font f(L"Microsoft YaHei UI", 15, FontStyleBold, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);

    if (primary) {
        Color c(255, 64, 158, 255);
        if (press) c = Color(255, 44, 138, 235);
        else if (hover) c = Color(255, 100, 180, 255);
        g.FillPath(&SolidBrush(c), &p);
        g.DrawString(text, -1, &f, RectF(x, y, w, h), &sf,
                     &SolidBrush(Color(255, 255, 255, 255)));
    } else {
        g.DrawPath(&Pen(Color(255, 150, 150, 150), 1.6f), &p);
        if (press) g.FillPath(&SolidBrush(Color(70, 0, 0, 0)), &p);
        else if (hover) g.FillPath(&SolidBrush(Color(36, 0, 0, 0)), &p);
        g.DrawString(text, -1, &f, RectF(x, y, w, h), &sf,
                     &SolidBrush(Color(255, 90, 90, 90)));
    }
}

void DrawSliderRow(Gdiplus::Graphics& g, int i)
{
    using namespace G;
    float cy = (float)kRowY[i];
    float x0 = (float)kTrackR.left, x1 = (float)kTrackR.right;
    float v = *kSliders[i].dst;

    Font labF(L"Microsoft YaHei UI", 14, FontStyleBold, UnitPixel);
    g.DrawString(kSliders[i].label, -1, &labF, PointF(30, cy - 10),
                 &SolidBrush(Color(255, 70, 70, 70)));

    GraphicsPath track;
    AddRoundRectPath(track, x0, cy - 3, x1 - x0, 6, 3);
    g.FillPath(&SolidBrush(Color(255, 226, 228, 235)), &track);

    float tx = ValueToX(i, v);
    if (tx > x0 + 1.0f) {
        GraphicsPath fill;
        AddRoundRectPath(fill, x0, cy - 3, tx - x0, 6, 3);
        g.FillPath(&SolidBrush(Color(255, 64, 158, 255)), &fill);
    }

    float r = 9.0f;
    g.FillEllipse(&SolidBrush(Color(255, 255, 255, 255)), tx - r, cy - r, r * 2, r * 2);
    g.DrawEllipse(&Pen(Color(255, 64, 158, 255), 2.0f), tx - r, cy - r, r * 2, r * 2);

    Font valF(L"Microsoft YaHei UI", 13, FontStyleBold, UnitPixel);
    std::wstring val = std::to_wstring((int)(v * 100.0f + 0.5f)) + L"%";
    StringFormat vsf;
    vsf.SetAlignment(StringAlignmentFar);
    g.DrawString(val.c_str(), -1, &valF, RectF(392, cy - 10, 38, 20), &vsf,
                 &SolidBrush(Color(255, 64, 158, 255)));
}

void PaintDialog(HWND hwnd, AppearState* st)
{
    using namespace G;
    if (!st->bmp) st->bmp = new Bitmap(DLG_W, DLG_H, PixelFormat32bppPARGB);
    Graphics g(st->bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));

    GraphicsPath sp;
    AddRoundRectPath(sp, 5, 9, DLG_W - 10, DLG_H - 10, 26);
    g.FillPath(&SolidBrush(Color(90, 0, 0, 0)), &sp);

    GraphicsPath bgp;
    AddRoundRectPath(bgp, 0, 0, DLG_W, DLG_H, 26);
    g.FillPath(&SolidBrush(Color(255, 255, 255, 255)), &bgp);

    g.DrawString(L"外观设置", -1,
        &Font(L"Microsoft YaHei UI", 23, FontStyleBold, UnitPixel), PointF(30, 16),
        &SolidBrush(Color(255, 51, 51, 51)));
    g.DrawString(L"上边选择头像图片，下边调整桌宠外形，拖动滑块实时生效", -1,
        &Font(L"Microsoft YaHei UI", 12, FontStyleRegular, UnitPixel), PointF(30, 52),
        &SolidBrush(Color(255, 150, 150, 150)));

    DrawImageSlot(g, st, 1, kCalmSlot, L"平静头像", g_headImage);
    DrawImageSlot(g, st, 5, kThinkSlot, L"思考头像", g_thinkImage);
    DrawImageSlot(g, st, 6, kMouthSlot, L"张嘴头像", g_mouthOpenImage);
    DrawImageSlot(g, st, 7, kUnhappySlot, L"不开心头像", g_unhappyImage);
    DrawImageSlot(g, st, 8, kAngrySlot, L"生气头像", g_angryImage);
    DrawImageSlot(g, st, 9, kHappySlot, L"开心头像", g_happyImage);

    for (int i = 0; i < 3; i++) DrawSliderRow(g, i);

    DrawButton(g, st, 2, kResetBtn, L"恢复默认", false);
    DrawButton(g, st, 3, kDoneBtn, L"完成", true);

    {
        bool hover = (st->hover == 4), press = (st->press == 4);
        if (press || hover) {
            g.FillEllipse(&SolidBrush(press ? Color(80, 0, 0, 0) : Color(36, 0, 0, 0)),
                636, 14, 36, 36);
        }
        Font xf(L"Microsoft YaHei UI", 17, FontStyleRegular, UnitPixel);
        StringFormat xsf;
        xsf.SetAlignment(StringAlignmentCenter);
        xsf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"\u00D7", -1, &xf, RectF(634, 12, 40, 40), &xsf,
                     &SolidBrush(Color(255, 140, 140, 140)));
    }

    PresentLayered(hwnd, DLG_W, DLG_H, st->bmp);
}

LRESULT CALLBACK AppearProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    AppearState* st = (AppearState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        st = (AppearState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (st) PaintDialog(hwnd, st);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DROPFILES: {
        // 把图片文件直接拖到选图框上添加
        HDROP hDrop = (HDROP)wp;
        UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        if (n > 0 && st) {
            WCHAR path[MAX_PATH * 2];
            if (DragQueryFileW(hDrop, 0, path, MAX_PATH * 2) > 0) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                int slot = HitSlotForDrop(pt.x, pt.y);   // 1 平静 5 思考 6 张嘴 7 不开心 8 生气 9 开心
                int which = (slot == 1) ? 1 : (slot == 5) ? 2 : (slot == 6) ? 3 :
                            (slot == 7) ? 4 : (slot == 8) ? 5 : 6;
                ApplySlotImage(hwnd, which, path);
            }
        }
        DragFinish(hDrop);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        if (!st) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (st->isDragging) {
            // 拖动整个窗口
            POINT cur;
            GetCursorPos(&cur);
            int nx = st->winStart.x + (cur.x - st->dragStart.x);
            int ny = st->winStart.y + (cur.y - st->dragStart.y);
            SetWindowPos(hwnd, NULL, nx, ny, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        if (st->dragSlider >= 0) {
            XToValue(st->dragSlider, pt.x);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        int h = HitButton(pt.x, pt.y);
        if (h != st->hover) {
            st->hover = h;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (st) {
            st->hover = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        if (!st) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int s = HitSlider(pt.x, pt.y);
        if (s >= 0) {
            st->dragSlider = s;
            XToValue(s, pt.x);
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        int b = HitButton(pt.x, pt.y);
        if (b) {
            st->press = b;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        // 空白处：拖动窗口
        GetCursorPos(&st->dragStart);
        RECT rc;
        GetWindowRect(hwnd, &rc);
        st->winStart.x = rc.left;
        st->winStart.y = rc.top;
        st->isDragging = true;
        SetCapture(hwnd);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!st) break;
        if (st->dragSlider >= 0) {
            st->dragSlider = -1;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (st->isDragging) {
            st->isDragging = false;
            ReleaseCapture();
            return 0;
        }
        if (st->press) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int b = HitButton(pt.x, pt.y);
            ReleaseCapture();
            st->press = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            if (b == 1) {
                ChooseSlotImage(hwnd, 1);   // 平静头像
            } else if (b == 5) {
                ChooseSlotImage(hwnd, 2);   // 思考头像
            } else if (b == 6) {
                ChooseSlotImage(hwnd, 3);   // 张嘴头像
            } else if (b == 7) {
                ChooseSlotImage(hwnd, 4);   // 不开心头像
            } else if (b == 8) {
                ChooseSlotImage(hwnd, 5);   // 生气头像
            } else if (b == 9) {
                ChooseSlotImage(hwnd, 6);   // 开心头像
            } else if (b == 2) {
                // 恢复默认：三个滑块回到 1.0
                g_bodyThickness = 1.0f;
                g_petScale = 1.0f;
                g_bodyScale = 1.0f;
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (b == 3 || b == 4) {
                DestroyWindow(hwnd);
            }
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        if (wp == VK_RETURN && st) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_CAPTURECHANGED:
        if (st) {
            st->dragSlider = -1;
            st->isDragging = false;
            st->press = 0;
        }
        return 0;
    case WM_DESTROY:
        DragAcceptFiles(hwnd, FALSE);
        if (st && st->bmp) { delete st->bmp; st->bmp = NULL; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

// 对外接口
int ShowAppearanceDialog(HINSTANCE hInst, HWND parent)
{
    AppearState st;

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = AppearProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"PetAppearClass";
        RegisterClassExW(&wc);
        s_registered = true;
    }

    // 定位在桌宠右侧（放不下则移到左侧），方便实时预览；
    // 以桌宠所在显示器的工作区为边界：桌宠在副屏时设置窗口也出现在副屏
    RECT pr, wa;
    if (g_hPetWnd && IsWindow(g_hPetWnd)) GetWindowRect(g_hPetWnd, &pr);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
    wa = MonitorWorkAreaNear((pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2);
    int x = pr.right + 14, y = pr.top;
    if (x + DLG_W > wa.right - 4) x = pr.left - DLG_W - 14;
    if (x < wa.left + 4) x = wa.left + 4;
    if (y + DLG_H > wa.bottom - 4) y = wa.bottom - DLG_H;
    if (y < wa.top + 4) y = wa.top + 4;

    HWND dlg = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PetAppearClass", L"", WS_POPUP,
        x, y, DLG_W, DLG_H, parent, NULL, hInst, &st);
    if (!dlg) return 0;
    DragAcceptFiles(dlg, TRUE);   // 支持把图片拖到选图框上
    SetPetFrozen(true);   // 设置界面打开期间桌宠停止走动
    ShowWindow(dlg, SW_SHOW);
    InvalidateRect(dlg, NULL, FALSE);
    UpdateWindow(dlg);
    SetForegroundWindow(dlg);
    SetFocus(dlg);

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    SetPetFrozen(false);
    SaveAppearanceSettings();
    return 1;
}

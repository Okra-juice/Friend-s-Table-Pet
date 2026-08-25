// 首次使用选图对话框（外观设置同款样式）：
// 顶部横排六个选图框（平静/思考/张嘴/不开心/生气/开心），底部"开始使用"按钮；
// "高级选项"手风琴内联设置聊天与桌宠大小；空白处可拖动窗口；× / Esc = 取消（用默认头像）
#include "common.h"

namespace {

const int DLG_W = 680;
const int DLG_H_COLLAPSED = 350;   // 折叠：仅"高级选项"一行

const RECT kCalmSlot    = { 20, 80, 116, 236 };   // 平静头像
const RECT kThinkSlot   = { 126, 80, 222, 236 };  // 思考头像
const RECT kMouthSlot   = { 232, 80, 328, 236 };  // 张嘴头像
const RECT kUnhappySlot = { 338, 80, 434, 236 };  // 不开心头像
const RECT kAngrySlot   = { 444, 80, 540, 236 };  // 生气头像
const RECT kHappySlot   = { 550, 80, 646, 236 };  // 开心头像
const RECT kStartBtn    = { 270, 262, 410, 304 }; // 开始使用
const RECT kCloseBtn    = { 636, 14, 672, 50 };   // 关闭 ×
const RECT kAdvRow      = { 24, 310, 440, 344 };  // 高级选项行（点击展开/收起）

// 滑块定义（桌宠大小，拖动实时生效）
struct SliderDef { const wchar_t* label; float* dst; float minV, maxV; };
SliderDef kSliders[3] = {
    { L"身体粗细", &g_bodyThickness, 0.6f, 1.6f },
    { L"大小",     &g_petScale,      0.8f, 1.4f },
    { L"身体大小", &g_bodyScale,      0.8f, 1.4f },
};

struct ChooserState {
    std::wstring calmPath, thinkPath, mouthPath, unhappyPath, angryPath, happyPath;
    Gdiplus::Image* calmImg = NULL;
    Gdiplus::Image* thinkImg = NULL;
    Gdiplus::Image* mouthImg = NULL;
    Gdiplus::Image* unhappyImg = NULL;
    Gdiplus::Image* angryImg = NULL;
    Gdiplus::Image* happyImg = NULL;
    int  hover = 0;      // 1-6 槽位 7 开始使用 8 关闭 9 高级选项 10 聊天设置 11 桌宠大小 12 保存
    int  press = 0;
    bool resultApplied = false;
    bool advancedOpen = false;
    bool chatOpen = false;
    bool appearOpen = false;     // 桌宠大小手风琴是否展开
    int  dragSlider = -1;        // 正在拖动的滑块下标（-1 = 无）
    HWND edits[5] = {};          // AI 表单输入框（0 地址 1 密钥 2 模型 3 记忆 4 提示词）
    int  bmpH = 0;               // 当前离屏缓冲的高度（随展开/收起变化）
    Gdiplus::Bitmap* bmp = NULL;

    bool isDragging = false;
    POINT dragStart = { 0, 0 };
    POINT winStart  = { 0, 0 };
};

// 动态布局：根据展开状态计算各区域位置与总高度
struct ChooserLayout {
    int H;
    RECT chatHead;      // "聊天设置" 分组行
    RECT appearHead;    // "桌宠大小" 分组行
    RECT tip;           // 灰色小字
    RECT edt[5];        // AI 表单输入框
    RECT saveBtn;       // 保存按钮
    RECT sl[3];         // 三行滑块
};

static void ComputeLayout(const ChooserState* st, ChooserLayout& L)
{
    ZeroMemory(&L, sizeof(L));
    if (!st->advancedOpen) { L.H = DLG_H_COLLAPSED; return; }
    L.chatHead = { 30, 352, 650, 388 };
    int y = 396;
    if (st->chatOpen) {
        L.edt[0] = { 120, 396, 650, 424 };
        L.edt[1] = { 120, 432, 650, 460 };
        L.edt[2] = { 120, 468, 650, 496 };
        L.edt[3] = { 120, 504, 310, 532 };
        L.edt[4] = { 30, 568, 650, 656 };
        L.saveBtn = { 540, 668, 650, 704 };
        y = 716;
    }
    L.appearHead = { 30, y, 650, y + 36 };
    if (st->appearOpen) {
        y += 44;
        for (int i = 0; i < 3; i++) { L.sl[i] = { 30, y, 650, y + 44 }; y += 48; }
        L.H = y + 10;
    } else {
        L.H = y + 40;   // 完整容纳"桌宠大小"分组行，避免底部被裁剪
    }
}

int HitButton(int x, int y, const ChooserState* st, const ChooserLayout& L)
{
    POINT pt = { x, y };
    if (PtInRect(&kCloseBtn, pt)) return 8;
    if (PtInRect(&kStartBtn, pt)) return 7;
    if (PtInRect(&kCalmSlot, pt)) return 1;
    if (PtInRect(&kThinkSlot, pt)) return 2;
    if (PtInRect(&kMouthSlot, pt)) return 3;
    if (PtInRect(&kUnhappySlot, pt)) return 4;
    if (PtInRect(&kAngrySlot, pt)) return 5;
    if (PtInRect(&kHappySlot, pt)) return 6;
    if (PtInRect(&kAdvRow, pt)) return 9;
    if (st->advancedOpen) {
        if (PtInRect(&L.chatHead, pt)) return 10;
        if (PtInRect(&L.appearHead, pt)) return 11;
        if (st->chatOpen && PtInRect(&L.saveBtn, pt)) return 12;
    }
    return 0;
}

int HitSlider(int x, int y, const ChooserLayout& L)
{
    for (int i = 0; i < 3; i++) {
        if (L.sl[i].top == 0) return -1;
        if (std::abs(y - (L.sl[i].top + L.sl[i].bottom) / 2) <= 22 &&
            x >= 130 && x <= 450)
            return i;
    }
    return -1;
}

float SliderX(int i, float v)
{
    const SliderDef& s = kSliders[i];
    float t = (v - s.minV) / (s.maxV - s.minV);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return 150.0f + t * 270.0f;
}

void XToSlider(int i, int x)
{
    const SliderDef& s = kSliders[i];
    float t = (x - 150.0f) / 270.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    *s.dst = s.minV + t * (s.maxV - s.minV);
}

// 选文件与拖拽共用：1 平静 2 思考 3 张嘴 4 不开心 5 生气 6 开心
void ApplySlotFile(HWND hwnd, ChooserState* st, int which, const std::wstring& path)
{
    G::Image* im = G::Image::FromFile(path.c_str());
    if (!im || im->GetLastStatus() != G::Ok) {
        if (im) delete im;
        MessageBoxW(hwnd,
            L"无法加载该图片，请换一张 PNG / JPG / BMP 图片试试。",
            L"桌宠", MB_ICONWARNING | MB_OK);
        return;
    }
    if (which == 1) { if (st->calmImg) delete st->calmImg; st->calmImg = im; st->calmPath = path; }
    else if (which == 2) { if (st->thinkImg) delete st->thinkImg; st->thinkImg = im; st->thinkPath = path; }
    else if (which == 3) { if (st->mouthImg) delete st->mouthImg; st->mouthImg = im; st->mouthPath = path; }
    else if (which == 4) { if (st->unhappyImg) delete st->unhappyImg; st->unhappyImg = im; st->unhappyPath = path; }
    else if (which == 5) { if (st->angryImg) delete st->angryImg; st->angryImg = im; st->angryPath = path; }
    else { if (st->happyImg) delete st->happyImg; st->happyImg = im; st->happyPath = path; }
    InvalidateRect(hwnd, NULL, FALSE);
}

// 槽位：1 平静 2 思考 3 张嘴 4 不开心 5 生气 6 开心
void ChooseSlotFile(HWND hwnd, ChooserState* st, int which)
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
        ApplySlotFile(hwnd, st, which, ofn.lpstrFile);
    }
}

// 拖拽放图时定位目标槽位；没落在槽上就取水平中心最近的槽
int HitSlotForDrop(int x, int y)
{
    POINT pt = { x, y };
    if (PtInRect(&kCalmSlot, pt)) return 1;
    if (PtInRect(&kThinkSlot, pt)) return 2;
    if (PtInRect(&kMouthSlot, pt)) return 3;
    if (PtInRect(&kUnhappySlot, pt)) return 4;
    if (PtInRect(&kAngrySlot, pt)) return 5;
    if (PtInRect(&kHappySlot, pt)) return 6;
    const int centers[6][2] = { {1, 68}, {2, 174}, {3, 280}, {4, 386}, {5, 492}, {6, 598} };
    int best = 1, bestD = 0x7fffffff;
    for (auto& c : centers) {
        int d = std::abs(x - c[1]);
        if (d < bestD) { bestD = d; best = c[0]; }
    }
    return best;
}

void DrawButton(Gdiplus::Graphics& g, ChooserState* st, int id, const RECT& rc,
                const wchar_t* text)
{
    using namespace G;
    float x = (float)rc.left, y = (float)rc.top;
    float w = (float)(rc.right - rc.left), h = (float)(rc.bottom - rc.top);
    GraphicsPath p;
    AddRoundRectPath(p, x, y, w, h, 8);
    bool hover = (st->hover == id), press = (st->press == id);
    Color c(255, 64, 158, 255);
    if (press) c = Color(255, 44, 138, 235);
    else if (hover) c = Color(255, 100, 180, 255);
    g.FillPath(&SolidBrush(c), &p);
    Font f(L"Microsoft YaHei UI", 15, FontStyleBold, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text, -1, &f, RectF(x, y, w, h), &sf,
                 &SolidBrush(Color(255, 255, 255, 255)));
}

// 手风琴分组行：左箭头 + 文字，整行可点
void DrawGroupHead(Gdiplus::Graphics& g, ChooserState* st, int id, const RECT& rc,
                   const wchar_t* text, bool open)
{
    using namespace G;
    bool hover = (st->hover == id), press = (st->press == id);
    if (hover || press) {
        GraphicsPath p;
        AddRoundRectPath(p, (float)rc.left, (float)rc.top,
            (float)(rc.right - rc.left), (float)(rc.bottom - rc.top), 9);
        g.FillPath(&SolidBrush(press ? Color(60, 0, 0, 0) : Color(30, 0, 0, 0)), &p);
    }
    PointF tri[3];
    float cy = (float)(rc.top + rc.bottom) / 2.0f;
    if (open) {   // 展开：向下箭头
        tri[0] = PointF(40, cy - 5); tri[1] = PointF(54, cy - 5); tri[2] = PointF(47, cy + 4);
    } else {      // 收起：向右箭头
        tri[0] = PointF(42, cy - 6); tri[1] = PointF(42, cy + 6); tri[2] = PointF(52, cy);
    }
    g.FillPolygon(&SolidBrush(Color(255, 130, 136, 150)), tri, 3);
    Font f(L"Microsoft YaHei UI", 13, FontStyleBold, UnitPixel);
    g.DrawString(text, -1, &f, PointF(64, (float)rc.top + 7),
                 &SolidBrush(Color(255, 70, 74, 88)));
}

void DrawSliderRow(Gdiplus::Graphics& g, int i, const RECT& rc)
{
    using namespace G;
    float cy = (float)(rc.top + rc.bottom) / 2.0f;
    Font labF(L"Microsoft YaHei UI", 12, FontStyleRegular, UnitPixel);
    g.DrawString(kSliders[i].label, -1, &labF, PointF(30, cy - 9),
                 &SolidBrush(Color(255, 90, 90, 90)));
    GraphicsPath track;
    AddRoundRectPath(track, 150, cy - 3, 270, 6, 3);
    g.FillPath(&SolidBrush(Color(255, 226, 228, 235)), &track);
    float tx = SliderX(i, *kSliders[i].dst);
    if (tx > 151.0f) {
        GraphicsPath fill;
        AddRoundRectPath(fill, 150, cy - 3, tx - 150, 6, 3);
        g.FillPath(&SolidBrush(Color(255, 64, 158, 255)), &fill);
    }
    float r = 9.0f;
    g.FillEllipse(&SolidBrush(Color(255, 255, 255, 255)), tx - r, cy - r, r * 2, r * 2);
    g.DrawEllipse(&Pen(Color(255, 64, 158, 255), 2.0f), tx - r, cy - r, r * 2, r * 2);
    Font valF(L"Microsoft YaHei UI", 12, FontStyleBold, UnitPixel);
    std::wstring val = std::to_wstring((int)(*kSliders[i].dst * 100.0f + 0.5f)) + L"%";
    StringFormat vsf;
    vsf.SetAlignment(StringAlignmentFar);
    g.DrawString(val.c_str(), -1, &valF, RectF(430, cy - 9, 60, 20), &vsf,
                 &SolidBrush(Color(255, 64, 158, 255)));
}

// 与外观设置一致的选图槽位
void DrawImageSlot(Gdiplus::Graphics& g, ChooserState* st, int id, const RECT& rc,
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

// 内联 AI 表单：输入框管理
const UINT WM_ADV_ESC = WM_APP + 20;
static WNDPROC g_chooserEditProc = NULL;

// 输入框子类化：ESC 收起聊天表单（避免直接关闭整个对话框）
LRESULT CALLBACK ChooserEditProc(HWND e, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        PostMessageW(GetParent(e), WM_ADV_ESC, 0, 0);
        return 0;
    }
    return CallWindowProcW(g_chooserEditProc, e, msg, wp, lp);
}

// 展开聊天表单时定位并显示输入框；visible=false 时全部隐藏
void LayoutEdits(ChooserState* st, const ChooserLayout* L, bool visible)
{
    for (int i = 0; i < 5; i++) {
        if (!st->edits[i]) continue;
        if (visible && L) {
            SetWindowPos(st->edits[i], NULL, L->edt[i].left, L->edt[i].top,
                         L->edt[i].right - L->edt[i].left,
                         L->edt[i].bottom - L->edt[i].top, SWP_NOZORDER);
            ShowWindow(st->edits[i], SW_SHOW);
        } else {
            ShowWindow(st->edits[i], SW_HIDE);
        }
    }
    if (visible && L) SetFocus(st->edits[0]);
}

static std::wstring TrimSpaces(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 从内联表单读取并保存 AI 配置
void SaveAIConfigFromEdits(HWND hwnd, ChooserState* st)
{
    auto get = [](HWND e) {
        int n = GetWindowTextLengthW(e);
        std::wstring s;
        if (n > 0) {
            s.resize(n);
            GetWindowTextW(e, &s[0], n + 1);
        }
        return s;
    };
    AIConfig cfg;
    cfg.apiUrl = TrimSpaces(get(st->edits[0]));
    cfg.apiKey = TrimSpaces(get(st->edits[1]));
    cfg.model  = TrimSpaces(get(st->edits[2]));
    cfg.prompt = get(st->edits[4]);
    std::wstring mem = TrimSpaces(get(st->edits[3]));
    cfg.memoryKB = mem.empty() ? 16 : _wtoi(mem.c_str());
    if (cfg.memoryKB < 1) cfg.memoryKB = 1;
    if (cfg.memoryKB > 8192) cfg.memoryKB = 8192;
    if (cfg.apiUrl.empty() || cfg.model.empty()) {
        MessageBoxW(hwnd, L"请填写 API 地址和模型名称。", L"AI 设置",
                    MB_ICONWARNING | MB_OK);
        return;
    }
    SaveAIConfigShared(cfg);
    MessageBoxW(hwnd, L"已保存，开始聊天时会生效。", L"AI 设置",
                MB_ICONINFORMATION | MB_OK);
}

// 调整窗口高度（保持中心位置不变）并更新圆角
void ResizeTo(HWND hwnd, int newH)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int cy = (rc.top + rc.bottom) / 2;
    SetWindowPos(hwnd, NULL, rc.left, cy - newH / 2, DLG_W, newH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, DLG_W + 1, newH + 1, 24, 24), TRUE);
}

// 绘制
void PaintDialog(HWND hwnd, ChooserState* st)
{
    using namespace G;
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);

    ChooserLayout L;
    ComputeLayout(st, L);
    int H = L.H;
    if (!st->bmp || st->bmpH != H) {
        if (st->bmp) { delete st->bmp; st->bmp = NULL; }
        st->bmp = new Bitmap(DLG_W, H, PixelFormat32bppPARGB);
        st->bmpH = H;
    }
    Graphics g(st->bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(255, 255, 255, 255));

    // 背景白卡 + 细边框（圆角由 SetWindowRgn 裁切）
    GraphicsPath bgp;
    AddRoundRectPath(bgp, 0, 0, DLG_W, H, 24);
    g.FillPath(&SolidBrush(Color(255, 255, 255, 255)), &bgp);
    g.DrawPath(&Pen(Color(255, 228, 228, 228), 1.0f), &bgp);

    g.DrawString(L"欢迎使用桌宠", -1,
        &Font(L"Microsoft YaHei UI", 23, FontStyleBold, UnitPixel), PointF(30, 16),
        &SolidBrush(Color(255, 51, 51, 51)));
    g.DrawString(L"后续可在右键菜单里面设置", -1,
        &Font(L"Microsoft YaHei UI", 12, FontStyleRegular, UnitPixel), PointF(30, 52),
        &SolidBrush(Color(255, 150, 150, 150)));

    DrawImageSlot(g, st, 1, kCalmSlot, L"平静头像", st->calmImg);
    DrawImageSlot(g, st, 2, kThinkSlot, L"思考头像", st->thinkImg);
    DrawImageSlot(g, st, 3, kMouthSlot, L"张嘴头像", st->mouthImg);
    DrawImageSlot(g, st, 4, kUnhappySlot, L"不开心头像", st->unhappyImg);
    DrawImageSlot(g, st, 5, kAngrySlot, L"生气头像", st->angryImg);
    DrawImageSlot(g, st, 6, kHappySlot, L"开心头像", st->happyImg);

    DrawButton(g, st, 7, kStartBtn, L"开始使用");

    bool hover = (st->hover == 8), press = (st->press == 8);
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

    // 高级选项：点击展开/收起
    bool advHover = (st->hover == 9), advPress = (st->press == 9);
    if (advHover || advPress) {
        GraphicsPath ap;
        AddRoundRectPath(ap, 22, 310, 420, 36, 9);
        g.FillPath(&SolidBrush(advPress ? Color(80, 0, 0, 0) : Color(40, 0, 0, 0)), &ap);
    }
    PointF tri[3];
    if (st->advancedOpen) {   // 展开：向下箭头
        tri[0] = PointF(30, 321); tri[1] = PointF(44, 321); tri[2] = PointF(37, 330);
    } else {                  // 折叠：向右箭头
        tri[0] = PointF(32, 321); tri[1] = PointF(32, 333); tri[2] = PointF(42, 327);
    }
    g.FillPolygon(&SolidBrush(Color(255, 130, 136, 150)), tri, 3);
    Font advF(L"Microsoft YaHei UI", 13, FontStyleBold, UnitPixel);
    g.DrawString(L"高级选项", -1, &advF, PointF(54, 315),
                 &SolidBrush(Color(255, 96, 100, 114)));

    // 展开区：聊天设置 / 桌宠大小 手风琴
    if (st->advancedOpen) {
        DrawGroupHead(g, st, 10, L.chatHead, L"聊天设置", st->chatOpen);
        if (st->chatOpen) {
            const wchar_t* kLabels[5] = { L"API 地址", L"API 密钥", L"模型名称",
                                          L"记忆长度", L"角色提示词" };
            Font labF(L"Microsoft YaHei UI", 12, FontStyleRegular, UnitPixel);
            SolidBrush labC(Color(255, 130, 130, 130));
            for (int i = 0; i < 4; i++)
                g.DrawString(kLabels[i], -1, &labF, PointF(30, (float)L.edt[i].top + 6), &labC);
            g.DrawString(kLabels[4], -1, &labF, PointF(30, (float)L.edt[4].top - 26), &labC);
            g.DrawString(L"KB", -1, &labF, PointF(318, (float)L.edt[3].top + 6), &labC);
            DrawButton(g, st, 12, L.saveBtn, L"保存");
        }
        DrawGroupHead(g, st, 11, L.appearHead, L"桌宠大小", st->appearOpen);
        if (st->appearOpen) {
            for (int i = 0; i < 3; i++) DrawSliderRow(g, i, L.sl[i]);
        }
    }

    // 离屏缓冲一次性 BitBlt 上屏（普通窗口，子控件区域由 WS_CLIPCHILDREN 排除）
    HDC mdc = CreateCompatibleDC(ps.hdc);
    HBITMAP hb = NULL;
    st->bmp->GetHBITMAP(Color(255, 255, 255, 255), &hb);
    HGDIOBJ old = SelectObject(mdc, hb);
    BitBlt(ps.hdc, 0, 0, DLG_W, H, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, old);
    DeleteObject(hb);
    DeleteDC(mdc);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK ChooserProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ChooserState* st = (ChooserState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        return 0;
    case WM_PAINT:
        if (st) PaintDialog(hwnd, st);
        return 0;
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
                ApplySlotFile(hwnd, st, HitSlotForDrop(pt.x, pt.y), path);
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
            POINT cur;
            GetCursorPos(&cur);
            SetWindowPos(hwnd, NULL,
                st->winStart.x + (cur.x - st->dragStart.x),
                st->winStart.y + (cur.y - st->dragStart.y),
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        if (st->dragSlider >= 0) {
            XToSlider(st->dragSlider, pt.x);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        ChooserLayout L;
        ComputeLayout(st, L);
        int h = HitButton(pt.x, pt.y, st, L);
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
        ChooserLayout L;
        ComputeLayout(st, L);
        int s = HitSlider(pt.x, pt.y, L);
        if (s >= 0) {
            st->dragSlider = s;
            XToSlider(s, pt.x);
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        int b = HitButton(pt.x, pt.y, st, L);
        if (b) {
            st->press = b;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        } else {
            // 空白处：拖动窗口
            GetCursorPos(&st->dragStart);
            RECT rc;
            GetWindowRect(hwnd, &rc);
            st->winStart.x = rc.left;
            st->winStart.y = rc.top;
            st->isDragging = true;
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!st) break;
        if (st->dragSlider >= 0) {
            st->dragSlider = -1;
            ReleaseCapture();
            SaveAppearanceSettings();   // 滑块调整完毕：保存外观设置
            return 0;
        }
        if (st->isDragging) {
            st->isDragging = false;
            ReleaseCapture();
            return 0;
        }
        if (st->press) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ChooserLayout L;
            ComputeLayout(st, L);
            int b = HitButton(pt.x, pt.y, st, L);
            ReleaseCapture();
            st->press = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            if (b >= 1 && b <= 6) {
                ChooseSlotFile(hwnd, st, b);
            } else if (b == 7) {
                st->resultApplied = true;
                DestroyWindow(hwnd);
            } else if (b == 8) {
                DestroyWindow(hwnd);
            } else if (b == 9) {
                st->advancedOpen = !st->advancedOpen;
                if (!st->advancedOpen) {
                    st->chatOpen = false;
                    st->appearOpen = false;
                    LayoutEdits(st, NULL, false);
                }
                ChooserLayout L2;
                ComputeLayout(st, L2);
                ResizeTo(hwnd, L2.H);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (b == 10) {
                // 聊天设置手风琴
                st->chatOpen = !st->chatOpen;
                st->appearOpen = false;
                ChooserLayout L2;
                ComputeLayout(st, L2);
                LayoutEdits(st, &L2, st->chatOpen);
                ResizeTo(hwnd, L2.H);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (b == 11) {
                // 桌宠大小手风琴
                st->appearOpen = !st->appearOpen;
                st->chatOpen = false;
                LayoutEdits(st, NULL, false);
                ChooserLayout L2;
                ComputeLayout(st, L2);
                ResizeTo(hwnd, L2.H);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (b == 12) {
                SaveAIConfigFromEdits(hwnd, st);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }
    case WM_ADV_ESC: {
        // 输入框内按 Esc：收起聊天表单，回到对话框本身
        if (st && st->chatOpen) {
            st->chatOpen = false;
            LayoutEdits(st, NULL, false);
            ChooserLayout L;
            ComputeLayout(st, L);
            ResizeTo(hwnd, L.H);
            InvalidateRect(hwnd, NULL, FALSE);
            SetFocus(hwnd);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        if (wp == VK_RETURN && st) { st->resultApplied = true; DestroyWindow(hwnd); return 0; }
        break;
    case WM_CAPTURECHANGED:
        if (st) { st->isDragging = false; st->press = 0; st->dragSlider = -1; }
        return 0;
    case WM_DESTROY:
        DragAcceptFiles(hwnd, FALSE);
        if (st) {
            if (st->calmImg) { delete st->calmImg; st->calmImg = NULL; }
            if (st->thinkImg) { delete st->thinkImg; st->thinkImg = NULL; }
            if (st->mouthImg) { delete st->mouthImg; st->mouthImg = NULL; }
            if (st->unhappyImg) { delete st->unhappyImg; st->unhappyImg = NULL; }
            if (st->angryImg) { delete st->angryImg; st->angryImg = NULL; }
            if (st->happyImg) { delete st->happyImg; st->happyImg = NULL; }
            if (st->bmp) { delete st->bmp; st->bmp = NULL; }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

// 对外接口
int ShowChooserDialog(HINSTANCE hInst, HWND parent,
                      const std::wstring& currentCalm, const std::wstring& currentThink,
                      const std::wstring& currentMouth, const std::wstring& currentUnhappy,
                      const std::wstring& currentAngry, const std::wstring& currentHappy,
                      std::wstring& outCalm, std::wstring& outThink,
                      std::wstring& outMouth, std::wstring& outUnhappy,
                      std::wstring& outAngry, std::wstring& outHappy)
{
    ChooserState st;
    st.calmPath = currentCalm;
    st.thinkPath = currentThink;
    st.mouthPath = currentMouth;
    st.unhappyPath = currentUnhappy;
    st.angryPath = currentAngry;
    st.happyPath = currentHappy;
    auto loadImg = [](const std::wstring& p) -> Gdiplus::Image* {
        if (p.empty()) return NULL;
        Gdiplus::Image* im = G::Image::FromFile(p.c_str());
        if (im && im->GetLastStatus() != G::Ok) { delete im; return NULL; }
        return im;
    };
    st.calmImg = loadImg(currentCalm);
    st.thinkImg = loadImg(currentThink);
    st.mouthImg = loadImg(currentMouth);
    st.unhappyImg = loadImg(currentUnhappy);
    st.angryImg = loadImg(currentAngry);
    st.happyImg = loadImg(currentHappy);

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ChooserProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"PetChooserDlgClass";
        RegisterClassExW(&wc);
        s_registered = true;
    }

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + ((wa.right - wa.left) - DLG_W) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - DLG_H_COLLAPSED) / 2;

    // 普通窗口（非分层）：内联设置需要 EDIT 子控件，分层窗口无法合成子控件
    HWND dlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PetChooserDlgClass", L"", WS_POPUP | WS_CLIPCHILDREN,
        x, y, DLG_W, DLG_H_COLLAPSED, parent, NULL, hInst, &st);
    if (!dlg) return 0;
    SetWindowRgn(dlg, CreateRoundRectRgn(0, 0, DLG_W + 1, DLG_H_COLLAPSED + 1, 24, 24), TRUE);
    DragAcceptFiles(dlg, TRUE);   // 支持把图片拖到选图框上

    // 创建内联 AI 表单输入框（初始隐藏，展开聊天设置时显示）
    AIConfig cfg = LoadAIConfigShared();
    for (int i = 0; i < 5; i++) {
        DWORD style = WS_CHILD | WS_TABSTOP;
        style |= (i == 4) ? (ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL)
                          : ES_AUTOHSCROLL;
        st.edits[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style,
            0, 0, 10, 10, dlg, NULL, hInst, NULL);
        SendMessageW(st.edits[i], WM_SETFONT, (WPARAM)GetUIFontShared(), TRUE);
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(st.edits[i], GWLP_WNDPROC,
                                                  (LONG_PTR)ChooserEditProc);
        if (!g_chooserEditProc) g_chooserEditProc = prev;
        ShowWindow(st.edits[i], SW_HIDE);
    }
    SetWindowTextW(st.edits[0], cfg.apiUrl.c_str());
    SetWindowTextW(st.edits[1], cfg.apiKey.c_str());
    SetWindowTextW(st.edits[2], cfg.model.c_str());
    SetWindowTextW(st.edits[3], std::to_wstring(cfg.memoryKB).c_str());
    SetWindowTextW(st.edits[4], cfg.prompt.c_str());

    SetPetFrozen(true);   // 选图期间桌宠停止走动
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

    outCalm = st.calmPath;
    outThink = st.thinkPath;
    outMouth = st.mouthPath;
    outUnhappy = st.unhappyPath;
    outAngry = st.angryPath;
    outHappy = st.happyPath;
    return st.resultApplied ? 1 : 0;
}

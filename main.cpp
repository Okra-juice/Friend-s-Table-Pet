// 桌宠主程序：透明无边框窗口 + GDI+ 绘制的柔软火柴人
// 功能：随机走动、柔软动画、右键菜单、首次运行选图对话框
#include "common.h"
#include <vector>

HINSTANCE g_hInst = NULL;
HWND g_hPetWnd = NULL;
std::wstring g_headImagePath;
Gdiplus::Image* g_headImage = NULL;   // NULL = 默认头像
std::wstring g_thinkImagePath;
Gdiplus::Image* g_thinkImage = NULL;  // 思考头像（NULL = 沿用平静头像）
std::wstring g_mouthOpenImagePath;
Gdiplus::Image* g_mouthOpenImage = NULL;  // 张嘴头像（吃东西时切换，NULL = 沿用平静头像）
std::wstring g_unhappyImagePath;
Gdiplus::Image* g_unhappyImage = NULL;    // 不开心头像（饥饿值较低时显示，NULL = 沿用平静头像）
std::wstring g_angryImagePath;
Gdiplus::Image* g_angryImage = NULL;      // 生气头像（表情状态为生气时显示，NULL = 沿用平静头像）
std::wstring g_happyImagePath;
Gdiplus::Image* g_happyImage = NULL;      // 开心头像（心情值较高/表情状态为开心时显示，NULL = 沿用平静头像）

int g_hunger = kHungerDefault;   // 当前饥饿值（0~100，随时间缓慢下降）
static float g_hungerAccum = 0.0f;
static const float kHungerDecaySec = 120.0f;   // 每 2 分钟自动下降 1 点

int g_mood = kMoodDefault;   // 当前心情值（0~100，通过喂食 / AI 回复中括号增减）
int g_moodFace = kMoodFaceCalm;   // 当前表情状态（0 平静 1 生气 2 不开心，由 AI 回复决定）

// 交互冻结：右键/聊天/思考时桌宠停止走动
static int  g_freezeCount = 0;   // 冻结计数（多个来源叠加）
static bool g_petFrozen   = false;
static int  g_petPose     = 0;   // 0 正常 1 思考(手撑头) 2 庆祝(双手前伸浮动)
static bool g_dialogOpen  = false;   // 模态设置窗口（AI 设置 / 选图）是否打开
static int  g_dialogOpenCount = 0;   // 支持嵌套（菜单/输入条/设置 可叠加）

// 计数式：多个来源同时暂停逐帧重绘，全部关闭后才恢复
void SetDialogOpen(bool open)
{
    g_dialogOpenCount += open ? 1 : -1;
    if (g_dialogOpenCount < 0) g_dialogOpenCount = 0;
    g_dialogOpen = g_dialogOpenCount > 0;
}

// 桌宠尺寸（基础值，实际窗口随外观设置缩放）
const int HEAD = 64;        // 头部尺寸
const int WIN_W = 140;      // 窗口宽（基础值）
const int WIN_H = 224;      // 窗口高（基础值）
const float GROUND = 210.0f;// 地面（基础值，窗口内 Y 坐标）

// 外观设置（实时生效）
float g_petScale      = 1.0f;   // 整体大小倍率（0.8~1.4，含头部）
float g_bodyScale     = 1.0f;   // 身体大小倍率（0.8~1.4，头部保持原尺寸）
float g_bodyThickness = 1.0f;   // 身体粗细倍率（0.6~1.6，四肢笔宽）
int   g_winW = WIN_W;           // 当前窗口宽（随外观设置动态变化）
int   g_winH = WIN_H;           // 当前窗口高

// 宠物状态
struct PetState {
    float posX = 0, posY = 0;   // 窗口左上角位置
    float animT = 0;            // 累计时间（呼吸等）
    float dragT = 0;            // 拎起累计时间（挣扎动画）
    float walkPh = 0;           // 行走相位
    float moveBlend = 0;        // 0..1 行走/静止 平滑过渡
    int   dir = 1;              // 朝向：1 右，-1 左
    int   mode = 0;             // 0=发呆 1=走路
    float modeTimer = 2.0f;
    float speed = 55.0f;        // 目标速度
    float curSpeed = 0;         // 当前实际速度（加速/减速，更自然的物理感）
    float vy = 0;               // 竖直速度（自由落体）
    float vx = 0;               // 水平速度（被甩出去）
    bool  dragging = false;     // 是否被鼠标拎起
    bool  airborne = false;     // 是否悬在空中
    float squash = 0;           // 落地压缩 1→0 衰减
    int   pose = 0;             // 0 正常 1 思考(手撑头) 2 庆祝(双手前伸浮动)
    int   lastPose = -1;        // 上一帧姿势（用于姿势切换时重置动画计时）
    float poseT = 0;            // 姿势动画计时
    bool  opening = false;      // 开场动画：启动时从屏幕上方落下
};

static Gdiplus::Bitmap* g_frameBmp = NULL;   // 渲染缓冲（也用于点击判定）

// 吃东西动画状态
static const float kEatingBiteDur = 0.75f;   // 每口的时长（秒），一共 4 口
static const float kEatingShrinkAt = 0.5f;   // 食物缩小发生的时间点（占每口周期的比例，晚于嘴闭，保证"先张嘴咬、再变小"）
static bool        g_eating = false;         // 是否正在吃东西
static int         g_eatingBites = 0;        // 已经吃完的口数（0~4）
static float       g_eatingT = 0.0f;         // 当前一口的计时
static std::wstring g_eatingFilePath;        // 正在吃的文件路径
static ULONGLONG   g_eatingFileSize = 0;     // 正在吃的文件大小（字节）
static int         g_eatingGain = 0;         // 吃完后回复的饥饿值

// 定义在文件后部（配置区），供窗口过程使用
void StartEating(const std::wstring& path);
static void FinishEating();

static RECT MonitorWorkArea(int cx, int cy)
{
    RECT rc = { 0, 0, 0, 0 };
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    HMONITOR hm = MonitorFromPoint(POINT{ cx, cy }, MONITOR_DEFAULTTONEAREST);
    if (hm && GetMonitorInfoW(hm, &mi)) rc = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
    return rc;
}

// 头部绘制（图片或默认卡通脸）；angleDeg != 0 时绕头部中心旋转（趴下时头横着放）
static void DrawHead(Gdiplus::Graphics& g, float cx, float cy, float size,
                     Gdiplus::Image* img = NULL, float angleDeg = 0.0f)
{
    using namespace G;
    float r = size * 0.5f;
    GraphicsPath clip;
    AddRoundRectPath(clip, cx - r, cy - r, size, size, size * 0.24f);

    g.SetClip(&clip);
    if (angleDeg != 0.0f) {
        g.TranslateTransform(cx, cy);
        g.RotateTransform(angleDeg);
        g.TranslateTransform(-cx, -cy);
    }
    if (img) {
        float iw = (float)img->GetWidth();
        float ih = (float)img->GetHeight();
        float s = std::min(iw, ih);
        float sx = (iw - s) / 2.0f, sy = (ih - s) / 2.0f;
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(img, RectF(cx - r, cy - r, size, size), sx, sy, s, s, UnitPixel);
    } else {
        // 默认卡通脸
        LinearGradientBrush bg(RectF(cx - r, cy - r, size, size),
            Color(255, 255, 214, 168), Color(255, 255, 186, 120), LinearGradientModeVertical);
        g.FillPath(&bg, &clip);
        SolidBrush eye(Color(255, 70, 55, 70));
        float ew = size * 0.13f, eh = size * 0.18f;
        float ey = cy - size * 0.04f;
        g.FillEllipse(&eye, cx - size * 0.30f, ey, ew, eh);
        g.FillEllipse(&eye, cx + size * 0.17f, ey, ew, eh);
        Pen mp(Color(255, 130, 85, 60), std::max(2.0f, size * 0.035f));
        mp.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawArc(&mp, cx - size * 0.17f, cy + size * 0.10f, size * 0.34f, size * 0.22f, 20, 140);
        SolidBrush blush(Color(110, 255, 150, 140));
        g.FillEllipse(&blush, cx - size * 0.44f, cy + size * 0.02f, size * 0.17f, size * 0.10f);
        g.FillEllipse(&blush, cx + size * 0.27f, cy + size * 0.02f, size * 0.17f, size * 0.10f);
    }
    if (angleDeg != 0.0f) {
        g.TranslateTransform(cx, cy);
        g.RotateTransform(-angleDeg);
        g.TranslateTransform(-cx, -cy);
    }
    g.ResetClip();
}

// 两段式手臂 IK：让手到达目标点（角度约定：x=sin(a)*L, y=cos(a)*L，a=0 垂直向下）
static void ArmIK(float sx, float sy, float tx, float ty,
                  float L1, float L2, float bendDir,
                  float& a1, float& af)
{
    float dx = tx - sx, dy = ty - sy;
    float d = std::sqrt(dx * dx + dy * dy);
    float maxd = (L1 + L2) * 0.98f;
    if (d > maxd) d = maxd;
    if (d < 1.0f) d = 1.0f;
    float baseA = std::atan2(dx, dy);
    float cb = (L1 * L1 + d * d - L2 * L2) / (2.0f * L1 * d);
    if (cb > 1.0f) cb = 1.0f;
    if (cb < -1.0f) cb = -1.0f;
    float B = std::acos(cb);
    a1 = baseA + bendDir * B;                    // 上臂
    float ex = sx + std::sin(a1) * L1;
    float ey = sy + std::cos(a1) * L1;
    af = std::atan2(tx - ex, ty - ey);           // 前臂直接指向手部目标
}

// 每帧渲染
static void RenderFrame(PetState& st)
{
    using namespace G;
    if (!g_frameBmp) g_frameBmp = new Bitmap(g_winW, g_winH, PixelFormat32bppPARGB);
    Graphics g(g_frameBmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0, 0, 0, 0));

    // 地面位置：窗口底部留出 14×整体大小 的边距
    float groundY = (float)g_winH - 14.0f * g_petScale;

    // 地面阴影（悬空/被拎起时阴影消失）
    if (!st.dragging && !st.airborne) {
        SolidBrush shBrush(Color(70, 0, 0, 0));
        g.FillEllipse(&shBrush,
            (float)(g_winW / 2) - 34.0f * g_petScale, groundY - 5.0f * g_petScale,
            68.0f * g_petScale, 11.0f * g_petScale);
    }

    // 原点移到地面中心；朝左时镜像（镜像作用于局部坐标，中心不变）
    g.TranslateTransform((float)(g_winW / 2), groundY);
    if (st.dir < 0) g.ScaleTransform(-1, 1);
    g.ScaleTransform(g_petScale, g_petScale);            // 整体大小（含头部）
    // 落地压缩：落地瞬间压扁，再回弹（以脚底为轴）
    if (st.squash > 0.01f) {
        g.ScaleTransform(1.0f + 0.10f * st.squash, 1.0f - 0.15f * st.squash);
    }
    g.ScaleTransform(g_bodyScale, g_bodyScale);          // 身体大小（头部保持原尺寸）

    bool moving = (st.mode == 1);
    bool hanging = st.dragging || st.airborne;   // 被拎或飞行中都用悬垂姿态
    bool thinking  = (st.pose == 1);
    bool celebrate = (st.pose == 2);
    bool eating    = (st.pose == 3);             // 吃东西（双手捧着食物）
    bool lying     = (st.pose == 4);             // 饥饿清零：趴在地上
    bool mouthOpen = eating && g_eatingBites < 4 && (g_eatingT < kEatingBiteDur * 0.45f);  // 张口吞
    // 吃太饱（饥饿值>85）：驼背弓身、头低垂（正常姿态下生效，特殊姿态不叠加）
    bool overfed = (g_hunger > 85) && !lying && !hanging && !thinking && !celebrate && !eating;
    float hunch = overfed ? 1.0f : 0.0f;

    // 趴在地上（饥饿值 ≤15）：身体水平躺倒，头横着枕在地上
    if (lying) {
        const float breath = 1.0f + 0.02f * std::sin(st.animT * 2.5f);   // 呼吸起伏
        const float GY = -5.0f * breath;    // 身体贴地高度
        const float headS = 58.0f / g_bodyScale;   // 与站立时头部大小接近

        Pen legPen(Color(255, 64, 56, 78), 7.0f * g_bodyThickness);
        legPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        PointF hip(-52, GY);
        // 两条腿向身后伸开（微弯）
        for (int i = 0; i < 2; i++) {
            float back = 32.0f + i * 8.0f;
            float bend = (i == 0 ? 7.0f : 14.0f);
            PointF knee(hip.X - back * 0.5f, GY - bend);
            PointF foot(hip.X - back, GY - 2.0f);
            PointF pts[3] = { hip, knee, foot };
            GraphicsPath lp;
            lp.AddCurve(pts, 3, 0.7f);
            g.DrawPath(&legPen, &lp);
        }

        // 躯干：从髋到肩 水平横躺
        PointF shoulder(hip.X + 66.0f, GY - 2.0f);
        PointF tp[4] = { hip, PointF(hip.X + 22, GY - 4),
                         PointF(hip.X + 44, GY - 5), shoulder };
        GraphicsPath torso;
        torso.AddCurve(tp, 4, 0.5f);
        Pen torsoPen(Color(255, 64, 56, 78), 8.0f * g_bodyThickness);
        torsoPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawPath(&torsoPen, &torso);

        // 手臂：后手垂在身侧，前手伸向头部方向
        Pen armPen(Color(255, 64, 56, 78), 6.5f * g_bodyThickness);
        armPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        for (int i = 0; i < 2; i++) {
            PointF hand = (i == 0) ? PointF(shoulder.X + 26.0f, GY - 5.0f)
                                   : PointF(shoulder.X - 18.0f, GY + 2.0f);
            PointF pts[3] = { shoulder, PointF((shoulder.X + hand.X) / 2, GY - 6.0f), hand };
            GraphicsPath ap;
            ap.AddCurve(pts, 3, 0.7f);
            g.DrawPath(&armPen, &ap);
        }

        // 头横着枕在地面上（绕中心转 90°）
        float headCX = shoulder.X + 38.0f;
        float headCY = -26.0f + (1.0f - breath) * 2.0f;
        Gdiplus::Image* hImg = (g_hunger <= kHungerLow && g_unhappyImage) ? g_unhappyImage
                                                                          : g_headImage;
        DrawHead(g, headCX, headCY, headS, hImg, 90.0f);
        g.ResetTransform();
        return;
    }

    float sw  = moving ? std::sin(st.walkPh) : 0.0f;
    float sw2 = moving ? std::sin(st.walkPh + 3.14159265f) : 0.0f;
    float bob = st.moveBlend * (std::fabs(sw) * 4.0f);          // 上身弹跳
    if (celebrate) bob += std::fabs(std::sin(st.poseT * 5.0f)) * 8.0f;  // 庆祝上下浮动
    float waveAmp = 4.5f * st.moveBlend;                        // 躯干波浪幅度
    // 被拎起时：轻柔摆荡（像被提着晃），飞行时：随速度倾斜
    float hangSway = st.dragging ? std::sin(st.dragT * 2.5f) : 0.0f;
    float flyTilt  = st.airborne ? std::tanh(st.vx * 0.0025f) * 0.20f : 0.0f;

    const float HIP_Y = -76.0f;
    PointF hip(0, HIP_Y);

    // 腿：站立打直/走路迈步/拎起时被动垂挂（微弯、轻晃）
    Pen legPen(Color(255, 64, 56, 78), 7.0f * g_bodyThickness);
    legPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    for (int i = 0; i < 2; i++) {
        float a, af, kneeBend = 0.05f, lift = 0.0f;
        if (hanging) {
            // 被拎起：两腿完全垂直下垂、几乎打直、贴身
            float side = (i == 0 ? 0.02f : -0.02f);
            a = side + hangSway * 0.08f;
            kneeBend = 0.02f;     // 几乎打直
            af = a * 0.6f + kneeBend;
        } else {
            float spreadF = 1.0f - st.moveBlend;
            float legSpread = (i == 0 ? 0.15f : -0.15f) * spreadF;
            float swing = (i == 0 ? sw : sw2) * 0.45f;
            a = legSpread + swing;
            kneeBend = 0.05f + st.moveBlend * 0.35f;
            af = a * 0.55f + kneeBend;
            lift = st.moveBlend * std::max(0.0f, std::sin(a)) * 6.0f;
        }
        float L1 = 38, L2 = 38;
        PointF knee(hip.X + std::sin(a) * L1, hip.Y + std::cos(a) * L1);
        PointF foot(knee.X + std::sin(af) * L2, knee.Y + std::cos(af) * L2 - lift);
        PointF pts[3] = { hip, knee, foot };
        GraphicsPath lp;
        lp.AddCurve(pts, 3, 0.7f);
        g.DrawPath(&legPen, &lp);
    }

    // 躯干：波浪柔软弯曲；拎起时身体被拉长下垂、轻晃；吃太饱时驼背弓身
    float shoulderY = -138.0f - bob - (hanging ? 7.0f : 0.0f) - hunch * 6.0f;  // 驼背：肩部下压
    float shoulderX = sw * 2.2f * st.moveBlend +
                      (hanging ? (hangSway * 3.0f + flyTilt * 12.0f) : 0.0f) + hunch * 5.0f;
    const int N = 10;
    PointF tp[N];
    for (int i = 0; i < N; i++) {
        float u = (float)i / (N - 1);
        float wx = std::sin(st.walkPh * 1.25f - u * 3.4f) * waveAmp;  // 波从髋传到肩
        tp[i] = PointF(shoulderX * u + wx, HIP_Y + (shoulderY - HIP_Y) * u);
    }
    GraphicsPath torso;
    torso.AddCurve(tp, N, 0.5f);
    Pen torsoPen(Color(255, 64, 56, 78), 8.0f * g_bodyThickness);
    torsoPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    g.DrawPath(&torsoPen, &torso);

    // 手臂：正常摆动 / 思考手撑头 / 庆祝双手前伸浮动 / 拎起下垂
    Pen armPen(Color(255, 64, 56, 78), 6.5f * g_bodyThickness);
    armPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    float idleWave = 0.12f * std::sin(st.animT * 2.0f);
    float headCX = shoulderX + std::sin(st.walkPh * 1.25f - 3.4f) * waveAmp;
    PointF shoulder(headCX, shoulderY);
    // 头位置提前计算（思考姿势的手臂需要下巴位置）
    float headCY = -166.0f - bob - (hanging ? 2.0f : 0.0f) - hunch * 8.0f;   // 驼背：头低垂
    float headCXf = headCX + (hanging ? (st.dir > 0 ? 6.0f : -6.0f) : 0.0f) + hunch * 3.0f;  // 头略前探
    float eatHandX = 0, eatHandY = 0;     // 前手位置
    float eatHand2X = 0, eatHand2Y = 0;   // 后手位置（双手捧着食物）
    for (int i = 0; i < 2; i++) {
        float a1, af;
        if (hanging) {
            // 被拎起：手臂完全垂直下垂、贴身（前臂也伸直向下）
            float side = (i == 0 ? 0.03f : -0.03f);
            a1 = side + hangSway * 0.06f;
            af = hangSway * 0.06f;   // 前臂也垂直，不弯回
        } else if (eating) {
            // 双手捧着食物举到嘴边：两只手用同一个 IK 函数，
            // 目标点绕肩部各转 7.5°，两只手正好岔开 15°
            const float kHalfSpread = 7.5f * 3.14159265f / 180.0f;
            float tx = headCXf + 10.0f, ty = headCY + 30.0f;
            float dx = tx - shoulder.X, dy = ty - shoulder.Y;
            float c = std::cos(kHalfSpread), s = std::sin(kHalfSpread);
            float nx, ny;
            if (i == 0) {   // 前手：+7.5°
                nx = shoulder.X + dx * c - dy * s;
                ny = shoulder.Y + dx * s + dy * c;
            } else {        // 后手：-7.5°
                nx = shoulder.X + dx * c + dy * s;
                ny = shoulder.Y - dx * s + dy * c;
            }
            ArmIK(shoulder.X, shoulder.Y, nx, ny, 30.0f, 26.0f,
                  (i == 0 ? -1.0f : 1.0f), a1, af);
        } else if (thinking) {
            if (i == 0) {
                // 前手撑头：肘斜向下垂、小臂呈抛物线（二次函数形）弯起托住头部
                float hx = headCXf + 10.0f;
                float hy = headCY + 24.0f;
                ArmIK(shoulder.X, shoulder.Y, hx, hy, 30.0f, 26.0f, -1.0f, a1, af);
            } else {
                a1 = -0.18f; af = -0.18f;   // 后手正常向下耷拉
            }
        } else if (celebrate) {
            // 说话比画：双手前伸、彼此分开约 30°，随节奏轻微浮动
            float wave = 0.22f * std::sin(st.poseT * 5.0f);
            a1 = 2.05f + (i == 0 ? 0.26f : -0.26f) + wave;
            af = a1 * 0.92f + 0.12f;
        } else {
            float spreadF = 1.0f - st.moveBlend;
            float armSpread = (i == 0 ? 0.28f : -0.28f) * spreadF;
            float swing = moving ? (i == 0 ? sw2 : sw) * 0.5f
                                 : idleWave * (i == 0 ? 1.0f : -1.0f);
            a1 = armSpread + swing;
            float elbowBend = 0.10f + st.moveBlend * 0.45f;
            af = a1 * 0.5f + elbowBend +
                 0.15f * std::sin(st.walkPh * 1.1f + i) * st.moveBlend;
        }
        float L1 = 30, L2 = 26;
        PointF elbow(shoulder.X + std::sin(a1) * L1, shoulder.Y + std::cos(a1) * L1);
        PointF hand(elbow.X + std::sin(af) * L2, elbow.Y + std::cos(af) * L2);
        if (eating && i == 0) { eatHandX = hand.X; eatHandY = hand.Y; }
        if (eating && i == 1) { eatHand2X = hand.X; eatHand2Y = hand.Y; }
        PointF pts[3] = { shoulder, elbow, hand };
        GraphicsPath ap;
        ap.AddCurve(pts, 3, 0.7f);
        g.DrawPath(&armPen, &ap);
    }

    // 头：张嘴[吃东西] > 思考 > 不开心[饥饿值低] > 生气/不开心[AI 表情] > 开心[心情值高] > 平静
    // 头在"身体缩放"变换内绘制：位置随身体缩放，绘制尺寸反向补偿以保持头部原大小
    bool unhappyLow = !eating && (g_hunger <= kHungerLow);
    // 心情值较高（≥70）且当前没有负面情绪时，自动倾向显示开心头像（心情好就多笑笑）
    bool moodHigh = !unhappyLow && (g_mood >= kMoodHigh) &&
                    (g_moodFace == kMoodFaceCalm || g_moodFace == kMoodFaceHappy);
    Gdiplus::Image* headImg = g_headImage;
    if (mouthOpen && g_mouthOpenImage) headImg = g_mouthOpenImage;
    else if (thinking && g_thinkImage) headImg = g_thinkImage;               // 思考优先
    else if (unhappyLow && g_unhappyImage) headImg = g_unhappyImage;         // 饿了强制不开心
    else if (g_moodFace == kMoodFaceAngry && g_angryImage) headImg = g_angryImage;
    else if (g_moodFace == kMoodFaceUnhappy && g_unhappyImage) headImg = g_unhappyImage;
    else if (g_moodFace == kMoodFaceHappy && g_happyImage) headImg = g_happyImage;
    else if (moodHigh && g_happyImage) headImg = g_happyImage;
    DrawHead(g, headCXf, headCY, HEAD / g_bodyScale, headImg);

    // 手里的食物（画在头的图层前面；双手捧着，每吃一口缩小，4 口吃完消失）
    if (eating && g_eatingBites < 4) {
        // 顺序调整：每口先张嘴→闭嘴咬合，之后食物才缩小一档（1.0→0.75→0.5→0.25→0）。
        // 原来食物在每口开头就变小，与嘴张开重叠；现在改到每口后半段（嘴已闭合）再缩小。
        float eatProg = g_eatingT / kEatingBiteDur;
        int   steps   = g_eatingBites + (eatProg >= kEatingShrinkAt ? 1 : 0);
        float fileScale = 1.0f - steps * 0.25f;
        if (fileScale < 0.0f) fileScale = 0.0f;
        float bob = std::sin(st.animT * 9.0f) * 1.5f;      // 轻轻晃动
        // 食物放在双手之间的中点，张嘴时再往嘴边送一点
        float hx = (eatHandX + eatHand2X) / 2.0f + (mouthOpen ? 5.0f : 0.0f);
        float hy = (eatHandY + eatHand2Y) / 2.0f - 8.0f + bob;
        float s  = 26.0f * fileScale;
        GraphicsPath doc;
        AddRoundRectPath(doc, hx - s / 2, hy - s * 0.42f, s, s * 0.84f, 2.5f);
        SolidBrush docB(Color(255, 255, 255, 255));
        g.FillPath(&docB, &doc);
        Pen docP(Color(255, 110, 120, 140), 1.4f);
        g.DrawPath(&docP, &doc);
        // 折角
        GraphicsPath fold;
        float fx = hx + s / 2, fy = hy - s * 0.42f;
        fold.AddLine(fx - s * 0.22f, fy, fx, fy + s * 0.22f);
        fold.AddLine(fx, fy + s * 0.22f, fx - s * 0.22f, fy + s * 0.22f);
        fold.CloseFigure();
        g.FillPath(&SolidBrush(Color(255, 220, 228, 240)), &fold);
        // 两行"文字"
        Pen lineP(Color(255, 176, 186, 206), 1.2f);
        for (int li = 0; li < 2; li++) {
            float ly = hy - s * 0.42f + 6.0f + li * 5.0f;
            g.DrawLine(&lineP, hx - s * 0.28f, ly, hx + s * 0.24f, ly);
        }
    }

    g.ResetTransform();
}

// 物理引擎：自由落体 / 惯性 / 碰撞 / 落地（任何状态下都生效）
static void UpdatePetPhysics(PetState& st, float dt)
{
    RECT wa = MonitorWorkArea((int)(st.posX + g_winW / 2), (int)(st.posY + g_winH / 2));
    float ground = (float)(wa.bottom - g_winH);
    float L = (float)wa.left, R = (float)(wa.right - g_winW);

    // 空中：自由落体 + 水平惯性（甩出后走抛物线）
    if (st.posY < ground - 0.5f) {
        st.airborne = true;
        st.vy += 2600.0f * dt;                   // 重力加速度
        if (st.vy > 2400.0f) st.vy = 2400.0f;   // 终端速度
        st.posY += st.vy * dt;
        // 水平移动 + 空气阻力衰减（衰减很弱，惯性飞得远）
        st.posX += st.vx * dt;
        st.vx *= std::pow(0.985f, dt);
        // 顶部边界：向上甩不会飞出屏幕（反弹并损失能量）
        // 开场动画期间允许从屏幕上方落下，不限制顶部
        float T = (float)wa.top;
        if (st.posY < T && !st.opening) {
            st.posY = T;
            if (st.vy < 0) st.vy = -st.vy * 0.35f;
        }
        // 撞墙弹回（带能量损失）
        if (st.posX < L) { st.posX = L; st.vx = -st.vx * 0.55f; st.dir = 1; }
        else if (st.posX > R) { st.posX = R; st.vx = -st.vx * 0.55f; st.dir = -1; }
        if (st.posY >= ground) {
            st.posY = ground;
            st.vy = 0.0f;
            st.vx *= 0.6f;                       // 落地保留部分水平速度，会滑行一段
            st.airborne = false;
            st.squash = 1.0f;                    // 触发落地压缩动画
            st.opening = false;                  // 开场落地，动画结束
        }
    } else {
        st.airborne = false;
        st.vy = 0.0f;
        st.vx = 0.0f;
    }

    // 地面滑行：残余 vx 继续推动并衰减，直到很小才停
    if (st.posY >= ground - 0.5f && std::fabs(st.vx) > 5.0f) {
        st.posX += st.vx * dt;
        st.vx *= std::pow(0.06f, dt);            // 地面摩擦，快速衰减但能滑一小段
        if (st.vx > 0) st.dir = 1; else st.dir = -1;
    }

    // 限制在当前显示器工作区内，到边停住
    if (st.posX < L) { st.posX = L; st.vx = 0.0f; st.dir = 1; }
    else if (st.posX > R) { st.posX = R; st.vx = 0.0f; st.dir = -1; }

    // 落地压缩衰减（回弹）
    st.squash += (0.0f - st.squash) * std::min(1.0f, dt * 7.0f);
}

// 状态更新（随机行为 + 自主走动；物理在 UpdatePetPhysics 里）
static void UpdatePet(PetState& st, float dt)
{
    st.animT += dt;
    st.modeTimer -= dt;
    if (st.modeTimer <= 0) {
        if (st.mode == 0) {                      // 发呆 -> 开始走
            st.mode = 1;
            st.dir = (rand() % 2) ? 1 : -1;
            st.speed = 48.0f + (rand() % 100) * 0.25f;
            st.modeTimer = 2.0f + (rand() % 40) * 0.08f;
        } else {                                  // 走 -> 停下来发呆
            st.mode = 0;
            st.modeTimer = 1.0f + (rand() % 30) * 0.08f;
        }
    }

    UpdatePetPhysics(st, dt);

    RECT wa = MonitorWorkArea((int)(st.posX + g_winW / 2), (int)(st.posY + g_winH / 2));
    float ground = (float)(wa.bottom - g_winH);

    // 地面行为：走路带加减速（起步/刹车的惯性感）
    if (st.posY >= ground - 0.5f) {
        float target = (st.mode == 1) ? 1.0f : 0.0f;
        st.moveBlend += (target - st.moveBlend) * std::min(1.0f, dt * 3.5f);

        // 落地滑行已由物理引擎处理；这里只处理正常的自主走动
        if (std::fabs(st.vx) <= 5.0f) {
            st.vx = 0.0f;
            // 吃太饱（饥饿值>85）：走不动路，目标速度降低
            float speedFactor = (g_hunger > 85) ? 0.55f : 1.0f;
            float targetSpd = (st.mode == 1) ? st.speed * speedFactor : 0.0f;
            st.curSpeed += (targetSpd - st.curSpeed) * std::min(1.0f, dt * 4.0f);
            st.posX += st.dir * st.curSpeed * dt;
        }
        st.walkPh += dt * (st.mode == 1 ? 9.0f : 0.9f);
    } else {
        // 空中：四肢收起，保持迈步相位但不水平移动
        st.moveBlend += (0.0f - st.moveBlend) * std::min(1.0f, dt * 3.0f);
        st.curSpeed = 0.0f;
        st.walkPh += dt * 0.9f;
    }
}

// 窗口过程
static LRESULT CALLBACK PetWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static PetState st;
    static ULONGLONG lastTick = 0;
    static bool  g_dragging = false;   // 是否被鼠标"拎起"
    static POINT g_dragOff = { 0, 0 }; // 按下点 - 窗口左上角 的偏移
    // 拖拽轨迹采样（松手时沿鼠标移动轨迹飞出，形成抛物线）
    static std::vector<POINT>     g_hist;    // 采样位置（屏幕坐标）
    static std::vector<ULONGLONG> g_histT;   // 采样时间
    static float g_lastVX = 0, g_lastVY = 0; // 最后一次有效移动速度（松手前减速也沿此方向飞）

    switch (msg) {
    case WM_CREATE: {
        RECT wa = MonitorWorkArea(GetSystemMetrics(SM_CXSCREEN) / 2,
                                  GetSystemMetrics(SM_CYSCREEN) / 2);
        int range = (wa.right - wa.left) - g_winW;
        st.posX = (float)(wa.left + (range > 0 ? rand() % range : 0));
        if (g_hunger > kHungerLie) {
            // 开场动画：从地面以上一小段高度落下（离地高度 = 一个窗口高度）
            st.posY = (float)(wa.bottom - g_winH) - g_winH;
            st.airborne = true;
            st.vy = 0.0f;
            st.opening = true;
        } else {
            st.posY = (float)(wa.bottom - g_winH);
        }
        lastTick = GetTickCount64();
        SetTimer(hwnd, 1, 33, NULL);   // 约 30 FPS
        return 0;
    }
    case WM_TIMER: {
        ULONGLONG now = GetTickCount64();
        float dt = (float)(now - lastTick) / 1000.0f;
        lastTick = now;
        if (dt > 0.1f) dt = 0.1f;

        // 饥饿值随时间缓慢下降（无论是否冻结都持续）
        UpdateHunger(dt);

        // 饥饿值过低（≤15）：趴在地上、不再移动
        // （吃东西期间除外，吃完恢复后自然会站起来）
        if (g_hunger <= kHungerLie && !g_eating) SetPetPose(4);

        // 吃东西动画推进：每口 0.75 秒，共 4 口，吃完后文件进回收站并回复饥饿值
        if (g_eating) {
            g_eatingT += dt;
            if (g_eatingT >= kEatingBiteDur) {
                g_eatingT = 0.0f;
                g_eatingBites++;
                if (g_eatingBites >= 4) {
                    FinishEating();
                }
            }
        }

        // 模态设置窗口打开期间暂停重绘，防止 30fps 的 UpdateLayeredWindow
        // 与顶层设置窗口互相干扰导致窗口闪烁
        if (g_dialogOpen) return 0;

        st.pose = g_petPose;
        // 姿势切换时重置动画计时（思考抬手的 180° 弧线动画从 0 开始）
        if (st.pose != st.lastPose) { st.poseT = 0; st.lastPose = st.pose; }
        if (g_dragging) {
            // 被拎起时：只保留呼吸与挣扎动画，停止自主走动
            st.animT += dt;
            st.dragT += dt;
            if (st.pose) st.poseT += dt;
            st.moveBlend += (0.0f - st.moveBlend) * std::min(1.0f, dt * 4.0f);
            st.squash += (0.0f - st.squash) * std::min(1.0f, dt * 7.0f);
        } else {
            st.animT += dt;
            if (st.pose) st.poseT += dt; else st.poseT = 0;
            if (g_petFrozen || g_hunger <= kHungerLie) {
                // 冻结或饥饿值过低：停止自主走动，但物理（重力/惯性/落地）照常生效
                st.moveBlend += (0.0f - st.moveBlend) * std::min(1.0f, dt * 4.0f);
                st.mode = 0;
                UpdatePetPhysics(st, dt);
            } else {
                UpdatePet(st, dt);
            }
        }
        // 外观设置/趴下状态变化时同步窗口尺寸：保持脚底（地面）屏幕位置不变
        bool lyingNow = (g_hunger <= kHungerLie && !g_eating);
        int nw = (int)std::lround((WIN_W + 60.0f * (g_bodyScale - 1.0f)) * g_petScale);
        int nh = (int)std::lround((WIN_H + 166.0f * (g_bodyScale - 1.0f)) * g_petScale);
        if (lyingNow)   // 趴下时加宽窗口，容纳横躺的身体和头
            nw = (int)std::lround((200.0f + 200.0f * (g_bodyScale - 1.0f)) * g_petScale);
        if (nw < 80) nw = 80;
        if (nh < 120) nh = 120;
        if (nw != g_winW || nh != g_winH) {
            int oldW = g_winW, oldH = g_winH;
            g_winW = nw;
            g_winH = nh;
            if (g_frameBmp) { delete g_frameBmp; g_frameBmp = NULL; }
            st.posY += (float)(oldH - g_winH);   // 窗口变高则上移，保持脚底贴地
            st.posX += (float)(oldW - g_winW) / 2.0f;   // 宽度变化时保持窗口中心不变
            RECT wa = MonitorWorkArea((int)(st.posX + g_winW / 2),
                                      (int)(st.posY + g_winH / 2));
            st.posX = std::max((float)wa.left,
                std::min(st.posX, (float)(wa.right - g_winW)));
            st.posY = std::max((float)wa.top,
                std::min(st.posY, (float)(wa.bottom - g_winH)));
            SetWindowPos(hwnd, NULL, (int)st.posX, (int)st.posY, g_winW, g_winH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }

        RenderFrame(st);
        POINT pt = { (int)st.posX, (int)st.posY };
        PresentLayered(hwnd, g_winW, g_winH, g_frameBmp, &pt);
        return 0;
    }
    case WM_NCHITTEST: {
        // 透明区域不响应鼠标，让点击"穿透"到桌面
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(hwnd, &pt);
        if (pt.x >= 0 && pt.y >= 0 && pt.x < g_winW && pt.y < g_winH) {
            if (g_frameBmp) {
                G::Color c;
                g_frameBmp->GetPixel(pt.x, pt.y, &c);
                if (c.GetAlpha() < 40) return HTTRANSPARENT;
            }
            return HTCLIENT;
        }
        return HTTRANSPARENT;
    }
    case WM_LBUTTONDOWN: {
        // 长按拎起：记录按下点相对窗口左上角的偏移
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(hwnd, &pt);
        g_dragOff.x = pt.x - (int)st.posX;
        g_dragOff.y = pt.y - (int)st.posY;
        g_dragging = true;
        st.dragging = true;
        st.dragT = 0.0f;
        st.opening = false;   // 开场下落中被拎起：结束开场动画，交给拖拽/物理处理
        // 初始化轨迹采样与速度追踪
        g_hist.clear();
        g_histT.clear();
        g_hist.push_back(pt);
        g_histT.push_back(GetTickCount64());
        g_lastVX = 0; g_lastVY = 0;
        SetCapture(hwnd);
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return 0;
    }
    case WM_MOUSEMOVE: {
        // 拎起状态下跟随鼠标移动
        if (g_dragging && (wp & MK_LBUTTON)) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(hwnd, &pt);
            ULONGLONG now = GetTickCount64();
            // 记录拖拽轨迹采样（最多 40 个点，约 1.3 秒）
            g_hist.push_back(pt);
            g_histT.push_back(now);
            if (g_hist.size() > 40) {
                g_hist.erase(g_hist.begin());
                g_histT.erase(g_histT.begin());
            }
            // 记住最后一次"有明确方向"的移动（松手前减速/停住时仍沿该方向飞）
            int hn = (int)g_hist.size();
            if (hn >= 2) {
                float dx = (float)(g_hist[hn-1].x - g_hist[hn-2].x);
                float dy = (float)(g_hist[hn-1].y - g_hist[hn-2].y);
                if (dx * dx + dy * dy > 9.0f) {   // 一次移动超过 3px 才算有效
                    float ddt = (float)(g_histT[hn-1] - g_histT[hn-2]) / 1000.0f;
                    if (ddt > 0.001f) {
                        g_lastVX = dx / ddt;
                        g_lastVY = dy / ddt;
                    }
                }
            }
            st.posX = (float)(pt.x - g_dragOff.x);
            st.posY = (float)(pt.y - g_dragOff.y);
            // 限制在当前显示器工作区内，防止拖出屏幕
            RECT wa = MonitorWorkArea((int)st.posX + g_winW / 2, (int)st.posY + g_winH / 2);
            st.posX = std::max((float)wa.left, std::min(st.posX, (float)(wa.right - g_winW)));
            st.posY = std::max((float)wa.top, std::min(st.posY, (float)(wa.bottom - g_winH)));
            POINT p = { (int)st.posX, (int)st.posY };
            PresentLayered(hwnd, g_winW, g_winH, g_frameBmp, &p);
            SetCursor(LoadCursor(NULL, IDC_HAND));
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        // 松手：沿鼠标最后一段轨迹的方向飞出，形成抛物线
        if (g_dragging) {
            g_dragging = false;
            st.dragging = false;
            ReleaseCapture();

            // 取最后约 150ms 轨迹的平均速度（方向 = 鼠标移动轨迹方向）
            float vx = 0, vy = 0;
            int hn = (int)g_hist.size();
            if (hn >= 2) {
                ULONGLONG tLast = g_histT[hn - 1];
                int start = hn - 1;
                while (start > 0 && (tLast - g_histT[start - 1]) < 150) start--;
                float dx = (float)(g_hist[hn-1].x - g_hist[start].x);
                float dy = (float)(g_hist[hn-1].y - g_hist[start].y);
                float dtS = (float)(tLast - g_histT[start]) / 1000.0f;
                if (dtS > 0.001f) { vx = dx / dtS; vy = dy / dtS; }
            }
            float spd = std::sqrt(vx * vx + vy * vy);
            if (spd < 30.0f &&
                (std::fabs(g_lastVX) > 1.0f || std::fabs(g_lastVY) > 1.0f)) {
                // 最后一段几乎没动：改沿最后一次有效移动方向飞
                vx = g_lastVX; vy = g_lastVY;
                spd = std::sqrt(vx * vx + vy * vy);
            }
            if (spd >= 20.0f) {
                const float MIN_FLY = 420.0f;    // 最小冲量：轻轻一拖也会飞一小段
                const float MAX_FLY = 1500.0f;   // 最大冲量：防止甩飞太夸张
                float launch = spd * 2.0f;
                if (launch < MIN_FLY) launch = MIN_FLY;
                if (launch > MAX_FLY) launch = MAX_FLY;
                st.vx = vx / spd * launch;
                st.vy = vy / spd * launch;
                st.airborne = true;
                if (std::fabs(vx) > 10.0f) st.dir = (vx > 0) ? 1 : -1;
            } else {
                st.vx = 0; st.vy = 0;   // 几乎没动：原地自由落体
            }
            st.mode = 0;
            st.modeTimer = 2.5f;
            g_hist.clear();
            g_histT.clear();
            g_lastVX = 0; g_lastVY = 0;
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        // 捕获意外丢失（如切换窗口）时解除拎起状态
        g_dragging = false;
        st.dragging = false;
        g_hist.clear();
        g_histT.clear();
        g_lastVX = 0; g_lastVY = 0;
        return 0;
    case WM_DROPFILES: {
        // 用户把文件拖给桌宠：询问是否可以吃掉
        HDROP hDrop = (HDROP)wp;
        UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        if (n > 0) {
            WCHAR path[MAX_PATH * 2];
            if (DragQueryFileW(hDrop, 0, path, MAX_PATH * 2) > 0) {
                StartEating(std::wstring(path));
            }
        }
        DragFinish(hDrop);
        return 0;
    }
    case WM_CONTEXTMENU: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if ((int)pt.x == -1 && (int)pt.y == -1) GetCursorPos(&pt);
        ShowPetMenu(hwnd, pt);
        return 0;
    }
    case WM_PETMENU_CMD: {
        switch (wp) {
        case 1:   // 聊天
            if (!IsAIConfigured()) {
                int r = ShowAISettingsDialog(g_hInst, hwnd, true);
                if (r != 1 || !IsAIConfigured()) break;
            }
            OpenChatWindow(g_hInst);
            break;
        case 2:   // 外观设置
            ShowAppearanceDialog(g_hInst, hwnd);
            break;
        case 3:   // AI 设置
            ShowAISettingsDialog(g_hInst, hwnd, false);
            break;
        case 5:   // 属性（显示饥饿值）
            ShowHungerDialog(g_hInst);
            break;
        case 4:   // 退出
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        SaveHunger();   // 结束游戏时记录当前的饥饿值
        SaveMood();     // 记录当前的心情值
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 配置读写
std::wstring GetExeDir()
{
    WCHAR buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p = p.substr(0, pos);
    return p;
}

std::wstring GetConfigPath() { return GetExeDir() + L"\\pet_config.ini"; }

bool ConfigFileExists()
{
    return GetFileAttributesW(GetConfigPath().c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::string ReadAllTextUtf8(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string s;
    char buf[512];
    DWORD rd;
    while (ReadFile(h, buf, sizeof(buf), &rd, NULL) && rd > 0) s.append(buf, rd);
    CloseHandle(h);
    return s;
}

// 读取 [config] 中的 key=value（保留其他键）
static std::string ReadConfigValue(const std::string& key)
{
    std::string s = ReadAllTextUtf8(GetConfigPath());
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF &&
        (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s.erase(0, 3);                                   // 去 BOM
    std::string pat = key + "=";
    size_t pos = s.find(pat);
    if (pos == std::string::npos) return "";
    pos += pat.size();
    size_t end = s.find_first_of("\r\n", pos);
    if (end == std::string::npos) end = s.size();
    std::string v = s.substr(pos, end - pos);
    size_t a = v.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = v.find_last_not_of(" \t");
    return v.substr(a, b - a + 1);
}

// 写入 key=value（保留其他键）
static void WriteConfigValue(const std::string& key, const std::string& value)
{
    std::string s = ReadAllTextUtf8(GetConfigPath());
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF) s.erase(0, 3);
    std::string line = key + "=" + value;
    std::string pat = key + "=";
    size_t pos = s.find(pat);
    if (pos != std::string::npos) {
        size_t lineStart = s.rfind('\n', pos);
        if (lineStart == std::string::npos) lineStart = 0; else lineStart += 1;
        size_t lineEnd = s.find('\n', pos);
        if (lineEnd == std::string::npos)
            s = s.substr(0, lineStart) + line + "\r\n";
        else
            s = s.substr(0, lineStart) + line + s.substr(lineEnd);
    } else {
        if (s.empty()) s = "[config]\r\n";
        else if (s.back() != '\n') s += "\r\n";
        s += line + "\r\n";
    }
    std::string bom = "\xEF\xBB\xBF";
    HANDLE h = CreateFileW(GetConfigPath().c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr;
    WriteFile(h, bom.data(), (DWORD)bom.size(), &wr, NULL);
    WriteFile(h, s.data(), (DWORD)s.size(), &wr, NULL);
    CloseHandle(h);
}

std::wstring ReadConfigImage()      { return Utf8ToWide(ReadConfigValue("head_image")); }
std::wstring ReadConfigThinkImage() { return Utf8ToWide(ReadConfigValue("think_image")); }
void WriteConfigImage(const std::wstring& path)      { WriteConfigValue("head_image", WideToUtf8(path)); }
void WriteConfigThinkImage(const std::wstring& path) { WriteConfigValue("think_image", WideToUtf8(path)); }

// 外观设置：配置读写
static float ReadConfigFloat(const std::string& key, float def)
{
    std::string v = ReadConfigValue(key);
    if (v.empty()) return def;
    float f = (float)atof(v.c_str());
    if (f <= 0.0f) return def;
    return f;
}

static void RecomputeWinSize()
{
    g_winW = (int)std::lround((WIN_W + 60.0f * (g_bodyScale - 1.0f)) * g_petScale);
    g_winH = (int)std::lround((WIN_H + 166.0f * (g_bodyScale - 1.0f)) * g_petScale);
    if (g_winW < 80) g_winW = 80;
    if (g_winH < 120) g_winH = 120;
}

void LoadAppearanceSettings()
{
    g_bodyThickness = ReadConfigFloat("thickness", 1.0f);
    g_petScale      = ReadConfigFloat("pet_scale", 1.0f);
    g_bodyScale     = ReadConfigFloat("body_scale", 1.0f);
    // 夹在有效范围内，防止配置被手改坏
    if (g_bodyThickness < 0.6f) g_bodyThickness = 0.6f;
    if (g_bodyThickness > 1.6f) g_bodyThickness = 1.6f;
    if (g_petScale < 0.8f) g_petScale = 0.8f;
    if (g_petScale > 1.4f) g_petScale = 1.4f;
    if (g_bodyScale < 0.8f) g_bodyScale = 0.8f;
    if (g_bodyScale > 1.4f) g_bodyScale = 1.4f;
    RecomputeWinSize();
}

void SaveAppearanceSettings()
{
    char b[32];
    snprintf(b, sizeof(b), "%.2f", g_bodyThickness);
    WriteConfigValue("thickness", b);
    snprintf(b, sizeof(b), "%.2f", g_petScale);
    WriteConfigValue("pet_scale", b);
    snprintf(b, sizeof(b), "%.2f", g_bodyScale);
    WriteConfigValue("body_scale", b);
}

static void LoadHeadImage(Gdiplus::Image*& slot, std::wstring& slotPath,
                          const std::wstring& path)
{
    if (slot) { delete slot; slot = NULL; }
    slotPath = L"";
    if (path.empty()) return;
    G::Image* img = G::Image::FromFile(path.c_str());
    if (img && img->GetLastStatus() == G::Ok) {
        slot = img;
        slotPath = path;
    } else {
        if (img) delete img;
        MessageBoxW(g_hPetWnd, L"无法加载该图片，已改用默认头像。\n请换一张 PNG / JPG / BMP 图片试试。",
                    L"桌宠", MB_ICONWARNING | MB_OK);
    }
}

void ApplyHeadImage(const std::wstring& path)
{
    WriteConfigImage(path);                       // 空路径 = 使用默认头像
    LoadHeadImage(g_headImage, g_headImagePath, path);
}

void ApplyThinkImage(const std::wstring& path)
{
    WriteConfigThinkImage(path);                  // 空路径 = 思考时沿用平静头像
    LoadHeadImage(g_thinkImage, g_thinkImagePath, path);
}

std::wstring ReadConfigMouthImage() { return Utf8ToWide(ReadConfigValue("mouth_image")); }
void WriteConfigMouthImage(const std::wstring& path) { WriteConfigValue("mouth_image", WideToUtf8(path)); }

void ApplyMouthImage(const std::wstring& path)
{
    WriteConfigMouthImage(path);                  // 空路径 = 吃东西时沿用平静头像
    if (g_mouthOpenImage) { delete g_mouthOpenImage; g_mouthOpenImage = NULL; }
    g_mouthOpenImagePath = L"";
    if (path.empty()) return;
    G::Image* img = G::Image::FromFile(path.c_str());
    if (img && img->GetLastStatus() == G::Ok) {
        g_mouthOpenImage = img;
        g_mouthOpenImagePath = path;
    } else {
        if (img) delete img;
        MessageBoxW(g_hPetWnd, L"无法加载该图片，已沿用平静头像。\n请换一张 PNG / JPG / BMP 图片试试。",
                    L"桌宠", MB_ICONWARNING | MB_OK);
    }
}

std::wstring ReadConfigUnhappyImage() { return Utf8ToWide(ReadConfigValue("unhappy_image")); }
void WriteConfigUnhappyImage(const std::wstring& path) { WriteConfigValue("unhappy_image", WideToUtf8(path)); }

void ApplyUnhappyImage(const std::wstring& path)
{
    WriteConfigUnhappyImage(path);                // 空路径 = 饥饿值较低时沿用平静头像
    if (g_unhappyImage) { delete g_unhappyImage; g_unhappyImage = NULL; }
    g_unhappyImagePath = L"";
    if (path.empty()) return;
    G::Image* img = G::Image::FromFile(path.c_str());
    if (img && img->GetLastStatus() == G::Ok) {
        g_unhappyImage = img;
        g_unhappyImagePath = path;
    } else {
        if (img) delete img;
        MessageBoxW(g_hPetWnd, L"无法加载该图片，已沿用平静头像。\n请换一张 PNG / JPG / BMP 图片试试。",
                    L"桌宠", MB_ICONWARNING | MB_OK);
    }
}

std::wstring ReadConfigAngryImage() { return Utf8ToWide(ReadConfigValue("angry_image")); }
void WriteConfigAngryImage(const std::wstring& path) { WriteConfigValue("angry_image", WideToUtf8(path)); }

void ApplyAngryImage(const std::wstring& path)
{
    WriteConfigAngryImage(path);                  // 空路径 = 生气时沿用平静头像
    if (g_angryImage) { delete g_angryImage; g_angryImage = NULL; }
    g_angryImagePath = L"";
    if (path.empty()) return;
    G::Image* img = G::Image::FromFile(path.c_str());
    if (img && img->GetLastStatus() == G::Ok) {
        g_angryImage = img;
        g_angryImagePath = path;
    } else {
        if (img) delete img;
        MessageBoxW(g_hPetWnd, L"无法加载该图片，已沿用平静头像。\n请换一张 PNG / JPG / BMP 图片试试。",
                    L"桌宠", MB_ICONWARNING | MB_OK);
    }
}

std::wstring ReadConfigHappyImage() { return Utf8ToWide(ReadConfigValue("happy_image")); }
void WriteConfigHappyImage(const std::wstring& path) { WriteConfigValue("happy_image", WideToUtf8(path)); }

void ApplyHappyImage(const std::wstring& path)
{
    WriteConfigHappyImage(path);                  // 空路径 = 心情好/表情开心时沿用平静头像
    if (g_happyImage) { delete g_happyImage; g_happyImage = NULL; }
    g_happyImagePath = L"";
    if (path.empty()) return;
    G::Image* img = G::Image::FromFile(path.c_str());
    if (img && img->GetLastStatus() == G::Ok) {
        g_happyImage = img;
        g_happyImagePath = path;
    } else {
        if (img) delete img;
        MessageBoxW(g_hPetWnd, L"无法加载该图片，已沿用平静头像。\n请换一张 PNG / JPG / BMP 图片试试。",
                    L"桌宠", MB_ICONWARNING | MB_OK);
    }
}

void LoadHunger()
{
    std::string v = ReadConfigValue("hunger");
    if (v.empty()) { g_hunger = kHungerDefault; return; }
    int val = atoi(v.c_str());
    g_hunger = val;
    if (g_hunger < 0) g_hunger = 0;
    if (g_hunger > kHungerMax) g_hunger = kHungerMax;
}

void SaveHunger()
{
    WriteConfigValue("hunger", std::to_string(g_hunger));
}

void UpdateHunger(float dt)
{
    if (g_hunger <= 0) { g_hungerAccum = 0.0f; return; }
    g_hungerAccum += dt;
    while (g_hungerAccum >= kHungerDecaySec) {
        g_hungerAccum -= kHungerDecaySec;
        if (g_hunger > 0) g_hunger--;
    }
}

void AddHunger(int gain)
{
    g_hunger += gain;
    if (g_hunger > kHungerMax) g_hunger = kHungerMax;
}

// 按文件大小计算回复的饥饿值：约每 0.5MB 加 1 点，最低 1 点，最高 20 点
int GetHungerGainForSize(ULONGLONG bytes)
{
    if (bytes <= 0) return 1;
    double mb = (double)bytes / (1024.0 * 1024.0);
    int gain = (int)(mb * 2.0 + 0.5);
    if (gain < 1) gain = 1;
    if (gain > 20) gain = 20;
    return gain;
}

void LoadMood()
{
    std::string v = ReadConfigValue("mood");
    if (v.empty()) { g_mood = kMoodDefault; return; }
    int val = atoi(v.c_str());
    g_mood = val;
    if (g_mood < 0) g_mood = 0;
    if (g_mood > kMoodMax) g_mood = kMoodMax;
}

void SaveMood()
{
    WriteConfigValue("mood", std::to_string(g_mood));
}

void AddMood(int gain)
{
    g_mood += gain;
    if (g_mood > kMoodMax) g_mood = kMoodMax;
    if (g_mood < 0) g_mood = 0;
}

void SetMoodFace(int face)
{
    if (face < kMoodFaceCalm) face = kMoodFaceCalm;
    if (face > kMoodFaceHappy) face = kMoodFaceHappy;
    g_moodFace = face;
}

// 吃东西（拖拽喂食）
static void MoveToRecycleBin(const std::wstring& path)
{
    std::wstring from = path + L'\0';   // SHFILEOPSTRUCT 要求双重空结尾
    SHFILEOPSTRUCTW op = {};
    op.hwnd = NULL;
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationW(&op);   // FOF_ALLOWUNDO = 放进回收站，而不是直接删除
}

// 把文件"喂"给桌宠：先询问，同意后播放 4 口吃掉动画
void StartEating(const std::wstring& path)
{
    if (g_eating) return;   // 正在吃，忽略新的拖拽

    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        ShowPetBubble(L"找不到这个文件，没办法吃掉呢。", true, true);
        return;
    }
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        ShowPetBubble(L"这是一个文件夹，桌宠咬不动它哦~", false, true);
        return;
    }

    // 饥饿值已满：吃不下了，不再弹"是否吃掉"确认框
    if (g_hunger >= kHungerMax) {
        ShowPetBubble(L"我吃不下的，已经吃饱啦~", false, true);
        return;
    }

    ULONGLONG size = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        size = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    }

    // 询问是否可以吃掉（自定义对话框：问句 + 小字提示 + 同意/不同意 按钮）
    if (!ConfirmEatDialog(g_hInst, g_hPetWnd)) return;   // 不同意：文件留在原处

    g_eating = true;
    g_eatingBites = 0;
    g_eatingT = 0.0f;
    g_eatingFilePath = path;
    g_eatingFileSize = size;
    g_eatingGain = GetHungerGainForSize(size);
    SetPetFrozen(true);   // 吃东西期间停止走动
    SetPetPose(3);        // 吃东西姿势（双手捧着食物举到嘴边）
}

// 4 口吃完：文件放进回收站、回复饥饿值、若启用 API 聊天则通知 AI
static void FinishEating()
{
    g_eating = false;
    SetPetPose(0);
    SetPetFrozen(false);

    int gain = g_eatingGain;
    if (gain <= 0) gain = 1;
    AddHunger(gain);      // 根据文件大小回复饥饿值（封顶 100）
    int moodGain = (gain + 1) / 2;   // 心情值少加一点：约按饥饿值的一半来加
    AddMood(moodGain);
    SaveHunger();
    SaveMood();

    MoveToRecycleBin(g_eatingFilePath);   // 放进回收站，不直接删除

    // 若启用了 API 聊天：把喂食信息告诉 AI（未启用则什么都不做）
    if (IsAIConfigured()) {
        NotifyAIFed(gain, g_hunger, moodGain);
    }

    g_eatingFilePath.clear();
    g_eatingFileSize = 0;
    g_eatingGain = 0;
}

void SetPetFrozen(bool f)
{
    g_freezeCount += f ? 1 : -1;
    if (g_freezeCount < 0) g_freezeCount = 0;
    g_petFrozen = g_freezeCount > 0;
}

void SetPetPose(int pose) { g_petPose = pose; }

// 入口
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    g_hInst = hInstance;
    OleInitialize(NULL);
    // 多显示器不同缩放（DPI）时，低层鼠标钩子的屏幕坐标与窗口坐标必须处于同一
    // 坐标系，否则"点击输入框内部"会被误判成"点击窗口外"导致窗口被关闭。
    // 优先使用"每显示器感知"（所有坐标统一为物理像素），旧系统回退到系统级感知。
    typedef BOOL(WINAPI* PFN_SetDpiAwarenessContext)(void*);
    PFN_SetDpiAwarenessContext pfnSetDpiCtx = (PFN_SetDpiAwarenessContext)
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (pfnSetDpiCtx) pfnSetDpiCtx((void*)-4);   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    else SetProcessDPIAware();

    G::GdiplusStartupInput si;
    ULONG_PTR token;
    G::GdiplusStartup(&token, &si, NULL);
    srand((unsigned)GetTickCount());

    // 首次使用（或配置文件缺失）时，一次性创建所有配置文件，不依赖用户后续操作：
    // pet_config.ini 写入默认头像(空=用默认)、外观参数与初始饥饿/心情值；
    // ai_config.json / chat_history.json 由 EnsureChatDataFiles 补齐默认内容
    bool firstRun = !ConfigFileExists();
    if (firstRun) {
        WriteConfigValue("head_image", "");
        WriteConfigValue("think_image", "");
        WriteConfigValue("mouth_image", "");
        WriteConfigValue("unhappy_image", "");
        WriteConfigValue("angry_image", "");
        WriteConfigValue("happy_image", "");
        WriteConfigValue("thickness", "1.00");
        WriteConfigValue("pet_scale", "1.00");
        WriteConfigValue("body_scale", "1.00");
        WriteConfigValue("hunger", std::to_string(kHungerDefault));
        WriteConfigValue("mood", std::to_string(kMoodDefault));
    }
    EnsureChatDataFiles();

    // 首次运行才弹出选图窗口（外观设置同款样式）；之后直接在"外观设置"里换图
    if (firstRun) {
        std::wstring out, outThink, outMouth, outUnhappy, outAngry, outHappy;
        int r = ShowChooserDialog(hInstance, NULL, L"", L"", L"", L"", L"", L"",
                                  out, outThink, outMouth, outUnhappy, outAngry, outHappy);
        ApplyHeadImage(r == 1 ? out : L"");
        ApplyThinkImage(r == 1 ? outThink : L"");
        ApplyMouthImage(r == 1 ? outMouth : L"");
        ApplyUnhappyImage(r == 1 ? outUnhappy : L"");
        ApplyAngryImage(r == 1 ? outAngry : L"");
        ApplyHappyImage(r == 1 ? outHappy : L"");
    } else {
        ApplyHeadImage(ReadConfigImage());
        ApplyThinkImage(ReadConfigThinkImage());
    }
    ApplyMouthImage(ReadConfigMouthImage());   // 张嘴头像（吃东西时切换）
    ApplyUnhappyImage(ReadConfigUnhappyImage()); // 不开心头像（饥饿值较低时显示）
    ApplyAngryImage(ReadConfigAngryImage());     // 生气头像（表情状态为生气时显示）
    ApplyHappyImage(ReadConfigHappyImage());     // 开心头像（心情值较高/表情状态为开心时显示）

    LoadAppearanceSettings();   // 外观设置：确定初始窗口尺寸
    LoadHunger();               // 读取上次结束游戏时记录的饥饿值
    LoadMood();                 // 读取上次结束游戏时记录的心情值

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PetWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"DeskPetWindowClass";
    RegisterClassExW(&wc);

    RECT wa = MonitorWorkArea(GetSystemMetrics(SM_CXSCREEN) / 2,
                              GetSystemMetrics(SM_CYSCREEN) / 2);
    int range = (wa.right - wa.left) - g_winW;
    int x = wa.left + (range > 0 ? rand() % range : 0);
    int y = wa.bottom - g_winH;

    g_hPetWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW |
                                    WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                L"DeskPetWindowClass", L"桌宠", WS_POPUP,
                                x, y, g_winW, g_winH, NULL, NULL, hInstance, NULL);
    if (!g_hPetWnd) {
        MessageBoxW(NULL, L"创建桌宠窗口失败", L"桌宠", MB_ICONERROR | MB_OK);
        return 1;
    }
    DragAcceptFiles(g_hPetWnd, TRUE);   // 接收文件拖拽（喂食）
    ShowWindow(g_hPetWnd, SW_SHOWNOACTIVATE);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_headImage) delete g_headImage;
    if (g_happyImage) delete g_happyImage;
    if (g_frameBmp) delete g_frameBmp;
    SaveAppearanceSettings();   // 退出前保存外观设置
    SaveHunger();               // 退出前保存饥饿值
    SaveMood();                 // 退出前保存心情值
    G::GdiplusShutdown(token);
    OleUninitialize();
    return 0;
}

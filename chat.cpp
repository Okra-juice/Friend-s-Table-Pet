// 聊天功能（极简版）：
//  - 极简输入条：输入框 + 设置 + 发送，回车发送后立即关闭
//  - 回复显示在桌宠头顶的气泡窗口里（不显示历史记录）
//  - 对话历史保存到 chat_history.json，用于上下文记忆
//  - 每次发送都附带角色提示词（system 消息）
// 配置文件：ai_config.json
#include "common.h"
#include <vector>
#include <thread>
#include <wininet.h>

// 点击窗口外部自动关闭
// 低层鼠标钩子运行在独立线程：鼠标事件由该线程快速处理并回投 WM_CLOSE，
// 不占用桌宠主线程，因此桌宠动画照常播放，鼠标也不会卡顿。
static HWND  g_hookTarget = NULL;
static HWND  g_hookPrev   = NULL;   // 上一个目标（设置/菜单关闭后恢复，如输入条）
static HHOOK g_mouseHook  = NULL;   // 仅钩子线程内访问
static HANDLE g_hookThread = NULL;
static HANDLE g_hookReady   = NULL;
static DWORD  g_hookThreadId = 0;

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wp, LPARAM lp)
{
    HWND t = g_hookTarget;
    if (t && nCode == HC_ACTION &&
        (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN)) {
        // IsWindowEnabled：模态文件选择框打开期间（窗口被系统禁用）不触发关闭
        if (IsWindow(t) && IsWindowVisible(t) && IsWindowEnabled(t)) {
            MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
            // 双重判断：点击点是否在窗口矩形内 / 命中的窗口是否为目标窗口或其子控件（或桌宠本体）。
            // 命中子控件时沿父链向上找，即使多显示器不同缩放（DPI）也不会误判
            RECT rc;
            GetWindowRect(t, &rc);
            bool inRect = (PtInRect(&rc, ms->pt) != FALSE);
            HWND clicked = WindowFromPoint(ms->pt);
            bool hitSelf = (clicked == t) || (clicked == g_hPetWnd);
            for (HWND cur = clicked; cur && !hitSelf; cur = GetParent(cur)) {
                if (cur == t) { hitSelf = true; break; }
            }
            if (!inRect && !hitSelf) {
                PostMessageW(t, WM_PET_CLICKAWAY, 0, 0);
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wp, lp);
}

DWORD WINAPI HookThreadProc(LPVOID)
{
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, GetModuleHandleW(NULL), 0);
    if (g_hookReady) SetEvent(g_hookReady);   // 通知主线程：钩子已注册
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = NULL; }
    return 0;
}

// 设置"点击窗口外关闭"的目标窗口；传 NULL 卸载（并恢复上一个目标）。
// 右键菜单 / 输入条 / 设置对话框共用。
void SetClickAwayTarget(HWND target)
{
    if (target == g_hookTarget) return;

    if (target) {
        if (g_hookTarget) g_hookPrev = g_hookTarget;   // 关闭后恢复
        g_hookTarget = target;
        if (!g_hookThread) {                            // 首次使用：启动钩子线程
            g_hookReady = CreateEventW(NULL, FALSE, FALSE, NULL);
            g_hookThread = CreateThread(NULL, 0, HookThreadProc, NULL, 0, &g_hookThreadId);
            if (g_hookThread && g_hookReady) WaitForSingleObject(g_hookReady, 2000);
            if (g_hookReady) { CloseHandle(g_hookReady); g_hookReady = NULL; }
        }
    } else {
        if (g_hookPrev) {                               // 恢复上一个目标（如输入条）
            g_hookTarget = g_hookPrev;
            g_hookPrev = NULL;
        } else {                                        // 无目标：停止钩子线程
            g_hookTarget = NULL;
            if (g_hookThread) {
                PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
                WaitForSingleObject(g_hookThread, 1500);
                CloseHandle(g_hookThread);
                g_hookThread = NULL;
                g_hookThreadId = 0;
            }
        }
    }
}

namespace {

// 小工具
std::wstring GetAIConfigPath() { return GetExeDir() + L"\\ai_config.json"; }
std::wstring GetHistoryPath()  { return GetExeDir() + L"\\chat_history.json"; }

// 内置底层提示词：不显示给用户，也不保存到配置文件。
// 每次发送消息时，会拼在用户填写的角色提示词后面一起作为 system 消息发给 API，
// 用来强制 AI 认真扮演角色、并控制回复长度。
const wchar_t* kBaseSystemPrompt =
    L"\n\n【角色扮演规则】请严格以角色设定进行对话："
    L"始终认真扮演这个角色，用符合该角色的语气、性格、身份和背景说话，不要跳出角色；"
    L"不要提及自己是 AI、模型或助手；"
    L"回复要贴合角色设定、自然简洁，长度适当，不要过长。";
static std::string ReadFileUtf8(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string s;
    char buf[2048];
    DWORD rd;
    while (ReadFile(h, buf, sizeof(buf), &rd, NULL) && rd > 0) s.append(buf, rd);
    CloseHandle(h);
    return s;
}

static void WriteFileUtf8(const std::wstring& path, const std::string& content)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    std::string bom = "\xEF\xBB\xBF";
    DWORD wr;
    WriteFile(h, bom.data(), (DWORD)bom.size(), &wr, NULL);
    WriteFile(h, content.data(), (DWORD)content.size(), &wr, NULL);
    CloseHandle(h);
}

static std::wstring TrimW(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// JSON 小工具
// 从 s[i]（开引号之后）读取到未转义引号为止，返回还原后的字符串
static std::string ReadEscaped(const std::string& s, size_t& i)
{
    std::string out;
    while (i < s.size()) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char e = s[i + 1];
            switch (e) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'u': {
                if (i + 6 <= s.size()) {
                    unsigned code = 0;
                    bool ok = true;
                    for (int k = 0; k < 4; k++) {
                        char h = s[i + 2 + k];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
                        else { ok = false; break; }
                    }
                    if (ok) {
                        if (code < 0x80) out += (char)code;
                        else if (code < 0x800) {
                            out += (char)(0xC0 | (code >> 6));
                            out += (char)(0x80 | (code & 0x3F));
                        } else {
                            out += (char)(0xE0 | (code >> 12));
                            out += (char)(0x80 | ((code >> 6) & 0x3F));
                            out += (char)(0x80 | (code & 0x3F));
                        }
                        i += 5;
                    } else out += e;
                } else out += e;
                break;
            }
            default: out += e;
            }
            i += 2;
        } else if (c == '"') {
            i++;
            break;
        } else {
            out += c;
            i++;
        }
    }
    return out;
}

static std::string JsonEscapeStr(const std::string& s)
{
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                snprintf(b, sizeof(b), "\\u%04x", c);
                out += b;
            } else out += (char)c;
        }
    }
    return out;
}

// 查找 "key":"..." 并返回还原后的值（容忍冒号前后空白）
static bool FindJsonStr(const std::string& s, const char* key, std::string& out)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat);
    while (p != std::string::npos) {
        size_t q = p + pat.size();
        while (q < s.size() && (s[q] == ' ' || s[q] == '\t')) q++;
        if (q < s.size() && s[q] == ':') {
            q++;
            while (q < s.size() && (s[q] == ' ' || s[q] == '\t')) q++;
            if (q < s.size() && s[q] == '"') {
                q++;
                out = ReadEscaped(s, q);
                return true;
            }
        }
        p = s.find(pat, p + 1);
    }
    return false;
}

// 查找 "key": 数字（如 memory_kb）
static bool FindJsonNum(const std::string& s, const char* key, std::string& out)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat);
    while (p != std::string::npos) {
        size_t q = p + pat.size();
        while (q < s.size() && (s[q] == ' ' || s[q] == '\t')) q++;
        if (q < s.size() && s[q] == ':') {
            q++;
            while (q < s.size() && (s[q] == ' ' || s[q] == '\t')) q++;
            size_t a = q;
            while (q < s.size() && s[q] >= '0' && s[q] <= '9') q++;
            if (q > a) { out = s.substr(a, q - a); return true; }
        }
        p = s.find(pat, p + 1);
    }
    return false;
}

// AI 配置
// struct AIConfig 定义见 common.h（开始界面的内联设置也要用）

static AIConfig LoadAIConfig()
{
    AIConfig cfg;
    std::string s = ReadFileUtf8(GetAIConfigPath());
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF) s.erase(0, 3);
    std::string v;
    if (FindJsonStr(s, "api_url", v)) cfg.apiUrl = Utf8ToWide(v);
    if (FindJsonStr(s, "api_key", v)) cfg.apiKey = Utf8ToWide(v);
    if (FindJsonStr(s, "model", v)) cfg.model = Utf8ToWide(v);
    if (FindJsonStr(s, "prompt", v)) cfg.prompt = Utf8ToWide(v);
    if (FindJsonNum(s, "memory_kb", v)) cfg.memoryKB = atoi(v.c_str());
    if (cfg.memoryKB < 1) cfg.memoryKB = 16;
    return cfg;
}

static void SaveAIConfig(const AIConfig& cfg)
{
    std::string s = "{\n";
    s += "  \"api_url\": \"" + JsonEscapeStr(WideToUtf8(cfg.apiUrl)) + "\",\n";
    s += "  \"api_key\": \"" + JsonEscapeStr(WideToUtf8(cfg.apiKey)) + "\",\n";
    s += "  \"model\": \"" + JsonEscapeStr(WideToUtf8(cfg.model)) + "\",\n";
    s += "  \"prompt\": \"" + JsonEscapeStr(WideToUtf8(cfg.prompt)) + "\",\n";
    s += "  \"memory_kb\": " + std::to_string(cfg.memoryKB) + "\n";
    s += "}\n";
    WriteFileUtf8(GetAIConfigPath(), s);
}

// 对话历史（独立文件，用于上下文，不展示给用户）
struct Msg { std::wstring role; std::wstring content; };
static std::vector<Msg> g_msgs;

static void LoadHistory()
{
    g_msgs.clear();
    std::string s = ReadFileUtf8(GetHistoryPath());
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF) s.erase(0, 3);
    size_t p = 0;
    while ((p = s.find("\"role\":\"", p)) != std::string::npos) {
        size_t q = p + 8;
        std::string role = ReadEscaped(s, q);
        size_t c = s.find("\"content\":\"", q);
        if (c == std::string::npos) break;
        size_t cq = c + 11;
        std::string content = ReadEscaped(s, cq);
        if (role == "user" || role == "assistant")
            g_msgs.push_back(Msg{ Utf8ToWide(role), Utf8ToWide(content) });
        p = cq;
    }
}

static void SaveHistory()
{
    // 每条消息独立一行，方便直接查看文件；解析时不依赖换行格式
    std::string s = "{\"messages\":[";
    bool first = true;
    for (auto& m : g_msgs) {
        // 聊天记录只保存实际对话（user / assistant），不保存 system 提示词；
        // system 每次发送前动态构建，不需要存档
        if (m.role != L"user" && m.role != L"assistant") continue;
        if (!first) s += ",";
        first = false;
        s += "\r\n  {\"role\":\"" + WideToUtf8(m.role) + "\",\"content\":\"" +
             JsonEscapeStr(WideToUtf8(m.content)) + "\"}";
    }
    s += "\r\n]}";
    WriteFileUtf8(GetHistoryPath(), s);
}

// HTTP 调用（OpenAI 兼容接口）
struct HttpResult { bool ok = false; int status = 0; std::string body; std::string err; };

static std::string BuildChatUrl(const std::wstring& base)
{
    std::string u = WideToUtf8(base);
    while (!u.empty() && (u.back() == ' ' || u.back() == '/')) u.pop_back();
    if (u.compare(0, 7, "http://") != 0 && u.compare(0, 8, "https://") != 0)
        u = "http://" + u;
    if (u.find("chat/completions") != std::string::npos) return u;
    return u + "/chat/completions";
}

static HttpResult HttpPostJson(const std::string& url, const std::string& body,
                               const std::string& apiKey)
{
    HttpResult r;
    HINTERNET hi = InternetOpenW(L"DeskPet/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hi) { r.err = "无法初始化网络"; return r; }

    DWORD timeout = 60000;
    InternetSetOptionW(hi, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(hi, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(hi, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

    std::wstring wurl = Utf8ToWide(url);
    URL_COMPONENTSW uc = {};
    uc.dwStructSize = sizeof(uc);
    WCHAR host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;      uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path;      uc.dwUrlPathLength  = 2048;
    if (!InternetCrackUrlW(wurl.c_str(), 0, 0, &uc)) {
        r.err = "API 地址格式不正确";
        InternetCloseHandle(hi);
        return r;
    }
    INTERNET_PORT port = uc.nPort ? uc.nPort
                                  : (uc.nScheme == INTERNET_SCHEME_HTTPS ? 443 : 80);
    HINTERNET hc = InternetConnectW(hi, host, port, NULL, NULL,
                                    INTERNET_SERVICE_HTTP, 0, 0);
    if (!hc) { r.err = "无法连接服务器"; InternetCloseHandle(hi); return r; }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS) flags |= INTERNET_FLAG_SECURE;
    HINTERNET hr = HttpOpenRequestW(hc, L"POST", path, NULL, NULL, NULL, flags, 0);
    if (!hr) {
        r.err = "无法创建请求";
        InternetCloseHandle(hc);
        InternetCloseHandle(hi);
        return r;
    }

    std::string headers = "Content-Type: application/json\r\n";
    if (!apiKey.empty()) headers += "Authorization: Bearer " + apiKey + "\r\n";
    std::wstring wheaders = Utf8ToWide(headers);

    if (!HttpSendRequestW(hr, wheaders.c_str(), -1L,
                          (LPVOID)body.data(), (DWORD)body.size())) {
        DWORD e = GetLastError();
        if (e == ERROR_INTERNET_NAME_NOT_RESOLVED) r.err = "无法解析服务器地址";
        else if (e == ERROR_INTERNET_CANNOT_CONNECT) r.err = "无法连接到服务器";
        else if (e == ERROR_INTERNET_TIMEOUT) r.err = "连接超时";
        else if (e == 12157) r.err = "安全连接失败（证书问题）";   // ERROR_INTERNET_SECURE_FAILURE
        else r.err = "网络请求失败";
        InternetCloseHandle(hr);
        InternetCloseHandle(hc);
        InternetCloseHandle(hi);
        return r;
    }

    std::string resp;
    char buf[8192];
    DWORD rd = 0;
    while (InternetReadFile(hr, buf, sizeof(buf), &rd) && rd > 0) {
        resp.append(buf, rd);
        rd = 0;
    }
    DWORD status = 0, slen = sizeof(status);
    if (HttpQueryInfoW(hr, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &status, &slen, NULL)) {
        r.status = (int)status;
        r.ok = (status >= 200 && status < 300);
    } else {
        r.ok = true;
    }
    r.body = resp;
    InternetCloseHandle(hr);
    InternetCloseHandle(hc);
    InternetCloseHandle(hi);
    return r;
}

// 构建完整的 system 提示词：用户填写的角色提示词 + 内置底层提示词 + 当前状态/规则。
// 每次发送前动态构建（饥饿/心情值随对话变化），同时原样存档到聊天记录。
static std::string BuildSystemPrompt(const AIConfig& cfg)
{
    std::string sys = WideToUtf8(cfg.prompt);
    sys += WideToUtf8(kBaseSystemPrompt);
    // 把饥饿值、心情值的上限和当前值一起混进提示词，让 AI 知道自己的状态
    sys += "\n\n【当前状态】你的饥饿值上限是 " + std::to_string(kHungerMax) +
           "，当前的饥饿值是 " + std::to_string(g_hunger) +
           "/" + std::to_string(kHungerMax) + "。"
           "饥饿值的分档标准：75 以上表示吃饱了、很饱；85 以上表示吃得太饱，撑得走不动路、懒洋洋的；"
           "50 左右（40~60）表示中等，不饿也不撑；25 以下表示很饿；15 以下表示已经饿趴下了、没有力气。"
           "你的心情值上限是 " + std::to_string(kMoodMax) +
           "，当前的心情值是 " + std::to_string(g_mood) +
           "/" + std::to_string(kMoodMax) + "。"
           "心情值的标准线是 50：在很平常、没有什么特别事情发生的时候，心情值就是 50，"
           "你按角色正常的状态回应即可，不需要刻意表现任何情绪；"
           "心情值明显高于 50 表示心情很好、很开心，明显低于 50 表示心情不好、不开心。";
    // 饥饿值较低时：让 AI 在回复中自然表达想吃饭的意思
    if (g_hunger <= kHungerLow) {
        sys += "\n\n【饥饿状态】你现在很饿，请在回复中自然地表达想吃东西、想让用户喂你的意思"
               "（比如提到\"我想吃饭\"）。";
    }
    // 心情值较低时：可以自然流露一点傲娇/生气/委屈，但不能刻意、过度
    if (g_mood <= kMoodLow) {
        sys += "\n\n【心情状态】你现在心情不太好（心情值只有 " + std::to_string(g_mood) +
               "），回复可以自然地流露出一点傲娇、生气或委屈的情绪，"
               "但不要表现得很刻意，也不要特别傲娇、过度夸张。";
    }
    // 心情值较高时：多表达开心，并在表情中括号里更频繁地写"开心"，
    // 让"开心头像"的出场频率跟着心情值一起变高
    else if (g_mood >= kMoodHigh) {
        sys += "\n\n【心情状态】你现在心情非常好（心情值高达 " + std::to_string(g_mood) +
               "），整个人都喜气洋洋的，回复请自然流露出开心、轻松、愉快的情绪；"
               "在第二个表情中括号里也更频繁地写\"开心\"，让高兴的表情多出现一些"
               "（心情平常或没特别事情时再写\"平静\"）。"
               "但心情好不是免死金牌：如果对方是在嘲讽、贬低、故意招惹你，"
               "也要如实写负数把心情降下来、切换成生气或不开心，"
               "不要因为心情好就被动地一直笑嘻嘻地忍着。";
    }
    // 心情值中括号规则：AI 每次回复都在开头输出 [数值][状态]，程序会解析并生效；
    // 心情值变动不再限制 ±8，由 AI 自由决定；表情与心情值区间强绑定（生气/不开心 ≤ 30，开心 ≥ 70）
    sys += "\n\n【心情值中括号规则】每次回复时，先在回复内容最前面输出一个中括号，"
           "里面写这次回复让心情值增加多少：正数表示心情变好（如 [+5]），负数表示心情变差（如 [-3]），"
           "不写符号视为增加，心情值范围是 0~100，变化幅度没有上限，按当时情绪自然决定即可；"
           "并且每次对话的心情值变化至少是 ±1，不能写 0。"
           "紧接着再输出一个中括号，里面写你当前的心情状态，只能是：开心 / 平静 / 生气 / 不开心 之一"
           "（例如 [+5][开心] 或 [-3][生气]）。"
           "表情和心情值必须匹配：写生气或不开心表示心情低落，写开心表示心情好，"
           "写平静表示心情平常；程序会按你给的数值真实调整心情值，"
           "并让表情跟着心情走（心情很低时程序会自动配上难看的表情，心情很高时自动开心）。"
           "所以表情要认真选：真的生气才写生气，真的开心才写开心，不要乱写。"
           "请根据你当前的饥饿值、心情值和对话内容自己决定状态：心情差可以写生气或不开心，"
           "心情好写开心，心情平常写平静。心情值高的时候尽量多写开心。"
           "这两个中括号是给程序用的，用户看不到，但第一个中括号里的数值会真实地影响你的心情值。"
           "【情绪波动大小由你的性格决定】请根据用户的角色提示词来判断这个角色的性格，"
           "从而决定每次心情值变化的大小：如果角色是暴脾气、情绪化、活泼热烈、容易激动这类性格，"
           "就把波动写大一些（比如 ±10~±30）；如果角色是情绪稳定、温和、沉稳、冷静这类性格，"
           "就把波动写小一些（比如 ±1~±5）；一般的角色就按对话内容和当时的情绪自然波动，"
           "情绪强烈时写大、平淡时写小，但至少是 ±1、不能写 0。"
           "【表情和数值必须一致】只要表情状态发生了变化（比如从平静换成生气、不开心或开心），"
           "数值就绝不能是 0，必须和表情方向一致地增减：表情变差（生气/不开心）写负数，"
           "表情变好（开心）写正数，绝对不允许出现\"表情是生气但心情值没变\"这种矛盾情况。"
           "【被惹怒时一定要生气】被人找茬、挑衅、贬低、嘲讽、故意气你，或说过分的话惹你生气时，"
           "即使你现在心情很好，也要如实把心情值降下来（写较大的负数，比如 -15~-30），"
           "把表情切换成生气或不开心，语气也随之变差、回怼对方，"
           "不要因为心情好就被动地一直笑嘻嘻地忍着；"
           "【区分调侃的性质】如果是善意的玩笑、打情骂俏、逗你开心，"
           "你可以开心或带点傲娇地回应（可写小正数，或写小幅负数反撒个娇）；"
           "但如果调侃带嘲讽、贬低、恶意，或一再踩中你的雷点，"
           "就必须当成真的惹你生气来回应，心情照降不误、表情也换；"
           "心情很差时，只有被哄好或遇到真正开心的事，才能写大正数把心情值拉回来并切换成开心。";
    // 性格规则：始终以用户设定的角色为准；只有心情不好时才自然流露一点傲娇，不能刻意、过度
    sys += "\n\n【性格规则】请始终按照用户设定的角色提示词来表现性格，不要额外加入与角色不符的设定。"
           "只有在心情不好（心情值明显低于 50）的时候，才可以自然地流露出一点点傲娇、别扭的情绪，"
           "不能表现得很刻意，也不要十分傲娇、过度夸张；"
           "心情平常（50 左右）按角色的正常状态回应即可，不需要刻意傲娇；"
           "心情好的时候可以更开心、更活泼一些，多流露出高兴的情绪。"
           "用户喂你吃东西会增加你的饥饿值和心情值、让你开心，被喂食后的回复可以稍微开心或傲娇一些。"
           "心情从差变好之后，可以傲娇地说自己\"原谅你了\"。"
           "如果用户惹你生气，可以在第一个中括号里写负数来降低心情值。";
    return sys;
}

// 按记忆长度(KB)截取最近的上下文；当前消息(最后一条)始终保留；
// 用户角色提示词 + 内置底层提示词 始终作为第一条 system 消息随每条消息一起发送
static std::string BuildRequestBody(const AIConfig& cfg, const std::vector<Msg>& msgs)
{
    std::string body = "{\"model\":\"" + JsonEscapeStr(WideToUtf8(cfg.model)) +
                       "\",\"messages\":[";
    // system 消息每次发送前动态构建（含最新的饥饿/心情状态）
    std::string sys = BuildSystemPrompt(cfg);
    body += "{\"role\":\"system\",\"content\":\"" + JsonEscapeStr(sys) + "\"},";

    int budget = cfg.memoryKB * 1024;
    int used = 0;
    int n = (int)msgs.size();
    std::vector<int> idx;
    for (int i = n - 1; i >= 0; i--) {
        if (msgs[i].role == L"system") continue;   // 历史里的 system 只存档、不占上下文预算
        int sz = (int)WideToUtf8(msgs[i].content).size() + 32;
        bool mustKeep = (i == n - 1);
        if (!mustKeep && used + sz > budget) break;
        used += sz;
        idx.push_back(i);
    }
    for (int k = (int)idx.size() - 1; k >= 0; k--) {
        const Msg& m = msgs[idx[k]];
        if (m.role == L"system") continue;   // system 已在上面动态构建，不再重复发送
        body += "{\"role\":\"" + WideToUtf8(m.role) + "\",\"content\":\"" +
                JsonEscapeStr(WideToUtf8(m.content)) + "\"},";
    }
    if (!body.empty() && body.back() == ',') body.pop_back();
    body += "],\"stream\":false}";
    return body;
}

// 从响应中提取 assistant 文本（兼容普通 JSON 与 SSE 流式格式）
static std::wstring ExtractReply(const HttpResult& r, std::wstring& errMsg)
{
    errMsg.clear();
    if (!r.ok) {
        std::string msg;
        size_t e = r.body.find("\"error\"");
        if (e != std::string::npos) {
            size_t m = r.body.find("\"message\":\"", e);
            if (m != std::string::npos) {
                size_t q = m + 11;
                msg = ReadEscaped(r.body, q);
            }
        }
        errMsg = L"请求失败";
        if (r.status) errMsg += L" (HTTP " + std::to_wstring(r.status) + L")";
        if (!msg.empty()) errMsg += L"：" + Utf8ToWide(msg);
        else if (!r.err.empty()) errMsg += L"：" + Utf8ToWide(r.err);
        return L"";
    }
    std::string joined;
    size_t p = 0;
    while ((p = r.body.find("\"content\":\"", p)) != std::string::npos) {
        size_t q = p + 11;
        joined += ReadEscaped(r.body, q);
        p = q;
    }
    if (joined.empty()) {   // 推理模型兜底
        p = 0;
        while ((p = r.body.find("\"reasoning_content\":\"", p)) != std::string::npos) {
            size_t q = p + 21;
            joined += ReadEscaped(r.body, q);
            p = q;
        }
    }
    if (joined.empty()) {
        errMsg = L"AI 没有返回内容";
        return L"";
    }
    return Utf8ToWide(joined);
}

// 解析 AI 回复开头的标记：[+5][生气] 正文
// outDelta：心情值增减；outFace：表情状态（0 平静 1 生气 2 不开心 3 开心）；outText：去掉标记后的正文
// 只处理紧贴在开头的中括号；正文里的中括号原样保留。
static void ParseMoodTags(const std::wstring& in, int& outDelta, int& outFace, std::wstring& outText)
{
    outDelta = 0;
    outFace = kMoodFaceCalm;
    outText = in;
    std::wstring t = TrimW(in);
    if (t.empty() || t[0] != L'[') return;
    size_t p = 0;
    bool haveNumber = false;
    while (p < t.size() && t[p] == L'[') {
        size_t close = t.find(L']', p);
        if (close == std::wstring::npos) break;
        std::wstring inner = t.substr(p + 1, close - p - 1);
        // 尝试解析为整数（允许 + 或 - 开头）
        size_t k = 0;
        int sign = 1;
        if (!inner.empty() && (inner[k] == L'+' || inner[k] == L'-')) {
            if (inner[k] == L'-') sign = -1;
            k++;
        }
        bool isNum = (k < inner.size());
        for (size_t j = k; j < inner.size() && isNum; j++)
            if (inner[j] < L'0' || inner[j] > L'9') isNum = false;
        bool knownState = (inner == L"生气" || inner == L"不开心" || inner == L"平静" || inner == L"开心");
        if (isNum) {
            int v = 0;
            for (size_t j = k; j < inner.size(); j++) v = v * 10 + (inner[j] - L'0');
            outDelta += sign * v;
            haveNumber = true;
        } else if (knownState) {
            if (inner == L"生气") outFace = kMoodFaceAngry;
            else if (inner == L"不开心") outFace = kMoodFaceUnhappy;
            else if (inner == L"开心") outFace = kMoodFaceHappy;
            else outFace = kMoodFaceCalm;
        } else if (haveNumber) {
            // 数字后面紧跟的未知状态词（如 [开心]）：按标记吞掉，表情保持当前值
        } else {
            break;   // 开头的未知内容：按正文处理，原样显示
        }
        p = close + 1;
        while (p < t.size() && (t[p] == L' ' || t[p] == L'\t')) p++;
        if (p >= t.size() || t[p] != L'[') break;   // 下一个不是中括号：正文开始
    }
    outText = TrimW(t.substr(p));
}

// 消息中继窗口（异步回复的接收者）
struct ReplyData { bool ok; std::wstring text; };
const UINT WM_CHAT_REPLY = WM_APP + 2;
static HWND g_msgHwnd = NULL;
static bool g_sending = false;

static void ShowBubble(const std::wstring& text, bool isError, bool transient);
static void HideBubble();
static void RepositionBubble();   // 跟随桌宠（定义在文件后部）

LRESULT CALLBACK MsgOnlyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CHAT_REPLY) {
        ReplyData* rd = (ReplyData*)lp;
        if (rd) {
            g_sending = false;
            bool isError = !rd->ok || rd->text.empty();
            std::wstring text = isError
                ? (rd->text.empty() ? L"AI 没有返回内容" : rd->text)
                : rd->text;
            if (!isError) {
                // 正常回复：解析开头的中括号标记（[数值][状态]），应用心情值增减与表情状态；
                // 标记部分不显示，只把正文存进历史并展示
                int delta = 0, face = kMoodFaceCalm;
                std::wstring clean;
                ParseMoodTags(rd->text, delta, face, clean);
                if (clean.empty()) clean = text;
                AddMood(delta);   // 心情值按 AI 数值增减，由 AI 情绪自由决定，不再被表情强制拉向高低端
                // 表情与最终心情值保持一致，避免"表情平静但心情归零"这类矛盾又吓人的画面：
                // 心情很低就配难看的表情，心情很高就自动开心，心情居中则沿用 AI 选的表情。
                if (g_mood <= kMoodLow) {
                    if (face == kMoodFaceCalm || face == kMoodFaceHappy) face = kMoodFaceUnhappy;
                } else if (g_mood >= kMoodHigh) {
                    if (face != kMoodFaceHappy) face = kMoodFaceHappy;
                }
                SaveMood();
                SetMoodFace(face);
                // 历史记录保留 AI 回复的完整原文（含 [数值][状态] 中括号标记），
                // 气泡里展示的则是去掉标记后的正文
                g_msgs.push_back(Msg{ L"assistant", rd->text });
                SaveHistory();
                SetPetPose(2);        // 回复到达：说话比画姿势（双手分开约 30° 浮动）
                text = clean;
            } else {
                // 报错：不做任何解析，原样展示（若含中括号也不会被去掉）
                g_msgs.push_back(Msg{ L"error", text });
                SetPetPose(0);        // 失败恢复平静姿势
            }
            delete rd;
            ShowBubble(text, isError, true);   // 按字数计时显示
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void EnsureMsgWindow()
{
    if (g_msgHwnd && IsWindow(g_msgHwnd)) return;
    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = MsgOnlyProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = L"PetChatMsgClass";
        RegisterClassExW(&wc);
        s_reg = true;
    }
    g_msgHwnd = CreateWindowExW(0, L"PetChatMsgClass", L"", 0,
                                0, 0, 0, 0, HWND_MESSAGE, NULL, g_hInst, NULL);
}

// 桌宠头顶气泡窗口
static HWND g_bubbleHwnd = NULL;
static HWND g_inputHwnd = NULL;   // 输入条主窗口；此处提前声明，供气泡避开输入框定位
static std::wstring g_bubbleText;
static bool g_bubbleError = false;
static int g_bubbleFontSize = 14;   // 气泡字号：长文本自动缩小以容纳更多内容

static void ComputeBubbleSize(const std::wstring& text, int& w, int& h)
{
    RECT pr;
    if (g_hPetWnd && IsWindow(g_hPetWnd)) GetWindowRect(g_hPetWnd, &pr);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
    RECT wa = MonitorWorkAreaNear((pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2);
    int maxH = (wa.bottom - wa.top) * 9 / 10;   // 高度上限：桌宠所在工作区高度的 90%（长文本也能完整显示）

    // 先用 14px + 280 宽测量；放不下则加宽到 360 并缩字号到 12px 再试
    int layoutW = 280, fs = 14;
    for (int attempt = 0; attempt < 2; attempt++) {
        HDC dc = GetDC(NULL);
        Gdiplus::Graphics g(dc);
        Gdiplus::Font f(L"Microsoft YaHei UI", (Gdiplus::REAL)fs,
                        Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat sf;
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);
        Gdiplus::SizeF layout((Gdiplus::REAL)layoutW, 1e6f), sz;
        g.MeasureString(text.c_str(), -1, &f, layout, &sf, &sz, NULL, NULL);
        // GDI+ 的 MeasureString 高度偶尔会比实际渲染低几个像素，长文本最后一行贴边时
        // 会被 LineLimit 裁掉末尾一两个字；这里只补少量像素（而非一整行）保证最后一行完整，
        // 既避免裁字，也不会在底部留出一大条空白
        sz.Height += 6.0f;
        ReleaseDC(NULL, dc);
        w = (int)sz.Width + 24 + 4;
        h = (int)sz.Height + 20 + 12 + 4;
        if (h <= maxH || attempt == 1) break;
        layoutW = 360;
        fs = 12;
    }
    if (h > maxH) h = maxH;   // 仍放不下则限高，超出部分不绘制（LineLimit 裁剪，不会越界）
    g_bubbleFontSize = fs;
}

static void BubblePaint(HWND hwnd)
{
    using namespace G;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    Bitmap* bmp = new Bitmap(W, H, PixelFormat32bppPARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));

    const int tailH = 12;
    const int cardW = W - 4, cardH = H - 4 - tailH;
    const float cx = cardW / 2.0f;

    // 阴影（卡片 + 尾巴）
    GraphicsPath sp;
    AddRoundRectPath(sp, 3, 6, cardW - 6, cardH - 6, 12);
    PointF st[3] = {
        { cx - 7, (float)cardH - 3 },
        { cx, (float)(cardH + tailH) - 3 },
        { cx + 7, (float)cardH - 3 }
    };
    sp.AddPolygon(st, 3);
    sp.CloseFigure();
    g.FillPath(&SolidBrush(Color(90, 0, 0, 0)), &sp);

    // 卡片
    GraphicsPath cp;
    AddRoundRectPath(cp, 0, 0, cardW, cardH, 12);
    g.FillPath(&SolidBrush(Color(255, 255, 255, 255)), &cp);
    Pen cpen(Color(255, 228, 228, 228), 1.2f);
    g.DrawPath(&cpen, &cp);

    // 尾巴
    PointF tp[3] = {
        { cx - 7, (float)cardH },
        { cx, (float)(cardH + tailH) },
        { cx + 7, (float)cardH }
    };
    SolidBrush whiteB(Color(255, 255, 255, 255));
    g.FillPolygon(&whiteB, tp, 3);
    g.DrawLine(&cpen, tp[0], tp[1]);
    g.DrawLine(&cpen, tp[1], tp[2]);

    // 文字
    Font f(L"Microsoft YaHei UI", (Gdiplus::REAL)g_bubbleFontSize,
           FontStyleRegular, UnitPixel);
    SolidBrush tc(g_bubbleError ? Color(255, 176, 58, 58) : Color(255, 51, 51, 51));
    StringFormat sf;
    sf.SetFormatFlags(StringFormatFlagsLineLimit);
    RectF tr(12, 10, cardW - 24, cardH - 20);
    g.DrawString(g_bubbleText.c_str(), -1, &f, tr, &sf, &tc);

    PresentLayered(hwnd, W, H, bmp);
    delete bmp;
}

LRESULT CALLBACK BubbleProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        BubblePaint(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wp == 2) {
            KillTimer(hwnd, 2);
            SetPetPose(0);          // 气泡消失：恢复平静姿势
            SetPetFrozen(false);    // 恢复走动
            HideBubble();
        } else if (wp == 3) {
            RepositionBubble();     // 跟随桌宠移动（含跨屏）
        }
        return 0;
    case WM_DESTROY:
        if (hwnd == g_bubbleHwnd) g_bubbleHwnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void PositionBubble(int w, int h)
{
    // 以桌宠所在显示器的工作区为准：桌宠移到副屏时气泡也跟过去
    RECT pr, wa;
    if (g_hPetWnd && IsWindow(g_hPetWnd)) GetWindowRect(g_hPetWnd, &pr);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
    wa = MonitorWorkAreaNear((pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2);
    int cx = (pr.left + pr.right) / 2;
    int x = cx - w / 2;
    if (x < wa.left + 4) x = wa.left + 4;
    if (x + w > wa.right - 4) x = wa.right - 4 - w;
    int y = pr.top - h - 6;
    // 输入框打开期间，若它被排到桌宠上方（屏幕下方放不下）并与气泡重叠，
    // 就让气泡顺着输入框的高度继续往上移，紧贴输入框顶部上方，留出空间不被遮挡
    if (g_inputHwnd && IsWindow(g_inputHwnd) && IsWindowVisible(g_inputHwnd)) {
        RECT ir;
        GetWindowRect(g_inputHwnd, &ir);
        if (ir.top < y + h && ir.bottom > y) {
            y = ir.top - h - 6;
        }
    }
    if (y < wa.top + 4) y = wa.top + 4;
    SetWindowPos(g_bubbleHwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

// 跟随桌宠：气泡显示期间定时重新贴到桌宠头顶（含跨显示器跟随）
static void RepositionBubble()
{
    if (!g_bubbleHwnd || !IsWindow(g_bubbleHwnd) || !IsWindowVisible(g_bubbleHwnd)) return;
    RECT rc;
    GetWindowRect(g_bubbleHwnd, &rc);
    PositionBubble(rc.right - rc.left, rc.bottom - rc.top);
}

static void HideBubble()
{
    if (g_bubbleHwnd && IsWindow(g_bubbleHwnd)) {
        KillTimer(g_bubbleHwnd, 2);
        KillTimer(g_bubbleHwnd, 3);
        ShowWindow(g_bubbleHwnd, SW_HIDE);
    }
}

static void ShowBubble(const std::wstring& text, bool isError, bool transient)
{
    if (text.empty()) { HideBubble(); return; }
    int w = 0, h = 0;
    ComputeBubbleSize(text, w, h);

    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = BubbleProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"PetBubbleClass";
        RegisterClassExW(&wc);
        s_reg = true;
    }
    if (!g_bubbleHwnd || !IsWindow(g_bubbleHwnd)) {
        g_bubbleHwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            L"PetBubbleClass", L"", WS_POPUP, 0, 0, w, h, NULL, NULL, g_hInst, NULL);
        if (!g_bubbleHwnd) return;
    }
    g_bubbleText = text;
    g_bubbleError = isError;
    PositionBubble(w, h);
    // 直接离屏绘制并上屏，不依赖 WM_PAINT 派发，避免复用气泡窗口时残留上一次的内容
    BubblePaint(g_bubbleHwnd);
    ShowWindow(g_bubbleHwnd, SW_SHOWNOACTIVATE);
    KillTimer(g_bubbleHwnd, 2);
    if (transient) {
        // 显示时长与文字数量成正比（每字约 130ms），长消息最长 20 秒
        int ms = 1300 + (int)text.size() * 130;
        if (ms > 20000) ms = 20000;
        SetTimer(g_bubbleHwnd, 2, ms, NULL);
    }
    SetTimer(g_bubbleHwnd, 3, 120, NULL);   // 跟随桌宠移动（含跨显示器）
}

// 极简输入条（多行，随输入自动变高）
const int IB_W = 420;              // 宽度固定
const int IB_H = 66;               // 初始高度（约 2 行），输入越多越长，最多 6 行

const RECT kSetBtn    = { 12, 18, 52, 58 };    // 设置（方块倒角）
const RECT kEditField = { 60, 14, 356, 52 };  // 编辑框区域：左/上/宽固定，高度随输入变长
const RECT kSendBtn   = { 364, 18, 408, 58 };  // 发送（方块倒角）

static int  g_editH = kEditField.bottom - kEditField.top;   // 编辑框当前高度（动态）
static int  g_barH  = IB_H;          // 输入条当前高度（动态）
static HRGN g_lastRgn = NULL;        // 输入条当前圆角区域（变高时替换并清理旧句柄）

static HWND g_editHwnd = NULL;
static WNDPROC g_editProc = NULL;
static int g_hover = 0, g_press = 0;   // 1设置 2发送

static HFONT g_uiFont = NULL;
static HFONT GetUIFont()
{
    if (!g_uiFont)
        g_uiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    return g_uiFont;
}

static void DoSend();

LRESULT CALLBACK EditSubProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN && !(GetKeyState(VK_PROCESSKEY) & 0x8000)) {
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                // Shift+Enter：插入换行（多行输入长内容）
                SendMessageW(hwnd, WM_CHAR, L'\n', 1);
                return 0;
            }
            DoSend();
            return 0;
        }
        if (wp == VK_ESCAPE) {
            if (g_inputHwnd && IsWindow(g_inputHwnd)) PostMessageW(g_inputHwnd, WM_CLOSE, 0, 0);
            return 0;
        }
    }
    return CallWindowProcW(g_editProc, hwnd, msg, wp, lp);
}

static void DrawSlidersIcon(Gdiplus::Graphics& g, float x, float y, float s)
{
    using namespace G;
    SolidBrush b(Color(255, 64, 158, 255));
    for (int i = 0; i < 3; i++) {
        float yy = y + s * 0.18f + i * s * 0.32f;
        g.FillRectangle(&b, x, yy, s * 0.58f, s * 0.10f);
        g.FillEllipse(&b, x + s * 0.50f, yy - s * 0.07f, s * 0.22f, s * 0.22f);
    }
}

// 方块按钮，只倒小圆角
static void DrawSquareBtn(Gdiplus::Graphics& g, const RECT& rc, const wchar_t* text,
                          bool primary, bool hover, bool press)
{
    using namespace G;
    float x = (float)rc.left, y = (float)rc.top;
    float w = (float)(rc.right - rc.left), h = (float)(rc.bottom - rc.top);
    GraphicsPath p;
    AddRoundRectPath(p, x, y, w, h, 6);
    if (primary) {
        // 天蓝色主按钮
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
    } else {
        g.FillPath(&SolidBrush(Color(255, 255, 255, 255)), &p);
        Pen pen(hover ? Color(255, 230, 74, 84) : Color(255, 220, 220, 220), 1.3f);
        g.DrawPath(&pen, &p);
    }
}

static void PaintInputBar(HWND hwnd)
{
    using namespace G;
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    Graphics g(ps.hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(255, 255, 255, 255));

    // 外框（高度随输入动态变化）
    GraphicsPath bp;
    AddRoundRectPath(bp, 0.5f, 0.5f, IB_W - 1, g_barH - 1, 10);
    g.DrawPath(&Pen(Color(255, 224, 224, 224), 1.0f), &bp);

    // 设置按钮（左侧）
    DrawSquareBtn(g, kSetBtn, NULL, false, g_hover == 1, g_press == 1);
    DrawSlidersIcon(g, (float)(kSetBtn.left + 11), (float)(kSetBtn.top + 11), 18);

    // 发送按钮（右侧）
    DrawSquareBtn(g, kSendBtn, L"发送", true, g_hover == 2, g_press == 2);

    // 输入框底色（高度随输入动态变化）
    GraphicsPath ip;
    AddRoundRectPath(ip, (float)kEditField.left, (float)kEditField.top,
        (float)(kEditField.right - kEditField.left),
        (float)g_editH, 6);
    g.FillPath(&SolidBrush(Color(255, 255, 255, 255)), &ip);
    g.DrawPath(&Pen(Color(255, 220, 220, 220), 1.2f), &ip);

    EndPaint(hwnd, &ps);
}

// 定位输入条：紧贴桌宠下方（放不下则移到上方），并限制在桌宠所在显示器的工作区内；
// 桌宠被拖到副屏时输入条也跟过去
static void PositionInputBar()
{
    if (!g_inputHwnd || !IsWindow(g_inputHwnd)) return;
    RECT pr, wa;
    if (g_hPetWnd && IsWindow(g_hPetWnd)) GetWindowRect(g_hPetWnd, &pr);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
    wa = MonitorWorkAreaNear((pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2);
    // 水平方向：以桌宠中心为基准把输入框居中对齐，再整体向左偏移一点
    int x = (pr.left + pr.right) / 2 - IB_W / 2 - 24;
    if (x + IB_W > wa.right) x = wa.right - IB_W;
    if (x < wa.left) x = wa.left;
    int y = pr.bottom + 10;
    if (y + g_barH > wa.bottom) y = pr.top - g_barH - 10;
    if (y < wa.top) y = wa.top;
    SetWindowPos(g_inputHwnd, NULL, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// 输入条随输入的行数自动变高：输入越多越长（最多 6 行），之后在编辑框内滚动
static void ResizeInputBar()
{
    if (!g_inputHwnd || !IsWindow(g_inputHwnd) || !g_editHwnd) return;

    int lines = (int)SendMessageW(g_editHwnd, EM_GETLINECOUNT, 0, 0);
    if (lines < 1) lines = 1;
    const int kMaxLines = 6;      // 最高显示 6 行
    if (lines > kMaxLines) lines = kMaxLines;
    const int kLineH = 21;        // 每行高度（约）
    int editH = lines * kLineH + 4;      // 略加余量，避免最后一行显示不全
    const int kEditMinH = 38;     // 初始约 2 行
    if (editH < kEditMinH) editH = kEditMinH;
    int barH = editH + 28;        // 上边距 14 + 下边距 14
    if (barH == g_barH) return;

    g_barH = barH;
    g_editH = editH;

    // 编辑框子控件同步加高（位置与宽度不变）
    SetWindowPos(g_editHwnd, NULL, 0, 0, kEditField.right - kEditField.left - 12, editH,
                 SWP_NOMOVE | SWP_NOZORDER);

    // 输入条整体加高，并重建圆角区域（先设新区域，再释放旧区域句柄）
    HRGN nr = CreateRoundRectRgn(0, 0, IB_W + 1, barH + 1, 12, 12);
    SetWindowRgn(g_inputHwnd, nr, TRUE);
    if (g_lastRgn) { DeleteObject(g_lastRgn); g_lastRgn = NULL; }
    g_lastRgn = nr;

    SetWindowPos(g_inputHwnd, NULL, 0, 0, IB_W, barH, SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(g_inputHwnd, NULL, TRUE);
    PositionInputBar();   // 变高后重新限制在屏幕工作区内
}

// 把一条用户消息异步发给 AI（聊天输入与喂食通知共用）
static void SendUserMessageAsync(const std::wstring& text)
{
    if (g_sending) return;   // 上一条还在回复中

    AIConfig cfg = LoadAIConfig();
    // 聊天记录只保存实际对话：system 提示词每次发送前动态构建（含最新的饥饿/心情状态），
    // 不写入聊天记录文件，让记录里只有 user / assistant 的对话内容
    g_msgs.push_back(Msg{ L"user", text });
    SaveHistory();

    SetPetFrozen(true);     // 思考期间桌宠停止走动
    SetPetPose(1);          // 手撑头思考姿势（换思考头像）
    SetMoodFace(kMoodFaceCalm);   // 新一轮对话开始：表情先回到平静，等待 AI 回复决定
    HideBubble();           // 思考期间不显示任何聊天框（含上一次的残留气泡）

    EnsureMsgWindow();
    g_sending = true;
    std::vector<Msg> snapshot = g_msgs;
    HWND hw = g_msgHwnd;
    std::thread([cfg, snapshot, hw]() {
        std::string url  = BuildChatUrl(cfg.apiUrl);
        std::string body = BuildRequestBody(cfg, snapshot);
        HttpResult r = HttpPostJson(url, body, WideToUtf8(cfg.apiKey));
        std::wstring err;
        std::wstring reply = ExtractReply(r, err);
        ReplyData* rd = new ReplyData();
        rd->ok = err.empty();
        rd->text = err.empty() ? reply : err;
        PostMessageW(hw, WM_CHAT_REPLY, 0, (LPARAM)rd);
    }).detach();
}

static void DoSend()
{
    if (!g_inputHwnd || !IsWindow(g_inputHwnd) || !g_editHwnd) return;
    // 动态读取完整文本（多行长内容不会被 4096 截断）
    int len = GetWindowTextLengthW(g_editHwnd);
    std::wstring text;
    if (len > 0) {
        text.resize(len);
        GetWindowTextW(g_editHwnd, &text[0], len + 1);
    }
    size_t a = text.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return;
    text = text.substr(a);
    if (g_sending) return;   // 上一条还在回复中

    PostMessageW(g_inputHwnd, WM_CLOSE, 0, 0);   // 回车后立即关闭输入条
    SendUserMessageAsync(text);
}

LRESULT CALLBACK InputBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_inputHwnd = hwnd;
        g_hover = g_press = 0;
        SetTimer(hwnd, 1, 120, NULL);   // 跟随桌宠移动（含跨屏）
        return 0;
    case WM_TIMER:
        if (wp == 1) PositionInputBar();   // 桌宠被拖走/跨屏时输入条跟着走
        return 0;
    case WM_COMMAND:
        // 编辑框内容变化：输入条随行数自动变高
        if (HIWORD(wp) == EN_CHANGE && (HWND)lp == g_editHwnd) {
            ResizeInputBar();
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PET_CLICKAWAY:
        // 点击输入条外任意区域：取消（关闭输入条，不发送消息）
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_PAINT:
        PaintInputBar(hwnd);
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = 0;
        if (PtInRect(&kSetBtn, pt)) h = 1;
        else if (PtInRect(&kSendBtn, pt)) h = 2;
        if (h != g_hover) { g_hover = h; InvalidateRect(hwnd, NULL, FALSE); }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hover) { g_hover = 0; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = 0;
        if (PtInRect(&kSetBtn, pt)) h = 1;
        else if (PtInRect(&kSendBtn, pt)) h = 2;
        if (h == 0) {
            // 点击输入条"空白处"（不在设置/发送按钮上；编辑框区域由子控件自行处理，不会进到这里）：
            // 视为想关闭输入框，取消本次输入
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        g_press = h;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = 0;
        if (PtInRect(&kSetBtn, pt)) h = 1;
        else if (PtInRect(&kSendBtn, pt)) h = 2;
        int p = g_press;
        g_press = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        if (p && p == h) {
            if (p == 1) {
                ShowAISettingsDialog(g_hInst, hwnd, false);
            } else if (p == 2) {
                DoSend();
            }
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(51, 51, 51));
        SetBkColor(hdc, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        g_inputHwnd = NULL;
        g_editHwnd = NULL;
        g_lastRgn = NULL;   // 圆角区域由系统在窗口销毁时释放
        SetClickAwayTarget(NULL);   // 输入条关闭后卸载钩子
        SetPetFrozen(false);   // 输入条关闭后解除一部分冻结（思考中的冻结由气泡管理）
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// AI 设置对话框
const int DLG_W = 430, DLG_H = 566;

const RECT kSClose  = { 384, 14, 420, 50 };
const RECT kSCancel = { 40,  512, 144, 558 };
const RECT kSDelete = { 158, 512, 268, 558 };   // 删除记忆（红色）
const RECT kSSave   = { 282, 512, 396, 558 };

const RECT kFieldBoxes[5] = {
    { 34, 112, 396, 146 },   // API 地址
    { 34, 180, 396, 214 },   // API 密钥
    { 34, 248, 396, 282 },   // 模型名称
    { 34, 316, 296, 350 },   // 记忆长度
    { 34, 384, 396, 486 },   // 角色提示词
};
const RECT kFieldEdits[5] = {
    { 42, 118, 380, 136 },
    { 42, 186, 380, 204 },
    { 42, 254, 380, 272 },
    { 42, 322, 284, 340 },
    { 42, 390, 380, 474 },
};
const int kLabY[5] = { 90, 158, 226, 294, 360 };
const wchar_t* kLabels[5] = {
    L"API 地址", L"API 密钥（可留空）", L"模型名称", L"记忆长度 (KB)", L"角色提示词"
};

struct SettingsState {
    AIConfig cfg;
    HWND edits[5] = {};
    int hover = 0, press = 0;   // 1取消 2保存 3关闭 4删除记忆
    bool saved = false;
};

static WNDPROC g_setupEditProc = NULL;
const UINT WM_SETUP_SAVE = WM_APP + 3;

static void DoSave(HWND hwnd, SettingsState* st)
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
    st->cfg.apiUrl = TrimW(get(st->edits[0]));
    st->cfg.apiKey = TrimW(get(st->edits[1]));
    st->cfg.model  = TrimW(get(st->edits[2]));
    st->cfg.prompt = get(st->edits[4]);
    if (st->cfg.apiUrl.empty() || st->cfg.model.empty()) {
        MessageBoxW(hwnd, L"请填写 API 地址和模型名称。", L"AI 设置",
                    MB_ICONWARNING | MB_OK);
        return;
    }
    std::wstring mem = TrimW(get(st->edits[3]));
    st->cfg.memoryKB = mem.empty() ? 16 : _wtoi(mem.c_str());
    if (st->cfg.memoryKB < 1) st->cfg.memoryKB = 1;
    if (st->cfg.memoryKB > 8192) st->cfg.memoryKB = 8192;
    SaveAIConfig(st->cfg);
    st->saved = true;
    DestroyWindow(hwnd);
}

LRESULT CALLBACK SetupEditProc(HWND e, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE) {
            PostMessageW(GetParent(e), WM_CLOSE, 0, 0);
            return 0;
        }
        if (wp == VK_RETURN && !(GetKeyState(VK_PROCESSKEY) & 0x8000)) {
            LONG style = GetWindowLongW(e, GWL_STYLE);
            if (!(style & ES_MULTILINE)) {
                PostMessageW(GetParent(e), WM_SETUP_SAVE, 0, 0);
                return 0;
            }
        }
    }
    return CallWindowProcW(g_setupEditProc, e, msg, wp, lp);
}

// 方块按钮，只倒小圆角；danger=true 时主按钮显示为红色（危险操作）
static void DrawSButton(Gdiplus::Graphics& g, SettingsState* st, int id,
                        const RECT& rc, const wchar_t* text, bool primary, bool danger = false)
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
        // 天蓝色主按钮；danger=true 时切换为红色
        Color c = danger ? Color(255, 225, 72, 72) : Color(255, 64, 158, 255);
        if (press) c = danger ? Color(255, 200, 52, 52) : Color(255, 44, 138, 235);
        else if (hover) c = danger ? Color(255, 240, 110, 110) : Color(255, 100, 180, 255);
        g.FillPath(&SolidBrush(c), &p);
        g.DrawString(text, -1, &f, RectF(x, y, w, h), &sf,
                     &SolidBrush(Color(255, 255, 255, 255)));
    } else {
        g.DrawPath(&Pen(Color(255, 160, 160, 160), 1.5f), &p);
        if (press) g.FillPath(&SolidBrush(Color(70, 0, 0, 0)), &p);
        else if (hover) g.FillPath(&SolidBrush(Color(36, 0, 0, 0)), &p);
        g.DrawString(text, -1, &f, RectF(x, y, w, h), &sf,
                     &SolidBrush(Color(255, 90, 90, 90)));
    }
}

static void PaintSettings(HWND hwnd, SettingsState* st)
{
    using namespace G;
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // 离屏缓冲绘制，最后一次性 BitBlt 上屏，避免悬停按钮等触发重绘时窗口闪烁
    Bitmap bmp(W, H, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(255, 255, 255, 255));

    g.DrawString(L"AI 设置", -1,
        &Font(L"Microsoft YaHei UI", 21, FontStyleBold, UnitPixel), PointF(30, 18),
        &SolidBrush(Color(255, 51, 51, 51)));
    g.DrawString(L"配置模型接口与角色提示词，保存后即可开始聊天", -1,
        &Font(L"Microsoft YaHei UI", 12, FontStyleRegular, UnitPixel), PointF(30, 54),
        &SolidBrush(Color(255, 150, 150, 150)));

    Font labF(L"Microsoft YaHei UI", 12, FontStyleRegular, UnitPixel);
    SolidBrush labC(Color(255, 130, 130, 130));
    for (int i = 0; i < 5; i++)
        g.DrawString(kLabels[i], -1, &labF, PointF(34, (float)kLabY[i]), &labC);

    for (int i = 0; i < 5; i++) {
        GraphicsPath fp;
        AddRoundRectPath(fp, (float)kFieldBoxes[i].left, (float)kFieldBoxes[i].top,
            (float)(kFieldBoxes[i].right - kFieldBoxes[i].left),
            (float)(kFieldBoxes[i].bottom - kFieldBoxes[i].top), 9);
        g.FillPath(&SolidBrush(Color(255, 255, 255, 255)), &fp);
        g.DrawPath(&Pen(Color(255, 226, 226, 226), 1.4f), &fp);
    }

    g.DrawString(L"KB", -1, &Font(L"Microsoft YaHei UI", 13, FontStyleRegular, UnitPixel),
        PointF(300, 320), &SolidBrush(Color(255, 150, 150, 150)));

    g.DrawString(L"提示词会始终随每条消息发送给 AI，记忆长度控制历史上下文大小", -1,
        &Font(L"Microsoft YaHei UI", 11, FontStyleRegular, UnitPixel), PointF(34, 494),
        &SolidBrush(Color(255, 170, 170, 170)));

    DrawSButton(g, st, 1, kSCancel, L"取消", false);
    DrawSButton(g, st, 2, kSSave, L"保存", true);
    DrawSButton(g, st, 4, kSDelete, L"删除记忆", true, true);   // 红色危险按钮

    bool hov = (st->hover == 3), pr = (st->press == 3);
    if (hov || pr) {
        g.FillEllipse(&SolidBrush(pr ? Color(80, 0, 0, 0) : Color(36, 0, 0, 0)),
            384, 14, 36, 36);
    }
    Font xf(L"Microsoft YaHei UI", 17, FontStyleRegular, UnitPixel);
    StringFormat xsf;
    xsf.SetAlignment(StringAlignmentCenter);
    xsf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"\u00D7", -1, &xf, RectF(382, 12, 40, 40), &xsf,
                 &SolidBrush(Color(255, 140, 140, 140)));

    // 离屏缓冲一次性上屏，消除逐笔绘制造成的闪烁
    HDC mdc = CreateCompatibleDC(ps.hdc);
    HBITMAP hb = NULL;
    bmp.GetHBITMAP(Color(255, 255, 255, 255), &hb);
    HGDIOBJ old = SelectObject(mdc, hb);
    BitBlt(ps.hdc, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, old);
    DeleteObject(hb);
    DeleteDC(mdc);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK SetupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SettingsState* st = (SettingsState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        st = (SettingsState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        for (int i = 0; i < 5; i++) {
            DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
            // 角色提示词：多行 + 自动垂直滚动 + 垂直滚动条，内容多时可拖滚动条或用鼠标滚轮查看
            style |= (i == 4) ? (ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL)
                              : ES_AUTOHSCROLL;
            st->edits[i] = CreateWindowExW(0, L"EDIT", L"", style,
                kFieldEdits[i].left, kFieldEdits[i].top,
                kFieldEdits[i].right - kFieldEdits[i].left,
                kFieldEdits[i].bottom - kFieldEdits[i].top,
                hwnd, NULL, g_hInst, NULL);
            SendMessageW(st->edits[i], WM_SETFONT, (WPARAM)GetUIFont(), TRUE);
            g_setupEditProc = (WNDPROC)SetWindowLongPtrW(st->edits[i], GWLP_WNDPROC,
                                                         (LONG_PTR)SetupEditProc);
        }
        SetWindowTextW(st->edits[0], st->cfg.apiUrl.c_str());
        SetWindowTextW(st->edits[1], st->cfg.apiKey.c_str());
        SetWindowTextW(st->edits[2], st->cfg.model.c_str());
        SetWindowTextW(st->edits[3], std::to_wstring(st->cfg.memoryKB).c_str());
        SetWindowTextW(st->edits[4], st->cfg.prompt.c_str());
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (st) PaintSettings(hwnd, st);
        return 0;
    case WM_MOUSEMOVE: {
        if (!st) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = 0;
        if (PtInRect(&kSClose, pt)) h = 3;
        else if (PtInRect(&kSCancel, pt)) h = 1;
        else if (PtInRect(&kSSave, pt)) h = 2;
        else if (PtInRect(&kSDelete, pt)) h = 4;
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
        if (PtInRect(&kSClose, pt)) h = 3;
        else if (PtInRect(&kSCancel, pt)) h = 1;
        else if (PtInRect(&kSSave, pt)) h = 2;
        else if (PtInRect(&kSDelete, pt)) h = 4;
        st->press = h;
        if (h) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!st) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = 0;
        if (PtInRect(&kSClose, pt)) h = 3;
        else if (PtInRect(&kSCancel, pt)) h = 1;
        else if (PtInRect(&kSSave, pt)) h = 2;
        else if (PtInRect(&kSDelete, pt)) h = 4;
        int p = st->press;
        st->press = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        if (p && p == h) {
            if (p == 1 || p == 3) DestroyWindow(hwnd);   // 取消 / 关闭 × = 不保存
            else if (p == 2) DoSave(hwnd, st);           // 保存
            else if (p == 4) {                           // 删除记忆
                if (g_sending) {
                    ShowPetBubble(L"AI 正在回复中，等它说完再删除记忆吧~", false, true);
                    return 0;
                }
                // 先弹出同款确认框，让用户确认是否删除
                bool del = ConfirmDialogEx(g_hInst, hwnd, L"确定要删除全部聊天记忆吗？",
                                           L"删除后所有对话上下文都会被清空，且无法恢复",
                                           L"取消", L"删除", true);
                if (del) {
                    g_msgs.clear();       // 清空内存中的上下文
                    SaveHistory();        // 清空 chat_history.json
                    g_mood = kMoodDefault;   // 记忆清空，心情也回到初始值 50
                    SaveMood();
                    SetMoodFace(kMoodFaceCalm);   // 表情回归平静，与心情 50 匹配
                    ShowPetBubble(L"聊天记忆已经清空啦~", false, true);
                }
            }
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(51, 51, 51));
        SetBkColor(hdc, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_SETUP_SAVE:
        if (st) DoSave(hwnd, st);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

// 共享接口（供开始界面高级选项的内联设置使用）
AIConfig LoadAIConfigShared() { return LoadAIConfig(); }
void SaveAIConfigShared(const AIConfig& cfg) { SaveAIConfig(cfg); }
HFONT GetUIFontShared() { return GetUIFont(); }

// 对外接口
// 首次使用（或文件缺失）时一次性创建聊天相关配置文件，不依赖用户后续操作：
//  ai_config.json = 默认空配置（api_url/model 为空 = 未启用 API 聊天，首次聊天会引导配置）
//  chat_history.json = 空的对话历史
void EnsureChatDataFiles()
{
    std::wstring dir = GetExeDir();

    std::wstring aiPath = dir + L"\\ai_config.json";
    if (GetFileAttributesW(aiPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::string s = "{\n"
                        "  \"api_url\": \"\",\n"
                        "  \"api_key\": \"\",\n"
                        "  \"model\": \"\",\n"
                        "  \"prompt\": \"\",\n"
                        "  \"memory_kb\": 16\n"
                        "}\n";
        HANDLE h = CreateFileW(aiPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            std::string bom = "\xEF\xBB\xBF";
            DWORD wr;
            WriteFile(h, bom.data(), (DWORD)bom.size(), &wr, NULL);
            WriteFile(h, s.data(), (DWORD)s.size(), &wr, NULL);
            CloseHandle(h);
        }
    }

    std::wstring histPath = dir + L"\\chat_history.json";
    if (GetFileAttributesW(histPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::string s = "{\"messages\":[]}";
        HANDLE h = CreateFileW(histPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            std::string bom = "\xEF\xBB\xBF";
            DWORD wr;
            WriteFile(h, bom.data(), (DWORD)bom.size(), &wr, NULL);
            WriteFile(h, s.data(), (DWORD)s.size(), &wr, NULL);
            CloseHandle(h);
        }
    }

    // 把上次的聊天记录读回内存：程序启动时从 chat_history.json 加载历史上下文，
    // 这样退出重启后 AI 依然记得之前的对话（而非每次都是空会话）
    LoadHistory();
}

bool IsAIConfigured()
{
    AIConfig c = LoadAIConfig();
    return !c.apiUrl.empty() && !c.model.empty();
}

int ShowAISettingsDialog(HINSTANCE hInst, HWND parent, bool firstRun)
{
    SettingsState st;
    st.cfg = LoadAIConfig();

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SetupProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"PetAISetupClass";
        RegisterClassExW(&wc);
        s_registered = true;
    }

    // 以桌宠/输入条所在显示器的工作区居中：桌宠在副屏时设置窗口也出现在副屏
    RECT pr;
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
    RECT wa = MonitorWorkAreaNear((pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2);
    int x = wa.left + ((wa.right - wa.left) - DLG_W) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - DLG_H) / 2;

    HWND dlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PetAISetupClass", L"", WS_POPUP | WS_CLIPCHILDREN,
        x, y, DLG_W, DLG_H, parent, NULL, hInst, &st);
    if (!dlg) return 0;
    SetPetFrozen(true);   // 设置界面打开期间桌宠停止走动
    // 接管钩子目标：主要是防止"从输入条打开设置"时，输入条在设置期间被点外部误关；
    // 设置窗口本身不响应点击外部（WM_PET_CLICKAWAY 落到默认处理，不做任何事）
    SetClickAwayTarget(dlg);
    SetWindowRgn(dlg, CreateRoundRectRgn(0, 0, DLG_W + 1, DLG_H + 1, 12, 12), TRUE);

    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    SetFocus(st.edits[0]);

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    SetPetFrozen(false);
    SetClickAwayTarget(NULL);   // 设置关闭后卸载钩子
    return st.saved ? 1 : 0;
}

void OpenChatWindow(HINSTANCE hInst)
{
    if (g_inputHwnd && IsWindow(g_inputHwnd)) {
        SetForegroundWindow(g_inputHwnd);
        return;
    }

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = InputBarProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"PetInputBarClass";
        RegisterClassExW(&wc);
        s_registered = true;
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PetInputBarClass", L"", WS_POPUP | WS_CLIPCHILDREN,
        0, 0, IB_W, IB_H, NULL, NULL, hInst, NULL);
    if (!hwnd) return;
    g_inputHwnd = hwnd;
    g_lastRgn = CreateRoundRectRgn(0, 0, IB_W + 1, IB_H + 1, 12, 12);
    SetWindowRgn(hwnd, g_lastRgn, TRUE);
    PositionInputBar();   // 定位到桌宠旁边（含跨显示器跟随）

    g_editHwnd = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        kEditField.left + 6, kEditField.top + 7,
        (kEditField.right - kEditField.left) - 12,
        g_editH,
        hwnd, NULL, hInst, NULL);
    SendMessageW(g_editHwnd, WM_SETFONT, (WPARAM)GetUIFont(), TRUE);
    g_editProc = (WNDPROC)SetWindowLongPtrW(g_editHwnd, GWLP_WNDPROC, (LONG_PTR)EditSubProc);

    SetPetFrozen(true);   // 输入条打开期间桌宠停止走动
    SetClickAwayTarget(hwnd);  // 点击输入条外部任意区域时关闭
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(g_editHwnd);
}

// 喂食提示气泡（与聊天回复同款样式）
// 在桌宠头顶显示气泡：喂食反馈（找不到文件 / 文件夹咬不动 / 吃饱了）也复用聊天气泡，
// 显示期间桌宠停止走动并摆出说话姿势，气泡自动消失后恢复
void ShowPetBubble(const std::wstring& text, bool isError, bool transient)
{
    SetPetFrozen(true);   // 气泡显示期间停止走动
    SetPetPose(2);        // 说话比画姿势
    ShowBubble(text, isError, transient);
}

// 喂食后的 AI 通知
// 桌宠吃完文件后调用：若启用了 API 聊天，就把喂食信息发给 AI 并显示回复气泡；
// 未启用 API 聊天则什么都不发生。
void NotifyAIFed(int gain, int current, int moodGain)
{
    if (!IsAIConfigured()) return;   // 未启用 API 聊天：什么都不做

    std::wstring text = L"【喂食通知】用户刚才喂了你食物，这次吃东西回复了 " +
        std::to_wstring(gain) + L" 点饥饿值，也让你开心了一些，心情值增加了 " +
        std::to_wstring(moodGain) + L" 点（心情值上限 " +
        std::to_wstring(kMoodMax) + L"，当前心情值 " +
        std::to_wstring(g_mood) + L"）。你的饥饿值上限是 " +
        std::to_wstring(kHungerMax) + L"，当前的饥饿值是 " +
        std::to_wstring(current) + L"/" + std::to_wstring(kHungerMax) +
        L"。请在回应中说出你现在的饥饿值，开心地表达感谢，可以稍微傲娇一点，并用角色的口吻回应。";
    SendUserMessageAsync(text);
}

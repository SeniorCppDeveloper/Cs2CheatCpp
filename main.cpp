#include <Windows.h>
#include <windowsx.h>
#include <TlHelp32.h>
#include <dwmapi.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

#define OFFSET_DW_ENTITY_LIST             0x2546BC0
#define OFFSET_DW_VIEW_MATRIX             0x2565A10
#define OFFSET_DW_LOCAL_PLAYER_PAWN       0x2560698
#define OFFSET_DW_LOCAL_PLAYER_CONTROLLER 0x2537A08
#define OFFSET_DW_VIEW_ANGLES             0x25767D8
#define OFFSET_DW_CSGO_INPUT              0x2576150
#define OFFSET_DW_GLOBAL_VARS             0x222BED8

#define OFFSET_M_HEALTH                   0x34C
#define OFFSET_M_LIFESTATE                0x354
#define OFFSET_M_TEAM                     0x3E7
#define OFFSET_M_FLAGS                    0x3F4
#define OFFSET_M_VEC_VELOCITY             0x430
#define OFFSET_M_H_PLAYER_PAWN            0x92C
#define OFFSET_M_PGAMESCENENODE           0x330
#define OFFSET_M_VEC_ORIGIN_IN_SCENENODE  0x80
#define OFFSET_M_ISCOPEN                  0x1EA0
#define OFFSET_M_ARMOR                    0x1ECC
#define OFFSET_M_VIEW_ANGLE               0x2580

HWND overlayWnd = NULL;
HWND gameWnd = NULL;
HANDLE gameProcess = NULL;
uintptr_t clientBase = 0;
bool running = true;
bool menuOpen = false;
bool settingsOpen = false;

int MENU_X = 200;
int MENU_Y = 200;
int SETTINGS_X = 540;
int SETTINGS_Y = 200;
const int MENU_W = 800;
const int MENU_H = 460;
const int TITLE_H = 36;
const int PAD = 14;
const int COL_W = (MENU_W - PAD * 3) / 2;
const int ROW_H = 30;
const int ROW_GAP = 5;

const int SET_W = 260;
const int SET_H = 520;
const int SET_ROW_GAP = 4;

bool dragging = false;
int dragOffsetX = 0;
int dragOffsetY = 0;

bool draggingSettings = false;
int dragSettingsOffsetX = 0;
int dragSettingsOffsetY = 0;

float menuAnim = 0.0f;
float settingsAnim = 0.0f;

bool cursorOverMenu = false;

float fps = 0.0f;
int frameCount = 0;
DWORD lastFpsTime = 0;

POINT lastMousePos = { -1, -1 };

struct MenuItem {
    RECT rect;
    int id;
};

std::vector<MenuItem> menuItems;

struct TrailPoint {
    float x;
    float y;
    float life;
    int r, g, b;
};

std::vector<TrailPoint> mouseTrail;

struct Sparkle {
    float x, y;
    float vx, vy;
    float life;
    float maxLife;
    int r, g, b;
    float size;
};

std::vector<Sparkle> sparkles;

struct Ripple {
    float x, y;
    float life;
    int r, g, b;
};

std::vector<Ripple> ripples;

struct Star {
    float x, y;
    float twinkle;
    float speed;
};

std::vector<Star> stars;

struct Beam {
    float y;
    float life;
    float width;
    int r, g, b;
};

std::vector<Beam> beams;

float menuTime = 0.0f;
float hueShift = 0.0f;

HDC memDC = NULL;
HBITMAP memBitmap = NULL;
HBITMAP oldBitmap = NULL;
int screenW = 0, screenH = 0;

struct Color3 {
    int r, g, b;
};

const Color3 COLOR_PALETTE[] = {
    {255, 60, 60},
    {255, 130, 60},
    {255, 220, 60},
    {100, 255, 100},
    {60, 220, 255},
    {100, 130, 255},
    {200, 100, 255},
    {255, 100, 200},
    {255, 255, 255},
    {180, 180, 180},
    {120, 80, 40},
    {40, 40, 40}
};
const int COLOR_COUNT = 12;

struct Settings {
    bool esp = true;
    bool bhop = true;
    bool box = true;
    bool line = true;
    bool health = true;
    bool armor = true;
    bool name = true;
    bool distance = true;
    bool snaplines = true;
    bool headDot = true;
    bool glow = false;
    bool triggerbot = false;
    bool silentAim = false;
    bool radar = true;
    bool crosshair = false;
    bool mouseTrail = true;
    bool particles = true;
    bool rgbMode = false;
    bool starsBg = true;
    bool beamsBg = true;
    bool soundFx = false;
    bool cursorGlow = true;

    int espColorMode = 0;
    int espColorIdx = 0;
    int trailColorIdx = 4;
    int menuAccentIdx = 4;

    float glowIntensity = 0.8f;
    int triggerDelay = 0;
    int aimFov = 30;
    float aimSmoothness = 5.0f;
};

Settings settings;

Color3 HSVtoRGB(float h, float s, float v) {
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    float r = 0, g = 0, b = 0;

    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }

    Color3 c3;
    c3.r = (int)((r + m) * 255);
    c3.g = (int)((g + m) * 255);
    c3.b = (int)((b + m) * 255);
    return c3;
}

Color3 GetAccent() {
    if (settings.rgbMode) {
        return HSVtoRGB(fmodf(hueShift, 360.0f), 1.0f, 1.0f);
    }
    return COLOR_PALETTE[settings.menuAccentIdx];
}

Color3 GetTrailColor() {
    if (settings.rgbMode) {
        return HSVtoRGB(fmodf(hueShift + 60.0f, 360.0f), 1.0f, 1.0f);
    }
    return COLOR_PALETTE[settings.trailColorIdx];
}

Color3 GetEspColor() {
    if (settings.rgbMode) {
        return HSVtoRGB(fmodf(hueShift + 180.0f, 360.0f), 1.0f, 1.0f);
    }
    return COLOR_PALETTE[settings.espColorIdx];
}

struct Vector3 {
    float x, y, z;

    float DistTo(const Vector3& other) const {
        return sqrtf((other.x - x) * (other.x - x) +
            (other.y - y) * (other.y - y) +
            (other.z - z) * (other.z - z));
    }
};

struct PlayerData {
    uintptr_t controller;
    uintptr_t pawn;
    Vector3 position;
    Vector3 headPos;
    int health;
    int armor;
    int team;
    bool alive;
    bool spotted;
    float distance;
    char name[64];
};

std::vector<PlayerData> players;

DWORD GetProcessId(const wchar_t* name) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snapshot, &pe)) {
        do { if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; } } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return pid;
}

uintptr_t GetModuleBase(DWORD pid, const wchar_t* module) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32W me = { sizeof(me) };
    uintptr_t base = 0;
    if (Module32FirstW(snapshot, &me)) {
        do {
            if (_wcsicmp(me.szModule, module) == 0) {
                base = (uintptr_t)me.modBaseAddr;
                break;
            }
        } while (Module32NextW(snapshot, &me));
    }
    CloseHandle(snapshot);
    return base;
}

template<typename T>
T ReadMem(uintptr_t address) {
    T buffer = {};
    ReadProcessMemory(gameProcess, (LPCVOID)address, &buffer, sizeof(T), NULL);
    return buffer;
}

template<typename T>
bool WriteMem(uintptr_t address, T value) {
    return WriteProcessMemory(gameProcess, (LPVOID)address, &value, sizeof(T), NULL);
}

float viewMatrix[16];

void UpdateViewMatrix() {
    uintptr_t viewMatrixAddr = clientBase + OFFSET_DW_VIEW_MATRIX;
    ReadProcessMemory(gameProcess, (LPCVOID)viewMatrixAddr, &viewMatrix, sizeof(viewMatrix), NULL);
}

Vector3 GetPlayerPos(uintptr_t pawn) {
    uintptr_t sceneNode = ReadMem<uintptr_t>(pawn + OFFSET_M_PGAMESCENENODE);
    if (!sceneNode) return { 0, 0, 0 };
    return ReadMem<Vector3>(sceneNode + OFFSET_M_VEC_ORIGIN_IN_SCENENODE);
}

bool WorldToScreen(Vector3 world, Vector3& screen, int screenWidth, int screenHeight) {
    float w = viewMatrix[12] * world.x + viewMatrix[13] * world.y + viewMatrix[14] * world.z + viewMatrix[15];
    if (w < 0.01f) return false;

    float invW = 1.0f / w;
    screen.x = (viewMatrix[0] * world.x + viewMatrix[1] * world.y + viewMatrix[2] * world.z + viewMatrix[3]) * invW;
    screen.y = (viewMatrix[4] * world.x + viewMatrix[5] * world.y + viewMatrix[6] * world.z + viewMatrix[7]) * invW;

    screen.x = (screenWidth / 2) + (screen.x * screenWidth / 2);
    screen.y = (screenHeight / 2) - (screen.y * screenHeight / 2);

    return true;
}

void InitBackBuffer() {
    screenW = GetSystemMetrics(SM_CXSCREEN);
    screenH = GetSystemMetrics(SM_CYSCREEN);

    HDC hdc = GetDC(overlayWnd);
    memDC = CreateCompatibleDC(hdc);
    memBitmap = CreateCompatibleBitmap(hdc, screenW, screenH);
    oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    ReleaseDC(overlayWnd, hdc);
}

void ShutdownBackBuffer() {
    if (memDC) {
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        memDC = NULL;
    }
}

void ClearScreen() {
    RECT full = { 0, 0, screenW, screenH };
    HBRUSH brush = CreateSolidBrush(RGB(1, 1, 1));
    FillRect(memDC, &full, brush);
    DeleteObject(brush);
}

void Present() {
    HDC hdc = GetDC(overlayWnd);
    BitBlt(hdc, 0, 0, screenW, screenH, memDC, 0, 0, SRCCOPY);
    ReleaseDC(overlayWnd, hdc);
}

void DrawFilledRect(int x, int y, int w, int h, int r, int g, int b, int a = 255) {
    RECT rect = { x, y, x + w, y + h };
    HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
    FillRect(memDC, &rect, brush);
    DeleteObject(brush);
}

void DrawRect(int x, int y, int w, int h, int r, int g, int b, int thickness = 2) {
    HPEN pen = CreatePen(PS_SOLID, thickness, RGB(r, g, b));
    HBRUSH brush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, brush);
    Rectangle(memDC, x, y, x + w, y + h);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(pen);
}

void DrawLine(int x1, int y1, int x2, int y2, int r, int g, int b, int thickness = 1) {
    HPEN pen = CreatePen(PS_SOLID, thickness, RGB(r, g, b));
    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
    MoveToEx(memDC, x1, y1, NULL);
    LineTo(memDC, x2, y2);
    SelectObject(memDC, oldPen);
    DeleteObject(pen);
}

void DrawText(int x, int y, const char* text, int r, int g, int b, int size = 12) {
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(r, g, b));
    HFONT font = CreateFontA(size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(memDC, font);
    TextOutA(memDC, x, y, text, (int)strlen(text));
    SelectObject(memDC, oldFont);
    DeleteObject(font);
}

void DrawCircle(int x, int y, int radius, int r, int g, int b) {
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(r, g, b));
    HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, brush);
    Ellipse(memDC, x - radius, y - radius, x + radius, y + radius);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawCircleOutline(int x, int y, int radius, int r, int g, int b, int thickness = 1) {
    HPEN pen = CreatePen(PS_SOLID, thickness, RGB(r, g, b));
    HBRUSH brush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, brush);
    Ellipse(memDC, x - radius, y - radius, x + radius, y + radius);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(pen);
}

void DrawFilledCircle(int x, int y, int radius, int r, int g, int b) {
    HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, brush);
    Ellipse(memDC, x - radius, y - radius, x + radius, y + radius);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawRoundedRect(int x, int y, int w, int h, int radius, int r, int g, int b) {
    HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, brush);
    RoundRect(memDC, x, y, x + w, y + h, radius, radius);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawRoundedRectOutline(int x, int y, int w, int h, int radius, int r, int g, int b, int thickness = 1) {
    HPEN pen = CreatePen(PS_SOLID, thickness, RGB(r, g, b));
    HBRUSH brush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, brush);
    RoundRect(memDC, x, y, x + w, y + h, radius, radius);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(pen);
}

float EaseOutCubic(float t) {
    return 1.0f - powf(1.0f - t, 3.0f);
}

void UpdateFPS() {
    frameCount++;
    DWORD now = GetTickCount();
    if (now - lastFpsTime >= 500) {
        fps = frameCount * 1000.0f / (now - lastFpsTime);
        frameCount = 0;
        lastFpsTime = now;
    }
}

void PlayClickSound() {
    if (settings.soundFx) {
        Beep(1200, 30);
    }
}

void InitStars() {
    stars.clear();
    for (int i = 0; i < 120; i++) {
        Star s;
        s.x = (float)(rand() % screenW);
        s.y = (float)(rand() % screenH);
        s.twinkle = (float)(rand() % 100) / 100.0f;
        s.speed = 0.005f + (rand() % 30) / 1000.0f;
        stars.push_back(s);
    }
}

void SpawnSparkle(float x, float y, int r, int g, int b) {
    if (sparkles.size() > 200) return;

    Sparkle s;
    s.x = x;
    s.y = y;
    float angle = (rand() % 360) * 3.14159f / 180.0f;
    float speed = 1.0f + (rand() % 100) / 50.0f;
    s.vx = cosf(angle) * speed;
    s.vy = sinf(angle) * speed;
    s.maxLife = 0.6f + (rand() % 60) / 100.0f;
    s.life = s.maxLife;
    s.r = r;
    s.g = g;
    s.b = b;
    s.size = 1.5f + (rand() % 20) / 10.0f;
    sparkles.push_back(s);
}

void SpawnBeam(int r, int g, int b) {
    if (beams.size() > 20) return;

    Beam bm;
    bm.y = (float)(rand() % 400);
    bm.life = 1.0f;
    bm.width = 40.0f + (rand() % 100);
    bm.r = r;
    bm.g = g;
    bm.b = b;
    beams.push_back(bm);
}

void UpdateParticles() {
    if (!settings.particles) {
        sparkles.clear();
        ripples.clear();
        return;
    }

    for (auto& s : sparkles) {
        s.x += s.vx;
        s.y += s.vy;
        s.vx *= 0.96f;
        s.vy *= 0.96f;
        s.vy += 0.08f;
        s.life -= 0.025f;
    }

    sparkles.erase(
        std::remove_if(sparkles.begin(), sparkles.end(),
            [](const Sparkle& s) { return s.life <= 0.0f; }),
        sparkles.end());

    for (auto& r : ripples) {
        r.life -= 0.04f;
    }

    ripples.erase(
        std::remove_if(ripples.begin(), ripples.end(),
            [](const Ripple& r) { return r.life <= 0.0f; }),
        ripples.end());
}

void UpdateStars() {
    if (!settings.starsBg) return;

    for (auto& s : stars) {
        s.twinkle += s.speed;
        if (s.twinkle > 1.0f) s.twinkle = 0.0f;
    }
}

void UpdateBeams() {
    if (!settings.beamsBg) {
        beams.clear();
        return;
    }

    for (auto& b : beams) {
        b.y += 0.8f;
        b.life -= 0.008f;
    }

    beams.erase(
        std::remove_if(beams.begin(), beams.end(),
            [](const Beam& b) { return b.life <= 0.0f || b.y > 500; }),
        beams.end());

    if ((rand() % 100) < 3) {
        Color3 a = GetAccent();
        SpawnBeam(a.r, a.g, a.b);
    }
}

void DrawStars(float anchorX, float anchorY, int w, int h) {
    if (!settings.starsBg) return;

    Color3 a = GetAccent();

    for (const auto& s : stars) {
        float tw = sinf(s.twinkle * 3.14159f * 2.0f);
        float alpha = 0.3f + 0.7f * fabsf(tw);

        int px = (int)(anchorX + fmodf(s.x, (float)w));
        int py = (int)(anchorY + fmodf(s.y, (float)h));

        if (px < anchorX || px > anchorX + w || py < anchorY || py > anchorY + h) continue;

        int r = (int)(a.r * alpha * 0.5f);
        int g = (int)(a.g * alpha * 0.5f);
        int b = (int)(a.b * alpha * 0.5f);

        if (r < 3) r = 3;
        if (g < 3) g = 3;
        if (b < 3) b = 3;

        int size = (int)(1 + fabsf(tw) * 2);
        DrawFilledRect(px, py, size, size, r, g, b);
    }
}

void DrawBeams(float anchorX, float anchorY, int w, int h) {
    if (!settings.beamsBg) return;

    for (const auto& b : beams) {
        float t = b.life;
        int by = (int)(anchorY + b.y);

        if (by < anchorY || by > anchorY + h) continue;

        int r = (int)(b.r * t * 0.15f);
        int g = (int)(b.g * t * 0.15f);
        int bb = (int)(b.b * t * 0.15f);

        if (r < 2) r = 2;
        if (g < 2) g = 2;
        if (bb < 2) bb = 2;

        DrawFilledRect((int)anchorX, by, w, (int)b.width, r, g, bb);
    }
}

void DrawParticles() {
    if (!settings.particles) return;

    for (const auto& s : sparkles) {
        float t = s.life / s.maxLife;
        int radius = (int)(s.size * t * 3.0f);
        if (radius < 1) radius = 1;

        int sr = (int)(s.r * t);
        int sg = (int)(s.g * t);
        int sb = (int)(s.b * t);

        if (sr < 15) sr = 15;
        if (sg < 15) sg = 15;
        if (sb < 15) sb = 15;

        DrawFilledCircle((int)s.x, (int)s.y, radius, sr, sg, sb);
    }

    for (const auto& r : ripples) {
        float t = r.life;
        int radius = (int)((1.0f - t) * 60.0f);

        int rr = (int)(r.r * t);
        int rg = (int)(r.g * t);
        int rb = (int)(r.b * t);

        if (rr < 20) rr = 20;
        if (rg < 20) rg = 20;
        if (rb < 20) rb = 20;

        DrawCircleOutline((int)r.x, (int)r.y, radius, rr, rg, rb, 2);
    }
}

void UpdateMouseTrail() {
    if (!settings.mouseTrail) {
        mouseTrail.clear();
        return;
    }

    POINT p;
    GetCursorPos(&p);
    ScreenToClient(overlayWnd, &p);

    for (auto& pt : mouseTrail) {
        pt.life -= 0.08f;
    }

    mouseTrail.erase(
        std::remove_if(mouseTrail.begin(), mouseTrail.end(),
            [](const TrailPoint& tp) { return tp.life <= 0.0f; }),
        mouseTrail.end());

    float dx = (float)(p.x - lastMousePos.x);
    float dy = (float)(p.y - lastMousePos.y);
    float moveDist = sqrtf(dx * dx + dy * dy);

    if (lastMousePos.x < 0 || moveDist > 2.0f) {
        Color3 c = GetTrailColor();
        TrailPoint tp;
        tp.x = (float)p.x;
        tp.y = (float)p.y;
        tp.life = 1.0f;
        tp.r = c.r;
        tp.g = c.g;
        tp.b = c.b;
        mouseTrail.push_back(tp);

        if (mouseTrail.size() > 22) {
            mouseTrail.erase(mouseTrail.begin());
        }

        if (settings.particles && moveDist > 6.0f && (rand() % 100) < 25) {
            SpawnSparkle((float)p.x, (float)p.y, c.r, c.g, c.b);
        }
    }

    lastMousePos = p;
}

void DrawMouseTrail() {
    if (!settings.mouseTrail) return;

    for (size_t i = 0; i < mouseTrail.size(); i++) {
        float life = mouseTrail[i].life;

        int radius = (int)(life * 4.0f);
        if (radius < 1) radius = 1;

        int tr = (int)(mouseTrail[i].r * life * life);
        int tg = (int)(mouseTrail[i].g * life * life);
        int tb = (int)(mouseTrail[i].b * life * life);

        if (tr < 20) tr = 20;
        if (tg < 20) tg = 20;
        if (tb < 20) tb = 20;

        int px = (int)mouseTrail[i].x;
        int py = (int)mouseTrail[i].y;

        if (i > 0) {
            int px2 = (int)mouseTrail[i - 1].x;
            int py2 = (int)mouseTrail[i - 1].y;
            int lineR = (int)(tr * life);
            int lineG = (int)(tg * life);
            int lineB = (int)(tb * life);
            if (lineR < 15) lineR = 15;
            if (lineG < 15) lineG = 15;
            if (lineB < 15) lineB = 15;
            DrawLine(px2, py2, px, py, lineR, lineG, lineB, radius);
        }
    }
}

void DrawCursorGlow() {
    if (!settings.cursorGlow) return;
    if (!menuOpen || menuAnim < 0.5f) return;

    POINT p;
    GetCursorPos(&p);
    ScreenToClient(overlayWnd, &p);

    if (p.x < MENU_X || p.x > MENU_X + MENU_W) return;
    if (p.y < MENU_Y || p.y > MENU_Y + MENU_H) return;

    Color3 a = GetAccent();

    for (int i = 6; i >= 1; i--) {
        int radius = i * 5;
        float alpha = (6 - i) / 6.0f * 0.3f;
        int r = (int)(a.r * alpha);
        int g = (int)(a.g * alpha);
        int b = (int)(a.b * alpha);
        if (r < 2) r = 2;
        if (g < 2) g = 2;
        if (b < 2) b = 2;
        DrawCircleOutline(p.x, p.y, radius, r, g, b, 2);
    }
}

void DrawCustomCursor() {
    if (!cursorOverMenu) return;

    POINT p;
    GetCursorPos(&p);
    ScreenToClient(overlayWnd, &p);

    Color3 a = GetAccent();

    DrawFilledCircle(p.x, p.y, 5, a.r, a.g, a.b);
    DrawFilledCircle(p.x, p.y, 2, 240, 245, 255);
    DrawCircleOutline(p.x, p.y, 8, a.r, a.g, a.b, 1);
}

void DrawHealthBar(int x, int y, int w, int h, int health) {
    int barHeight = (int)(h * (health / 100.0f));
    int red = 255 - (int)(health * 2.55f);
    int green = (int)(health * 2.55f);
    DrawFilledRect(x - 6, y + h - barHeight, 4, barHeight, red, green, 0);
    DrawFilledRect(x - 6, y, 4, h - barHeight, 255, 0, 0, 100);
}

void DrawArmorBar(int x, int y, int w, int h, int armor) {
    int barHeight = (int)(h * (armor / 100.0f));
    DrawFilledRect(x - 11, y + h - barHeight, 3, barHeight, 100, 150, 255);
}

void UpdateOverlayPosition() {
    static bool positioned = false;
    if (positioned) return;
    SetWindowPos(overlayWnd, HWND_TOPMOST, 0, 0,
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    positioned = true;
}

void UpdateCursorState() {
    POINT p;
    GetCursorPos(&p);

    bool over = false;

    if (menuOpen && menuAnim > 0.5f) {
        if (p.x >= MENU_X && p.x <= MENU_X + MENU_W &&
            p.y >= MENU_Y && p.y <= MENU_Y + MENU_H) {
            over = true;
        }
    }

    if (settingsOpen && menuAnim > 0.5f) {
        if (p.x >= SETTINGS_X && p.x <= SETTINGS_X + SET_W &&
            p.y >= SETTINGS_Y && p.y <= SETTINGS_Y + SET_H) {
            over = true;
        }
    }

    cursorOverMenu = over;
    ShowCursor(cursorOverMenu ? FALSE : TRUE);
}

void BunnyHop() {
    if (!settings.bhop) return;

    uintptr_t localPawn = ReadMem<uintptr_t>(clientBase + OFFSET_DW_LOCAL_PLAYER_PAWN);
    if (!localPawn) return;

    int flags = ReadMem<int>(localPawn + OFFSET_M_FLAGS);
    bool isOnGround = (flags & 1) != 0;

    if ((GetAsyncKeyState(VK_SPACE) & 0x8000) && isOnGround) {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_SPACE;
        SendInput(1, &input, sizeof(INPUT));
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void TriggerBot() {
    if (!settings.triggerbot) return;
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;

    uintptr_t localPawn = ReadMem<uintptr_t>(clientBase + OFFSET_DW_LOCAL_PLAYER_PAWN);
    if (!localPawn) return;

    int crosshairId = ReadMem<int>(localPawn + 0x33FC);
    if (crosshairId <= 0 || crosshairId > 64) return;

    uintptr_t entityList = ReadMem<uintptr_t>(clientBase + OFFSET_DW_ENTITY_LIST);
    if (!entityList) return;

    uintptr_t listEntry = ReadMem<uintptr_t>(entityList + (8 * (crosshairId & 0x7FFF) >> 9) + 16);
    if (!listEntry) return;

    uintptr_t playerController = ReadMem<uintptr_t>(listEntry + 120 * (crosshairId & 0x1FF));
    if (!playerController) return;

    uint32_t pawnHandle = ReadMem<uint32_t>(playerController + OFFSET_M_H_PLAYER_PAWN);
    if (!pawnHandle) return;

    uintptr_t listEntry2 = ReadMem<uintptr_t>(entityList + (8 * ((pawnHandle & 0x7FFF) >> 9) + 16));
    if (!listEntry2) return;

    uintptr_t pawn = ReadMem<uintptr_t>(listEntry2 + 120 * (pawnHandle & 0x1FF));
    if (!pawn) return;

    int health = ReadMem<int>(pawn + OFFSET_M_HEALTH);
    if (health <= 0) return;

    int localTeam = ReadMem<int>(localPawn + OFFSET_M_TEAM);
    int targetTeam = ReadMem<int>(pawn + OFFSET_M_TEAM);
    if (targetTeam == localTeam) return;

    if (settings.triggerDelay > 0) Sleep(settings.triggerDelay);

    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    Sleep(1);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
}

void SilentAim() {
    if (!settings.silentAim) return;
    if (!(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) return;

    uintptr_t localPawn = ReadMem<uintptr_t>(clientBase + OFFSET_DW_LOCAL_PLAYER_PAWN);
    if (!localPawn) return;

    Vector3 localPos = GetPlayerPos(localPawn);
    int localTeam = ReadMem<int>(localPawn + OFFSET_M_TEAM);

    PlayerData* target = nullptr;
    float closestFov = settings.aimFov;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    for (auto& player : players) {
        if (player.team == localTeam) continue;
        if (player.health <= 0) continue;

        Vector3 screenHead;
        if (!WorldToScreen(player.headPos, screenHead, sw, sh)) continue;

        float fov = sqrtf(pow(screenHead.x - sw / 2, 2) + pow(screenHead.y - sh / 2, 2));
        if (fov < closestFov) {
            closestFov = fov;
            target = &player;
        }
    }

    if (!target) return;

    Vector3 delta = { target->headPos.x - localPos.x, target->headPos.y - localPos.y, target->headPos.z - localPos.z };
    float distance = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (distance < 1.0f) return;

    float pitch = -asinf(delta.z / distance) * (180.0f / 3.14159265f);
    float yaw = atan2f(delta.y, delta.x) * (180.0f / 3.14159265f);

    Vector3 currentAngles = ReadMem<Vector3>(clientBase + OFFSET_DW_VIEW_ANGLES);

    float smoothFactor = 1.0f / settings.aimSmoothness;
    pitch = currentAngles.x + (pitch - currentAngles.x) * smoothFactor;
    yaw = currentAngles.y + (yaw - currentAngles.y) * smoothFactor;

    Vector3 newAngles = { pitch, yaw, 0 };

    WriteMem(clientBase + OFFSET_DW_VIEW_ANGLES, newAngles);
}

void UpdatePlayers() {
    players.clear();

    uintptr_t localPawn = ReadMem<uintptr_t>(clientBase + OFFSET_DW_LOCAL_PLAYER_PAWN);
    if (!localPawn) return;

    int localTeam = ReadMem<int>(localPawn + OFFSET_M_TEAM);
    Vector3 localPos = GetPlayerPos(localPawn);

    uintptr_t entityList = ReadMem<uintptr_t>(clientBase + OFFSET_DW_ENTITY_LIST);
    if (!entityList) return;

    for (int i = 1; i <= 64; i++) {
        uintptr_t listEntry = ReadMem<uintptr_t>(entityList + (8 * (i & 0x7FFF) >> 9) + 16);
        if (!listEntry) continue;

        uintptr_t playerController = ReadMem<uintptr_t>(listEntry + 120 * (i & 0x1FF));
        if (!playerController) continue;

        uint32_t pawnHandle = ReadMem<uint32_t>(playerController + OFFSET_M_H_PLAYER_PAWN);
        if (!pawnHandle || pawnHandle == -1) continue;

        uintptr_t listEntry2 = ReadMem<uintptr_t>(entityList + (8 * ((pawnHandle & 0x7FFF) >> 9) + 16));
        if (!listEntry2) continue;

        uintptr_t pawn = ReadMem<uintptr_t>(listEntry2 + 120 * (pawnHandle & 0x1FF));
        if (!pawn || pawn == localPawn) continue;

        int lifeState = ReadMem<int>(pawn + OFFSET_M_LIFESTATE);
        if (lifeState != 256) continue;

        int health = ReadMem<int>(pawn + OFFSET_M_HEALTH);
        if (health <= 0 || health > 100) continue;

        int team = ReadMem<int>(pawn + OFFSET_M_TEAM);

        PlayerData player;
        player.controller = playerController;
        player.pawn = pawn;
        player.position = GetPlayerPos(pawn);
        player.headPos = player.position;
        player.headPos.z += 70.0f;
        player.health = health;
        player.armor = ReadMem<int>(pawn + OFFSET_M_ARMOR);
        player.team = team;
        player.alive = true;
        player.distance = localPos.DistTo(player.position);

        ReadProcessMemory(gameProcess, (LPCVOID)(playerController + 0x6F4), player.name, sizeof(player.name), NULL);

        players.push_back(player);
    }

    std::sort(players.begin(), players.end(), [](const PlayerData& a, const PlayerData& b) {
        return a.distance < b.distance;
        });
}

void RenderESP() {
    if (!settings.esp) return;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    uintptr_t localPawn = ReadMem<uintptr_t>(clientBase + OFFSET_DW_LOCAL_PLAYER_PAWN);
    if (!localPawn) return;

    int localTeam = ReadMem<int>(localPawn + OFFSET_M_TEAM);

    UpdateViewMatrix();

    Color3 espC = GetEspColor();

    for (const auto& player : players) {
        bool isEnemy = (player.team != localTeam);

        int r = isEnemy ? espC.r : 100;
        int g = isEnemy ? espC.g : 255;
        int b = isEnemy ? espC.b : 100;

        switch (settings.espColorMode) {
        case 0:
            r = isEnemy ? espC.r : 100;
            g = isEnemy ? espC.g : 255;
            b = isEnemy ? espC.b : 100;
            break;
        case 1:
            r = 255; g = 255; b = 255;
            break;
        case 2:
            r = espC.r; g = espC.g; b = espC.b;
            break;
        }

        Vector3 screenPos, screenHead;
        if (!WorldToScreen(player.position, screenPos, sw, sh)) continue;
        if (!WorldToScreen(player.headPos, screenHead, sw, sh)) continue;

        float boxHeight = fabs(screenPos.y - screenHead.y);
        float boxWidth = boxHeight / 1.8f;
        if (boxHeight < 20) boxHeight = 20;
        if (boxHeight > 200) boxHeight = 200;
        if (boxWidth < 12) boxWidth = 12;

        int boxX = (int)(screenPos.x - boxWidth / 2);
        int boxY = (int)(screenHead.y);
        int boxW = (int)boxWidth;
        int boxH = (int)boxHeight;

        float pulse = 0.7f + 0.3f * sinf(menuTime * 3.0f + player.distance * 0.1f);

        if (settings.box) {
            int rr = (int)(r * pulse);
            int gg = (int)(g * pulse);
            int bb = (int)(b * pulse);

            DrawRect(boxX - 1, boxY - 1, boxW + 2, boxH + 2, 1, 1, 1, 1);
            DrawRect(boxX, boxY, boxW, boxH, rr, gg, bb, 2);
        }

        if (settings.health) {
            DrawHealthBar(boxX + boxW, boxY, boxW, boxH, player.health);
        }

        if (settings.armor && player.armor > 0) {
            DrawArmorBar(boxX + boxW, boxY, boxW, boxH, player.armor);
        }

        if (settings.snaplines) {
            DrawLine(sw / 2, sh, (int)screenPos.x, (int)screenPos.y, r, g, b, 1);
        }

        if (settings.headDot) {
            DrawCircle((int)screenHead.x, (int)screenHead.y, 3, r, g, b);
        }

        if (settings.line) {
            DrawLine((int)screenPos.x, (int)screenPos.y, (int)screenPos.x, sh, r, g, b, 1);
        }

        int textY = boxY - 18;

        if (settings.name && player.name[0]) {
            DrawText(boxX, textY, player.name, 255, 255, 255, 10);
            textY -= 14;
        }

        if (settings.distance) {
            char distText[32];
            sprintf_s(distText, "%.0fm", player.distance);
            DrawText(boxX + boxW / 2 - 20, boxY + boxH + 2, distText, 255, 255, 255, 10);
        }

        char hpText[16];
        sprintf_s(hpText, "%d", player.health);
        DrawText(boxX + boxW + 8, boxY + boxH - 15, hpText, r, g, b, 11);

        char colorChar[2] = { (char)(player.team == 2 ? 'T' : (player.team == 3 ? 'C' : '?')), 0 };
        DrawText(boxX - 15, boxY + boxH - 15, colorChar, r, g, b, 10);
    }
}

void DrawCrosshair() {
    if (!settings.crosshair) return;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int cx = sw / 2;
    int cy = sh / 2;

    Color3 a = GetAccent();

    DrawLine(cx - 10, cy, cx + 10, cy, a.r, a.g, a.b, 1);
    DrawLine(cx, cy - 10, cx, cy + 10, a.r, a.g, a.b, 1);
    DrawCircle(cx, cy, 4, a.r, a.g, a.b);
}

void DrawRadar() {
    if (!settings.radar) return;

    int radarX = GetSystemMetrics(SM_CXSCREEN) - 210;
    int radarY = 10;
    int radarSize = 200;

    Color3 a = GetAccent();

    DrawFilledRect(radarX, radarY, radarSize, radarSize, 0, 0, 0, 180);
    DrawRect(radarX, radarY, radarSize, radarSize, a.r / 2, a.g / 2, a.b / 2, 1);

    DrawLine(radarX + radarSize / 2, radarY, radarX + radarSize / 2, radarY + radarSize, a.r, a.g, a.b, 1);
    DrawLine(radarX, radarY + radarSize / 2, radarX + radarSize, radarY + radarSize / 2, a.r, a.g, a.b, 1);

    float pulseR = 5.0f + 2.0f * sinf(menuTime * 4.0f);
    DrawCircle(radarX + radarSize / 2, radarY + radarSize / 2, (int)pulseR, a.r, a.g, a.b);

    uintptr_t localPawn = ReadMem<uintptr_t>(clientBase + OFFSET_DW_LOCAL_PLAYER_PAWN);
    if (!localPawn) return;

    Vector3 localPos = GetPlayerPos(localPawn);
    int localTeam = ReadMem<int>(localPawn + OFFSET_M_TEAM);

    Color3 e = GetEspColor();

    for (const auto& player : players) {
        if (player.team == localTeam) continue;

        float dx = player.position.x - localPos.x;
        float dy = player.position.y - localPos.y;
        float distance = sqrtf(dx * dx + dy * dy);
        if (distance > 1500) continue;

        float rad = atan2(dy, dx);
        float nx = cos(rad) * (distance / 1500.0f) * (radarSize / 2);
        float ny = sin(rad) * (distance / 1500.0f) * (radarSize / 2);

        int px = (int)(radarX + radarSize / 2 + nx);
        int py = (int)(radarY + radarSize / 2 + ny);

        if (px >= radarX && px <= radarX + radarSize && py >= radarY && py <= radarY + radarSize) {
            DrawFilledRect(px - 2, py - 2, 4, 4, e.r, e.g, e.b);
        }
    }
}

void DrawToggle(int x, int y, int w, int h, int id, const char* label, bool state) {
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(overlayWnd, &p);
    bool hover = (p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h);

    Color3 a = GetAccent();

    int bg = hover ? 22 : 10;

    if (hover) {
        for (int i = 3; i >= 1; i--) {
            int glowR = (int)(a.r * 0.1f);
            int glowG = (int)(a.g * 0.1f);
            int glowB = (int)(a.b * 0.1f);
            DrawRoundedRect(x - i, y - i, w + i * 2, h + i * 2, 8 + i, glowR, glowG, glowB);
        }
    }

    DrawRoundedRect(x, y, w, h, 8, bg, bg, bg);

    if (hover) {
        DrawRoundedRectOutline(x, y, w, h, 8, a.r, a.g, a.b, 1);
    }

    DrawText(x + 14, y + (h - 14) / 2, label, 220, 225, 235, 12);

    int toggleX = x + w - 62;
    int toggleY = y + (h - 22) / 2;
    int toggleW = 46;
    int toggleH = 22;

    if (state) {
        DrawRoundedRect(toggleX, toggleY, toggleW, toggleH, 11, a.r, a.g, a.b);

        int knobX = toggleX + toggleW - 19;
        HBRUSH brush = CreateSolidBrush(RGB(240, 245, 255));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(240, 245, 255));
        HPEN oldPen = (HPEN)SelectObject(memDC, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, brush);
        Ellipse(memDC, knobX, toggleY + 3, knobX + 16, toggleY + 19);
        SelectObject(memDC, oldPen);
        SelectObject(memDC, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }
    else {
        DrawRoundedRect(toggleX, toggleY, toggleW, toggleH, 11, 35, 35, 38);

        int knobX = toggleX + 3;
        HBRUSH brush = CreateSolidBrush(RGB(150, 155, 170));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(150, 155, 170));
        HPEN oldPen = (HPEN)SelectObject(memDC, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, brush);
        Ellipse(memDC, knobX, toggleY + 3, knobX + 16, toggleY + 19);
        SelectObject(memDC, oldPen);
        SelectObject(memDC, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    MenuItem item;
    item.rect = { x, y, x + w, y + h };
    item.id = id;
    menuItems.push_back(item);
}

void DrawStepper(int x, int y, int w, int h, int idMinus, int idPlus,
    const char* label, const char* value) {
    Color3 a = GetAccent();

    DrawText(x + 4, y + (h - 14) / 2, label, 220, 225, 235, 12);

    POINT mp;
    GetCursorPos(&mp);
    ScreenToClient(overlayWnd, &mp);

    int bx = x + w - 130;
    int bw = 32;

    bool hoverMinus = (mp.x >= bx && mp.x <= bx + bw && mp.y >= y && mp.y <= y + h);
    DrawRoundedRect(bx, y + 2, bw, h - 4, 6,
        hoverMinus ? a.r / 2 : 30,
        hoverMinus ? a.g / 2 : 30,
        hoverMinus ? a.b / 2 : 35);
    DrawText(bx + 11, y + (h - 16) / 2, "-", 220, 220, 230, 16);
    MenuItem mi; mi.rect = { bx, y, bx + bw, y + h }; mi.id = idMinus;
    menuItems.push_back(mi);

    int vx = bx + 42;
    DrawText(vx, y + (h - 14) / 2, value, a.r, a.g, a.b, 12);

    int px = bx + 90;
    bool hoverPlus = (mp.x >= px && mp.x <= px + bw && mp.y >= y && mp.y <= y + h);
    DrawRoundedRect(px, y + 2, bw, h - 4, 6,
        hoverPlus ? a.r / 2 : 30,
        hoverPlus ? a.g / 2 : 30,
        hoverPlus ? a.b / 2 : 35);
    DrawText(px + 10, y + (h - 16) / 2, "+", 220, 220, 230, 16);
    MenuItem pi; pi.rect = { px, y, px + bw, y + h }; pi.id = idPlus;
    menuItems.push_back(pi);
}

void DrawColorPicker(int x, int y, int w, int h, int idLeft, int idRight,
    const char* label, int colorIdx) {

    Color3 a = GetAccent();
    Color3 sel = COLOR_PALETTE[colorIdx];

    DrawText(x + 4, y + (h - 14) / 2, label, 220, 225, 235, 12);

    POINT mp;
    GetCursorPos(&mp);
    ScreenToClient(overlayWnd, &mp);

    int bx = x + w - 130;
    int bw = 32;

    bool hoverMinus = (mp.x >= bx && mp.x <= bx + bw && mp.y >= y && mp.y <= y + h);
    DrawRoundedRect(bx, y + 2, bw, h - 4, 6,
        hoverMinus ? a.r / 2 : 30,
        hoverMinus ? a.g / 2 : 30,
        hoverMinus ? a.b / 2 : 35);
    DrawText(bx + 11, y + (h - 16) / 2, "<", 220, 220, 230, 14);
    MenuItem mi; mi.rect = { bx, y, bx + bw, y + h }; mi.id = idLeft;
    menuItems.push_back(mi);

    DrawFilledRect(bx + 40, y + (h - 18) / 2, 30, 18, sel.r, sel.g, sel.b);
    DrawRect(bx + 40, y + (h - 18) / 2, 30, 18, 200, 200, 200, 1);

    int px = bx + 90;
    bool hoverPlus = (mp.x >= px && mp.x <= px + bw && mp.y >= y && mp.y <= y + h);
    DrawRoundedRect(px, y + 2, bw, h - 4, 6,
        hoverPlus ? a.r / 2 : 30,
        hoverPlus ? a.g / 2 : 30,
        hoverPlus ? a.b / 2 : 35);
    DrawText(px + 11, y + (h - 16) / 2, ">", 220, 220, 230, 14);
    MenuItem pi; pi.rect = { px, y, px + bw, y + h }; pi.id = idRight;
    menuItems.push_back(pi);
}

void RenderSettingsPanel() {
    Color3 a = GetAccent();

    float sa = settingsAnim;
    if (sa < 0.01f) return;

    int sx = SETTINGS_X;
    int sy = SETTINGS_Y;
    int sh = (int)(SET_H * EaseOutCubic(sa));

    for (int i = 6; i >= 0; i--) {
        int gray = 1 + (int)((7 - i) * 2 * sa);
        DrawRoundedRect(sx - i, sy - i, SET_W + i * 2, sh + i * 2, 10 + i, gray, gray, gray);
    }

    DrawRoundedRect(sx, sy, SET_W, sh, 10, 0, 0, 0);
    DrawStars((float)sx, (float)sy, SET_W, sh);
    DrawBeams((float)sx, (float)sy, SET_W, sh);
    DrawRoundedRectOutline(sx, sy, SET_W, sh, 10, a.r, a.g, a.b, 2);

    if (sh < 60) return;

    DrawRoundedRect(sx, sy, SET_W, 32, 10, 15, 15, 15);
    DrawFilledRect(sx, sy + 22, SET_W, 10, 15, 15, 15);
    DrawFilledRect(sx, sy + 31, SET_W, 1, a.r, a.g, a.b);

    DrawText(sx + 12, sy + 8, "SETTINGS", 230, 235, 245, 13);

    int gearSize = 16;
    int gearX = sx + SET_W - gearSize - 10;
    int gearY = sy + (32 - gearSize) / 2;

    float gearRot = menuTime * 2.0f;
    for (int i = 0; i < 8; i++) {
        float ang = gearRot + i * 3.14159f / 4.0f;
        int px = gearX + gearSize / 2 + (int)(cosf(ang) * 6);
        int py = gearY + gearSize / 2 + (int)(sinf(ang) * 6);
        DrawFilledRect(px - 1, py - 1, 2, 2, a.r, a.g, a.b);
    }
    DrawFilledCircle(gearX + gearSize / 2, gearY + gearSize / 2, 4, a.r, a.g, a.b);
    DrawFilledCircle(gearX + gearSize / 2, gearY + gearSize / 2, 2, 0, 0, 0);

    if (settingsAnim < 0.9f) return;
    if (sh < SET_H - 10) return;

    int cx = sx + 14;
    int cw = SET_W - 28;
    int cy = sy + 44;

    DrawColorPicker(cx, cy, cw, ROW_H, 40, 41, "Menu Color", settings.menuAccentIdx);
    cy += ROW_H + SET_ROW_GAP;

    DrawColorPicker(cx, cy, cw, ROW_H, 42, 43, "Trail Color", settings.trailColorIdx);
    cy += ROW_H + SET_ROW_GAP;

    DrawColorPicker(cx, cy, cw, ROW_H, 44, 45, "ESP Color", settings.espColorIdx);
    cy += ROW_H + SET_ROW_GAP;

    char fovBuf[16]; sprintf_s(fovBuf, "%d", settings.aimFov);
    DrawStepper(cx, cy, cw, ROW_H, 46, 47, "Aim FOV", fovBuf);
    cy += ROW_H + SET_ROW_GAP;

    char smoothBuf[16]; sprintf_s(smoothBuf, "%.1f", settings.aimSmoothness);
    DrawStepper(cx, cy, cw, ROW_H, 48, 49, "Smoothness", smoothBuf);
    cy += ROW_H + SET_ROW_GAP;

    char delayBuf[16]; sprintf_s(delayBuf, "%dms", settings.triggerDelay);
    DrawStepper(cx, cy, cw, ROW_H, 50, 51, "Trig Delay", delayBuf);
    cy += ROW_H + SET_ROW_GAP;

    DrawToggle(cx, cy, cw, ROW_H, 52, "RGB Mode", settings.rgbMode);
    cy += ROW_H + SET_ROW_GAP;

    DrawToggle(cx, cy, cw, ROW_H, 53, "Particles", settings.particles);
    cy += ROW_H + SET_ROW_GAP;

    DrawToggle(cx, cy, cw, ROW_H, 54, "Stars BG", settings.starsBg);
    cy += ROW_H + SET_ROW_GAP;

    DrawToggle(cx, cy, cw, ROW_H, 55, "Beams BG", settings.beamsBg);
    cy += ROW_H + SET_ROW_GAP;

    DrawToggle(cx, cy, cw, ROW_H, 56, "Sound FX", settings.soundFx);
    cy += ROW_H + SET_ROW_GAP;

    DrawToggle(cx, cy, cw, ROW_H, 57, "Cursor Glow", settings.cursorGlow);
}

void RenderMenu() {
    menuItems.clear();

    if (GetAsyncKeyState(VK_INSERT) & 1) {
        menuOpen = !menuOpen;
        if (!menuOpen) settingsOpen = false;
        PlayClickSound();
        Sleep(150);
    }

    if (GetAsyncKeyState(VK_END) & 1) {
        running = false;
        Sleep(150);
    }

    float target = menuOpen ? 1.0f : 0.0f;
    menuAnim += (target - menuAnim) * 0.18f;
    if (menuAnim < 0.001f) menuAnim = 0.0f;
    if (menuAnim > 0.999f) menuAnim = 1.0f;

    float settingsTarget = settingsOpen ? 1.0f : 0.0f;
    settingsAnim += (settingsTarget - settingsAnim) * 0.22f;
    if (settingsAnim < 0.001f) settingsAnim = 0.0f;
    if (settingsAnim > 0.999f) settingsAnim = 1.0f;

    if (menuAnim < 0.01f) return;

    float ease = EaseOutCubic(menuAnim);
    int scaledW = (int)(MENU_W * (0.85f + 0.15f * ease));
    int scaledH = (int)(MENU_H * (0.85f + 0.15f * ease));
    int offsetX = (MENU_W - scaledW) / 2;
    int offsetY = (MENU_H - scaledH) / 2;

    int drawX = MENU_X + offsetX;
    int drawY = MENU_Y + offsetY;

    Color3 a = GetAccent();

    for (int i = 10; i >= 0; i--) {
        float t = (10 - i) / 10.0f;
        float hue = fmodf(hueShift * 0.5f + i * 15.0f, 360.0f);
        Color3 glow = HSVtoRGB(hue, 0.8f, 0.5f * t * ease);
        int r = glow.r * t * 0.3f;
        int g = glow.g * t * 0.3f;
        int b = glow.b * t * 0.3f;
        if (r < 2) r = 2;
        if (g < 2) g = 2;
        if (b < 2) b = 2;
        DrawRoundedRect(drawX - i, drawY - i, scaledW + i * 2, scaledH + i * 2, 12 + i, r, g, b);
    }

    DrawRoundedRect(drawX, drawY, scaledW, scaledH, 12, 0, 0, 0);
    DrawStars((float)drawX, (float)drawY, scaledW, scaledH);
    DrawBeams((float)drawX, (float)drawY, scaledW, scaledH);
    DrawRoundedRectOutline(drawX, drawY, scaledW, scaledH, 12, a.r, a.g, a.b, 2);

    DrawRoundedRect(drawX, drawY, scaledW, TITLE_H, 12, 0, 0, 0);
    DrawFilledRect(drawX, drawY + TITLE_H - 12, scaledW, 12, 0, 0, 0);

    for (int i = 0; i < scaledW; i += 3) {
        float t = (float)i / scaledW;
        float hue = fmodf(hueShift + t * 240.0f, 360.0f);
        Color3 lineC = HSVtoRGB(hue, 1.0f, 1.0f);
        DrawFilledRect(drawX + i, drawY + TITLE_H - 2, 3, 2, lineC.r, lineC.g, lineC.b);
    }

    DrawFilledCircle(drawX + 18, drawY + TITLE_H / 2, 5, a.r, a.g, a.b);
    DrawText(drawX + 32, drawY + (TITLE_H - 16) / 2, "NULLCORE", 230, 235, 245, 14);

    int gearSize = 24;
    int gearX = drawX + scaledW - gearSize - 14;
    int gearY = drawY + (TITLE_H - gearSize) / 2;

    POINT mp;
    GetCursorPos(&mp);
    ScreenToClient(overlayWnd, &mp);
    bool gearHover = (mp.x >= gearX && mp.x <= gearX + gearSize &&
        mp.y >= gearY && mp.y <= gearY + gearSize);

    DrawRoundedRect(gearX, gearY, gearSize, gearSize, 6,
        gearHover ? a.r / 3 : 12,
        gearHover ? a.g / 3 : 12,
        gearHover ? a.b / 3 : 12);

    float gearRot = menuTime * 1.5f;
    if (gearHover) gearRot = menuTime * 6.0f;

    for (int i = 0; i < 8; i++) {
        float ang = gearRot + i * 3.14159f / 4.0f;
        int px = gearX + gearSize / 2 + (int)(cosf(ang) * 8);
        int py = gearY + gearSize / 2 + (int)(sinf(ang) * 8);
        DrawFilledRect(px - 1, py - 1, 3, 3, a.r, a.g, a.b);
    }

    DrawFilledCircle(gearX + gearSize / 2, gearY + gearSize / 2, 6, a.r, a.g, a.b);
    DrawFilledCircle(gearX + gearSize / 2, gearY + gearSize / 2, 3, 0, 0, 0);

    MenuItem gearItem;
    gearItem.rect = { gearX, gearY, gearX + gearSize, gearY + gearSize };
    gearItem.id = 99;
    menuItems.push_back(gearItem);

    int leftX = drawX + PAD;
    int rightX = drawX + PAD * 2 + COL_W;
    int startY = drawY + TITLE_H + PAD;

    int ly = startY;
    DrawToggle(leftX, ly, COL_W, ROW_H, 1, "ESP", settings.esp);                 ly += ROW_H + ROW_GAP;
    DrawToggle(leftX, ly, COL_W, ROW_H, 2, "BunnyHop", settings.bhop);           ly += ROW_H + ROW_GAP;
    DrawToggle(leftX, ly, COL_W, ROW_H, 3, "Box ESP", settings.box);             ly += ROW_H + ROW_GAP;
    DrawToggle(leftX, ly, COL_W, ROW_H, 4, "Snaplines", settings.snaplines);     ly += ROW_H + ROW_GAP;
    DrawToggle(leftX, ly, COL_W, ROW_H, 5, "Health ESP", settings.health);       ly += ROW_H + ROW_GAP;
    DrawToggle(leftX, ly, COL_W, ROW_H, 6, "Armor ESP", settings.armor);         ly += ROW_H + ROW_GAP;
    DrawToggle(leftX, ly, COL_W, ROW_H, 7, "Distance", settings.distance);       ly += ROW_H + ROW_GAP;
    DrawToggle(leftX, ly, COL_W, ROW_H, 13, "Head Dot", settings.headDot);       ly += ROW_H + ROW_GAP;
    DrawToggle(leftX, ly, COL_W, ROW_H, 14, "Name", settings.name);              ly += ROW_H + ROW_GAP;

    int ry = startY;
    DrawToggle(rightX, ry, COL_W, ROW_H, 8, "TriggerBot", settings.triggerbot);  ry += ROW_H + ROW_GAP;
    DrawToggle(rightX, ry, COL_W, ROW_H, 9, "Silent Aim", settings.silentAim);   ry += ROW_H + ROW_GAP;
    DrawToggle(rightX, ry, COL_W, ROW_H, 10, "Radar", settings.radar);           ry += ROW_H + ROW_GAP;
    DrawToggle(rightX, ry, COL_W, ROW_H, 11, "Crosshair", settings.crosshair);   ry += ROW_H + ROW_GAP;
    DrawToggle(rightX, ry, COL_W, ROW_H, 12, "Mouse Trail", settings.mouseTrail); ry += ROW_H + ROW_GAP;

    RenderSettingsPanel();

    int footerY = drawY + scaledH - 22;
    DrawText(drawX + 16, footerY, "INSERT - close", 130, 135, 155, 11);

    char fpsText[32];
    sprintf_s(fpsText, "%.0f FPS", fps);
    DrawText(drawX + scaledW - 100, footerY, fpsText, 130, 135, 155, 11);
}

int HitTestMenu(int mx, int my) {
    for (auto it = menuItems.rbegin(); it != menuItems.rend(); ++it) {
        if (mx >= it->rect.left && mx <= it->rect.right &&
            my >= it->rect.top && my <= it->rect.bottom) {
            return it->id;
        }
    }
    return -1;
}

void CycleColor(int& idx, int dir) {
    idx = (idx + dir + COLOR_COUNT) % COLOR_COUNT;
}

void HandleMenuClick(int mx, int my) {
    int id = HitTestMenu(mx, my);
    if (id < 0) return;

    Color3 a = GetAccent();

    switch (id) {
    case 1: settings.esp = !settings.esp; break;
    case 2: settings.bhop = !settings.bhop; break;
    case 3: settings.box = !settings.box; break;
    case 4: settings.snaplines = !settings.snaplines; break;
    case 5: settings.health = !settings.health; break;
    case 6: settings.armor = !settings.armor; break;
    case 7: settings.distance = !settings.distance; break;
    case 8: settings.triggerbot = !settings.triggerbot; break;
    case 9: settings.silentAim = !settings.silentAim; break;
    case 10: settings.radar = !settings.radar; break;
    case 11: settings.crosshair = !settings.crosshair; break;
    case 12: settings.mouseTrail = !settings.mouseTrail; break;
    case 13: settings.headDot = !settings.headDot; break;
    case 14: settings.name = !settings.name; break;

    case 99:
        settingsOpen = !settingsOpen;
        PlayClickSound();
        break;

    case 40: CycleColor(settings.menuAccentIdx, -1); break;
    case 41: CycleColor(settings.menuAccentIdx, 1); break;
    case 42: CycleColor(settings.trailColorIdx, -1); break;
    case 43: CycleColor(settings.trailColorIdx, 1); break;
    case 44: CycleColor(settings.espColorIdx, -1); break;
    case 45: CycleColor(settings.espColorIdx, 1); break;

    case 46: settings.aimFov = max(5, settings.aimFov - 5); break;
    case 47: settings.aimFov = min(180, settings.aimFov + 5); break;
    case 48: settings.aimSmoothness = max(1.0f, settings.aimSmoothness - 0.5f); break;
    case 49: settings.aimSmoothness = min(20.0f, settings.aimSmoothness + 0.5f); break;
    case 50: settings.triggerDelay = max(0, settings.triggerDelay - 10); break;
    case 51: settings.triggerDelay = min(200, settings.triggerDelay + 10); break;

    case 52: settings.rgbMode = !settings.rgbMode; break;
    case 53: settings.particles = !settings.particles; break;
    case 54: settings.starsBg = !settings.starsBg; break;
    case 55: settings.beamsBg = !settings.beamsBg; break;
    case 56: settings.soundFx = !settings.soundFx; break;
    case 57: settings.cursorGlow = !settings.cursorGlow; break;
    }

    for (int i = 0; i < 4; i++) {
        SpawnSparkle((float)mx, (float)my, a.r, a.g, a.b);
    }
    Ripple rip;
    rip.x = (float)mx;
    rip.y = (float)my;
    rip.life = 1.0f;
    rip.r = a.r;
    rip.g = a.g;
    rip.b = a.b;
    ripples.push_back(rip);

    PlayClickSound();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
        ValidateRect(hwnd, NULL);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR: {
        if (cursorOverMenu) {
            SetCursor(NULL);
            return TRUE;
        }
        break;
    }

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);

        if (menuOpen && menuAnim > 0.5f) {
            if (pt.x >= MENU_X && pt.x <= MENU_X + MENU_W &&
                pt.y >= MENU_Y && pt.y <= MENU_Y + MENU_H) {
                return HTCLIENT;
            }
        }

        if (settingsOpen && menuAnim > 0.5f) {
            if (pt.x >= SETTINGS_X && pt.x <= SETTINGS_X + SET_W &&
                pt.y >= SETTINGS_Y && pt.y <= SETTINGS_Y + SET_H) {
                return HTCLIENT;
            }
        }

        return HTTRANSPARENT;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        if (settingsOpen && menuAnim > 0.5f) {
            if (mx >= SETTINGS_X && mx <= SETTINGS_X + SET_W &&
                my >= SETTINGS_Y && my <= SETTINGS_Y + 32) {
                draggingSettings = true;
                dragSettingsOffsetX = mx - SETTINGS_X;
                dragSettingsOffsetY = my - SETTINGS_Y;
                SetCapture(hwnd);
                return 0;
            }
        }

        if (menuOpen && menuAnim > 0.5f) {
            if (mx >= MENU_X && mx <= MENU_X + MENU_W &&
                my >= MENU_Y && my <= MENU_Y + TITLE_H) {

                bool overGear = false;
                for (const auto& item : menuItems) {
                    if (item.id == 99 &&
                        mx >= item.rect.left && mx <= item.rect.right &&
                        my >= item.rect.top && my <= item.rect.bottom) {
                        overGear = true;
                        break;
                    }
                }
                if (!overGear) {
                    dragging = true;
                    dragOffsetX = mx - MENU_X;
                    dragOffsetY = my - MENU_Y;
                    SetCapture(hwnd);
                    return 0;
                }
            }

            HandleMenuClick(mx, my);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        if (dragging) {
            MENU_X = mx - dragOffsetX;
            MENU_Y = my - dragOffsetY;

            if (MENU_X < 0) MENU_X = 0;
            if (MENU_Y < 0) MENU_Y = 0;
            if (MENU_X + MENU_W > screenW) MENU_X = screenW - MENU_W;
            if (MENU_Y + MENU_H > screenH) MENU_Y = screenH - MENU_H;
        }

        if (draggingSettings) {
            SETTINGS_X = mx - dragSettingsOffsetX;
            SETTINGS_Y = my - dragSettingsOffsetY;

            if (SETTINGS_X < 0) SETTINGS_X = 0;
            if (SETTINGS_Y < 0) SETTINGS_Y = 0;
            if (SETTINGS_X + SET_W > screenW) SETTINGS_X = screenW - SET_W;
            if (SETTINGS_Y + SET_H > screenH) SETTINGS_Y = screenH - SET_H;
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (dragging) {
            dragging = false;
            ReleaseCapture();
        }
        if (draggingSettings) {
            draggingSettings = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_DESTROY:
    case WM_CLOSE:
        running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND CreateOverlay() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"NULLCORE_OVERLAY";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"NULLCORE_OVERLAY", L"NULLCORE", WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, wc.hInstance, NULL);

    SetLayeredWindowAttributes(hwnd, RGB(1, 1, 1), 0, LWA_COLORKEY);
    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    ShowWindow(hwnd, SW_SHOW);
    return hwnd;
}

int main() {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    printf("[NULLCORE] Starting...\n");

    MessageBoxW(NULL, L"Click OK when CS2 is running", L"NULLCORE", MB_OK);

    DWORD pid = GetProcessId(L"cs2.exe");
    if (!pid) {
        MessageBoxW(NULL, L"CS2 NOT FOUND!", L"ERROR", MB_OK);
        return 1;
    }

    gameProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!gameProcess) {
        MessageBoxW(NULL, L"Run as Administrator!", L"ERROR", MB_OK);
        return 1;
    }

    clientBase = GetModuleBase(pid, L"client.dll");
    gameWnd = FindWindowW(NULL, L"Counter-Strike 2");
    overlayWnd = CreateOverlay();
    InitBackBuffer();
    InitStars();

    lastFpsTime = GetTickCount();

    printf("[NULLCORE] CS2 PID: %d\n", pid);
    printf("[NULLCORE] client.dll: 0x%llX\n", clientBase);

    MSG msg;
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        menuTime += 0.016f;
        hueShift += 2.0f;
        if (hueShift > 360.0f) hueShift -= 360.0f;

        UpdateFPS();
        UpdateOverlayPosition();
        UpdateCursorState();
        ClearScreen();
        UpdateMouseTrail();
        UpdateParticles();
        UpdateStars();
        UpdateBeams();

        if (settings.esp || settings.radar || settings.silentAim) {
            UpdatePlayers();
        }

        RenderESP();
        DrawRadar();
        DrawCrosshair();
        DrawParticles();
        RenderMenu();
        DrawMouseTrail();
        DrawCursorGlow();
        DrawCustomCursor();
        BunnyHop();
        TriggerBot();
        SilentAim();

        Present();

        Sleep(10);
    }

    ShowCursor(TRUE);
    ShutdownBackBuffer();
    FreeConsole();
    CloseHandle(gameProcess);
    return 0;
}

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

const int MENU_X = 60;
const int MENU_Y = 60;
const int MENU_W = 380;
const int MENU_H = 620;

struct MenuItem {
    RECT rect;
    int id;
};

std::vector<MenuItem> menuItems;

HDC memDC = NULL;
HBITMAP memBitmap = NULL;
HBITMAP oldBitmap = NULL;
int screenW = 0, screenH = 0;

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
    int espColorMode = 0;
    float glowIntensity = 0.8f;
    int triggerDelay = 0;
    int aimFov = 30;
    float aimSmoothness = 5.0f;
};

Settings settings;

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
    HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
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
        DEFAULT_QUALITY, DEFAULT_PITCH, "Verdana");
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
    if (!gameWnd) gameWnd = FindWindowW(NULL, L"Counter-Strike 2");
    if (gameWnd) {
        RECT gameRect;
        GetWindowRect(gameWnd, &gameRect);
        SetWindowPos(overlayWnd, HWND_TOPMOST, gameRect.left, gameRect.top,
            gameRect.right - gameRect.left, gameRect.bottom - gameRect.top, SWP_SHOWWINDOW);
    }
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

    char localInfo[128];
    sprintf_s(localInfo, "[NULLCORE] HP: %d | ARMOR: %d | ENEMIES: %d",
        ReadMem<int>(localPawn + OFFSET_M_HEALTH),
        ReadMem<int>(localPawn + OFFSET_M_ARMOR),
        (int)players.size());
    DrawText(10, 10, localInfo, 0, 255, 0, 11);

    for (const auto& player : players) {
        bool isEnemy = (player.team != localTeam);

        int r = isEnemy ? 255 : 100;
        int g = isEnemy ? 0 : 255;
        int b = isEnemy ? 0 : 100;

        switch (settings.espColorMode) {
        case 1: r = 255; g = 255; b = 255; break;
        case 2: r = 0; g = 255; b = 255; break;
        case 3: r = 255; g = 0; b = 255; break;
        case 4: r = 255; g = 255; b = 0; break;
        default: break;
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

        if (settings.box) {
            DrawRect(boxX, boxY, boxW, boxH, r, g, b, 2);
            DrawRect(boxX - 1, boxY - 1, boxW + 2, boxH + 2, 0, 0, 0, 1);
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

    DrawLine(cx - 10, cy, cx + 10, cy, 0, 255, 0, 1);
    DrawLine(cx, cy - 10, cx, cy + 10, 0, 255, 0, 1);
    DrawCircle(cx, cy, 4, 0, 255, 0);
}

void DrawRadar() {
    if (!settings.radar) return;

    int radarX = GetSystemMetrics(SM_CXSCREEN) - 210;
    int radarY = 10;
    int radarSize = 200;

    DrawFilledRect(radarX, radarY, radarSize, radarSize, 0, 0, 0, 180);
    DrawRect(radarX, radarY, radarSize, radarSize, 50, 50, 50, 1);

    DrawLine(radarX + radarSize / 2, radarY, radarX + radarSize / 2, radarY + radarSize, 255, 255, 255, 1);
    DrawLine(radarX, radarY + radarSize / 2, radarX + radarSize, radarY + radarSize / 2, 255, 255, 255, 1);

    DrawCircle(radarX + radarSize / 2, radarY + radarSize / 2, 5, 0, 255, 0);

    uintptr_t localPawn = ReadMem<uintptr_t>(clientBase + OFFSET_DW_LOCAL_PLAYER_PAWN);
    if (!localPawn) return;

    Vector3 localPos = GetPlayerPos(localPawn);
    int localTeam = ReadMem<int>(localPawn + OFFSET_M_TEAM);

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
            DrawFilledRect(px - 2, py - 2, 4, 4, 255, 0, 0);
        }
    }
}

void DrawToggle(int x, int y, int w, int h, int id, const char* label, bool state) {
    POINT p;
    GetCursorPos(&p);
    bool hover = (p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h);

    int bg = hover ? 45 : 32;
    DrawFilledRect(x, y, w, h, bg, bg, bg + 16);
    DrawRect(x, y, w, h, state ? 0 : 90, state ? 200 : 90, state ? 255 : 110, 1);

    DrawText(x + 12, y + (h - 14) / 2, label, 230, 230, 240, 12);

    const char* st = state ? "ON" : "OFF";
    int sr = state ? 0 : 220;
    int sg = state ? 255 : 80;
    int sb = state ? 100 : 80;
    DrawFilledRect(x + w - 58, y + 5, 50, h - 10, state ? 20 : 40, state ? 80 : 20, state ? 40 : 20);
    DrawText(x + w - 46, y + (h - 14) / 2, st, sr, sg, sb, 12);

    MenuItem item;
    item.rect = { x, y, x + w, y + h };
    item.id = id;
    menuItems.push_back(item);
}

void DrawStepper(int x, int y, int w, int h, int idMinus, int idPlus,
    const char* label, const char* value) {
    DrawText(x, y + (h - 14) / 2, label, 230, 230, 240, 12);

    int bx = x + w - 110;
    int bw = 30;

    DrawFilledRect(bx, y, bw, h, 40, 40, 60);
    DrawRect(bx, y, bw, h, 90, 90, 120, 1);
    DrawText(bx + 9, y + (h - 14) / 2, "-", 220, 220, 230, 14);
    MenuItem mi; mi.rect = { bx, y, bx + bw, y + h }; mi.id = idMinus;
    menuItems.push_back(mi);

    DrawText(bx + 38, y + (h - 14) / 2, value, 0, 200, 255, 12);

    int px = bx + 80;
    DrawFilledRect(px, y, bw, h, 40, 40, 60);
    DrawRect(px, y, bw, h, 90, 90, 120, 1);
    DrawText(px + 9, y + (h - 14) / 2, "+", 220, 220, 230, 14);
    MenuItem pi; pi.rect = { px, y, px + bw, y + h }; pi.id = idPlus;
    menuItems.push_back(pi);
}

void RenderMenu() {
    menuItems.clear();

    if (GetAsyncKeyState(VK_INSERT) & 1) {
        menuOpen = !menuOpen;
        Sleep(150);
    }

    if (GetAsyncKeyState(VK_END) & 1) {
        running = false;
        Sleep(150);
    }

    if (!menuOpen) return;

    DrawFilledRect(MENU_X, MENU_Y, MENU_W, MENU_H, 18, 18, 28);
    DrawRect(MENU_X, MENU_Y, MENU_W, MENU_H, 0, 150, 255, 2);

    DrawText(MENU_X + MENU_W / 2 - 90, MENU_Y + 12, "NULLCORE CS2", 0, 200, 255, 16);
    DrawLine(MENU_X + 12, MENU_Y + 40, MENU_X + MENU_W - 12, MENU_Y + 40, 0, 150, 255, 1);

    int x = MENU_X + 15;
    int w = MENU_W - 30;
    int y = MENU_Y + 55;
    int rowH = 30;
    int gap = 6;

    DrawToggle(x, y, w, rowH, 1, "ESP", settings.esp);                    y += rowH + gap;
    DrawToggle(x, y, w, rowH, 2, "BunnyHop", settings.bhop);              y += rowH + gap;
    DrawToggle(x, y, w, rowH, 3, "Box ESP", settings.box);                y += rowH + gap;
    DrawToggle(x, y, w, rowH, 4, "Snaplines", settings.snaplines);        y += rowH + gap;
    DrawToggle(x, y, w, rowH, 5, "Health ESP", settings.health);          y += rowH + gap;
    DrawToggle(x, y, w, rowH, 6, "Armor ESP", settings.armor);            y += rowH + gap;
    DrawToggle(x, y, w, rowH, 7, "Distance", settings.distance);          y += rowH + gap;
    DrawToggle(x, y, w, rowH, 8, "TriggerBot", settings.triggerbot);      y += rowH + gap;
    DrawToggle(x, y, w, rowH, 9, "Silent Aim", settings.silentAim);       y += rowH + gap;
    DrawToggle(x, y, w, rowH, 10, "Radar", settings.radar);               y += rowH + gap;
    DrawToggle(x, y, w, rowH, 11, "Crosshair", settings.crosshair);       y += rowH + gap;

    y += 6;
    DrawLine(x, y - 3, x + w, y - 3, 60, 60, 90, 1);

    const char* colors[] = { "Team", "White", "Cyan", "Pink", "Yellow" };
    DrawStepper(x, y, w, rowH, 20, 21, "ESP Color", colors[settings.espColorMode]);
    y += rowH + gap;

    char delayBuf[16]; sprintf_s(delayBuf, "%dms", settings.triggerDelay);
    DrawStepper(x, y, w, rowH, 22, 23, "Trigger Delay", delayBuf);
    y += rowH + gap;

    char fovBuf[16]; sprintf_s(fovBuf, "%d", settings.aimFov);
    DrawStepper(x, y, w, rowH, 24, 25, "Aim FOV", fovBuf);
    y += rowH + gap;

    char smoothBuf[16]; sprintf_s(smoothBuf, "%.1f", settings.aimSmoothness);
    DrawStepper(x, y, w, rowH, 26, 27, "Smoothness", smoothBuf);
    y += rowH + gap;

    DrawText(MENU_X + MENU_W / 2 - 70, MENU_Y + MENU_H - 24,
        "INSERT - toggle menu", 130, 130, 160, 11);
}

int HitTestMenu(int mx, int my) {
    for (const auto& item : menuItems) {
        if (mx >= item.rect.left && mx <= item.rect.right &&
            my >= item.rect.top && my <= item.rect.bottom) {
            return item.id;
        }
    }
    return -1;
}

void HandleMenuClick(int mx, int my) {
    int id = HitTestMenu(mx, my);
    if (id < 0) return;

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

    case 20: settings.espColorMode = (settings.espColorMode + 4) % 5; break;
    case 21: settings.espColorMode = (settings.espColorMode + 1) % 5; break;

    case 22: settings.triggerDelay = max(0, settings.triggerDelay - 10); break;
    case 23: settings.triggerDelay = min(200, settings.triggerDelay + 10); break;

    case 24: settings.aimFov = max(5, settings.aimFov - 5); break;
    case 25: settings.aimFov = min(180, settings.aimFov + 5); break;

    case 26: settings.aimSmoothness = max(1.0f, settings.aimSmoothness - 0.5f); break;
    case 27: settings.aimSmoothness = min(20.0f, settings.aimSmoothness + 0.5f); break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
        ValidateRect(hwnd, NULL);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        if (menuOpen) {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            HandleMenuClick(mx, my);
        }
        return 0;
    }

    case WM_NCHITTEST: {
        if (!menuOpen) return HTTRANSPARENT;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (pt.x >= MENU_X && pt.x <= MENU_X + MENU_W &&
            pt.y >= MENU_Y && pt.y <= MENU_Y + MENU_H) {
            return HTCLIENT;
        }
        return HTTRANSPARENT;
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

    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
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

    printf("[NULLCORE] CS2 PID: %d\n", pid);
    printf("[NULLCORE] client.dll: 0x%llX\n", clientBase);

    MSG msg;
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        UpdateOverlayPosition();
        ClearScreen();

        if (settings.esp || settings.radar || settings.silentAim) {
            UpdatePlayers();
        }

        RenderESP();
        DrawRadar();
        DrawCrosshair();
        RenderMenu();
        BunnyHop();
        TriggerBot();
        SilentAim();

        Present();

        Sleep(10);
    }

    ShutdownBackBuffer();
    FreeConsole();
    CloseHandle(gameProcess);
    return 0;
}

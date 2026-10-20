#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define APP_NAME L"RoomStudio"
#define MAX_ITEMS 48
#define SIDEBAR_WIDTH 316
#define HEADER_HEIGHT 68

typedef enum {
    ITEM_SOFA,
    ITEM_CHAIR,
    ITEM_TABLE,
    ITEM_LAMP,
    ITEM_PLANT,
    ITEM_BOOKSHELF,
    ITEM_RUG,
    ITEM_COUNT
} ItemType;

typedef struct {
    ItemType type;
    float x;
    float z;
    int material;
} Furniture;

typedef struct {
    const wchar_t *name;
    COLORREF color;
} Material;

static const Material materials[] = {
    {L"Sand", RGB(204, 163, 116)},
    {L"Forest", RGB(94, 133, 113)},
    {L"Ocean", RGB(102, 135, 167)},
    {L"Berry", RGB(177, 111, 121)},
    {L"Charcoal", RGB(91, 101, 113)}
};

static const wchar_t *itemNames[ITEM_COUNT] = {
    L"Sofa", L"Armchair", L"Coffee table", L"Floor lamp",
    L"Plant", L"Bookshelf", L"Rug"
};

static Furniture items[MAX_ITEMS];
static int itemCount;
static int selection = -1;
static int roomSize = 500;
static int dragging;
static int activeMaterial;
static POINT roomOrigin;
static POINT dragOffset;
static HWND mainWindow;

static const COLORREF COLOR_INK = RGB(32, 43, 53);
static const COLORREF COLOR_MUTED = RGB(116, 127, 132);
static const COLORREF COLOR_ACCENT = RGB(94, 133, 113);
static const COLORREF COLOR_CANVAS = RGB(243, 245, 242);
static const COLORREF COLOR_WALL = RGB(239, 232, 218);
static const COLORREF COLOR_WALL_SIDE = RGB(229, 221, 208);
static const COLORREF COLOR_FLOOR = RGB(219, 197, 164);
static const COLORREF COLOR_WHITE = RGB(255, 255, 255);

static COLORREF Shade(COLORREF color, int amount)
{
    int red = GetRValue(color) + amount;
    int green = GetGValue(color) + amount;
    int blue = GetBValue(color) + amount;
    if (red < 0) red = 0;
    if (green < 0) green = 0;
    if (blue < 0) blue = 0;
    if (red > 255) red = 255;
    if (green > 255) green = 255;
    if (blue > 255) blue = 255;
    return RGB(red, green, blue);
}

static void Fill(HDC dc, RECT rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

static void Text(HDC dc, const wchar_t *value, int x, int y, int width,
    int height, COLORREF color, int size, int weight)
{
    HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT previous = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT bounds = {x, y, x + width, y + height};
    DrawTextW(dc, value, -1, &bounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, previous);
    DeleteObject(font);
}

static void DrawPolygon(HDC dc, const POINT *points, int count, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, Shade(color, -18));
    HBRUSH previousBrush = (HBRUSH)SelectObject(dc, brush);
    HPEN previousPen = (HPEN)SelectObject(dc, pen);
    Polygon(dc, points, count);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void StrokeLine(HDC dc, POINT from, POINT to, COLORREF color, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN previous = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, from.x, from.y, NULL);
    LineTo(dc, to.x, to.y);
    SelectObject(dc, previous);
    DeleteObject(pen);
}

static POINT Project(float x, float z, float height)
{
    POINT point;
    point.x = roomOrigin.x + (int)((x - z) * 0.56f);
    point.y = roomOrigin.y + (int)((x + z) * 0.28f - height * 0.72f);
    return point;
}

static float AnchorHeight(ItemType type)
{
    switch (type) {
    case ITEM_SOFA: return 34.0f;
    case ITEM_CHAIR: return 30.0f;
    case ITEM_TABLE: return 12.0f;
    case ITEM_LAMP: return 54.0f;
    case ITEM_PLANT: return 60.0f;
    case ITEM_BOOKSHELF: return 72.0f;
    default: return 2.0f;
    }
}

static float FurnitureMargin(ItemType type)
{
    switch (type) {
    case ITEM_SOFA: return 80.0f;
    case ITEM_CHAIR: return 40.0f;
    case ITEM_TABLE: return 55.0f;
    case ITEM_LAMP: return 34.0f;
    case ITEM_PLANT: return 52.0f;
    case ITEM_BOOKSHELF: return 42.0f;
    case ITEM_RUG: return 108.0f;
    default: return 40.0f;
    }
}

static void ClampFurnitureToRoom(Furniture *item)
{
    float margin = FurnitureMargin(item->type);
    float maximum = (float)roomSize - margin;
    if (item->x < margin) item->x = margin;
    if (item->x > maximum) item->x = maximum;
    if (item->z < margin) item->z = margin;
    if (item->z > maximum) item->z = maximum;
}

static COLORREF ItemColor(const Furniture *item)
{
    return materials[item->material].color;
}

static void DrawCuboid(HDC dc, float x, float z, float width, float depth,
    float height, COLORREF color)
{
    POINT base[4];
    POINT top[4];
    base[0] = Project(x, z, 0);
    base[1] = Project(x + width, z, 0);
    base[2] = Project(x + width, z + depth, 0);
    base[3] = Project(x, z + depth, 0);
    top[0] = Project(x, z, height);
    top[1] = Project(x + width, z, height);
    top[2] = Project(x + width, z + depth, height);
    top[3] = Project(x, z + depth, height);

    POINT faceX[] = {base[1], base[2], top[2], top[1]};
    POINT faceZ[] = {base[3], base[2], top[2], top[3]};
    DrawPolygon(dc, faceX, 4, Shade(color, -35));
    DrawPolygon(dc, faceZ, 4, Shade(color, -18));
    DrawPolygon(dc, top, 4, Shade(color, 22));
}

static void DrawPlant(HDC dc, const Furniture *item)
{
    float left = item->x - 18;
    float back = item->z - 18;
    POINT center = Project(item->x, item->z, 46);
    POINT stemTop = Project(item->x, item->z, 83);
    POINT leaves[4];
    DrawCuboid(dc, left, back, 36, 36, 28, ItemColor(item));
    StrokeLine(dc, center, stemTop, RGB(64, 104, 72), 3);

    leaves[0] = Project(item->x - 28, item->z, 65);
    leaves[1] = Project(item->x - 14, item->z - 19, 87);
    leaves[2] = Project(item->x + 20, item->z - 22, 76);
    leaves[3] = Project(item->x + 25, item->z + 12, 92);

    for (int i = 0; i < 4; ++i) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(70, 113, 80));
        HBRUSH brush = CreateSolidBrush(i % 2 ? RGB(108, 153, 110) : RGB(80, 132, 91));
        HPEN previousPen = (HPEN)SelectObject(dc, pen);
        HBRUSH previousBrush = (HBRUSH)SelectObject(dc, brush);
        Ellipse(dc, leaves[i].x - 14, leaves[i].y - 8,
            leaves[i].x + 14, leaves[i].y + 8);
        SelectObject(dc, previousPen);
        SelectObject(dc, previousBrush);
        DeleteObject(brush);
        DeleteObject(pen);
        StrokeLine(dc, center, leaves[i], RGB(64, 104, 72), 1);
    }
}

static void DrawLamp(HDC dc, const Furniture *item)
{
    POINT stem = Project(item->x, item->z, 102);
    POINT shade[4];
    DrawCuboid(dc, item->x - 15, item->z - 15, 30, 30, 6, ItemColor(item));
    StrokeLine(dc, Project(item->x, item->z, 5), stem, RGB(90, 82, 68), 4);
    shade[0] = Project(item->x - 22, item->z - 22, 96);
    shade[1] = Project(item->x + 22, item->z - 22, 96);
    shade[2] = Project(item->x + 22, item->z + 22, 96);
    shade[3] = Project(item->x - 22, item->z + 22, 96);
    DrawPolygon(dc, shade, 4, Shade(ItemColor(item), 32));
}

static void DrawFurniture(HDC dc, const Furniture *item)
{
    float x = item->x;
    float z = item->z;
    COLORREF color = ItemColor(item);

    if (item->type == ITEM_RUG) {
        POINT rug[] = {
            Project(item->x - 105, item->z - 70, 2),
            Project(item->x + 105, item->z - 70, 2),
            Project(item->x + 105, item->z + 70, 2),
            Project(item->x - 105, item->z + 70, 2)
        };
        DrawPolygon(dc, rug, 4, Shade(color, 38));
        POINT seam1 = Project(item->x, item->z - 67, 3);
        POINT seam2 = Project(item->x, item->z + 67, 3);
        StrokeLine(dc, seam1, seam2, Shade(color, -8), 1);
        return;
    }

    if (item->type == ITEM_PLANT) {
        DrawPlant(dc, item);
        return;
    }

    if (item->type == ITEM_LAMP) {
        DrawLamp(dc, item);
        return;
    }

    if (item->type == ITEM_SOFA) {
        DrawCuboid(dc, x - 74, z - 33, 148, 72, 25, color);
        DrawCuboid(dc, x - 74, z - 37, 148, 22, 75, Shade(color, -15));
        DrawCuboid(dc, x - 66, z - 12, 61, 39, 26, Shade(color, 18));
        DrawCuboid(dc, x + 5, z - 12, 61, 39, 26, Shade(color, 18));
        DrawCuboid(dc, x - 77, z - 31, 16, 67, 36, Shade(color, -7));
        DrawCuboid(dc, x + 61, z - 31, 16, 67, 36, Shade(color, -7));
    } else if (item->type == ITEM_CHAIR) {
        DrawCuboid(dc, x - 34, z - 24, 68, 52, 24, color);
        DrawCuboid(dc, x - 34, z - 27, 68, 18, 69, Shade(color, -16));
        DrawCuboid(dc, x - 31, z - 19, 62, 39, 19, Shade(color, 17));
        DrawCuboid(dc, x - 37, z - 24, 12, 49, 30, Shade(color, -8));
        DrawCuboid(dc, x + 25, z - 24, 12, 49, 30, Shade(color, -8));
    } else if (item->type == ITEM_TABLE) {
        DrawCuboid(dc, x - 42, z - 29, 9, 9, 32, Shade(color, -36));
        DrawCuboid(dc, x + 33, z - 29, 9, 9, 32, Shade(color, -36));
        DrawCuboid(dc, x - 42, z + 20, 9, 9, 32, Shade(color, -36));
        DrawCuboid(dc, x + 33, z + 20, 9, 9, 32, Shade(color, -36));
        DrawCuboid(dc, x - 51, z - 37, 102, 74, 12, color);
    } else if (item->type == ITEM_BOOKSHELF) {
        DrawCuboid(dc, x - 39, z - 22, 78, 44, 146, color);
        DrawCuboid(dc, x - 35, z - 24, 70, 5, 137, Shade(color, 26));
        for (int shelf = 1; shelf <= 3; ++shelf) {
            DrawCuboid(dc, x - 35, z - 24, 70, 47,
                (float)(7 + shelf * 34), Shade(color, 12));
        }
        for (int book = 0; book < 4; ++book) {
            COLORREF bookColor = materials[(item->material + book + 1) % 5].color;
            DrawCuboid(dc, x - 29 + book * 15, z - 13,
                11, 7, (float)(29 + (book % 2) * 8), bookColor);
        }
    }
}

static void DrawButton(HDC dc, RECT rect, const wchar_t *label,
    BOOL emphasized, BOOL disabled)
{
    COLORREF fill = emphasized ? COLOR_ACCENT : RGB(247, 248, 246);
    COLORREF ink = emphasized ? COLOR_WHITE : COLOR_INK;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1,
        emphasized ? COLOR_ACCENT : RGB(224, 229, 225));
    HBRUSH previousBrush = (HBRUSH)SelectObject(dc, brush);
    HPEN previousPen = (HPEN)SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 10, 10);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(brush);
    DeleteObject(pen);
    if (disabled) ink = RGB(167, 174, 171);
    RECT textBounds = rect;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ink);
    HFONT font = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT previousFont = (HFONT)SelectObject(dc, font);
    DrawTextW(dc, label, -1, &textBounds,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, previousFont);
    DeleteObject(font);
}

static int FurnitureAt(float screenX, float screenY)
{
    for (int i = itemCount - 1; i >= 0; --i) {
        Furniture *item = &items[i];
        POINT center = Project(item->x, item->z, AnchorHeight(item->type));
        int radiusX = 30;
        int radiusY = 27;
        switch (item->type) {
        case ITEM_SOFA: radiusX = 62; radiusY = 39; break;
        case ITEM_CHAIR: radiusX = 39; radiusY = 32; break;
        case ITEM_TABLE: radiusX = 43; radiusY = 29; break;
        case ITEM_LAMP: radiusX = 28; radiusY = 31; break;
        case ITEM_PLANT: radiusX = 31; radiusY = 36; break;
        case ITEM_BOOKSHELF: radiusX = 33; radiusY = 48; break;
        case ITEM_RUG: radiusX = 85; radiusY = 52; break;
        default: break;
        }
        if (screenX >= center.x - radiusX && screenX <= center.x + radiusX &&
            screenY >= center.y - radiusY && screenY <= center.y + radiusY) {
            return i;
        }
    }
    return -1;
}

static void AddFurniture(ItemType type)
{
    if (itemCount >= MAX_ITEMS) {
        MessageBoxW(mainWindow,
            L"This room already contains the maximum of 48 furniture items.",
            APP_NAME, MB_OK | MB_ICONINFORMATION);
        return;
    }

    Furniture *item = &items[itemCount];
    item->type = type;
    item->material = activeMaterial;
    item->x = (float)roomSize * 0.5f + (itemCount % 3 - 1) * 38;
    item->z = (float)roomSize * 0.60f + (itemCount % 4 - 1) * 34;
    if (type == ITEM_RUG) {
        item->x = (float)roomSize * 0.5f;
        item->z = (float)roomSize * 0.58f;
    }
    if (type == ITEM_BOOKSHELF) {
        item->x = (float)roomSize - 52.0f;
        item->z = (float)roomSize * 0.62f;
    }
    if (type == ITEM_PLANT || type == ITEM_LAMP) {
        item->x = (float)roomSize * 0.77f;
        item->z = (float)roomSize * 0.28f;
    }
    if (selection >= 0 && selection < itemCount) {
        Furniture *previous = &items[selection];
        item->x = previous->x + 35;
        item->z = previous->z + 28;
    }
    ClampFurnitureToRoom(item);
    ++itemCount;
    selection = itemCount - 1;
}

static void AddStarterRoom(void)
{
    itemCount = 0;
    selection = -1;
    activeMaterial = 0;
    AddFurniture(ITEM_RUG);
    items[0].x = (float)roomSize * 0.49f;
    items[0].z = (float)roomSize * 0.59f;
    AddFurniture(ITEM_SOFA);
    items[1].x = (float)roomSize * 0.52f;
    items[1].z = (float)roomSize * 0.28f;
    items[1].material = 1;
    AddFurniture(ITEM_CHAIR);
    items[2].x = (float)roomSize * 0.22f;
    items[2].z = (float)roomSize * 0.55f;
    items[2].material = 2;
    AddFurniture(ITEM_TABLE);
    items[3].x = (float)roomSize * 0.49f;
    items[3].z = (float)roomSize * 0.52f;
    AddFurniture(ITEM_LAMP);
    items[4].x = (float)roomSize * 0.78f;
    items[4].z = (float)roomSize * 0.24f;
    items[4].material = 3;
    AddFurniture(ITEM_PLANT);
    items[5].x = (float)roomSize * 0.80f;
    items[5].z = (float)roomSize * 0.80f;
    items[5].material = 1;
    AddFurniture(ITEM_BOOKSHELF);
    items[6].x = (float)roomSize * 0.82f;
    items[6].z = (float)roomSize * 0.48f;
    items[6].material = 4;
    selection = 1;
}

static void RemoveSelection(void)
{
    if (selection < 0 || selection >= itemCount) return;
    for (int i = selection; i < itemCount - 1; ++i) {
        items[i] = items[i + 1];
    }
    --itemCount;
    selection = itemCount > 0 ? itemCount - 1 : -1;
}

static BOOL SaveRoom(void)
{
    OPENFILENAMEW dialog;
    wchar_t path[MAX_PATH] = L"my-room.room";
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = mainWindow;
    dialog.lpstrFilter = L"RoomStudio design (*.room)\0*.room\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"room";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) return FALSE;

    FILE *file = _wfopen(path, L"wb");
    if (file == NULL) {
        MessageBoxW(mainWindow, L"The design could not be saved.",
            APP_NAME, MB_OK | MB_ICONERROR);
        return FALSE;
    }

    BOOL ok = fprintf(file, "ROOMSTUDIO 1\n%d %d\n", roomSize, itemCount) >= 0;
    for (int i = 0; ok && i < itemCount; ++i) {
        ok = fprintf(file, "%d %.2f %.2f %d\n",
            (int)items[i].type, items[i].x, items[i].z,
                items[i].material) >= 0;
    }
    if (fclose(file) != 0) ok = FALSE;
    if (!ok) {
        MessageBoxW(mainWindow, L"There was a problem writing the design file.",
            APP_NAME, MB_OK | MB_ICONERROR);
    }
    return ok;
}

static BOOL LoadRoom(void)
{
    OPENFILENAMEW dialog;
    wchar_t path[MAX_PATH] = L"";
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = mainWindow;
    dialog.lpstrFilter = L"RoomStudio design (*.room)\0*.room\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return FALSE;

    FILE *file = _wfopen(path, L"rb");
    if (file == NULL) {
        MessageBoxW(mainWindow, L"The design file could not be opened.",
            APP_NAME, MB_OK | MB_ICONERROR);
        return FALSE;
    }

    char signature[24];
    int version = 0;
    int loadedSize = 0;
    int loadedCount = 0;
    Furniture loaded[MAX_ITEMS];
    BOOL valid = fscanf_s(file, "%23s", signature,
        (unsigned)sizeof(signature)) == 1 &&
        strcmp(signature, "ROOMSTUDIO") == 0 &&
        fscanf_s(file, "%d%d%d", &version, &loadedSize, &loadedCount) == 3 &&
        version == 1;
    if (valid && (loadedSize < 400 || loadedSize > 600 ||
        loadedCount < 0 || loadedCount > MAX_ITEMS)) {
        valid = FALSE;
    }

    for (int i = 0; valid && i < loadedCount; ++i) {
        int type;
        int material;
        if (fscanf_s(file, "%d%f%f%d", &type, &loaded[i].x, &loaded[i].z,
            &material) != 4 ||
            type < 0 || type >= ITEM_COUNT || material < 0 || material >= 5 ||
            !isfinite(loaded[i].x) || !isfinite(loaded[i].z) ||
            loaded[i].x < 0 || loaded[i].x > loadedSize ||
            loaded[i].z < 0 || loaded[i].z > loadedSize) {
            valid = FALSE;
            break;
        }
        loaded[i].type = (ItemType)type;
        loaded[i].material = material;
        ClampFurnitureToRoom(&loaded[i]);
    }
    fclose(file);

    if (!valid) {
        MessageBoxW(mainWindow, L"This file is not a valid RoomStudio design.",
            APP_NAME, MB_OK | MB_ICONERROR);
        return FALSE;
    }
    roomSize = loadedSize;
    itemCount = loadedCount;
    selection = itemCount > 0 ? 0 : -1;
    for (int i = 0; i < itemCount; ++i) items[i] = loaded[i];
    return TRUE;
}

static void DrawRoom(HDC dc, RECT client)
{
    int right = client.right;
    int stageLeft = SIDEBAR_WIDTH;
    int stageWidth = right - stageLeft;
    roomOrigin.x = stageLeft + stageWidth / 2;
    roomOrigin.y = 226;

    POINT backWall[4] = {
        Project(0, 0, 164),
        Project((float)roomSize, 0, 164),
        Project((float)roomSize, 0, 0),
        Project(0, 0, 0)
    };
    POINT sideWall[4] = {
        Project(0, 0, 164),
        Project(0, (float)roomSize, 164),
        Project(0, (float)roomSize, 0),
        Project(0, 0, 0)
    };
    POINT floor[4] = {
        Project(0, 0, 0),
        Project((float)roomSize, 0, 0),
        Project((float)roomSize, (float)roomSize, 0),
        Project(0, (float)roomSize, 0)
    };

    DrawPolygon(dc, backWall, 4, COLOR_WALL);
    DrawPolygon(dc, sideWall, 4, COLOR_WALL_SIDE);
    DrawPolygon(dc, floor, 4, COLOR_FLOOR);

    for (int board = 1; board < 8; ++board) {
        float offset = roomSize * board / 8.0f;
        StrokeLine(dc, Project(offset, 0, 1), Project(offset, (float)roomSize, 1),
            RGB(197, 175, 144), 1);
        StrokeLine(dc, Project(0, offset, 1), Project((float)roomSize, offset, 1),
            RGB(197, 175, 144), 1);
    }

    if (selection >= 0 && selection < itemCount) {
        const Furniture *selected = &items[selection];
        POINT mark = Project(selected->x, selected->z, 3);
        HPEN pen = CreatePen(PS_DASH, 1, COLOR_ACCENT);
        HPEN previousPen = (HPEN)SelectObject(dc, pen);
        HBRUSH previousBrush = (HBRUSH)SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Ellipse(dc, mark.x - 31, mark.y - 16, mark.x + 31, mark.y + 16);
        SelectObject(dc, previousBrush);
        SelectObject(dc, previousPen);
        DeleteObject(pen);
    }

    for (int i = 0; i < itemCount; ++i) {
        for (int j = i + 1; j < itemCount; ++j) {
            if (items[i].type != ITEM_RUG && items[j].type == ITEM_RUG) {
                Furniture temporary = items[i];
                items[i] = items[j];
                items[j] = temporary;
                if (selection == i) selection = j;
                else if (selection == j) selection = i;
            } else if (items[i].type != ITEM_RUG && items[j].type != ITEM_RUG &&
                items[i].x + items[i].z > items[j].x + items[j].z) {
                Furniture temporary = items[i];
                items[i] = items[j];
                items[j] = temporary;
                if (selection == i) selection = j;
                else if (selection == j) selection = i;
            }
        }
    }
    for (int i = 0; i < itemCount; ++i) DrawFurniture(dc, &items[i]);
}

static void DrawInterface(HDC dc, RECT client)
{
    RECT header = {0, 0, client.right, HEADER_HEIGHT};
    Fill(dc, client, COLOR_CANVAS);
    Fill(dc, header, RGB(32, 43, 53));

    HBRUSH iconBrush = CreateSolidBrush(COLOR_ACCENT);
    RECT icon = {20, 17, 52, 49};
    FillRect(dc, &icon, iconBrush);
    DeleteObject(iconBrush);
    Text(dc, L"R", 20, 16, 32, 32, COLOR_WHITE, 18, FW_BOLD);
    Text(dc, L"RoomStudio", 63, 11, 200, 25, COLOR_WHITE, 17, FW_SEMIBOLD);
    Text(dc, L"3D INTERIOR DESIGNER", 64, 37, 230, 17,
        RGB(181, 194, 188), 9, FW_NORMAL);

    RECT saveButton = {client.right - 277, 15, client.right - 163, 53};
    RECT newButton = {client.right - 151, 15, client.right - 21, 53};
    DrawButton(dc, saveButton, L"Save design", TRUE, FALSE);
    DrawButton(dc, newButton, L"New room", FALSE, FALSE);

    RECT sidebar = {0, HEADER_HEIGHT, SIDEBAR_WIDTH, client.bottom};
    Fill(dc, sidebar, RGB(255, 255, 255));
    Text(dc, L"FURNITURE", 24, 84, 245, 20, COLOR_MUTED, 10, FW_BOLD);
    Text(dc, L"Build your room", 24, 106, 270, 26, COLOR_INK, 18, FW_SEMIBOLD);

    const wchar_t *labels[8] = {
        L"+  Sofa", L"+  Armchair", L"+  Coffee table", L"+  Floor lamp",
        L"+  Plant", L"+  Bookshelf", L"+  Rug", L"Open saved room"
    };
    for (int i = 0; i < 8; ++i) {
        int col = i % 2;
        int row = i / 2;
        RECT button = {24 + col * 140, 150 + row * 52,
            156 + col * 140, 194 + row * 52};
        DrawButton(dc, button, labels[i], i == 0, FALSE);
    }

    Text(dc, L"ROOM SIZE", 24, 375, 140, 19, COLOR_MUTED, 10, FW_BOLD);
    RECT smaller = {24, 402, 156, 440};
    RECT larger = {164, 402, 296, 440};
    DrawButton(dc, smaller, L"-  Make smaller", FALSE, roomSize <= 400);
    DrawButton(dc, larger, L"+  Make larger", FALSE, roomSize >= 600);
    wchar_t sizeLabel[64];
    swprintf_s(sizeLabel, 64, L"%d x %d cm", roomSize, roomSize);
    Text(dc, sizeLabel, 24, 444, 270, 23, COLOR_INK, 12, FW_SEMIBOLD);

    Text(dc, L"MATERIAL", 24, 488, 150, 19, COLOR_MUTED, 10, FW_BOLD);
    for (int i = 0; i < 5; ++i) {
        int x = 25 + i * 51;
        HBRUSH swatch = CreateSolidBrush(materials[i].color);
        HPEN pen = CreatePen(PS_SOLID, i == activeMaterial ? 3 : 1,
            i == activeMaterial ? COLOR_ACCENT : RGB(224, 229, 225));
        HBRUSH previousBrush = (HBRUSH)SelectObject(dc, swatch);
        HPEN previousPen = (HPEN)SelectObject(dc, pen);
        Ellipse(dc, x, 516, x + 34, 550);
        SelectObject(dc, previousBrush);
        SelectObject(dc, previousPen);
        DeleteObject(swatch);
        DeleteObject(pen);
    }

    Text(dc, L"SELECTED ITEM", 24, 573, 180, 19, COLOR_MUTED, 10, FW_BOLD);
    if (selection >= 0 && selection < itemCount) {
        wchar_t selectionLabel[100];
        swprintf_s(selectionLabel, 100, L"%s  -  %s",
            itemNames[items[selection].type],
            materials[items[selection].material].name);
        Text(dc, selectionLabel, 24, 594, 272, 23, COLOR_INK, 13, FW_SEMIBOLD);
        Text(dc, L"Drag it to move. Use the arrow keys for precision.",
            24, 620, 276, 34, COLOR_MUTED, 10, FW_NORMAL);
        RECT removeButton = {24, 661, 156, 700};
        RECT rotateButton = {164, 661, 296, 700};
        DrawButton(dc, removeButton, L"Remove item", FALSE, FALSE);
        DrawButton(dc, rotateButton, L"Change color", FALSE, FALSE);
    } else {
        Text(dc, L"Select an item in the room to edit it.",
            24, 594, 272, 36, COLOR_MUTED, 11, FW_NORMAL);
    }

    Text(dc, L"DESIGN", 24, 727, 120, 18, COLOR_MUTED, 10, FW_BOLD);
    Text(dc, L"Save and reopen your .room files.", 24, 747, 272, 21,
        COLOR_MUTED, 10, FW_NORMAL);

    RECT separator = {SIDEBAR_WIDTH - 1, HEADER_HEIGHT,
        SIDEBAR_WIDTH, client.bottom};
    Fill(dc, separator, RGB(231, 235, 231));
    RECT canvasTitle = {SIDEBAR_WIDTH + 20, 82, client.right - 18, 109};
    Text(dc, L"Your room", canvasTitle.left, canvasTitle.top,
        270, 26, COLOR_INK, 17, FW_SEMIBOLD);
    Text(dc, L"Click an item to select it, then drag to rearrange",
        canvasTitle.left + 143, canvasTitle.top + 1,
        430, 24, COLOR_MUTED, 11, FW_NORMAL);
    DrawRoom(dc, client);

    RECT footer = {SIDEBAR_WIDTH + 16, client.bottom - 33,
        client.right - 16, client.bottom - 8};
    wchar_t status[80];
    swprintf_s(status, 80, L"%d items   |   %d x %d cm room",
        itemCount, roomSize, roomSize);
    Text(dc, status, footer.left, footer.top, 330, 20,
        COLOR_MUTED, 10, FW_NORMAL);
}

static int ActionAt(int x, int y, int windowWidth)
{
    if (y >= 15 && y < 53) {
        if (x >= windowWidth - 277 && x < windowWidth - 163) return 100;
        if (x >= windowWidth - 151 && x < windowWidth - 21) return 101;
    }
    if (x >= 24 && x < 296 && y >= 150 && y < 358) {
        int col = (x - 24) / 140;
        int row = (y - 150) / 52;
        if (col < 2 && row < 4) {
            int button = row * 2 + col;
            if (button < ITEM_COUNT) return button;
            if (button == 7) return 102;
        }
    }
    if (y >= 402 && y < 440 && x >= 24 && x < 296) {
        return x < 156 ? 103 : 104;
    }
    if (y >= 512 && y < 555 && x >= 24 && x < 279) {
        int material = (x - 25) / 51;
        if (material >= 0 && material < 5) return 110 + material;
    }
    if (y >= 661 && y < 700 && x >= 24 && x < 296 &&
        selection >= 0 && selection < itemCount) {
        return x < 156 ? 105 : 106;
    }
    return -1;
}

static void ResizeRoom(int delta)
{
    roomSize += delta;
    if (roomSize < 400) roomSize = 400;
    if (roomSize > 600) roomSize = 600;
    for (int i = 0; i < itemCount; ++i) {
        ClampFurnitureToRoom(&items[i]);
    }
}

static void MoveSelection(int dx, int dz)
{
    if (selection < 0 || selection >= itemCount) return;
    items[selection].x += (float)dx;
    items[selection].z += (float)dz;
    if (items[selection].x < 30) items[selection].x = 30;
    if (items[selection].x > (float)roomSize - 30)
        items[selection].x = (float)roomSize - 30;
    if (items[selection].z < 30) items[selection].z = 30;
    if (items[selection].z > (float)roomSize - 30)
        items[selection].z = (float)roomSize - 30;
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        mainWindow = window;
        AddStarterRoom();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT client;
        GetClientRect(window, &client);
        DrawInterface(dc, client);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int action;
        RECT client;
        GetClientRect(window, &client);
        action = ActionAt(x, y, client.right);
        if (action >= 0 && action < ITEM_COUNT) {
            AddFurniture((ItemType)action);
        } else if (action == 100) {
            SaveRoom();
        } else if (action == 101) {
            AddStarterRoom();
        } else if (action == 102) {
            LoadRoom();
        } else if (action == 103) {
            ResizeRoom(-50);
        } else if (action == 104) {
            ResizeRoom(50);
        } else if (action == 105) {
            RemoveSelection();
        } else if (action == 106) {
            activeMaterial = (activeMaterial + 1) % 5;
            if (selection >= 0) items[selection].material = activeMaterial;
        } else if (action >= 110 && action <= 114) {
            activeMaterial = action - 110;
            if (selection >= 0) items[selection].material = activeMaterial;
        } else if (x >= SIDEBAR_WIDTH && y > HEADER_HEIGHT + 30) {
            selection = FurnitureAt((float)x, (float)y);
            if (selection >= 0) {
                POINT anchor = Project(items[selection].x, items[selection].z,
                    AnchorHeight(items[selection].type));
                dragOffset.x = x - anchor.x;
                dragOffset.y = y - anchor.y;
                dragging = 1;
                SetCapture(window);
            }
        } else {
            selection = -1;
        }
        InvalidateRect(window, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (dragging && selection >= 0 && selection < itemCount) {
            int sx = GET_X_LPARAM(lParam) - dragOffset.x;
            int sy = GET_Y_LPARAM(lParam) - dragOffset.y +
                (int)(AnchorHeight(items[selection].type) * 0.72f);
            float dx = (sx - roomOrigin.x) / 0.56f;
            float dz = (sy - roomOrigin.y) / 0.28f;
            items[selection].x = (dx + dz) * 0.5f;
            items[selection].z = (dz - dx) * 0.5f;
            ClampFurnitureToRoom(&items[selection]);
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (dragging) {
            dragging = 0;
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        dragging = 0;
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_DELETE || wParam == VK_BACK) {
            RemoveSelection();
        } else if (wParam == VK_LEFT) {
            MoveSelection(-8, 0);
        } else if (wParam == VK_RIGHT) {
            MoveSelection(8, 0);
        } else if (wParam == VK_UP) {
            MoveSelection(0, -8);
        } else if (wParam == VK_DOWN) {
            MoveSelection(0, 8);
        } else {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lParam;
        limits->ptMinTrackSize.x = 900;
        limits->ptMinTrackSize.y = 720;
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine,
    int showCommand)
{
    (void)previous;
    (void)commandLine;
    WNDCLASSEXW windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"RoomStudioMainWindow";
    if (!RegisterClassExW(&windowClass)) return 1;

    HWND window = CreateWindowExW(0, windowClass.lpszClassName,
        L"RoomStudio - 3D Interior Designer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1420, 900,
        NULL, NULL, instance, NULL);
    if (window == NULL) return 1;

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}

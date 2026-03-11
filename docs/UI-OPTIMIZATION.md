# Boot Manager Pro v3 - UI 优化说明

## 优化日期
2026-03-11

## 优化内容

### 1. 导航按钮选中状态样式 ✅

**改进前**: 所有导航按钮样式相同，无法区分当前页面

**改进后**:
- 选中按钮使用主色调背景 `RGB(59, 130, 246)`
- 左侧添加白色垂直指示条（4px 宽）
- 文字颜色变为白色 `RGB(243, 244, 246)`
- 未选中按钮为深灰色背景 `RGB(28, 28, 34)`，次要文字色

**实现方式**:
- 使用 `BS_OWNERDRAW` 自绘按钮
- 子类化消息处理 (`NavButtonSubclassProc`)
- 追踪 `g_navSelectedIndex` 状态

---

### 2. 按钮悬停/按下效果 ✅

**改进前**: 按钮无交互反馈

**改进后**:
- **悬停效果**: 背景色变亮为 `RGB(96, 165, 250)`
- **按下效果**: 背景色变深为 `RGB(37, 99, 235)`
- **危险按钮**: 删除/卸载按钮使用红色主题 `RGB(239, 68, 68)`
- 圆角设计（6px）

**实现方式**:
- `WM_MOUSEMOVE` 追踪悬停状态
- `WM_TIMER` 检测鼠标离开
- `WM_DRAWITEM` 自绘按钮
- 状态变量：`g_navHoverIndex`, `g_hoverButtonId`, `g_pressedButtonId`

---

### 3. ListView 表头样式 ✅

**改进前**: 默认表头样式，与深色主题不协调

**改进后**:
- 表头字体：Microsoft YaHei UI, 13px, 加粗
- 文字居中对齐 (`HDF_CENTER`)
- 与整体深色主题协调

**实现方式**:
```c
static void CustomizeListViewHeader(HWND hListView)
{
    HWND hHeader = ListView_GetHeader(hListView);
    SendMessage(hHeader, WM_SETFONT, (WPARAM)g_hFontHeader, TRUE);
    
    // 设置居中对齐
    HDITEM hdi = { 0 };
    hdi.mask = HDI_FORMAT;
    for (int i = 0; i < itemCount; i++) {
        Header_GetItem(hHeader, i, &hdi);
        hdi.fmt |= HDF_CENTER;
        Header_SetItem(hHeader, i, &hdi);
    }
}
```

---

### 4. 卡片圆角和边框 ✅

**改进前**: 直角边框，视觉生硬

**改进后**:
- 圆角矩形设计（8px 半径）
- 使用 `RoundRect()` API 绘制
- 卡片背景色：`RGB(28, 28, 34)`
- 边框色：`RGB(55, 65, 81)`

**实现方式**:
```c
static void DrawRoundRect(HDC hdc, RECT* rc, int radius, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, 
              radius * 2, radius * 2);
    
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}
```

---

### 5. 整体视觉层次感 ✅

**改进前**: 扁平无层次，所有元素同等重要

**改进后**:
- **颜色层次**:
  - 主背景：`RGB(18, 18, 22)` - 最深
  - 卡片背景：`RGB(28, 28, 34)` - 次深
  - 主色调：`RGB(59, 130, 246)` - 高亮交互元素
  - 文字：`RGB(243, 244, 246)` - 主文字
  - 次要文字：`RGB(156, 163, 175)` - 说明文字

- **字体层次**:
  - 标题：16px 加粗
  - 正文：14px 常规
  - 小字：12px 常规
  - 表头：13px 加粗

- **间距层次**:
  - 内容区内边距：24px
  - 卡片内边距：20px
  - 按钮间距：12-15px

---

## 技术细节

### 自绘按钮 (Owner Draw)
```c
// 创建时使用 BS_OWNERDRAW 样式
CreateWindowExW(0, L"BUTTON", L"文本",
    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
    ...);

// 处理 WM_DRAWITEM 消息
case WM_DRAWITEM: {
    DRAWITEMSTRUCT* pDIS = (DRAWITEMSTRUCT*)lParam;
    if (pDIS->CtlType == ODT_BUTTON) {
        // 自绘逻辑
        DrawButton(hdc, &rc, text, bgColor, textColor, isHovered, isPressed);
    }
    return TRUE;
}
```

### 子类化 (Subclassing)
```c
// 安装子类过程
SetWindowSubclass(g_navButtons[i], NavButtonSubclassProc, i, 0);

// 子类过程处理悬停
LRESULT CALLBACK NavButtonSubclassProc(HWND hWnd, UINT msg, 
    WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    case WM_MOUSEMOVE:
        // 更新悬停状态并重绘
        break;
    case WM_TIMER:
        // 检测鼠标离开
        break;
}
```

### 双缓冲（预留）
```c
static HDC g_hMemDC = NULL;
static HBITMAP g_hMemBitmap = NULL;

// 在 WM_CREATE 中初始化
g_hMemDC = CreateCompatibleDC(hdc);
g_hMemBitmap = CreateCompatibleBitmap(hdc, width, height);
SelectObject(g_hMemDC, g_hMemBitmap);

// 在 WM_DESTROY 中清理
DeleteDC(g_hMemDC);
DeleteObject(g_hMemBitmap);
```

---

## 颜色规范速查

| 用途 | RGB 值 | 十六进制 |
|------|--------|----------|
| 背景色 | RGB(18, 18, 22) | #121216 |
| 卡片色 | RGB(28, 28, 34) | #1C1C22 |
| 主色调 | RGB(59, 130, 246) | #3B82F6 |
| 悬停色 | RGB(96, 165, 250) | #60A5FA |
| 按下色 | RGB(37, 99, 235) | #2563EB |
| 文字色 | RGB(243, 244, 246) | #F3F4F6 |
| 次要文字 | RGB(156, 163, 175) | #9CA3AF |
| 边框色 | RGB(55, 65, 81) | #374151 |
| 危险色 | RGB(239, 68, 68) | #EF4444 |

---

## 编译命令

```bash
gcc -municode src\ui\main.c src\core\*.c -o dist\BootManagerPro.exe ^
    -m64 -mwindows -lcomctl32 -lshell32 -ladvapi32 -lole32 -luuid -O2
```

---

## 测试清单

- [x] 导航按钮选中状态高亮
- [x] 导航按钮悬停效果
- [x] 操作按钮悬停/按下效果
- [x] 危险按钮红色主题
- [x] ListView 表头样式
- [x] 圆角设计
- [x] 深色主题配色
- [x] 字体层次
- [x] 编译无警告

---

## 后续优化建议

1. **动画效果**: 添加页面切换淡入淡出
2. **图标支持**: 导航按钮添加 Emoji 或图标
3. **高 DPI 适配**: 支持 4K 显示器缩放
4. **主题切换**: 支持浅色/深色主题切换
5. **自定义绘制**: 完全自绘 ListView 以匹配主题

---

*优化完成时间：2026-03-11 18:15*
*优化执行：qwen (UI 专家)*

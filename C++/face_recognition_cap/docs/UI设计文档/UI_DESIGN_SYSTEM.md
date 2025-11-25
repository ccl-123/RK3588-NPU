# 🎨 人脸识别考勤系统 - UI 设计系统规范 v2.0

## 📋 目录
1. [颜色系统](#颜色系统)
2. [字体规范](#字体规范)
3. [间距系统](#间距系统)
4. [圆角与阴影](#圆角与阴影)
5. [组件尺寸](#组件尺寸)
6. [动画与过渡](#动画与过渡)
7. [图标使用](#图标使用)
8. [布局规范](#布局规范)

---

## 🎨 颜色系统

### 主色系（Primary Colors）

浅色模式下的核心颜色：

```css
/* Brand Color - 品牌蓝 */
--primary-color: #1890ff;          /* 主要操作 */
--primary-hover: #40a9ff;          /* 悬停状态 */
--primary-pressed: #096dd9;        /* 按下状态 */
--primary-light: #e6f7ff;          /* 浅色背景 */
--primary-border: #91d5ff;         /* 边框色 */

/* Success - 成功绿 */
--success-color: #52c41a;
--success-light: #f6ffed;
--success-border: #b7eb8f;

/* Warning - 警告黄 */
--warning-color: #faad14;
--warning-light: #fffbe6;
--warning-border: #ffe58f;

/* Error/Danger - 错误红 */
--error-color: #ff4d4f;
--error-light: #fff2f0;
--error-border: #ffccc7;

/* Info - 信息蓝 */
--info-color: #1890ff;
--info-light: #e6f7ff;
--info-border: #91d5ff;
```

### 中性色（Neutral Colors）

```css
/* 文字颜色 */
--text-primary: #262626;           /* 主要文字 */
--text-secondary: #595959;         /* 次要文字 */
--text-tertiary: #8c8c8c;          /* 说明文字 */
--text-disabled: #bfbfbf;          /* 禁用文字 */

/* 背景颜色 */
--bg-primary: #ffffff;             /* 主背景（卡片） */
--bg-secondary: #fafafa;           /* 次背景（表格行） */
--bg-tertiary: #f5f5f7;            /* 页面背景 */
--bg-disabled: #f5f5f5;            /* 禁用背景 */

/* 边框颜色 */
--border-primary: #d9d9d9;         /* 主要边框 */
--border-secondary: #e8e8e8;       /* 次要边框 */
--border-light: #f0f0f0;           /* 浅色边框 */
```

### 暗色模式（Dark Mode）

```css
/* 主色系（暗色） */
--dark-primary-color: #177ddc;
--dark-primary-hover: #1890ff;
--dark-primary-pressed: #0958d9;

/* 文字颜色（暗色） */
--dark-text-primary: #e8e8e8;
--dark-text-secondary: #b0b0b0;
--dark-text-tertiary: #8c8c8c;

/* 背景颜色（暗色） */
--dark-bg-primary: #2a2a2a;        /* 卡片背景 */
--dark-bg-secondary: #262626;      /* 次背景 */
--dark-bg-tertiary: #1f1f1f;       /* 页面背景 */
--dark-bg-sidebar: #141414;        /* 侧边栏 */

/* 边框颜色（暗色） */
--dark-border-primary: #3a3a3a;
--dark-border-secondary: #4a4a4a;
```

---

## 📝 字体规范

### 字体家族

```css
font-family: "Microsoft YaHei UI", "Segoe UI", "PingFang SC", 
             "Helvetica Neue", "Arial", sans-serif;
```

**优先级**：
1. Windows: Microsoft YaHei UI
2. macOS: PingFang SC
3. 通用: Segoe UI, Arial

### 字体大小

| 用途 | 大小 | 行高 | 权重 | CSS 类名 |
|------|------|------|------|----------|
| 大标题 | 24px | 32px | 600 | `labelType="title"` |
| 副标题 | 18px | 26px | 500 | `labelType="subtitle"` |
| 正文 | 14px | 22px | 400 | 默认 |
| 小字 | 12px | 20px | 400 | `labelType="caption"` |
| 按钮 | 14px | 22px | 500 | - |
| 表头 | 14px | 22px | 600 | - |

### 字重（Font Weight）

- **Regular (400)**: 正文、说明文字
- **Medium (500)**: 按钮、副标题
- **Semibold (600)**: 主标题、表头、强调

---

## 📏 间距系统（Spacing Scale）

采用 8px 基准的间距系统：

| 变量名 | 值 | 用途 |
|--------|-----|------|
| `spacing-xs` | 4px | 极小间距（图标与文字） |
| `spacing-sm` | 8px | 小间距（内部元素） |
| `spacing-md` | 12px | 中等间距（组件内部） |
| `spacing-lg` | 16px | 大间距（组件间距） |
| `spacing-xl` | 24px | 特大间距（卡片内边距） |
| `spacing-2xl` | 32px | 超大间距（页面边距） |
| `spacing-3xl` | 48px | 巨大间距（页面分区） |

### 实际应用

```css
/* 卡片内边距 */
padding: 24px;  /* spacing-xl */

/* 按钮内边距 */
padding: 0 20px;

/* 表格单元格内边距 */
padding: 12px 16px;

/* 布局间距 */
gap: 16px;  /* spacing-lg */
```

---

## 🔲 圆角与阴影

### 圆角（Border Radius）

| 大小 | 值 | 用途 |
|------|-----|------|
| Small | 6px | Tag、小按钮 |
| Medium | 8px | 按钮、输入框、列表项 |
| Large | 12px | 卡片、对话框 |
| XLarge | 16px | 大型卡片 |
| Round | 50% | 圆形图标、头像 |

### 阴影（Box Shadow）

```css
/* 悬浮阴影（卡片 hover） */
box-shadow: 0 2px 8px rgba(0, 0, 0, 0.08);

/* 浮起阴影（对话框） */
box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);

/* 深层阴影（抽屉） */
box-shadow: 0 8px 20px rgba(0, 0, 0, 0.2);

/* Focus 光晕 */
box-shadow: 0 0 0 3px rgba(24, 144, 255, 0.12);
```

---

## 📐 组件尺寸

### 按钮尺寸

| 尺寸 | 高度 | 内边距 | 字体 |
|------|------|--------|------|
| Small | 32px | 0 16px | 12px |
| Medium | 36px | 0 20px | 14px |
| Large | 40px | 0 24px | 16px |

### 输入框尺寸

| 尺寸 | 高度 | 内边距 |
|------|------|--------|
| Small | 32px | 8px 12px |
| Medium | 36px | 8px 12px |
| Large | 40px | 12px 16px |

### 图标尺寸

| 用途 | 尺寸 |
|------|------|
| 小图标（内联） | 16×16 |
| 标准图标 | 24×24 |
| 大图标 | 32×32 |
| 特大图标 | 48×48 |

---

## ⚡ 动画与过渡

### 过渡时长（Transition Duration）

```css
/* 快速（按钮、链接） */
transition: all 0.15s ease;

/* 标准（卡片、输入框） */
transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);

/* 慢速（抽屉、对话框） */
transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
```

### 缓动函数（Easing）

```css
--ease-in-out: cubic-bezier(0.4, 0, 0.2, 1);
--ease-out: cubic-bezier(0, 0, 0.2, 1);
--ease-in: cubic-bezier(0.4, 0, 1, 1);
```

---

## 🎯 图标使用

### 图标规范

- **尺寸**: 24×24 px（标准）
- **描边**: 2px
- **风格**: 线性图标（Feather Icons 风格）
- **颜色**: `currentColor`（继承父元素）

### Qt 中使用 SVG

```cpp
// 方法一：直接加载
QIcon icon(":/icons/navigation/home.svg");
button->setIcon(icon);
button->setIconSize(QSize(24, 24));

// 方法二：自定义颜色
IconButton *btn = new IconButton(":/icons/actions/camera.svg");
btn->setIconColor(QColor("#1890ff"));
```

---

## 📱 布局规范

### 主窗口结构

```
┌──────────────────────────────────────────────┐
│  TitleBar (56px)                             │
├──────────────────────────────────────────────┤
│      │                                       │
│      │                                       │
│ Side │         Main Content Area            │
│ Menu │         (CardWidget 布局)             │
│ 240px│                                       │
│      │                                       │
│      │                                       │
└──────────────────────────────────────────────┘
```

### 页面边距

```css
/* 主容器 */
padding: 24px;

/* 卡片间距 */
gap: 16px;

/* 内容区域最大宽度 */
max-width: 1440px;
```

### 栅格系统

采用 24 列栅格系统：

- **1 列**: 4.16%
- **2 列**: 8.33%
- **3 列**: 12.5%
- **6 列**: 25%
- **8 列**: 33.33%
- **12 列**: 50%
- **24 列**: 100%

---

## 🔧 Qt 实现示例

### 1. 加载 QSS 主题

```cpp
// main.cpp
#include <QApplication>
#include <QFile>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // 加载浅色主题
    QFile styleFile(":/themes/modern_theme.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QString styleSheet = QLatin1String(styleFile.readAll());
        app.setStyleSheet(styleSheet);
        styleFile.close();
    }
    
    return app.exec();
}
```

### 2. 动态切换主题

```cpp
void MainWindow::toggleTheme() {
    QString themePath = isDarkMode_ ? 
        ":/themes/modern_theme.qss" : 
        ":/themes/modern_theme_dark.qss";
    
    QFile styleFile(themePath);
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        qApp->setStyleSheet(styleFile.readAll());
        styleFile.close();
    }
    
    isDarkMode_ = !isDarkMode_;
}
```

### 3. 使用自定义属性

```cpp
// 设置按钮为 Primary 样式
QPushButton *btn = new QPushButton("确定");
btn->setProperty("buttonType", "primary");
btn->style()->polish(btn);  // 应用样式

// 设置标签为标题样式
QLabel *title = new QLabel("人脸识别");
title->setProperty("labelType", "title");
title->style()->polish(title);
```

### 4. 创建卡片布局

```cpp
CardWidget *card = new CardWidget(this);
card->setFixedHeight(200);

QVBoxLayout *cardLayout = new QVBoxLayout(card);
cardLayout->setSpacing(12);
cardLayout->setContentsMargins(24, 24, 24, 24);

QLabel *title = new QLabel("考勤统计", card);
title->setProperty("labelType", "subtitle");

QLabel *count = new QLabel("今日签到: 42人", card);

cardLayout->addWidget(title);
cardLayout->addWidget(count);
cardLayout->addStretch();
```

---

## ✅ 设计检查清单

### 颜色
- [ ] 使用规范中的颜色变量
- [ ] 确保文字与背景有足够对比度（WCAG AA标准）
- [ ] 暗色模式颜色适配

### 字体
- [ ] 字体大小符合规范
- [ ] 字重使用正确
- [ ] 行高设置合理

### 间距
- [ ] 使用 8px 基准的间距系统
- [ ] 元素之间间距一致
- [ ] 内边距和外边距合理

### 圆角与阴影
- [ ] 圆角大小符合组件规范
- [ ] 悬浮效果有阴影
- [ ] 阴影不过度使用

### 图标
- [ ] 使用 SVG 图标
- [ ] 图标大小统一（24×24）
- [ ] 图标风格一致

### 响应式
- [ ] 不同屏幕尺寸下布局合理
- [ ] 最小/最大宽度设置
- [ ] 可滚动区域正常工作

---

## 📚 参考资源

- **Ant Design**: https://ant.design/
- **Material Design**: https://material.io/
- **Fluent Design**: https://www.microsoft.com/design/fluent/
- **Feather Icons**: https://feathericons.com/

---

## 📝 更新日志

### v2.0 (2025-11-25)
- 完整重写 QSS 主题系统
- 采用现代化配色方案
- 添加暗色模式支持
- SVG 图标系统
- 完整设计规范文档

### v1.0
- 初始版本
- 基础 QSS 样式


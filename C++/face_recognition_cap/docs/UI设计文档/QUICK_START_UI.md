# 🚀 UI 现代化 - 快速启动指南

## 📦 立即使用新 UI（3步）

### 1️⃣ 确认图标资源

SVG 图标已预置在项目中：

```
gui/resources/icons/
├── actions/       # 操作图标 (plus, edit, delete, save, etc.)
├── navigation/    # 导航图标 (home, users, calendar, settings, etc.)
├── status/        # 状态图标 (check-circle, alert-circle, user, etc.)
└── ui/            # UI图标 (sun, moon, minimize, close, etc.)
```

### 2️⃣ 主题系统已集成

主题管理器在 `main_gui.cc` 中自动初始化：

```cpp
#include "themes/theme_manager.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // ✨ 主题系统自动初始化
    ThemeManager::instance()->initialize();
    
    MainWindow window;
    window.show();
    return app.exec();
}
```

### 3️⃣ 编译并运行

```bash
cd /home/firefly/open_project/edge2-npu/C++/face_recognition_cap
./build.sh
./install/face_recognition_cap/face_recognition_cap_gui
```

---

## 🎨 常用代码片段

### 主要按钮
```cpp
QPushButton* btn = new QPushButton("确定");
btn->setProperty("buttonType", "primary");
```

### 危险按钮
```cpp
QPushButton* btn = new QPushButton("删除");
btn->setProperty("buttonType", "danger");
```

### 图标按钮
```cpp
#include "widgets/icon_button.h"

IconButton* btn = new IconButton(this);
btn->setSvg(":/icons/actions/plus.svg", QSize(20, 20));
```

### 卡片组件
```cpp
#include "widgets/card_widget.h"

CardWidget* card = new CardWidget(this);
card->setTitle("标题");
auto layout = new QVBoxLayout(card->bodyContainer());
layout->addWidget(new QLabel("内容"));
```

### 主题切换
```cpp
#include "themes/theme_manager.h"

// 切换主题
ThemeManager::instance()->toggleTheme();

// 获取当前主题
auto theme = ThemeManager::instance()->currentTheme();
bool isDark = (theme == ThemeManager::ThemeType::Dark);
```

### SVG 图标加载
```cpp
#include "utils/svg_icon_manager.h"

// 加载带颜色的图标
QIcon icon = SvgIconManager::icon(":/icons/status/check-circle.svg", 
                                   QSize(20, 20), 
                                   QColor("#52c41a"));
```

---

## 📚 完整文档

| 文档 | 说明 |
|------|------|
| [UI_DESIGN_SYSTEM.md](./UI_DESIGN_SYSTEM.md) | 设计系统完整规范 |
| [UI_MIGRATION_GUIDE.md](./UI_MIGRATION_GUIDE.md) | UI迁移指南 |
| [UI_MODERNIZATION_SUMMARY.md](./UI_MODERNIZATION_SUMMARY.md) | 现代化总结 |
| [ICONS_SPEC.md](../../gui/resources/icons/ICONS_SPEC.md) | 图标使用规范 |

---

## 🎯 快速对照表

### 颜色变量
| 名称 | 浅色主题 | 暗色主题 |
|------|----------|----------|
| Primary | `#1890ff` | `#177ddc` |
| Success | `#52c41a` | `#49aa19` |
| Warning | `#faad14` | `#d89614` |
| Error | `#ff4d4f` | `#d32029` |
| Background | `#ffffff` | `#1f1f1f` |
| Text | `#262626` | `#e8e8e8` |

### 尺寸规范
| 元素 | 尺寸 |
|------|------|
| 按钮高度 | 36px (标准) / 40px (大) |
| 图标大小 | 20×20 (小) / 24×24 (标准) |
| 圆角 | 8px (标准) / 12px (卡片) |
| 间距 | 8px / 16px / 24px |
| 标题栏高度 | 56px |
| 侧边栏宽度 | 220px |

### 按钮类型
| Property | 效果 |
|----------|------|
| `buttonType="primary"` | 蓝色主要按钮 |
| `buttonType="danger"` | 红色危险按钮 |
| `buttonType="success"` | 绿色成功按钮 |
| `objectName="GhostButton"` | 透明边框按钮 |

### 组件 ObjectName
| ObjectName | 组件 |
|------------|------|
| `#SideMenu` | 侧边导航栏 |
| `#TitleBar` | 顶部标题栏 |
| `#CardWidget` | 卡片容器 |
| `#ModernTableView` | 现代表格 |

---

## 🎉 完成！

现在您的应用已经拥有现代化的 UI！

**特性：**
- ✨ 清爽、专业的界面风格
- 🌓 完整的浅色/暗色主题切换
- 🎯 SVG 图标系统
- 📱 响应式布局
- 🔊 音频反馈系统

**下一步：**
1. 查看 [设计系统规范](./UI_DESIGN_SYSTEM.md)
2. 按需自定义主题颜色
3. 添加更多自定义组件

祝使用愉快！🚀

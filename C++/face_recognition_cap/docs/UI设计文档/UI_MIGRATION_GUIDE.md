# 🚀 UI 现代化迁移指南

## 📋 概述

本文档指导如何将现有 UI 代码迁移到新的现代化设计系统。

---

## 📦 1. 目录结构（最终版）

```
face_recognition_cap/
├── gui/
│   ├── include/
│   │   ├── gui/
│   │   │   ├── main_window.h
│   │   │   ├── face_registration_dialog.h
│   │   │   └── ...
│   │   ├── ui/
│   │   │   ├── recognition_page.h
│   │   │   ├── settings_page.h
│   │   │   └── ...
│   │   ├── widgets/
│   │   │   ├── card_widget.h
│   │   │   ├── icon_button.h
│   │   │   ├── modern_table_view.h
│   │   │   └── ...
│   │   ├── themes/
│   │   │   └── theme_manager_v2.h
│   │   └── utils/
│   │       ├── ui_utils.h
│   │       └── audio_manager.h
│   ├── src/
│   │   ├── gui/
│   │   ├── ui/
│   │   ├── widgets/
│   │   ├── themes/
│   │   │   ├── modern_theme.qss           ← 新主题
│   │   │   ├── modern_theme_dark.qss      ← 暗色主题
│   │   │   └── theme_manager_v2.cc
│   │   └── utils/
│   └── resources/
│       ├── resources.qrc                   ← 统一资源文件
│       └── icons/                          ← SVG 图标目录
│           ├── navigation/
│           ├── actions/
│           ├── status/
│           └── ui/
├── docs/
│   ├── UI_DESIGN_SYSTEM.md                ← 设计规范
│   └── UI_MIGRATION_GUIDE.md              ← 本文档
└── ...
```

---

## 🔧 2. 主窗口重构建议

### 2.1 main.cpp 改动

**旧代码：**
```cpp
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
```

**新代码（推荐）：**
```cpp
#include <QApplication>
#include "gui/main_window.h"
#include "themes/theme_manager_v2.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // 设置应用程序信息
    app.setApplicationName("FaceRecognitionSystem");
    app.setOrganizationName("YourCompany");
    app.setApplicationVersion("2.0");
    
    // 初始化主题管理器
    ThemeManager::instance()->initialize();
    
    // 创建主窗口
    MainWindow window;
    window.show();
    
    return app.exec();
}
```

### 2.2 MainWindow 构造函数优化

**现有问题：**
- 所有 UI 初始化在构造函数中完成
- 代码臃肿，难以维护

**优化方案：**

```cpp
// main_window.h
class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    
private:
    void setupUI();              // UI 初始化
    void setupConnections();     // 信号槽连接
    void setupTheme();           // 主题初始化
    void loadSettings();         // 加载配置
    
private slots:
    void on_theme_toggle();      // 主题切换
    // ...
};

// main_window.cc
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUI();
    setupConnections();
    setupTheme();
    loadSettings();
}

void MainWindow::setupUI() {
    // 创建中心 Widget
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    // 主布局
    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    // 侧边栏
    SideMenu* sideMenu = new SideMenu(this);
    sideMenu->setFixedWidth(240);
    mainLayout->addWidget(sideMenu);
    
    // 内容区域
    QWidget* contentArea = new QWidget(this);
    QVBoxLayout* contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setSpacing(16);
    contentLayout->setContentsMargins(24, 24, 24, 24);
    
    // ... 添加各种卡片和组件
    
    mainLayout->addWidget(contentArea, 1);
}

void MainWindow::setupConnections() {
    // 主题切换
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MainWindow::onThemeChanged);
    
    // 其他信号槽
    // ...
}

void MainWindow::setupTheme() {
    // 应用主题
    ThemeManager::instance()->applyTheme(ThemeType::Light);
}
```

---

## 🎨 3. 组件迁移示例

### 3.1 按钮升级

**旧代码：**
```cpp
QPushButton* btn = new QPushButton("确定", this);
btn->setStyleSheet("background-color: #1890ff; color: white;");
```

**新代码：**
```cpp
QPushButton* btn = new QPushButton("确定", this);
btn->setProperty("buttonType", "primary");
btn->style()->polish(btn);  // 重新应用样式
```

**图标按钮：**
```cpp
IconButton* btnAdd = new IconButton(this);
btnAdd->setIcon(QIcon(":/icons/actions/plus.svg"));
btnAdd->setIconSize(QSize(24, 24));
btnAdd->setToolTip("添加用户");
```

### 3.2 输入框升级

**旧代码：**
```cpp
QLineEdit* input = new QLineEdit(this);
```

**新代码（带搜索图标）：**
```cpp
SearchInput* searchInput = new SearchInput(this);
searchInput->setPlaceholderText("搜索用户...");

// 或使用标准 QLineEdit + 样式
QLineEdit* input = new QLineEdit(this);
input->setProperty("inputType", "search");
input->style()->polish(input);
```

### 3.3 表格升级

**旧代码：**
```cpp
QTableWidget* table = new QTableWidget(this);
```

**新代码：**
```cpp
ModernTableView* table = new ModernTableView(this);
table->setAlternatingRowColors(true);

// 设置表头
QStringList headers = {"姓名", "工号", "部门", "状态"};
table->setHorizontalHeaderLabels(headers);

// 设置列宽
table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
```

### 3.4 卡片布局

**新代码示例：**
```cpp
CardWidget* statsCard = new CardWidget(this);
statsCard->setFixedHeight(160);

QVBoxLayout* cardLayout = new QVBoxLayout(statsCard);
cardLayout->setSpacing(12);

// 标题
QLabel* title = new QLabel("今日考勤", statsCard);
title->setProperty("labelType", "subtitle");

// 统计数字
QLabel* count = new QLabel("42", statsCard);
count->setProperty("labelType", "title");
count->setAlignment(Qt::AlignCenter);

// 说明文字
QLabel* desc = new QLabel("已签到", statsCard);
desc->setProperty("labelType", "caption");
desc->setAlignment(Qt::AlignCenter);

cardLayout->addWidget(title);
cardLayout->addWidget(count);
cardLayout->addWidget(desc);
cardLayout->addStretch();
```

### 3.5 状态标签

**新代码：**
```cpp
StatusTag* tag = new StatusTag("正常", this);
tag->setProperty("tagType", "success");
tag->style()->polish(tag);

// 不同状态
StatusTag* lateTag = new StatusTag("迟到", this);
lateTag->setProperty("tagType", "warning");

StatusTag* absentTag = new StatusTag("缺勤", this);
absentTag->setProperty("tagType", "error");
```

---

## 🔄 4. 常见问题迁移

### 4.1 去除旧的 setStyleSheet

**❌ 不推荐：**
```cpp
btn->setStyleSheet("QPushButton { background: red; }");
```

**✅ 推荐：**
```cpp
btn->setProperty("buttonType", "danger");
btn->style()->polish(btn);
```

### 4.2 PNG 图标替换为 SVG

**旧代码：**
```cpp
QIcon icon(":/images/user.png");  // 模糊、不可缩放
```

**新代码：**
```cpp
QIcon icon(":/icons/status/user.svg");  // 清晰、无限缩放
btn->setIcon(icon);
btn->setIconSize(QSize(24, 24));
```

### 4.3 颜色使用

**旧代码：**
```cpp
label->setStyleSheet("color: #1890ff;");
```

**新代码：**
```cpp
QColor primaryColor = ThemeManager::instance()->getColor("primary");
QPalette palette = label->palette();
palette.setColor(QPalette::WindowText, primaryColor);
label->setPalette(palette);
```

---

## 📝 5. 具体文件修改清单

### 5.1 需要修改的主要文件

| 文件路径 | 修改内容 | 优先级 |
|---------|---------|--------|
| `main.cpp` | 添加主题管理器初始化 | 高 |
| `gui/src/gui/main_window.cc` | 重构 UI 初始化，使用新组件 | 高 |
| `gui/src/gui/face_registration_dialog.cc` | 更新按钮、输入框样式 | 中 |
| `gui/src/ui/settings_page.cc` | 添加主题切换按钮 | 中 |
| `gui/src/ui/user_management_page.cc` | 升级表格、按钮 | 中 |
| `CMakeLists.txt` | 添加新的资源文件 | 高 |

### 5.2 需要删除的文件

**旧主题文件（可选保留作为备份）：**
- `gui/src/themes/theme.qss` → 替换为 `modern_theme.qss`
- `gui/src/themes/theme_dark.qss` → 替换为 `modern_theme_dark.qss`

**旧图标文件：**
- 检查 `data/img/` 目录中的 PNG 图标
- 如果已替换为 SVG，可以删除旧的 PNG 文件

**未使用的 UI 文件：**
```bash
# 查找未使用的 .h/.cc 文件
find gui/ -name "*.h" -o -name "*.cc" | xargs grep -L "class\|#include"
```

---

## 🎯 6. 分步骤迁移计划

### Phase 1: 基础设施（1-2天）
- [ ] 创建 SVG 图标目录
- [ ] 更新 QRC 资源文件
- [ ] 集成新的 QSS 主题
- [ ] 测试主题加载

### Phase 2: 核心组件（2-3天）
- [ ] 重构 MainWindow
- [ ] 升级自定义 Widget
- [ ] 更新 SettingsPage（添加主题切换）
- [ ] 测试基本功能

### Phase 3: 页面迁移（3-4天）
- [ ] 迁移 RecognitionPage
- [ ] 迁移 UserManagementPage
- [ ] 迁移 AttendancePage
- [ ] 更新所有对话框

### Phase 4: 测试优化（2-3天）
- [ ] 全功能测试
- [ ] 暗色模式测试
- [ ] 性能优化
- [ ] 代码清理

---

## 🔍 7. 测试检查清单

### 功能测试
- [ ] 所有按钮可点击
- [ ] 输入框正常工作
- [ ] 表格显示正确
- [ ] 对话框弹出正常
- [ ] 侧边栏导航正常

### 样式测试
- [ ] 浅色主题显示正常
- [ ] 暗色主题显示正常
- [ ] 主题切换流畅
- [ ] 所有图标清晰
- [ ] 悬停效果正常

### 响应式测试
- [ ] 窗口缩放正常
- [ ] 最小化/最大化正常
- [ ] 滚动条工作正常
- [ ] 不同分辨率下正常

---

## 💡 8. 最佳实践

### 8.1 使用 Property 而非 StyleSheet

**✅ 推荐：**
```cpp
btn->setProperty("buttonType", "primary");
btn->style()->polish(btn);
```

**❌ 避免：**
```cpp
btn->setStyleSheet("...");
```

### 8.2 统一图标尺寸

```cpp
const int ICON_SIZE_SMALL = 16;
const int ICON_SIZE_NORMAL = 24;
const int ICON_SIZE_LARGE = 32;

btn->setIconSize(QSize(ICON_SIZE_NORMAL, ICON_SIZE_NORMAL));
```

### 8.3 使用布局而非固定位置

**✅ 推荐：**
```cpp
QVBoxLayout* layout = new QVBoxLayout(widget);
layout->addWidget(label);
layout->addWidget(input);
```

**❌ 避免：**
```cpp
label->move(10, 10);
input->move(10, 40);
```

### 8.4 响应主题变化

```cpp
connect(ThemeManager::instance(), &ThemeManager::themeChanged,
        this, [this](ThemeType type) {
    // 更新自定义绘制的颜色
    updateColors();
    update();
});
```

---

## 📚 9. 参考资源

### 内部文档
- [UI 设计系统规范](UI_DESIGN_SYSTEM.md)
- [SVG 图标规范](../gui/resources/icons/ICONS_SPEC.md)

### 外部资源
- Qt QSS 文档: https://doc.qt.io/qt-5/stylesheet.html
- Ant Design: https://ant.design/
- Feather Icons: https://feathericons.com/

---

## 🐛 10. 常见问题解决

### Q1: 样式不生效？
```cpp
// 确保调用 polish() 重新应用样式
widget->setProperty("customProperty", value);
widget->style()->polish(widget);
```

### Q2: SVG 图标不显示？
```cpp
// 检查资源文件是否正确编译
qrc_resources.cpp  // 应该存在

// 检查路径是否正确
QIcon icon(":/icons/actions/plus.svg");  // 注意前缀
```

### Q3: 暗色模式颜色不对？
```cpp
// 确保两套 QSS 中都定义了相同的选择器
// modern_theme.qss 和 modern_theme_dark.qss 应该结构一致
```

### Q4: 性能问题？
```cpp
// 避免频繁 setStyleSheet
// 使用 Property + QSS 伪状态

// 避免大量重复代码
// 提取公共组件和样式
```

---

## ✅ 11. 验收标准

- [ ] 所有页面使用新的设计系统
- [ ] 没有内联 setStyleSheet
- [ ] 所有图标使用 SVG
- [ ] 支持暗色模式
- [ ] 代码结构清晰
- [ ] 无明显性能问题
- [ ] 通过所有测试用例

---

## 📝 12. 更新日志

### v2.0 (2025-11-25)
- 完整 UI 现代化迁移指南
- 新设计系统集成
- SVG 图标系统
- 主题管理器 v2.0

---

**祝迁移顺利！🎉**

如有问题，请参考设计规范文档或联系开发团队。


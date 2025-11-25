# 🎨 SVG 图标规范

## 📐 设计规范

| 属性 | 值 |
|------|-----|
| **尺寸** | 24×24 px |
| **视口** | `viewBox="0 0 24 24"` |
| **描边宽度** | 2px |
| **圆角** | round |
| **填充** | none（线性图标） |
| **颜色** | currentColor（继承父元素） |
| **风格** | 简约、现代、统一线条 |

## 📦 图标分类

### 1. Navigation（导航图标）- `/icons/navigation/`

| 文件名 | 说明 | 使用位置 |
|--------|------|----------|
| `home.svg` | 首页 | 侧边栏 - 首页 |
| `users.svg` | 用户管理 | 侧边栏 - 用户列表 |
| `calendar.svg` | 考勤管理 | 侧边栏 - 考勤记录 |
| `settings.svg` | 设置 | 侧边栏 - 系统设置 |
| `menu.svg` | 菜单展开 | 顶部栏 - 菜单按钮 |
| `arrow-left.svg` | 返回 | 导航返回 |
| `chevron-left.svg` | 左箭头 | 分页、导航 |
| `chevron-right.svg` | 右箭头 | 分页、导航 |
| `chevron-down.svg` | 下箭头 | 下拉菜单 |
| `chevron-up.svg` | 上箭头 | 折叠菜单 |

### 2. Actions（操作图标）- `/icons/actions/`

| 文件名 | 说明 | 使用位置 |
|--------|------|----------|
| `plus.svg` | 添加 | 添加用户、注册人脸 |
| `edit.svg` | 编辑 | 编辑用户信息 |
| `delete.svg` | 删除 | 删除用户 |
| `save.svg` | 保存 | 保存配置 |
| `search.svg` | 搜索 | 搜索框图标 |
| `filter.svg` | 筛选 | 数据筛选 |
| `refresh.svg` | 刷新 | 刷新列表 |
| `download.svg` | 下载 | 导出数据 |
| `upload.svg` | 上传 | 上传图片 |
| `camera.svg` | 摄像头 | 开启摄像头 |
| `camera-off.svg` | 关闭摄像头 | 关闭摄像头 |
| `play.svg` | 播放 | 开始识别 |
| `pause.svg` | 暂停 | 暂停识别 |
| `stop.svg` | 停止 | 停止识别 |
| `check.svg` | 确认 | 复选框勾选 |
| `close.svg` | 关闭 | 关闭窗口、对话框 |
| `eye.svg` | 查看 | 查看详情 |
| `eye-off.svg` | 隐藏 | 隐藏密码 |
| `copy.svg` | 复制 | 复制文本 |

### 3. Status（状态图标）- `/icons/status/`

| 文件名 | 说明 | 使用位置 |
|--------|------|----------|
| `check-circle.svg` | 成功 | 签到成功、操作成功 |
| `alert-circle.svg` | 警告 | 迟到、早退警告 |
| `x-circle.svg` | 错误 | 操作失败 |
| `info.svg` | 信息 | 提示信息 |
| `clock.svg` | 时间 | 考勤时间显示 |
| `user.svg` | 用户 | 用户信息 |
| `user-check.svg` | 用户已验证 | 识别成功 |
| `user-x.svg` | 用户未识别 | 陌生人 |
| `wifi.svg` | 网络连接 | 网络状态 |
| `wifi-off.svg` | 网络断开 | 网络错误 |

### 4. UI Elements（UI 元素）- `/icons/ui/`

| 文件名 | 说明 | 使用位置 |
|--------|------|----------|
| `sun.svg` | 浅色主题 | 主题切换 |
| `moon.svg` | 深色主题 | 主题切换 |
| `maximize.svg` | 最大化 | 窗口控制 |
| `minimize.svg` | 最小化 | 窗口控制 |
| `bell.svg` | 通知 | 消息通知 |
| `bell-off.svg` | 静音 | 关闭通知 |
| `volume.svg` | 音量 | 音频控制 |
| `volume-off.svg` | 静音 | 关闭音频 |
| `help-circle.svg` | 帮助 | 帮助文档 |
| `more-vertical.svg` | 更多操作 | 更多菜单 |
| `more-horizontal.svg` | 更多操作 | 更多选项 |
| `log-out.svg` | 退出 | 退出系统 |

## 🎨 示例 SVG 代码

### 标准图标模板
```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" 
     fill="none" stroke="currentColor" stroke-width="2" 
     stroke-linecap="round" stroke-linejoin="round">
    <!-- 图标路径 -->
</svg>
```

### 示例：home.svg
```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" 
     fill="none" stroke="currentColor" stroke-width="2" 
     stroke-linecap="round" stroke-linejoin="round">
    <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>
    <polyline points="9 22 9 12 15 12 15 22"/>
</svg>
```

### 示例：users.svg
```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" 
     fill="none" stroke="currentColor" stroke-width="2" 
     stroke-linecap="round" stroke-linejoin="round">
    <path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/>
    <circle cx="9" cy="7" r="4"/>
    <path d="M23 21v-2a4 4 0 0 0-3-3.87"/>
    <path d="M16 3.13a4 4 0 0 1 0 7.75"/>
</svg>
```

### 示例：camera.svg
```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" 
     fill="none" stroke="currentColor" stroke-width="2" 
     stroke-linecap="round" stroke-linejoin="round">
    <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"/>
    <circle cx="12" cy="13" r="4"/>
</svg>
```

## 💻 Qt Widgets 中使用 SVG

### 方法一：QIcon + SVG（推荐）

```cpp
// 加载 SVG 图标
QIcon icon(":/icons/navigation/home.svg");

// 设置按钮图标
ui->btnHome->setIcon(icon);
ui->btnHome->setIconSize(QSize(24, 24));

// SVG 自动支持缩放，不会模糊
ui->largeButton->setIconSize(QSize(48, 48));  // 48×48也清晰
```

### 方法二：QSvgWidget（适用于大图标）

```cpp
#include <QSvgWidget>

QSvgWidget *svgWidget = new QSvgWidget(":/icons/actions/camera.svg");
svgWidget->setFixedSize(64, 64);
layout->addWidget(svgWidget);
```

### 方法三：自定义IconButton（带颜色切换）

```cpp
class IconButton : public QPushButton {
    Q_OBJECT
public:
    explicit IconButton(const QString& svgPath, QWidget* parent = nullptr)
        : QPushButton(parent), svgPath_(svgPath) {
        loadIcon();
    }
    
    void setIconColor(const QColor& color) {
        color_ = color;
        loadIcon();
    }
    
private:
    void loadIcon() {
        // 读取 SVG 并替换颜色
        QFile file(svgPath_);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString svgContent = file.readAll();
            svgContent.replace("currentColor", color_.name());
            
            QSvgRenderer renderer(svgContent.toUtf8());
            QPixmap pixmap(iconSize());
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            renderer.render(&painter);
            
            setIcon(QIcon(pixmap));
        }
    }
    
    QString svgPath_;
    QColor color_{Qt::black};
};
```

## 📦 推荐图标库

如果需要快速获取这些图标，推荐使用以下开源图标库：

1. **Feather Icons** - https://feathericons.com/
   - 简约、清爽、线性风格
   - 完美契合现代 UI

2. **Lucide Icons** - https://lucide.dev/
   - Feather 的升级版本
   - 更多图标选择

3. **Heroicons** - https://heroicons.com/
   - Tailwind CSS 官方图标
   - 两种风格：outline & solid

## 🎯 使用建议

1. **统一尺寸**：所有图标使用 24×24 px
2. **统一风格**：选择一个图标库，不要混用
3. **颜色继承**：使用 `currentColor` 让图标继承父元素颜色
4. **SVG 优化**：使用 SVGO 工具优化 SVG 文件大小
5. **命名规范**：使用小写 + 连字符（kebab-case）

## 📄 文件命名规范

```
icons/
├── navigation/
│   ├── home.svg
│   ├── users.svg
│   └── settings.svg
├── actions/
│   ├── plus.svg
│   ├── edit.svg
│   └── delete.svg
├── status/
│   ├── check-circle.svg
│   └── alert-circle.svg
└── ui/
    ├── sun.svg
    └── moon.svg
```

## 🔧 自动化脚本

### 批量下载图标（示例）
```bash
#!/bin/bash
# download_icons.sh

ICON_BASE_URL="https://api.feathericons.com/icons"
ICONS_DIR="gui/resources/icons"

icons=(
    "home" "users" "calendar" "settings" 
    "plus" "edit" "delete" "search"
    "camera" "check" "x" "info"
)

for icon in "${icons[@]}"; do
    curl -o "$ICONS_DIR/navigation/$icon.svg" "$ICON_BASE_URL/$icon.svg"
done
```

## ✅ 检查清单

- [ ] 所有图标统一 24×24 尺寸
- [ ] 使用 currentColor 支持颜色继承
- [ ] 优化 SVG 文件大小
- [ ] 测试暗色模式下的显示效果
- [ ] 确保图标在不同 DPI 下清晰
- [ ] 所有图标命名符合规范


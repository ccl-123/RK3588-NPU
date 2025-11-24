# Qt Widgets 现代化 UI 方案（Ant Design 风）

## 1. 主界面框架设计

### 1.1 布局与区域规范
- **窗口结构**：左侧固定导航（240px）+ 顶部 TitleBar（64px）+ 主内容区（剩余空间）。
- **内容区内边距**：全局 32px，卡片之间 24px，卡片内元素 16px/12px。
- **栅格体系**：12 列布局，每列间距 16px，卡片可跨 3/4/6/12 列。
- **留白策略**：背景灰（#f5f6fb），卡片白底，利用阴影区分层级，不使用立体描边。
- **交互规范**：所有交互控件提供 hover/pressed/disabled 状态；菜单与按钮使用 8px 圆角；卡片 16px。

### 1.2 控件组织示例（简化 UML）
```
MainWindow : QMainWindow
 ├─ TitleBar : QWidget
 │   ├─ LogoLabel
 │   ├─ BreadcrumbLabel
 │   └─ UserArea (IconButton × n, AvatarMenu)
 ├─ BodyHost : QWidget (QHBoxLayout)
 │   ├─ SideMenu : QWidget (固定宽度)
 │   └─ ContentWrapper : QWidget (QVBoxLayout)
 │       ├─ PageHeader : QWidget（面包屑/搜索/操作）
 │       └─ RouterStack : QStackedWidget
 │           ├─ DashboardPage
 │           ├─ RecognitionPage
 │           ├─ AttendancePage
 │           ├─ UserManagementPage
 │           └─ SettingsPage
 └─ ToastLayer : QWidget (overlay, 右上角)
```

### 1.3 主页面切换
- `RouterStack` 基于 `QStackedWidget`，封装 `UiRouter`（存储 key→index 映射、懒加载页面）。
- `SideMenu` 发射 `routeChanged(const QString& key)`，`MainWindow` 监听后调用 `router->push(key)`。
- 支持 `QPropertyAnimation` 做滑入/淡入，动画时间 180–220ms。

### 1.4 卡片封装（CardWidget）
- 继承 `QFrame`，内部 `QVBoxLayout`，分 `header/body/footer` 槽。
- Header 支持标题、副标题、操作区；Body 作为容器暴露 `bodyContainer()` 供外部布局。
- 统一 ObjectName=`"CardWidget"`，便于 QSS 设置阴影/圆角。
- 额外属性：`variant`（default/ghost/dark）、`loading`（显示骨架视图）。

## 2. 现代化组件设计

### 2.1 SideMenu
- **用途**：全局路由导航，支持图标、徽标、二级菜单。
- **实现**：`QListView + QStandardItemModel + QStyledItemDelegate` 自绘。
- **接口**：
  ```cpp
  struct MenuItem { QString key, icon, text, badge; QList<MenuItem> children; };
  void setItems(const QList<MenuItem>& items);
  void setActiveKey(const QString& key);
  signals:
      void routeChanged(const QString& key);
  ```
- **QSS 钩子**：`SideMenu QListView::item`、`SideMenu QListView::item:selected`，通过 `property("level")` 调整缩进。
- **动画**：选中高亮条 `QPropertyAnimation` 平滑移动；子菜单折叠用 `QParallelAnimationGroup` 控制高度和透明度。

### 2.2 TitleBar
- **用途**：替代原 `QMenuBar/QToolBar`，提供 Logo、搜索、通知、用户菜单。
- **实现**：`QWidget` 的自定义布局，结合 `FramelessHelper` 支持无边框拖动。
- **接口**：
  ```cpp
  void setBreadcrumb(const QStringList& crumbs);
  void setUserMenu(QMenu* menu);
  void setSecondaryActions(const QList<QAction*>& actions);
  signals:
      void requestMinimize();
      void requestClose();
  ```
- **QSS**：`TitleBar` 对象名，自定义渐变背景、底部阴影线。
- **拖拽**：重载 `mousePressEvent/mouseMoveEvent`，配合 `QWindow::startSystemMove` 或手写 frame-less 拖动逻辑。

### 2.3 CardWidget
- **接口**：
  ```cpp
  void setTitle(const QString&);
  void setSubtitle(const QString&);
  void setHeaderWidget(QWidget*);
  QWidget* bodyContainer();
  void setFooterWidget(QWidget*);
  void setVariant(const QString& variant); // default/ghost/dark
  ```
- **示例**：
  ```cpp
  auto statsCard = new CardWidget();
  statsCard->setTitle("今日有效签到");
  statsCard->setSubtitle("同比 +12%");
  auto valueLabel = new QLabel("1,238");
  valueLabel->setObjectName("StatsValue");
  statsCard->bodyContainer()->setLayout(new QVBoxLayout);
  statsCard->bodyContainer()->layout()->addWidget(valueLabel, 0, Qt::AlignCenter);
  ```
- **QSS**：`CardWidget[variant="ghost"] { background: transparent; border: 1px dashed #d9d9d9; }`.

### 2.4 IconButton
- **用途**：TitleBar、工具栏、状态区的统一图标按钮。
- **实现**：继承 `QToolButton`，默认 `Qt::ToolButtonIconOnly`。
- **接口**：
  ```cpp
  void setSvg(const QString& path, const QSize& size = {20,20});
  void setBadge(int count);
  ```
- **状态动画**：`QPropertyAnimation` 控制 `backgroundColor` 属性（使用 `Q_GADGET` + `Q_PROPERTY`）。
- **QSS**：`IconButton { border-radius:8px; padding:8px; } IconButton:hover { background: rgba(22,119,255,0.12); }`.

### 2.5 SearchInput
- **用途**：统一的搜索/过滤输入。
- **实现**：`QWidget` + `QHBoxLayout` 包含图标（`QLabel`）与 `QLineEdit`；或继承 `QLineEdit` 重绘左侧留白。
- **接口**：
  ```cpp
  void setPlaceholder(const QString&);
  void setDebounceInterval(int ms);
  signals:
      void searchTriggered(const QString& text);
  ```
- **QSS**：`SearchInput QLineEdit { padding-left: 32px; background: #fff; }`.

### 2.6 ModernTableView
- **用途**：统一表格排版（考勤、用户）。
- **实现**：`QTableView` 子类 + 自定义 Header（可显示排序图标、分组标题）。
- **特性**：行高 48px，表头 44px，hover 高亮，空状态/加载骨架，分页外挂 `PaginationWidget`。
- **QSS**：`ModernTableView::item:selected { background: rgba(22,119,255,0.16); }`.

### 2.7 StatusTag
- **用途**：显示启用/禁用、在线/离线等状态。
- **实现**：`QLabel` + `enum Type {Default, Success, Warning, Error, Processing}`。
- **接口**：`void setType(Type type); void setClosable(bool);`.
- **QSS**：`StatusTag[type="success"] { background:#f6ffed; color:#52c41a; border:1px solid #b7eb8f; }`.

### 2.8 ToastNotification
- **用途**：右上角消息提示。
- **实现**：`ToastManager` 单例维护 `QVBoxLayout`，每条 toast 为 `ToastItem`（QFrame）+ `QPropertyAnimation`。
- **接口**：
  ```cpp
  enum Level { Info, Success, Warning, Error };
  static void post(QWidget* anchor, const QString& title,
                   const QString& desc, Level level,
                   int duration = 3000);
  ```
- **动画**：入场 `opacity 0→1` + `y -12→0`，退出反向；同时管理最大显示条数=4。
- **QSS**：`ToastItem[level="warning"] { border-left: 4px solid #faad14; }`.

## 3. 页面布局方案

### 3.1 Dashboard
- **结构**：`PageHeader`（标题+日期范围+搜索），下方三行：
  1. KPI 卡片（4× CardWidget）。
  2. 左：趋势图（Card + chart view），右：状态分布饼图/Top5 列表。
  3. 底部：`ModernTableView` 展示最近签到/异常。
- **组件组合**：`CardWidget` + `StatusTag` + `IconButton` + 图表（可用 `QtCharts` 或 `QCustomPlot`）。
- **QSS 标签**：根对象 `DashboardPage`，卡片 `CardWidget[kpi="true"]`。

### 3.2 人脸识别
- **结构**：左右两列。
  - 左侧：视频卡（大 Card，嵌入 `VideoDisplayWidget`），底部放控制按钮（Start/Stop，截屏）。
  - 右侧：上半 `CardWidget` 显示实时识别信息（头像、姓名、相似度、`StatusTag`）；下半 `ModernTableView` 展示今日签到记录。
- **状态栏**：TitleBar 下方一条 `StatusStrip`，显示摄像头/模型/数据库状态（图标 + 色块）。
- **风格**：深背景视频卡 `variant="dark"`，右侧卡片 `variant="default"`。
- **QSS**：`VideoCard CardWidget { background:#0f172a; color:#fff; }`.

### 3.3 考勤记录
- **结构**：顶部过滤条（Card）包含 `SearchInput`、时间范围、部门/状态下拉、`IconButton`(过滤/重置)。
  中部 `ModernTableView` + 分页器（底部居右）。
  右侧可选 `SummaryCard`（统计本月出勤率等）。
- **QSS**：过滤条 `CardWidget[class="filter"] { border:1px solid #d9d9d9; background:#fff; }`.

### 3.4 用户管理
- **结构**：左侧表格 + 工具栏（新增/批量/导出），右侧详情卡（用户信息、最近签到）。
- **对话框**：`UserFormDialog` 使用 `CardWidget` 风格 footer（主按钮 + 次按钮 120px 间距）。
- **QSS**：表单控件命名 `FormField`，标签 `FormLabel { color:#8c8c8c; text-transform:uppercase; }`.

### 3.5 设置
- **结构**：两列 card，每个 card 内使用 `QFormLayout` + `SwitchButton`、`ComboBox`、`IconButton`。
- **分组**：`SectionHeader`（Label + Divider）区分“系统”“模型”“通知”。
- **QSS**：`SwitchButton[checked="true"] { background:#1677ff; }`.

## 4. QSS 主题设计

### 4.1 色板与字体
| 类型      | 颜色值 |
|-----------|--------|
| Primary   | #1677ff |
| Success   | #52c41a |
| Warning   | #faad14 |
| Danger    | #ff4d4f |
| Info      | #13c2c2 |
| Text 主/次/弱 | #1f1f1f / #595959 / #8c8c8c |
| 背景 Base/Content | #f5f6fb / #ffffff |
| 边框      | #e5e6eb |

字体：`"Tencent Sans","Source Han Sans","Microsoft YaHei",sans-serif`；字号 32/24/20/16/14/12，行高 1.4。

### 4.2 控件样式
- **按钮**：高度 36px，圆角 8px。Hover 亮度 +8%，Pressed -6%，禁用 `#f0f0f0`。
- **输入框**：圆角 12px，边框 `#d9d9d9`，Focus 时 `border:#1677ff` + 外阴影 `0 0 0 3px rgba(22,119,255,0.12)`。
- **表格**：表头 44px，背景 #fafafa，文字 #262626；行高 48px，hover `rgba(22,119,255,0.08)`。
- **卡片**：`border-radius:16px; border:1px solid #eef0f6; box-shadow:0 10px 30px rgba(15,23,42,0.08)`。
- **滚动条**：宽 6px，thumb `rgba(0,0,0,0.25)`，hover `rgba(0,0,0,0.35)`，track 透明。
- **导航选中**：背景 `rgba(22,119,255,0.12)`，左侧 4px Primary 条。

### 4.3 QSS 骨架
```css
/* theme.qss */
@define-color primary #1677ff;
@define-color success #52c41a;
/* ... */

QWidget {
    font-family: "Tencent Sans","Source Han Sans","Microsoft YaHei",sans-serif;
    color: #1f1f1f;
    background: #f5f6fb;
}

CardWidget {
    background: #fff;
    border-radius: 16px;
    border: 1px solid #eef0f6;
    padding: 24px;
    box-shadow: 0 10px 30px rgba(15,23,42,0.08);
}

SideMenu QListView::item {
    padding: 12px 18px;
    border-radius: 12px;
}
SideMenu QListView::item:selected {
    background: rgba(22,119,255,0.12);
    color: #1677ff;
}

ToastItem {
    border-radius: 12px;
    padding: 16px 20px;
    background: #fff;
    border-left: 4px solid @primary;
}
```

## 5. 图标体系

- **分类**：
  - 导航：dashboard、camera、attendance、users、settings。
  - 操作：add、edit、delete、export、search、filter、refresh、play/pause。
  - 状态：success、warning、error、info、offline、loading。
- **尺寸建议**：导航 24×24，操作 20×20，状态 16×16，小型提示 14×14。
- **目录结构**：`gui/assets/icons/nav/*.svg`, `.../actions/*.svg`, `.../status/*.svg`。
- **加载**：
  ```cpp
  QIcon SvgIconManager::icon(const QString& name, const QSize& size, const QColor& color) {
      QPixmap pix(size);
      pix.fill(Qt::transparent);
      QSvgRenderer renderer(QString(":/icons/%1").arg(name));
      QPainter p(&pix);
      if (color.isValid()) {
          p.setCompositionMode(QPainter::CompositionMode_Source);
          p.fillRect(pix.rect(), color);
          p.setCompositionMode(QPainter::CompositionMode_SourceIn);
      }
      renderer.render(&p);
      return QIcon(pix);
  }
  ```
- **QSS 着色 (mask)**：
  ```css
  IconButton#nav-dashboard {
      background-color: transparent;
      mask: url(:/icons/nav/dashboard.svg) center/20px 20px no-repeat;
      -qt-mask-origin: content;
  }
  IconButton#nav-dashboard[active="true"] {
      background-color: #1677ff;
  }
  ```

## 6. 动画与交互

| 场景 | 动画 | 说明 |
|------|------|------|
| 页面切换 | `ContentWrapper` 的 `pos`/`opacity` `QPropertyAnimation`（200ms OutCubic） | 左右滑动 + 渐隐 |
| SideMenu 选择 | 自定义 `selectionBar` 属性  | `QVariantAnimation` 驱动 y 位置 |
| 按钮 Hover | 自定义 `qproperty-hoverProgress` (0→1) | 用于渐变背景/阴影 |
| Card Hover | 阴影 `blurRadius` 与 `yOffset` 动画 | 150ms linear |
| Toast | 进入/退出 `opacity` + `y` | 控制停留时间 |
| 状态提示 | 面包屑/Tag 切换 `QPropertyAnimation` | 平滑过渡 |

页面性能：动画帧率保持 60fps；必要时禁用阴影以适配低性能设备。

## 7. 工程结构与维护

```
gui/src/
├─ main_window.h/.cc
├─ ui/
│   ├─ dashboard_page.h/.cc
│   ├─ recognition_page.h/.cc
│   ├─ attendance_page.h/.cc
│   ├─ user_management_page.h/.cc
│   └─ settings_page.h/.cc
├─ widgets/
│   ├─ side_menu.h/.cc
│   ├─ title_bar.h/.cc
│   ├─ card_widget.h/.cc
│   ├─ icon_button.h/.cc
│   ├─ search_input.h/.cc
│   ├─ modern_table_view.h/.cc
│   ├─ status_tag.h/.cc
│   └─ toast_notification.h/.cc
├─ themes/
│   ├─ theme.qss
│   ├─ components/
│   └─ palettes.h
├─ assets/
│   └─ icons/...
└─ utils/
    ├─ svg_icon_manager.h/.cc
    ├─ ui_router.h/.cc
    └─ animation_utils.h/.cc
```

- **依赖方向**：`widgets` 不依赖业务；`ui/<page>` 组合 `widgets`; `MainWindow` 只依赖 `widgets` 与 `ui`。
- **样式加载**：`ThemeManager` 按主题枚举加载不同 qss（支持动态切换）。
- **资源管理**：统一 `ui_resources.qrc`，使用别名 `:/ui/icons/...`。
- **测试**：`tests/ui/` 添加 `QTest` 用例验证 SideMenu 选中逻辑、Toast 生命周期；使用截图回归（`QPixmap::grabWidget`）。
- **开发流程**：新增页面先定义布局草图→实现 `PageView` → 接入 Router → 编写 QSS 片段 → 添加示例数据与单元测试。

> 以上方案可在保持现有业务逻辑的前提下，实现 Ant Design 风格的 Qt Widgets 现代化 UI，通过组件化、主题化、动画化手段提升整体一致性与可维护性。


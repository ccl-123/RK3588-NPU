<!-- 7c70db4d-cad2-4adf-ac55-f177aed1fa2e 13943b24-0765-4001-b12f-cdd2965e8799 -->
# 修复QML系统所有Bug

## 一、修复签到功能（最关键）

### 1.1 实现 is_duplicate_check

- **文件**: `src/service/attendance_service.cc`
- **问题**: 第67-69行函数体为空
- **修复**: 查询数据库，检查用户在指定时间内是否已签到
- **逻辑**: 查询最近5分钟内该用户的记录，如果存在则返回true

### 1.2 连接识别回调到签到

- **文件**: `src/main_qml.cpp`
- **问题**: recognition_callback 中没有调用 record_attendance
- **修复**: 
  - 调用 `attendance_service->record_attendance()`
  - 根据返回值判断是否新签到
  - 传递 isNewAttendance 给 RecognitionManager

### 1.3 添加签到成功提示

- **文件**: `qml/pages/FaceRecognitionPage.qml`
- **修复**: 在视频画面上叠加"已签到"提示（3秒后消失）
- **位置**: 画面顶部中央，绿色背景

## 二、重新设计人脸识别页面布局

### 2.1 新布局方案

```
┌─────────────────────────────────────────┐
│  [摄像头选择] [刷新] [启动/停止]       │
├─────────────────────────────────────────┤
│                                         │
│         [视频显示区域]                  │
│         （包含"已签到"叠加层）          │
│                                         │
├─────────────────────────────────────────┤
│  [识别日志]                             │
│  最近10条识别记录                        │
├─────────────────────────────────────────┤
│  [用户列表]  |  [今日已签到]           │
│   3个用户    |    5人已签到             │
└─────────────────────────────────────────┘
```

### 2.2 修改文件

- `qml/pages/FaceRecognitionPage.qml`: 重新布局为单列
- 添加签到提示叠加层
- 底部分左右两栏：用户列表 + 已签到列表

## 三、修复Segmentation Fault

### 3.1 潜在原因

- Qt对象在错误线程访问
- 空指针解引用（user_service或attendance_service）
- UserManager/AttendanceManager构造时service为nullptr

### 3.2 修复措施

- **文件**: `qml_src/managers/*.cpp`
- 所有Manager中添加nullptr检查
- 使用 Qt::QueuedConnection 跨线程调用
- 确保service指针有效性

### 3.3 按钮无响应问题

- 检查所有 onClicked 信号连接
- 添加 console.log 调试输出
- 确保Manager方法正确暴露（Q_INVOKABLE）

## 四、优化深色主题

### 4.1 调整颜色对比度

- **文件**: `qml/styles/Theme.qml`
- **问题**: 深色模式下textPrimary为白色但背景也偏亮
- **修复**: 
  - textPrimary: "#FFFFFF" → "#E8E8E8"
  - textSecondary: "#B0B0B0" → "#A0A0A0"  
  - 确保所有文字颜色使用 Theme.colors.textXxx

### 4.2 检查所有Label

- 确保没有硬编码颜色
- 统一使用 Theme.colors.textPrimary/Secondary

## 五、添加签到列表显示

### 5.1 创建TodayAttendanceList组件

- **文件**: `qml/components/data/TodayAttendanceList.qml`
- 显示今日已签到用户
- 数据源：AttendanceManager.recordsList（筛选今日+签到类型）

### 5.2 集成到人脸识别页面

- 右下角显示今日签到列表
- 实时更新（有新签到时自动刷新）

## 六、修复is_duplicate_check实现

### 6.1 完善attendance_service.cc

- 实现 is_duplicate_check 函数体
- 查询用户最近N秒内的记录
- SQL: `SELECT * FROM attendance_records WHERE user_id=? AND check_time > ?`

### 6.2 在main_qml.cpp中正确调用

- 先调用 record_attendance
- 返回值 > 0 表示新签到
- 返回值 = -1 表示重复，忽略

## 修复优先级

1. **签到功能** - 最高优先级
2. **Segmentation Fault** - 高优先级  
3. **布局优化** - 中优先级
4. **主题颜色** - 低优先级

### To-dos

- [x] 查看现有UI交互逻辑和FaceRecognitionApp接口
- [x] 创建QML项目目录结构
- [x] 实现C++ Manager类(VideoStreamManager等)
- [x] 实现QML主题系统(Theme.qml)
- [x] 实现QML基础组件(Card, Button等)
- [x] 实现Main.qml主框架
- [x] 实现人脸识别页面(FaceRecognitionPage.qml)
- [x] 更新CMakeLists.txt支持QML
- [x] 创建main_qml.cpp入口文件
- [x] 测试编译和运行
- [x] 修复画面闪烁和设置默认摄像头21
- [x] 创建UserManager和AttendanceManager
- [x] 创建基础UI组件(Input, Badge, Modal等)
- [x] 实现用户管理页面
- [x] 实现考勤记录页面
- [x] 实现仪表盘页面
- [x] 实现系统设置页面
- [ ] 实现人脸注册对话框
- [x] 删除gui目录和旧GUI代码
- [x] 更新CMakeLists.txt移除GUI配置
- [x] 完整测试所有功能
- [x] 
- [x] 
- [x] 
- [ ] 查看现有UI交互逻辑和FaceRecognitionApp接口
- [ ] 创建QML项目目录结构
- [ ] 实现C++ Manager类(VideoStreamManager等)
- [ ] 实现QML主题系统(Theme.qml)
- [ ] 实现QML基础组件(Card, Button等)
- [ ] 实现Main.qml主框架
- [ ] 实现人脸识别页面(FaceRecognitionPage.qml)
- [ ] 更新CMakeLists.txt支持QML
- [ ] 创建main_qml.cpp入口文件
- [ ] 测试编译和运行
- [ ] 实现is_duplicate_check和签到功能
- [ ] 修复Segmentation Fault和按钮无响应
- [ ] 重新设计人脸识别页面布局
- [ ] 优化深色主题颜色对比度
- [ ] 添加今日签到列表
- [ ] 添加签到成功提示叠加层
-- ============================================
-- 人脸识别考勤系统数据库初始化脚本
-- 数据库类型: SQLite
-- 创建日期: 2025-11-20
-- ============================================

-- 1. 用户信息表
CREATE TABLE IF NOT EXISTS users (
    user_id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_name       VARCHAR(50) NOT NULL UNIQUE,      -- 姓名
    employee_id     VARCHAR(50) UNIQUE,               -- 工号
    department      VARCHAR(100),                     -- 部门
    position        VARCHAR(100),                     -- 职位
    phone           VARCHAR(20),                      -- 电话
    email           VARCHAR(100),                     -- 邮箱
    photo_path      VARCHAR(255),                     -- 照片路径
    status          INTEGER DEFAULT 1,                -- 状态: 1=启用, 0=禁用
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 用户表索引
CREATE INDEX IF NOT EXISTS idx_user_name ON users(user_name);
CREATE INDEX IF NOT EXISTS idx_employee_id ON users(employee_id);
CREATE INDEX IF NOT EXISTS idx_status ON users(status);

-- 2. 人脸特征表
CREATE TABLE IF NOT EXISTS face_features (
    feature_id      INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL,                 -- 关联用户ID
    feature_vector  BLOB NOT NULL,                    -- 512维特征向量(2048字节)
    feature_quality REAL DEFAULT 0.0,                 -- 特征质量分数
    source_image    VARCHAR(255),                     -- 来源图片路径
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
);

-- 特征表索引
CREATE INDEX IF NOT EXISTS idx_feature_user_id ON face_features(user_id);

-- 3. 考勤记录表
CREATE TABLE IF NOT EXISTS attendance_records (
    record_id       INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL,                 -- 关联用户ID
    user_name       VARCHAR(50) NOT NULL,             -- 冗余姓名(查询优化)
    check_time      DATETIME NOT NULL,                -- 打卡时间
    check_type      INTEGER DEFAULT 1,                -- 类型: 1=签到, 2=签退
    similarity      REAL,                             -- 识别相似度
    face_image      VARCHAR(255),                     -- 抓拍照片路径
    device_id       VARCHAR(50),                      -- 设备编号
    location        VARCHAR(100),                     -- 位置
    status          INTEGER DEFAULT 1,                -- 状态: 1=正常, 2=迟到, 3=早退
    remark          TEXT,                             -- 备注
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
);

-- 考勤记录索引
CREATE INDEX IF NOT EXISTS idx_attendance_user_id ON attendance_records(user_id);
CREATE INDEX IF NOT EXISTS idx_attendance_check_time ON attendance_records(check_time);
CREATE INDEX IF NOT EXISTS idx_attendance_date ON attendance_records(DATE(check_time));
CREATE INDEX IF NOT EXISTS idx_attendance_user_date ON attendance_records(user_id, DATE(check_time));

-- 4. 考勤规则表
CREATE TABLE IF NOT EXISTS attendance_rules (
    rule_id         INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_name       VARCHAR(100) NOT NULL,            -- 规则名称
    work_start_time TEXT NOT NULL,                    -- 上班时间 (HH:MM:SS)
    work_end_time   TEXT NOT NULL,                    -- 下班时间 (HH:MM:SS)
    late_threshold  INTEGER DEFAULT 15,               -- 迟到阈值(分钟)
    early_threshold INTEGER DEFAULT 30,               -- 早退阈值(分钟)
    duplicate_interval INTEGER DEFAULT 300,           -- 重复打卡间隔(秒)
    is_active       INTEGER DEFAULT 1,                -- 是否启用
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 5. 系统日志表
CREATE TABLE IF NOT EXISTS system_logs (
    log_id          INTEGER PRIMARY KEY AUTOINCREMENT,
    log_level       INTEGER,                          -- 级别: 1=INFO, 2=WARN, 3=ERROR
    log_type        VARCHAR(50),                      -- 类型: RECOGNITION, DATABASE, SYSTEM
    message         TEXT,                             -- 日志内容
    user_id         INTEGER,                          -- 相关用户
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 日志表索引
CREATE INDEX IF NOT EXISTS idx_log_created_at ON system_logs(created_at);
CREATE INDEX IF NOT EXISTS idx_log_type ON system_logs(log_type);

-- 插入默认考勤规则
INSERT INTO attendance_rules (rule_name, work_start_time, work_end_time, late_threshold, early_threshold, duplicate_interval, is_active)
VALUES ('默认规则', '09:00:00', '18:00:00', 15, 30, 300, 1);

-- 插入测试用户(可选)
-- INSERT INTO users (user_name, employee_id, department, position, status)
-- VALUES ('测试用户', 'EMP001', '研发部', '工程师', 1);

-- 显示表结构
.schema users
.schema face_features
.schema attendance_records
.schema attendance_rules
.schema system_logs

-- 完成提示
SELECT '数据库初始化完成!' AS message;


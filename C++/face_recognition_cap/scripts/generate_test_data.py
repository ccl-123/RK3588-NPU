#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成人脸识别考勤系统的测试数据
生成最近30天的完整考勤记录
"""

import sqlite3
import random
from datetime import datetime, timedelta

# 用户信息
USERS = [
    ('张三', 'EMP001', '技术部', '高级工程师'),
    ('李四', 'EMP002', '技术部', '工程师'),
    ('王五', 'EMP003', '市场部', '市场经理'),
    ('赵六', 'EMP004', '市场部', '市场专员'),
    ('孙七', 'EMP005', '行政部', '行政主管'),
    ('周八', 'EMP006', '行政部', '行政助理'),
    ('吴九', 'EMP007', '财务部', '财务经理'),
    ('郑十', 'EMP008', '财务部', '财务专员'),
]

# 员工表现等级（影响迟到/早退概率）
PERFORMANCE = {
    '张三': {'late_prob': 0.15, 'early_prob': 0.08},      # 一般
    '李四': {'late_prob': 0.05, 'early_prob': 0.02},      # 优秀
    '王五': {'late_prob': 0.35, 'early_prob': 0.20},      # 较差
    '赵六': {'late_prob': 0.12, 'early_prob': 0.10},      # 一般
    '孙七': {'late_prob': 0.03, 'early_prob': 0.02},      # 优秀
    '周八': {'late_prob': 0.18, 'early_prob': 0.12},      # 一般
    '吴九': {'late_prob': 0.02, 'early_prob': 0.01},      # 优秀
    '郑十': {'late_prob': 0.15, 'early_prob': 0.08},      # 一般
}

def is_workday(date):
    """判断是否为工作日（周一-周五）"""
    return date.weekday() < 5

def generate_checkin_time(date, is_late=False):
    """生成签到时间"""
    if is_late:
        # 迟到：9:05-9:59
        hour = 9
        minute = random.randint(5, 59)
    else:
        # 正常：8:30-8:59
        hour = 8
        minute = random.randint(30, 59)
    dt = datetime.combine(date, datetime.min.time())
    return dt.replace(hour=hour, minute=minute, second=random.randint(0, 59))

def generate_checkout_time(date, is_early=False):
    """生成签退时间"""
    if is_early:
        # 早退：17:30-17:59
        hour = 17
        minute = random.randint(30, 59)
    else:
        # 正常：18:00-18:59
        hour = 18
        minute = random.randint(0, 59)
    dt = datetime.combine(date, datetime.min.time())
    return dt.replace(hour=hour, minute=minute, second=random.randint(0, 59))

def generate_test_data(db_path):
    """生成测试数据"""
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    # 1. 插入用户
    print("插入用户信息...")
    for name, emp_id, dept, pos in USERS:
        cursor.execute('''
            INSERT OR IGNORE INTO users (user_name, employee_id, department, position, status)
            VALUES (?, ?, ?, ?, 1)
        ''', (name, emp_id, dept, pos))
    conn.commit()
    
    # 2. 获取用户ID映射
    cursor.execute('SELECT user_id, user_name FROM users')
    user_map = {row[1]: row[0] for row in cursor.fetchall()}
    
    # 3. 生成考勤记录
    print("生成考勤记录...")
    end_date = datetime.now().date()
    start_date = end_date - timedelta(days=29)
    
    records = []
    current_date = start_date
    
    while current_date <= end_date:
        if is_workday(current_date):
            # 工作日为每个员工生成考勤记录
            for name, _, _, _ in USERS:
                user_id = user_map[name]
                perf = PERFORMANCE[name]
                
                # 判断是否迟到
                is_late = random.random() < perf['late_prob']
                is_early = random.random() < perf['early_prob']
                
                # 签到
                checkin_time = generate_checkin_time(current_date, is_late)
                checkin_status = 2 if is_late else 1  # 2=迟到, 1=正常
                
                records.append((
                    user_id, name, checkin_time.strftime('%Y-%m-%d %H:%M:%S'), 1,  # check_type=1(签到)
                    round(random.uniform(0.85, 0.99), 2),  # similarity
                    'DEV001', '大门', checkin_status,
                    '迟到' if is_late else '正常签到'
                ))

                # 签退
                checkout_time = generate_checkout_time(current_date, is_early)
                checkout_status = 3 if is_early else 1  # 3=早退, 1=正常

                records.append((
                    user_id, name, checkout_time.strftime('%Y-%m-%d %H:%M:%S'), 2,  # check_type=2(签退)
                    round(random.uniform(0.85, 0.99), 2),  # similarity
                    'DEV001', '大门', checkout_status,
                    '早退' if is_early else '正常签退'
                ))
        
        current_date += timedelta(days=1)
    
    # 4. 批量插入考勤记录
    print(f"插入 {len(records)} 条考勤记录...")
    cursor.executemany('''
        INSERT INTO attendance_records 
        (user_id, user_name, check_time, check_type, similarity, device_id, location, status, remark)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''', records)
    conn.commit()
    
    # 5. 统计信息
    print("\n=== 数据统计 ===")
    cursor.execute('SELECT COUNT(*) FROM users')
    print(f"用户总数: {cursor.fetchone()[0]}")
    
    cursor.execute('SELECT COUNT(*) FROM attendance_records')
    print(f"考勤记录总数: {cursor.fetchone()[0]}")
    
    cursor.execute('''
        SELECT user_name, COUNT(*) as count FROM attendance_records 
        WHERE status = 2 GROUP BY user_name ORDER BY count DESC
    ''')
    print("\n迟到统计:")
    for row in cursor.fetchall():
        print(f"  {row[0]}: {row[1]} 次")
    
    cursor.execute('''
        SELECT user_name, COUNT(*) as count FROM attendance_records 
        WHERE status = 3 GROUP BY user_name ORDER BY count DESC
    ''')
    print("\n早退统计:")
    for row in cursor.fetchall():
        print(f"  {row[0]}: {row[1]} 次")
    
    conn.close()
    print("\n✅ 测试数据生成完成！")

if __name__ == '__main__':
    import sys
    db_path = sys.argv[1] if len(sys.argv) > 1 else 'data/database/face_recognition.db'
    generate_test_data(db_path)


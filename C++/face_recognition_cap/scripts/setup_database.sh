#!/bin/bash
# 数据库初始化脚本

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DB_DIR="${PROJECT_DIR}/data/database"
DB_FILE="${DB_DIR}/face_recognition.db"
SQL_FILE="${PROJECT_DIR}/scripts/init_database.sql"

echo "=========================================="
echo "  Database Setup Script"
echo "=========================================="

# 1. 创建数据库目录
echo "Creating database directory..."
mkdir -p "${DB_DIR}"

# 2. 检查数据库是否已存在
if [ -f "${DB_FILE}" ]; then
    read -p "Database already exists. Overwrite? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -f "${DB_FILE}"
        echo "Removed existing database"
    else
        echo "Keeping existing database"
        exit 0
    fi
fi

# 3. 初始化数据库
echo "Initializing database..."
if [ -f "${SQL_FILE}" ]; then
    sqlite3 "${DB_FILE}" < "${SQL_FILE}"
    echo "Database initialized from SQL file"
else
    # 使用 db_tool 初始化
    if [ -f "${PROJECT_DIR}/install/face_recognition_cap/db_tool" ]; then
        "${PROJECT_DIR}/install/face_recognition_cap/db_tool" init "${DB_FILE}"
        echo "Database initialized using db_tool"
    else
        echo "Error: Neither SQL file nor db_tool found"
        echo "Please build the project first: bash device_build.sh"
        exit 1
    fi
fi

# 4. 验证数据库
echo "Verifying database..."
TABLE_COUNT=$(sqlite3 "${DB_FILE}" "SELECT COUNT(*) FROM sqlite_master WHERE type='table';")
echo "Tables created: ${TABLE_COUNT}"

if [ "${TABLE_COUNT}" -ge 5 ]; then
    echo "✓ Database setup successful!"
    echo "Database location: ${DB_FILE}"
else
    echo "✗ Database setup failed"
    exit 1
fi

# 5. 添加示例用户(可选)
read -p "Add sample users? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if [ -f "${PROJECT_DIR}/install/face_recognition_cap/db_tool" ]; then
        "${PROJECT_DIR}/install/face_recognition_cap/db_tool" add_user "${DB_FILE}" "张三" "技术部"
        "${PROJECT_DIR}/install/face_recognition_cap/db_tool" add_user "${DB_FILE}" "李四" "市场部"
        "${PROJECT_DIR}/install/face_recognition_cap/db_tool" add_user "${DB_FILE}" "王五" "人事部"
        echo "✓ Sample users added"
        
        # 显示用户列表
        "${PROJECT_DIR}/install/face_recognition_cap/db_tool" list_users "${DB_FILE}"
    fi
fi

echo ""
echo "=========================================="
echo "  Setup Complete!"
echo "=========================================="
echo "Next steps:"
echo "1. Add face features for users"
echo "2. Run face recognition with --db flag"
echo "   ./face_recognition_cap ... --db"


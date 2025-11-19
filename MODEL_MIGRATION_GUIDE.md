# 人脸识别模型迁移指南

## 📋 概述

本文档记录了从旧模型 `facenet.rknn` (128维) 迁移到新模型 `w600k_mbf.rknn` (512维) 的完整过程。

---

## 🔄 模型对比

| 项目 | 旧模型 (facenet.rknn) | 新模型 (w600k_mbf.rknn) |
|------|----------------------|------------------------|
| **训练数据集** | 未知 | WebFace600K (60万人脸) |
| **网络架构** | FaceNet | MobileFaceNet + ArcFace Loss |
| **特征维度** | 128 维 | **512 维** ⭐ |
| **输入尺寸** | 动态查询 | 1×3×112×112 (RGB, UINT8) |
| **输出格式** | FLOAT32 | FLOAT32 (FP16精度) |
| **模型大小** | 3.6 MB | 7.3 MB |

---

## ✅ 已完成的代码修改

### 1. 头文件修改 (postprocess.h)

**文件路径:**
- `C++/face_recognition/include/postprocess.h`
- `C++/face_recognition_cap/include/postprocess.h`

**修改内容:**
```cpp
// 新增宏定义
#define FACENET_FEATURE_DIM 512  // 人脸特征向量维度
```

---

### 2. 特征处理函数修改 (postprocess.cc)

**文件路径:**
- `C++/face_recognition/src/postprocess.cc`
- `C++/face_recognition_cap/src/postprocess.cc`

**修改函数:**
- `l2_normalize()` - L2归一化
- `eu_distance()` - 欧氏距离计算
- `compare_eu_distance()` - 欧氏距离比较
- `cos_similarity()` - 余弦相似度计算

**修改示例:**
```cpp
// 修改前
for (int i = 0; i < 128; ++i)

// 修改后
for (int i = 0; i < FACENET_FEATURE_DIM; ++i)
```

---

### 3. 主程序修改 (main.cc)

#### face_recognition/src/main.cc
- 第 200 行: 特征库读取数组大小 `128` → `FACENET_FEATURE_DIM`
- 第 316 行: 特征保存循环 `i < 128` → `i < FACENET_FEATURE_DIM`

#### face_recognition_cap/src/main.cc
- 第 195 行: 动态分配特征数组 `new float[128]` → `new float[FACENET_FEATURE_DIM]`

---

### 4. 文档更新

**README.md 更新内容:**
- 更新模型文件名: `facenet.rknn` → `w600k_mbf.rknn`
- 添加模型信息说明
- 添加特征维度变更警告
- 更新使用示例

---

## 🚀 部署步骤

### 步骤 1: 准备新模型文件

```bash
# 将新模型放入模型目录
cp w600k_mbf.rknn C++/face_recognition/data/model/
cp w600k_mbf.rknn C++/face_recognition_cap/data/model/
```

### 步骤 2: 编译项目

```bash
# 编译 face_recognition
cd C++/face_recognition
bash build.sh

# 编译 face_recognition_cap
cd C++/face_recognition_cap
bash build.sh
```

### 步骤 3: 重新生成特征库

**⚠️ 重要: 旧的 128 维特征库不兼容，必须重新生成！**

```bash
cd C++/face_recognition/install/face_recognition

# 删除旧特征库
rm -rf data/face_feature_lib/*

# 使用新模型生成特征库
./face_recognition data/model/retinaface.rknn data/model/w600k_mbf.rknn 1
```

### 步骤 4: 测试识别功能

```bash
# 静态图片识别测试
./face_recognition data/model/retinaface.rknn data/model/w600k_mbf.rknn data/img/test.jpg

# 实时摄像头识别测试
cd C++/face_recognition_cap/install/face_recognition_cap
cp -r ../../face_recognition/install/face_recognition/data/face_feature_lib ./data/
./face_recognition_cap data/model/retinaface.rknn data/model/w600k_mbf.rknn usb 0
```

---

## 📝 代码修改清单

| 文件 | 修改行数 | 修改类型 |
|------|---------|---------|
| `face_recognition/include/postprocess.h` | +2 | 新增宏定义 |
| `face_recognition_cap/include/postprocess.h` | +2 | 新增宏定义 |
| `face_recognition/src/postprocess.cc` | 8 | 替换硬编码 |
| `face_recognition_cap/src/postprocess.cc` | 8 | 替换硬编码 |
| `face_recognition/src/main.cc` | 2 | 替换硬编码 |
| `face_recognition_cap/src/main.cc` | 1 | 替换硬编码 |
| `face_recognition/README.md` | 全文 | 更新文档 |
| `face_recognition_cap/README.md` | 全文 | 更新文档 |

**总计:** 8 个文件，23 处修改

---

## ⚠️ 注意事项

1. **特征库不兼容**: 128维和512维特征向量完全不兼容，必须重新生成
2. **阈值调整**: 新模型可能需要调整 `FACENET_THRESH` 阈值以获得最佳识别效果
3. **性能影响**: 512维特征计算量更大，但精度更高
4. **内存占用**: 特征库文件大小增加约 4 倍 (128→512)

---

## 🔍 验证检查

- [x] 所有硬编码的 128 已替换为 FACENET_FEATURE_DIM
- [x] 编译无错误无警告
- [x] README 文档已更新
- [x] 代码注释已添加
- [ ] 新模型推理测试通过
- [ ] 特征库生成测试通过
- [ ] 人脸识别准确率测试通过

---

## 📞 技术支持

如遇到问题，请检查：
1. 模型文件是否正确放置
2. 特征库是否使用新模型重新生成
3. 编译是否成功无警告
4. 输入图片是否符合要求（单人脸、清晰）

---

**修改完成时间:** 2025-11-19  
**修改人员:** Augment Agent  
**版本:** v2.0 (512-dim MobileFaceNet)


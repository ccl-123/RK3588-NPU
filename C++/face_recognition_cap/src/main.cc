/**
 * @file main.cc
 * @brief 人脸识别应用程序入口
 * @author CL
 * @date 2025-11-20
 *
 * 功能: 实时人脸识别系统
 * - 支持 USB/MIPI 摄像头
 * - RetinaFace 人脸检测
 * - FaceNet 特征提取和识别
 * - 多线程优化 (预处理、渲染、异步采集)
 */

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include "app/face_recognition_app.h"

/**
 * @brief 主函数 - 程序入口
 *
 * @param argc 参数个数
 * @param argv 参数列表
 *   argv[1]: RetinaFace 模型路径
 *   argv[2]: FaceNet 模型路径
 *   argv[3]: 摄像头类型 (usb/mipi)
 *   argv[4]: 设备编号
 *
 * @return 0 成功, -1 失败
 */
int main(int argc, char** argv)
{
    // 1. 参数检查
    if (argc != 5) {
        printf("Usage: %s <retinaface model> <facenet model> <usb or mipi> <device number>\n", argv[0]);
        printf("\nExample:\n");
        printf("  %s data/model/retinaface.rknn data/model/w600k_mbf.rknn usb 21\n", argv[0]);
        return -1;
    }

    // 2. 解析命令行参数
    AppConfig config;
    config.retinaface_model_path = argv[1];
    config.facenet_model_path = argv[2];
    config.camera_type = argv[3];
    config.device_number = argv[4];

    // 3. 创建应用实例
    FaceRecognitionApp app;

    // 4. 初始化应用
    std::cout << "========================================" << std::endl;
    std::cout << "  Face Recognition System" << std::endl;
    std::cout << "========================================" << std::endl;

    if (app.initialize(config) != 0) {
        std::cerr << "Failed to initialize application" << std::endl;
        return -1;
    }

    // 5. 运行主循环
    int ret = app.run();

    // 6. 正常退出
    std::cout << "Application exited" << std::endl;
    return ret;
}

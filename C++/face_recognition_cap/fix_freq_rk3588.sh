#!/bin/bash
# ==============================================================================
# RK3588 满血高性能锁频脚本
# ==============================================================================

# 检查 root 权限
if [ "$EUID" -ne 0 ]; then
  echo "请以 root 权限运行此脚本 (例如: sudo bash $0)"
  exit 1
fi

echo "========================================"
echo "🚀 开始锁死 RK3588 最高性能模式..."
echo "========================================"

# 1. 禁用 CPU Idle 深度省电状态以减少唤醒时延
echo " -> 禁用 CPU Idle 深度休眠..."
for i in {0..7}; do
  if [ -f "/sys/devices/system/cpu/cpu$i/cpuidle/state1/disable" ]; then
    echo 1 > "/sys/devices/system/cpu/cpu$i/cpuidle/state1/disable"
  fi
done

# 2. 锁定 NPU 频率至最大值 (1GHz)
echo " -> 锁定 NPU 至最大频率..."
if [ -d "/sys/class/devfreq/fdab0000.npu" ]; then
  echo "    可用频率: $(cat /sys/class/devfreq/fdab0000.npu/available_frequencies)"
  echo userspace > /sys/class/devfreq/fdab0000.npu/governor
  echo 1000000000 > /sys/class/devfreq/fdab0000.npu/userspace/set_freq
  echo "    当前频率: $(cat /sys/class/devfreq/fdab0000.npu/cur_freq)"
fi

# 3. 锁定 CPU 频率至最大值 (L小核: 1.8GHz, B大核: 2.35GHz/2.4GHz)
echo " -> 锁定 CPU 至最大频率..."
# Policy 0 (A55 小核集群)
if [ -d "/sys/devices/system/cpu/cpufreq/policy0" ]; then
  echo userspace > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
  echo 1800000 > /sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed
  echo "    Policy 0 (小核) 频率: $(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq)"
fi

# Policy 4 (A76 大核集群 1)
if [ -d "/sys/devices/system/cpu/cpufreq/policy4" ]; then
  echo userspace > /sys/devices/system/cpu/cpufreq/policy4/scaling_governor
  echo 2352000 > /sys/devices/system/cpu/cpufreq/policy4/scaling_setspeed
  echo "    Policy 4 (大核1) 频率: $(cat /sys/devices/system/cpu/cpufreq/policy4/scaling_cur_freq)"
fi

# Policy 6 (A76 大核集群 2)
if [ -d "/sys/devices/system/cpu/cpufreq/policy6" ]; then
  echo userspace > /sys/devices/system/cpu/cpufreq/policy6/scaling_governor
  echo 2352000 > /sys/devices/system/cpu/cpufreq/policy6/scaling_setspeed
  echo "    Policy 6 (大核2) 频率: $(cat /sys/devices/system/cpu/cpufreq/policy6/scaling_cur_freq)"
fi

# 4. 锁定 GPU 频率至最大值 (1GHz)
echo " -> 锁定 GPU 至最大频率..."
if [ -d "/sys/class/devfreq/fb000000.gpu" ]; then
  echo userspace > /sys/class/devfreq/fb000000.gpu/governor
  echo 1000000000 > /sys/class/devfreq/fb000000.gpu/userspace/set_freq
  echo "    当前 GPU 频率: $(cat /sys/class/devfreq/fb000000.gpu/cur_freq)"
fi

# 5. 锁定 DDR (内存总线) 频率至最大值 (2.11GHz)
echo " -> 锁定 DDR 至最大频率..."
if [ -d "/sys/class/devfreq/dmc" ]; then
  echo userspace > /sys/class/devfreq/dmc/governor
  echo 2112000000 > /sys/class/devfreq/dmc/userspace/set_freq
  echo "    当前 DDR 频率: $(cat /sys/class/devfreq/dmc/cur_freq)"
fi

echo "========================================"
echo "✅ RK3588 满血性能锁定成功！"
echo "========================================"

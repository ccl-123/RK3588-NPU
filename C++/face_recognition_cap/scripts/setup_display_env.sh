# 为本机 X11 桌面补全 DISPLAY（SSH/Cursor 等无图形变量的会话）
# 规则：仅当 DISPLAY 为空且检测到 X socket 时设置，不覆盖已有桌面环境。

setup_display_env() {
  # 桌面终端、已配置 SSH 等：已有 DISPLAY，直接跳过
  if [[ -n "${DISPLAY:-}" ]]; then
    return 0
  fi

  local uid
  uid="$(id -u)"

  if [[ -S /tmp/.X11-unix/X0 ]]; then
    export DISPLAY=:0
  elif [[ -S /tmp/.X11-unix/X1 ]]; then
    export DISPLAY=:1
  else
    return 0
  fi

  if [[ -z "${XAUTHORITY:-}" ]]; then
    if [[ -f "/run/user/${uid}/gdm/Xauthority" ]]; then
      export XAUTHORITY="/run/user/${uid}/gdm/Xauthority"
    elif [[ -f "${HOME}/.Xauthority" ]]; then
      export XAUTHORITY="${HOME}/.Xauthority"
    fi
  fi

  if [[ -z "${DBUS_SESSION_BUS_ADDRESS:-}" && -S "/run/user/${uid}/bus" ]]; then
    export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${uid}/bus"
  fi

  return 0
}

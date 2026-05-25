# 为 SSH/Cursor 会话补全图形与网络环境，与 HDMI 桌面会话对齐。
# 规则：仅补空缺项，不覆盖已有 DISPLAY / 代理变量（不影响本机桌面终端直接运行）。

# 从 GNOME 读取系统代理（桌面浏览器/Qt 走 192.168.137.1:7890 等）
setup_proxy_env() {
  if [[ -n "${http_proxy:-}" || -n "${HTTP_PROXY:-}" ]]; then
    return 0
  fi

  local uid bus
  uid="$(id -u)"
  bus="/run/user/${uid}/bus"
  if [[ ! -S "${bus}" ]]; then
    return 0
  fi

  local mode host port
  mode="$(DBUS_SESSION_BUS_ADDRESS="unix:path=${bus}" \
    gsettings get org.gnome.system.proxy mode 2>/dev/null || true)"
  if [[ "${mode}" != "'manual'" ]]; then
    return 0
  fi

  host="$(DBUS_SESSION_BUS_ADDRESS="unix:path=${bus}" \
    gsettings get org.gnome.system.proxy.http host 2>/dev/null | tr -d "'")"
  port="$(DBUS_SESSION_BUS_ADDRESS="unix:path=${bus}" \
    gsettings get org.gnome.system.proxy.http port 2>/dev/null | tr -d "'")"

  if [[ -z "${host}" || "${port}" == "0" ]]; then
    return 0
  fi

  export http_proxy="http://${host}:${port}"
  export https_proxy="${http_proxy}"
  export HTTP_PROXY="${http_proxy}"
  export HTTPS_PROXY="${https_proxy}"
  export no_proxy="localhost,127.0.0.0/8,::1"
  export NO_PROXY="${no_proxy}"
  return 0
}

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

  setup_proxy_env
  return 0
}

# 桌面终端已有 DISPLAY 时单独调用，仅同步代理
setup_session_env() {
  setup_display_env
  if [[ -n "${DISPLAY:-}" ]]; then
    setup_proxy_env
  fi
}

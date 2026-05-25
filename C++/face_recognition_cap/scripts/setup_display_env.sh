# 为 SSH/Cursor 会话补全图形与网络环境，与 HDMI 桌面会话对齐。
# 规则：仅选择在当前 SSH 会话中能够认证连接的 DISPLAY，避免 Qt 卡在 XCB 初始化。

_display_socket_exists() {
  local num="${1#:}"
  [[ -S "/tmp/.X11-unix/X${num}" ]]
}

_display_is_reachable() {
  timeout 2s xdpyinfo -display "$1" >/dev/null 2>&1
}

# 选择当前可用的 X11 显示（重启后可能是 :1 而非 :0）
_pick_display() {
  local n
  # 优先匹配已登录图形会话（常见为 :1），再回退 :0
  for n in 1 0 2 3; do
    if _display_socket_exists ":${n}" && _display_is_reachable ":${n}"; then
      echo ":${n}"
      return 0
    fi
  done
  return 1
}

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
  local uid picked
  uid="$(id -u)"
  picked=""

  if [[ -z "${XAUTHORITY:-}" ]]; then
    if [[ -f "/run/user/${uid}/gdm/Xauthority" ]]; then
      export XAUTHORITY="/run/user/${uid}/gdm/Xauthority"
    elif [[ -f "${HOME}/.Xauthority" ]]; then
      export XAUTHORITY="${HOME}/.Xauthority"
    fi
  fi

  if [[ -n "${DISPLAY:-}" ]] &&
     _display_socket_exists "${DISPLAY}" &&
     _display_is_reachable "${DISPLAY}"; then
    picked="${DISPLAY}"
  elif picked="$(_pick_display 2>/dev/null)"; then
    export DISPLAY="${picked}"
  else
    unset DISPLAY
    return 0
  fi

  if [[ -z "${DBUS_SESSION_BUS_ADDRESS:-}" && -S "/run/user/${uid}/bus" ]]; then
    export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${uid}/bus"
  fi

  setup_proxy_env
  return 0
}

setup_session_env() {
  setup_display_env
  if [[ -n "${DISPLAY:-}" ]]; then
    setup_proxy_env
  fi
}

import { ref, readonly } from 'vue'

const connected = ref(false)
const ws = ref<WebSocket | null>(null)
const listeners = new Set<(msg: any) => void>()

let reconnectTimer: ReturnType<typeof setTimeout> | null = null

function getWsUrl(): string {
  const loc = window.location
  const proto = loc.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${proto}//${loc.host}/ws`
}

function connect() {
  if (ws.value && ws.value.readyState <= WebSocket.OPEN) return

  const socket = new WebSocket(getWsUrl())
  ws.value = socket

  socket.onopen = () => {
    connected.value = true
    if (reconnectTimer) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
  }

  socket.onmessage = (ev) => {
    try {
      const msg = JSON.parse(ev.data)
      listeners.forEach((fn) => fn(msg))
    } catch { /* ignore non-json */ }
  }

  socket.onclose = () => {
    connected.value = false
    scheduleReconnect()
  }

  socket.onerror = () => {
    socket.close()
  }
}

function scheduleReconnect() {
  if (reconnectTimer) return
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null
    connect()
  }, 1500)
}

function onMessage(fn: (msg: any) => void) {
  listeners.add(fn)
  return () => listeners.delete(fn)
}

connect()

export function useWebSocket() {
  return {
    connected: readonly(connected),
    onMessage,
  }
}

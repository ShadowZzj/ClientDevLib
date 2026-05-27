import { useInstances } from './useInstances'

export async function sendCommand(action: string, args: Record<string, any> = {}) {
  const { selectedPid } = useInstances()
  const pid = selectedPid.value
  if (!pid) throw new Error('未选择实例')

  const res = await fetch(`/api/command/${pid}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action, args }),
  })

  if (!res.ok) {
    const text = await res.text()
    throw new Error(text || `HTTP ${res.status}`)
  }

  return res.json()
}

export function useApi() {
  return { sendCommand }
}

import { ref, readonly, computed } from 'vue'
import { useWebSocket } from './useWebSocket'
import type { Instance } from '@/types'

const instances = ref<Instance[]>([])
const selectedPid = ref<number | null>(null)

const { onMessage } = useWebSocket()

onMessage((msg) => {
  if (msg.type === 'snapshot') {
    instances.value = (msg.instances ?? msg.payload) as Instance[]
    if (selectedPid.value && !instances.value.find((i) => i.pid === selectedPid.value)) {
      selectedPid.value = instances.value.length > 0 ? instances.value[0].pid : null
    }
    if (!selectedPid.value && instances.value.length > 0) {
      selectedPid.value = instances.value[0].pid
    }
  }
})

async function fetchInstances() {
  try {
    const res = await fetch('/api/instances')
    if (res.ok) {
      const data = await res.json()
      instances.value = data as Instance[]
    }
  } catch { /* silent */ }
}

fetchInstances()
setInterval(fetchInstances, 5000)

const selectedInstance = computed(() =>
  instances.value.find((i) => i.pid === selectedPid.value) ?? null
)

export function useInstances() {
  return {
    instances: readonly(instances),
    selectedPid,
    selectedInstance,
    fetchInstances,
  }
}

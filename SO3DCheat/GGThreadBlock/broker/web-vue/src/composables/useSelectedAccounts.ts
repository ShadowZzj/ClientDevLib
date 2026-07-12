import { computed, ref } from 'vue'
import { useInstances } from './useInstances'

// 寻路移动 / 复活传送 两个标签页共用的「账号清单」。和右上角的 selectedPid 无关 ——
// 这里维护一份按角色名选中的批量名单(单例 ref,SPA 内两页共享),并持久化到
// localStorage,刷新后回填。在线判定 / pid 解析都基于 useInstances 的实时快照。
const STORAGE_KEY = 'ggtb.selectedAccounts'

function load(): string[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    const arr = raw ? JSON.parse(raw) : []
    return Array.isArray(arr) ? arr.map((n) => String(n)) : []
  } catch {
    return []
  }
}

// 单例:模块作用域内只建一次,任意页面 import 拿到的是同一份。
const selectedNames = ref<string[]>(load())

export function useSelectedAccounts() {
  const { instances } = useInstances()

  // 有角色名的实例才算「在线」(连上但还没拿到角色名的不参与批量)。
  const onlineInstances = computed(() => instances.value.filter((i) => !!i.characterName))
  const onlineCount = computed(() => onlineInstances.value.length)

  // 选项 = 在线角色 ∪ 已选但当前离线的角色;离线项标注但不禁用,否则其标签无法移除。
  const characterOptions = computed(() => {
    const set = new Set<string>()
    for (const i of onlineInstances.value) set.add(i.characterName as string)
    for (const n of selectedNames.value) set.add(n)
    return Array.from(set).map((n) => {
      const online = onlineInstances.value.some((i) => i.characterName === n)
      return { value: n, label: online ? `${n} (在线)` : `${n} (离线)` }
    })
  })

  // 选中名单里当前在线的 {pid, characterName},批量下发就打这些。
  const selectedTargets = computed(() =>
    selectedNames.value
      .map((name) => onlineInstances.value.find((i) => i.characterName === name))
      .filter((i): i is NonNullable<typeof i> => !!i)
      .map((i) => ({ pid: i.pid, characterName: i.characterName as string }))
  )

  // 首个选中的在线账号 pid —— 单账号交互工具(读位置 / NPC / 对话)用它当主控。
  const primaryPid = computed<number | null>(() => selectedTargets.value[0]?.pid ?? null)

  function persist() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(selectedNames.value))
    } catch {
      /* ignore quota / private mode */
    }
  }

  function selectAllOnline() {
    selectedNames.value = onlineInstances.value.map((i) => i.characterName as string)
    persist()
  }

  function clearSelection() {
    selectedNames.value = []
    persist()
  }

  function setSelection(names: string[]) {
    selectedNames.value = Array.from(new Set(names.map((n) => String(n))))
    persist()
  }

  return {
    selectedNames,
    onlineInstances,
    onlineCount,
    characterOptions,
    selectedTargets,
    primaryPid,
    selectAllOnline,
    clearSelection,
    setSelection,
    persist,
  }
}

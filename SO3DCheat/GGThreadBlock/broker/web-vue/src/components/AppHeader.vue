<template>
  <el-header class="app-header">
    <div class="header-left">
      <span class="status-dot" :class="{ online: connected }"></span>
      <span class="title">GGThreadBlock Console</span>
    </div>
    <div class="header-right">
      <el-select
        v-model="selectedPid"
        placeholder="选择实例"
        class="instance-select"
        :class="selectedInstanceClass"
        style="width: 260px"
        :disabled="instances.length === 0"
      >
        <el-option
          v-for="inst in sortedInstances"
          :key="inst.pid"
          :label="instLabel(inst)"
          :value="inst.pid"
        >
          <span class="instance-option-label" :class="instanceLifeClass(inst)">
            {{ instLabel(inst) }}
          </span>
        </el-option>
      </el-select>
      <span class="meta" v-if="selectedInstance">
        钱包: {{ fmtMoney(selectedInstance.money) }}
      </span>
    </div>
  </el-header>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useWebSocket } from '@/composables/useWebSocket'
import { useInstances } from '@/composables/useInstances'
import type { Instance } from '@/types'

const { connected } = useWebSocket()
const { instances, selectedPid, selectedInstance } = useInstances()

const selectedInstanceClass = computed(() =>
  selectedInstance.value ? instanceLifeClass(selectedInstance.value) : ''
)

const sortedInstances = computed(() => {
  return [...instances.value].sort((a, b) => {
    const an = a.characterName || '~'
    const bn = b.characterName || '~'
    if (an !== bn) return an.localeCompare(bn)
    return a.pid - b.pid
  })
})

function instLabel(inst: Instance): string {
  return inst.characterName
    ? `${inst.characterName} (pid ${inst.pid})`
    : `(pid ${inst.pid} connecting…)`
}

function isDead(inst: Instance): boolean {
  return inst.hp === 0
}

function instanceLifeClass(inst: Instance): string {
  if (isDead(inst)) return 'is-dead'
  return typeof inst.hp === 'number' && inst.hp > 0 ? 'is-alive' : 'is-unknown'
}

function fmtMoney(n: number | undefined): string {
  if (typeof n !== 'number') return '?'
  return n.toLocaleString()
}
</script>

<style scoped>
.app-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  border-bottom: 1px solid var(--el-border-color);
  height: 56px;
}
.header-left {
  display: flex;
  align-items: center;
  gap: 10px;
}
.header-right {
  display: flex;
  align-items: center;
  gap: 14px;
}
.status-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--el-color-danger);
  transition: background 0.3s;
}
.status-dot.online {
  background: var(--el-color-success);
}
.title {
  font-size: 16px;
  font-weight: 600;
}
.meta {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}
.instance-option-label {
  font-weight: 600;
}
.instance-option-label.is-dead,
.instance-select.is-dead :deep(.el-select__selected-item),
.instance-select.is-dead :deep(.el-input__inner) {
  color: var(--el-color-danger);
}
.instance-option-label.is-alive,
.instance-select.is-alive :deep(.el-select__selected-item),
.instance-select.is-alive :deep(.el-input__inner) {
  color: var(--el-color-success);
}
</style>

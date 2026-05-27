<template>
  <div class="overview">
    <h3>在线实例 ({{ instances.length }})</h3>
    <div class="card-grid" v-if="instances.length > 0">
      <el-card v-for="inst in instances" :key="inst.pid" shadow="hover" class="inst-card">
        <template #header>
          <span class="card-title">{{ inst.characterName || '(连接中…)' }}</span>
        </template>
        <div class="card-row">PID: {{ inst.pid }}</div>
        <div class="card-row">钱包: {{ fmtMoney(inst.money) }}</div>
        <div class="card-row">hostExe: {{ inst.hostExe || '?' }}</div>
        <div class="card-row">心跳: {{ heartbeatAge(inst) }}</div>
      </el-card>
    </div>
    <el-empty v-else description="暂无在线实例" />
  </div>
</template>

<script setup lang="ts">
import { useInstances } from '@/composables/useInstances'
import type { Instance } from '@/types'

const { instances } = useInstances()

function fmtMoney(n: number | undefined): string {
  if (typeof n !== 'number') return '?'
  return n.toLocaleString()
}

function heartbeatAge(inst: Instance): string {
  if (!inst.lastSeen) return '?'
  const sec = ((Date.now() - inst.lastSeen) / 1000).toFixed(1)
  return `${sec}s 前`
}
</script>

<style scoped>
.overview { padding: 8px; }
.card-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
  gap: 14px;
  margin-top: 12px;
}
.card-title { font-weight: 600; }
.card-row { font-size: 13px; line-height: 1.8; color: var(--el-text-color-regular); }
</style>

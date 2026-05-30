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
        <el-button
          size="small"
          type="primary"
          :disabled="!inst.characterName"
          @click="openStats(inst)"
        >
          查看统计
        </el-button>
      </el-card>
    </div>
    <el-empty v-else description="暂无在线实例" />

    <h3 style="margin-top: 22px">每日打钱收益</h3>
    <el-table :data="dailyRows" size="small" stripe style="max-width: 860px">
      <el-table-column prop="characterName" label="角色" min-width="160" />
      <el-table-column prop="date" label="日期" width="130" />
      <el-table-column label="获得金钱" width="160" align="right">
        <template #default="{ row }">{{ fmtMoney(row.gain) }}</template>
      </el-table-column>
    </el-table>

    <h3 style="margin-top: 22px">每日自动复活次数</h3>
    <el-table :data="dailyReviveRows" size="small" stripe style="max-width: 860px">
      <el-table-column prop="characterName" label="角色" min-width="160" />
      <el-table-column prop="date" label="日期" width="130" />
      <el-table-column label="自动复活次数" width="160" align="right">
        <template #default="{ row }">{{ row.count }}</template>
      </el-table-column>
    </el-table>

    <el-drawer
      v-model="statsVisible"
      :title="selectedName ? `${selectedName} 统计` : '统计'"
      size="720px"
    >
      <div class="stats-toolbar">
        <el-radio-group v-model="statsUnit" size="small" @change="loadStats">
          <el-radio-button label="hour">按小时</el-radio-button>
          <el-radio-button label="day">按天</el-radio-button>
        </el-radio-group>
        <el-button size="small" :loading="statsLoading" @click="loadStats">刷新</el-button>
      </div>
      <h4>钱包变化</h4>
      <div class="chart-wrap">
        <svg viewBox="0 0 680 260" class="stats-chart" preserveAspectRatio="none">
          <polyline v-if="moneyChartPoints" :points="moneyChartPoints" fill="none" stroke="var(--el-color-primary)" stroke-width="3" />
          <line x1="40" y1="230" x2="660" y2="230" stroke="var(--el-border-color)" />
          <line x1="40" y1="20" x2="40" y2="230" stroke="var(--el-border-color)" />
        </svg>
        <el-empty v-if="stats.series.length === 0 && !statsLoading" description="暂无金钱历史" />
      </div>
      <div class="chart-meta" v-if="stats.series.length">
        <span>起始 {{ fmtMoney(stats.series[0].money) }}</span>
        <span>最新 {{ fmtMoney(stats.series[stats.series.length - 1].money) }}</span>
      </div>

      <h4>自动复活次数</h4>
      <div class="chart-wrap">
        <svg viewBox="0 0 680 260" class="stats-chart" preserveAspectRatio="none">
          <polyline v-if="reviveChartPoints" :points="reviveChartPoints" fill="none" stroke="var(--el-color-success)" stroke-width="3" />
          <line x1="40" y1="230" x2="660" y2="230" stroke="var(--el-border-color)" />
          <line x1="40" y1="20" x2="40" y2="230" stroke="var(--el-border-color)" />
        </svg>
        <el-empty v-if="reviveStats.series.length === 0 && !statsLoading" description="暂无自动复活记录" />
      </div>
      <div class="chart-meta" v-if="reviveStats.series.length">
        <span>总计 {{ reviveTotal }} 次</span>
        <span>最新 {{ reviveStats.series[reviveStats.series.length - 1].count }} 次</span>
      </div>

      <h4>每日获得金钱</h4>
      <el-table :data="stats.dailyGain.slice().reverse()" size="small" stripe>
        <el-table-column prop="date" label="日期" width="140" />
        <el-table-column label="获得金钱" align="right">
          <template #default="{ row }">{{ fmtMoney(row.gain) }}</template>
        </el-table-column>
      </el-table>

      <h4>每日自动复活次数</h4>
      <el-table :data="reviveStats.daily.slice().reverse()" size="small" stripe>
        <el-table-column prop="date" label="日期" width="140" />
        <el-table-column label="自动复活次数" align="right">
          <template #default="{ row }">{{ row.count }}</template>
        </el-table-column>
      </el-table>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useInstances } from '@/composables/useInstances'
import type { Instance } from '@/types'

const { instances } = useInstances()

interface MoneySeriesPoint { time: number; money: number }
interface MoneyDailyGain { date: string; gain: number }
interface MoneyStatsSnapshot {
  characterName: string
  unit: 'hour' | 'day'
  series: MoneySeriesPoint[]
  dailyGain: MoneyDailyGain[]
}
interface AutoReviveSeriesPoint { time: number; count: number }
interface AutoReviveDailyCount { date: string; count: number }
interface AutoReviveStatsSnapshot {
  characterName: string
  unit: 'hour' | 'day'
  series: AutoReviveSeriesPoint[]
  daily: AutoReviveDailyCount[]
}

interface DailyRow extends MoneyDailyGain { characterName: string }
interface DailyReviveRow extends AutoReviveDailyCount { characterName: string }

const statsVisible = ref(false)
const statsLoading = ref(false)
const selectedName = ref('')
const statsUnit = ref<'hour' | 'day'>('hour')
const stats = ref<MoneyStatsSnapshot>({ characterName: '', unit: 'hour', series: [], dailyGain: [] })
const reviveStats = ref<AutoReviveStatsSnapshot>({ characterName: '', unit: 'hour', series: [], daily: [] })
const allDailyGain = ref<Record<string, MoneyDailyGain[]>>({})
const allDailyRevive = ref<Record<string, AutoReviveDailyCount[]>>({})
let dailyTimer: number | undefined

const dailyRows = computed<DailyRow[]>(() => {
  const rows: DailyRow[] = []
  for (const [characterName, gains] of Object.entries(allDailyGain.value)) {
    for (const g of gains) rows.push({ characterName, ...g })
  }
  return rows.sort((a, b) => b.date.localeCompare(a.date) || a.characterName.localeCompare(b.characterName))
})

const dailyReviveRows = computed<DailyReviveRow[]>(() => {
  const rows: DailyReviveRow[] = []
  for (const [characterName, counts] of Object.entries(allDailyRevive.value)) {
    for (const c of counts) rows.push({ characterName, ...c })
  }
  return rows.sort((a, b) => b.date.localeCompare(a.date) || a.characterName.localeCompare(b.characterName))
})

const moneyChartPoints = computed(() =>
  buildChartPoints(stats.value.series.map((p) => ({ time: p.time, value: p.money })))
)

const reviveChartPoints = computed(() =>
  buildChartPoints(reviveStats.value.series.map((p) => ({ time: p.time, value: p.count })))
)

const reviveTotal = computed(() => reviveStats.value.series.reduce((sum, p) => sum + p.count, 0))

function buildChartPoints(data: Array<{ time: number; value: number }>): string {
  if (data.length === 0) return ''
  const minT = data[0].time
  const maxT = data[data.length - 1].time
  const minV = Math.min(...data.map((p) => p.value))
  const maxV = Math.max(...data.map((p) => p.value))
  const w = 620
  const h = 210
  const timeSpan = Math.max(1, maxT - minT)
  const valueSpan = Math.max(1, maxV - minV)
  return data.map((p) => {
    const x = 40 + ((p.time - minT) / timeSpan) * w
    const y = 230 - ((p.value - minV) / valueSpan) * h
    return `${x.toFixed(1)},${y.toFixed(1)}`
  }).join(' ')
}

function fmtMoney(n: number | undefined): string {
  if (typeof n !== 'number') return '?'
  return n.toLocaleString()
}

function heartbeatAge(inst: Instance): string {
  if (!inst.lastSeen) return '?'
  const sec = ((Date.now() - inst.lastSeen) / 1000).toFixed(1)
  return `${sec}s 前`
}

async function openStats(inst: Instance) {
  if (!inst.characterName) return
  selectedName.value = inst.characterName
  statsVisible.value = true
  await loadStats()
}

async function loadStats() {
  if (!selectedName.value) return
  statsLoading.value = true
  try {
    const [moneyRes, reviveRes] = await Promise.all([
      fetch(`/api/money-stats/${encodeURIComponent(selectedName.value)}?unit=${statsUnit.value}`),
      fetch(`/api/auto-revive-stats/${encodeURIComponent(selectedName.value)}?unit=${statsUnit.value}`),
    ])
    if (moneyRes.ok) stats.value = await moneyRes.json()
    if (reviveRes.ok) reviveStats.value = await reviveRes.json()
  } finally {
    statsLoading.value = false
  }
}

async function loadDailyGain() {
  const res = await fetch('/api/money-stats/daily-gain')
  if (res.ok) allDailyGain.value = await res.json()
}

async function loadDailyRevive() {
  const res = await fetch('/api/auto-revive-stats/daily')
  if (res.ok) allDailyRevive.value = await res.json()
}

onMounted(() => {
  loadDailyGain()
  loadDailyRevive()
  dailyTimer = window.setInterval(() => {
    loadDailyGain()
    loadDailyRevive()
  }, 30000)
})
onUnmounted(() => {
  if (dailyTimer) window.clearInterval(dailyTimer)
})
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
.stats-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 12px;
}
.chart-wrap {
  position: relative;
  min-height: 280px;
  border: 1px solid var(--el-border-color-lighter);
  margin-bottom: 8px;
}
.stats-chart {
  width: 100%;
  height: 280px;
}
.chart-meta {
  display: flex;
  justify-content: space-between;
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-bottom: 18px;
}
</style>

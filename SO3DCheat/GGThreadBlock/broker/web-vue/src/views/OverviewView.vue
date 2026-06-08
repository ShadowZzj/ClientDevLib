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
      @opened="ensureCharts"
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
        <div ref="moneyChartEl" class="stats-chart"></div>
        <el-empty v-if="moneyChartEmpty && !statsLoading" class="chart-empty" description="暂无金钱历史" />
      </div>
      <div class="chart-meta" v-if="stats.series.length">
        <span>起始 {{ fmtMoney(stats.series[0].money) }}</span>
        <span>最新 {{ fmtMoney(stats.series[stats.series.length - 1].money) }}</span>
      </div>

      <h4>自动复活次数</h4>
      <div class="chart-wrap">
        <div ref="reviveChartEl" class="stats-chart"></div>
        <el-empty v-if="reviveChartEmpty && !statsLoading" class="chart-empty" description="暂无自动复活记录" />
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

      <h4>收益明细</h4>
      <el-table :data="stats.gainEvents.slice().reverse()" size="small" stripe max-height="260">
        <el-table-column prop="date" label="时间" width="180" />
        <el-table-column label="获得金钱" width="140" align="right">
          <template #default="{ row }">{{ fmtMoney(row.gain) }}</template>
        </el-table-column>
        <el-table-column label="上涨后钱包" align="right">
          <template #default="{ row }">{{ fmtMoney(row.money) }}</template>
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
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import type { EChartsOption, EChartsType } from 'echarts'
import { useInstances } from '@/composables/useInstances'
import type { Instance } from '@/types'

const { instances } = useInstances()

interface MoneySeriesPoint { time: number; money: number }
interface MoneyDailyGain { date: string; gain: number }
interface MoneyGainEvent { time: number; date: string; gain: number; money: number }
interface MoneyWalletEvent { time: number; date: string; money: number }
interface MoneyStatsSnapshot {
  characterName: string
  unit: 'hour' | 'day'
  series: MoneySeriesPoint[]
  dailyGain: MoneyDailyGain[]
  gainEvents: MoneyGainEvent[]
  walletEvents: MoneyWalletEvent[]
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
type MoneyLinePoint = [number, number]
type MoneyGainChartPoint = {
  value: [number, number]
  gain: number
  money: number
  date: string
  startTime: number
  endTime: number
  eventCount: number
}
type ReviveChartPoint = [number, number]

const statsVisible = ref(false)
const statsLoading = ref(false)
const selectedName = ref('')
const statsUnit = ref<'hour' | 'day'>('hour')
const stats = ref<MoneyStatsSnapshot>({ characterName: '', unit: 'hour', series: [], dailyGain: [], gainEvents: [], walletEvents: [] })
const reviveStats = ref<AutoReviveStatsSnapshot>({ characterName: '', unit: 'hour', series: [], daily: [] })
const allDailyGain = ref<Record<string, MoneyDailyGain[]>>({})
const allDailyRevive = ref<Record<string, AutoReviveDailyCount[]>>({})
const moneyChartEl = ref<HTMLDivElement | null>(null)
const reviveChartEl = ref<HTMLDivElement | null>(null)
let moneyChartInst: EChartsType | null = null
let reviveChartInst: EChartsType | null = null
let dailyTimer: number | undefined
const MONEY_GAIN_BUCKET_MS = 10 * 60 * 1000

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

const moneyChartEmpty = computed(() =>
  stats.value.walletEvents.length === 0 &&
  stats.value.gainEvents.length === 0 &&
  stats.value.series.length === 0
)
const reviveChartEmpty = computed(() => reviveStats.value.series.length === 0)
const reviveTotal = computed(() => reviveStats.value.series.reduce((sum, p) => sum + p.count, 0))

function mergeMoneyLinePoints(
  series: MoneySeriesPoint[],
  walletEvents: MoneyWalletEvent[],
  gainEvents: MoneyGainEvent[],
): MoneyLinePoint[] {
  const byTime = new Map<number, number>()
  for (const p of series) byTime.set(p.time, p.money)
  for (const p of walletEvents) byTime.set(p.time, p.money)
  for (const p of gainEvents) byTime.set(p.time, p.money)
  return Array.from(byTime.entries()).sort(([a], [b]) => a - b)
}

function moneyGainPoints(): MoneyGainChartPoint[] {
  const buckets = new Map<number, { gain: number; money: number; lastTime: number; eventCount: number }>()
  const events = stats.value.gainEvents.slice().sort((a, b) => a.time - b.time)
  for (const p of events) {
    const endTime = Math.ceil(p.time / MONEY_GAIN_BUCKET_MS) * MONEY_GAIN_BUCKET_MS
    const bucket = buckets.get(endTime)
    if (bucket) {
      bucket.gain += p.gain
      bucket.eventCount += 1
      if (p.time >= bucket.lastTime) {
        bucket.lastTime = p.time
        bucket.money = p.money
      }
    } else {
      buckets.set(endTime, { gain: p.gain, money: p.money, lastTime: p.time, eventCount: 1 })
    }
  }

  return Array.from(buckets.entries())
    .sort(([a], [b]) => a - b)
    .map(([endTime, bucket]) => ({
      value: [endTime, bucket.gain],
      gain: bucket.gain,
      money: bucket.money,
      date: formatDateTime(endTime),
      startTime: endTime - MONEY_GAIN_BUCKET_MS,
      endTime,
      eventCount: bucket.eventCount,
    }))
}

function reviveLinePoints(): ReviveChartPoint[] {
  return reviveStats.value.series.map((p) => [p.time, p.count])
}

function formatDateTime(ts: number): string {
  const d = new Date(ts)
  const year = d.getFullYear()
  const month = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  const hh = String(d.getHours()).padStart(2, '0')
  const mm = String(d.getMinutes()).padStart(2, '0')
  const ss = String(d.getSeconds()).padStart(2, '0')
  return `${year}-${month}-${day} ${hh}:${mm}:${ss}`
}

function formatAxisTime(ts: number): string {
  const d = new Date(ts)
  const month = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  const hh = String(d.getHours()).padStart(2, '0')
  const mm = String(d.getMinutes()).padStart(2, '0')
  return statsUnit.value === 'day' ? `${month}-${day}` : `${month}-${day} ${hh}:${mm}`
}

function fmtCompactNumber(value: number): string {
  const abs = Math.abs(value)
  if (abs >= 100_000_000) return `${trimNumber(value / 100_000_000)}亿`
  if (abs >= 10_000) return `${trimNumber(value / 10_000)}万`
  return String(Math.round(value))
}

function trimNumber(value: number): string {
  return value.toFixed(value >= 10 ? 0 : 1).replace(/\.0$/, '')
}

function fmtMoney(n: number | undefined): string {
  if (typeof n !== 'number') return '?'
  return n.toLocaleString()
}

function buildMoneyChartOption(): EChartsOption {
  const line = mergeMoneyLinePoints(stats.value.series, stats.value.walletEvents, stats.value.gainEvents)
  const gains = moneyGainPoints()
  return {
    animation: false,
    grid: { left: 72, right: 84, top: 22, bottom: 70 },
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      formatter(params) {
        const items = Array.isArray(params) ? params : [params]
        const first = items[0] as any
        const ts = Number(Array.isArray(first?.value) ? first.value[0] : first?.axisValue)
        const lines = [`<b>${formatDateTime(ts)}</b>`]
        for (const item of items as any[]) {
          const value = Array.isArray(item.value) ? item.value[1] : item.value
          if (item.seriesIndex === 1) {
            const data = item.data as MoneyGainChartPoint
            lines.push(`${item.marker}10分钟收益: ${fmtMoney(data.gain)}`)
            lines.push(`窗口: ${formatDateTime(data.startTime)} ~ ${formatDateTime(data.endTime)}`)
            lines.push(`卖出次数: ${data.eventCount}`)
            lines.push(`窗口后钱包: ${fmtMoney(data.money)}`)
          } else {
            lines.push(`${item.marker}钱包: ${fmtMoney(value)}`)
          }
        }
        return lines.join('<br/>')
      },
    },
    xAxis: {
      type: 'time',
      axisLabel: { formatter: (value: number) => formatAxisTime(value) },
      splitLine: { show: true, lineStyle: { type: 'dashed' } },
    },
    yAxis: [
      {
        type: 'value',
        name: '钱包',
        min: (value) => Math.max(0, Math.floor(value.min)),
        axisLabel: { formatter: (value: number) => fmtCompactNumber(value) },
        splitLine: { show: true },
      },
      {
        type: 'value',
        name: '10分钟收益',
        min: 0,
        axisLabel: { formatter: (value: number) => fmtCompactNumber(value) },
        splitLine: { show: false },
      },
    ],
    dataZoom: chartDataZoom(),
    series: [
      {
        name: '钱包',
        type: 'line',
        data: line,
        yAxisIndex: 0,
        showSymbol: false,
        symbolSize: 5,
        smooth: false,
        connectNulls: true,
        lineStyle: { width: 3 },
        itemStyle: { color: '#409eff' },
        emphasis: { focus: 'series' },
      },
      {
        name: '10分钟收益',
        type: 'scatter',
        data: gains,
        yAxisIndex: 1,
        symbolSize: 10,
        itemStyle: { color: '#e6a23c' },
        emphasis: { scale: 1.4 },
        z: 5,
      },
    ],
  }
}

function buildReviveChartOption(): EChartsOption {
  return {
    animation: false,
    grid: { left: 72, right: 22, top: 22, bottom: 70 },
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      formatter(params) {
        const items = Array.isArray(params) ? params : [params]
        const first = items[0] as any
        const ts = Number(Array.isArray(first?.value) ? first.value[0] : first?.axisValue)
        const item = items[0] as any
        const count = Array.isArray(item?.value) ? item.value[1] : item?.value
        return `<b>${formatDateTime(ts)}</b><br/>自动复活: ${count || 0} 次`
      },
    },
    xAxis: {
      type: 'time',
      axisLabel: { formatter: (value: number) => formatAxisTime(value) },
      splitLine: { show: true, lineStyle: { type: 'dashed' } },
    },
    yAxis: {
      type: 'value',
      min: 0,
      minInterval: 1,
      axisLabel: { formatter: (value: number) => `${Math.round(value)}` },
      splitLine: { show: true },
    },
    dataZoom: chartDataZoom(),
    series: [
      {
        name: '自动复活',
        type: 'line',
        data: reviveLinePoints(),
        showSymbol: true,
        symbolSize: 7,
        smooth: false,
        lineStyle: { width: 3 },
        itemStyle: { color: '#67c23a' },
      },
    ],
  }
}

function chartDataZoom(): EChartsOption['dataZoom'] {
  return [
    {
      type: 'inside',
      xAxisIndex: 0,
      filterMode: 'none',
      zoomOnMouseWheel: true,
      moveOnMouseMove: true,
      moveOnMouseWheel: false,
      preventDefaultMouseMove: true,
      throttle: 50,
    },
    {
      type: 'slider',
      xAxisIndex: 0,
      filterMode: 'none',
      height: 18,
      bottom: 18,
      brushSelect: false,
      showDetail: false,
    },
  ]
}

async function ensureCharts() {
  await nextTick()
  if (moneyChartEl.value && !moneyChartInst) moneyChartInst = echarts.init(moneyChartEl.value)
  if (reviveChartEl.value && !reviveChartInst) reviveChartInst = echarts.init(reviveChartEl.value)
  renderCharts()
}

function renderCharts() {
  if (moneyChartInst) {
    moneyChartInst.setOption(buildMoneyChartOption(), true)
    moneyChartInst.resize()
  }
  if (reviveChartInst) {
    reviveChartInst.setOption(buildReviveChartOption(), true)
    reviveChartInst.resize()
  }
}

function resizeCharts() {
  moneyChartInst?.resize()
  reviveChartInst?.resize()
}

function disposeCharts() {
  moneyChartInst?.dispose()
  reviveChartInst?.dispose()
  moneyChartInst = null
  reviveChartInst = null
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
  await ensureCharts()
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
    await ensureCharts()
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
  window.addEventListener('resize', resizeCharts)
  dailyTimer = window.setInterval(() => {
    loadDailyGain()
    loadDailyRevive()
  }, 30000)
})
onUnmounted(() => {
  if (dailyTimer) window.clearInterval(dailyTimer)
  window.removeEventListener('resize', resizeCharts)
  disposeCharts()
})

watch(statsVisible, async (visible) => {
  if (visible) {
    await ensureCharts()
  }
})

watch([stats, reviveStats, statsUnit], () => {
  renderCharts()
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
.chart-empty {
  position: absolute;
  inset: 0;
  pointer-events: none;
}
.chart-meta {
  display: flex;
  justify-content: space-between;
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-bottom: 18px;
}
</style>

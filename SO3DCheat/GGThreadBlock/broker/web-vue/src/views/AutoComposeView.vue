<template>
  <div class="compose-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>自动合成技能宝石 — 三合一,把低于目标品级的同级宝石一路合上来</span>
          <el-select
            v-model="selectedName"
            placeholder="选择角色"
            size="small"
            style="width: 200px"
            filterable
          >
            <el-option
              v-for="opt in characterOptions"
              :key="opt.value"
              :label="opt.label"
              :value="opt.value"
            />
          </el-select>
        </div>
      </template>

      <div v-if="!selectedName" class="empty-hint">先选一个角色(需在线才能读背包/合成)。</div>

      <template v-else>
        <el-form label-width="120px" size="default">
          <el-form-item label="目标品级">
            <el-select v-model="config.targetRank" style="width: 200px">
              <el-option v-for="o in targetOptions" :key="o.value" :value="o.value" :label="o.label" />
            </el-select>
            <span class="hint" style="margin-left:8px">
              把所有「低于目标品级」的宝石,按等级三合一逐级合上来,直到没有任何低于目标的组还能凑满 3 个。
            </span>
          </el-form-item>

          <el-form-item label="两次合成间隔">
            <el-input-number v-model="config.intervalMs" :min="80" :max="10000" :step="50" style="width:140px" />
            <span class="hint" style="margin-left:8px">毫秒。给回包落地留时间;最小 80ms。</span>
          </el-form-item>

          <el-form-item label="最多合成次数">
            <el-input-number v-model="config.maxBatches" :min="0" :step="10" style="width:140px" />
            <span class="hint" style="margin-left:8px">0 = 不限(合到没料为止)。防失控可设个上限。</span>
          </el-form-item>

          <el-form-item label="启用(开始合)">
            <el-switch v-model="config.enabled" />
            <span class="hint" style="margin-left:8px">打开并保存后立即开始;合完/没料会自动停。</span>
          </el-form-item>

          <el-form-item>
            <el-button type="primary" @click="saveConfig" :loading="saving">保存 / 开始</el-button>
            <el-button @click="stopNow" :disabled="!config.enabled && live.status !== 'running'">停止</el-button>
            <el-button @click="reload">重新加载</el-button>
          </el-form-item>
        </el-form>
      </template>
    </el-card>

    <!-- 运行状态 -->
    <el-card shadow="never" v-if="selectedName" class="status-card">
      <template #header>
        <div class="card-header">
          <span>运行状态</span>
          <el-tag size="small" :type="statusTagType(live.status)">{{ statusLabel(live.status) }}</el-tag>
        </div>
      </template>
      <el-descriptions :column="2" border size="small">
        <el-descriptions-item label="已合成次数">{{ live.batchCount }}</el-descriptions-item>
        <el-descriptions-item label="最近运行">{{ lastRunLabel }}</el-descriptions-item>
        <el-descriptions-item label="提示" :span="2" v-if="live.lastError">
          <span style="color: var(--el-color-danger)">{{ live.lastError }}</span>
        </el-descriptions-item>
      </el-descriptions>

      <el-divider content-position="left">最近读到的宝石分组</el-divider>
      <el-table v-if="live.lastGroups && live.lastGroups.length" :data="live.lastGroups" size="small" border style="width:100%">
        <el-table-column label="品级" width="80">
          <template #default="{ row }">
            <el-tag size="small" :type="row.gradeRank >= 0 ? 'success' : 'info'">{{ row.gradeLabel }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="示例宝石" min-width="200">
          <template #default="{ row }">{{ row.sampleName || ('#' + row.sampleItemId) }}</template>
        </el-table-column>
        <el-table-column label="数量" width="80" prop="totalCount" />
        <el-table-column label="可合次数" width="100">
          <template #default="{ row }">{{ Math.floor(row.totalCount / 3) }}</template>
        </el-table-column>
        <el-table-column label="gradeKey" width="100" prop="gradeKey" />
      </el-table>
      <div v-else class="hint">—(还没读到分组,或角色离线)</div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useInstances } from '@/composables/useInstances'
import { ElMessage } from 'element-plus'

interface ComposeGroupView {
  gradeKey: number
  sampleItemId: number
  sampleName: string
  totalCount: number
  gradeRank: number
  gradeLabel: string
  wireSlots: number[]
}
interface AutoComposeConfig {
  characterName: string
  enabled: boolean
  targetRank: number
  intervalMs: number
  maxBatches: number
  batchCount: number
  status: string
  lastError: string
  lastRunAt: number
  lastGroups: ComposeGroupView[]
}

// rank: 1=G 2=DG 3=XG(把所有 rank<targetRank 的合上来)。本游戏宝石最高 XG,没有 SG。
const targetOptions = [
  { value: 1, label: '合到 G' },
  { value: 2, label: '合到 DG' },
  { value: 3, label: '合到 XG' },
]

const { instances, selectedInstance } = useInstances()

const selectedName = ref('')
const config = ref<AutoComposeConfig>(emptyConfig(''))
const saving = ref(false)
const knownNames = ref<string[]>([])

interface LiveStatus {
  enabled: boolean
  batchCount: number
  status: string
  lastError: string
  lastRunAt: number
  lastGroups: ComposeGroupView[]
}
function emptyLive(): LiveStatus {
  return { enabled: false, batchCount: 0, status: 'idle', lastError: '', lastRunAt: 0, lastGroups: [] }
}
const live = ref<LiveStatus>(emptyLive())
let statusTimer: ReturnType<typeof setInterval> | null = null

function emptyConfig(name: string): AutoComposeConfig {
  return {
    characterName: name, enabled: false, targetRank: 1,
    intervalMs: 250, maxBatches: 0, batchCount: 0,
    status: 'idle', lastError: '', lastRunAt: 0, lastGroups: [],
  }
}

const characterOptions = computed(() => {
  const set = new Set<string>()
  for (const i of instances.value) if (i.characterName) set.add(i.characterName)
  for (const n of knownNames.value) set.add(n)
  return Array.from(set).map((n) => {
    const online = instances.value.some((i) => i.characterName === n)
    return { value: n, label: online ? `${n} (在线)` : `${n} (离线)` }
  })
})

const lastRunLabel = computed(() => {
  if (!live.value.lastRunAt) return '—'
  return new Date(live.value.lastRunAt).toLocaleString()
})

function statusLabel(s: string): string {
  switch (s) {
    case 'running': return '运行中'
    case 'done': return '已合完,已停止'
    case 'no-gems': return '没有可合的宝石,已停止'
    case 'reached-max': return '到达次数上限,已停止'
    case 'error': return '出错,已停止'
    case 'offline': return '角色离线'
    default: return '空闲'
  }
}
function statusTagType(s: string): 'success' | 'info' | 'warning' | 'danger' {
  if (s === 'running') return 'warning'
  if (s === 'done') return 'success'
  if (s === 'error') return 'danger'
  return 'info'
}

async function loadConfigList() {
  try {
    const list = (await fetch('/api/auto-compose/configs').then((r) => r.json())) as AutoComposeConfig[]
    knownNames.value = Array.isArray(list) ? list.map((c) => c.characterName) : []
  } catch { /* silent */ }
}

async function reload() {
  if (!selectedName.value) return
  try {
    const res = await fetch(`/api/auto-compose/configs/${encodeURIComponent(selectedName.value)}`)
    if (res.status === 404) {
      config.value = emptyConfig(selectedName.value)
      return
    }
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const cfg = (await res.json()) as AutoComposeConfig
    config.value = { ...emptyConfig(selectedName.value), ...cfg }
  } catch (e: any) {
    ElMessage.error(`加载失败: ${e.message}`)
  }
}

async function saveConfig() {
  if (!selectedName.value) return
  saving.value = true
  try {
    const res = await fetch(`/api/auto-compose/configs/${encodeURIComponent(selectedName.value)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const updated = (await res.json()) as AutoComposeConfig
    config.value = { ...emptyConfig(selectedName.value), ...updated }
    syncLive(updated)
    if (updated.enabled) startStatusPoll()
    else stopStatusPoll()
    await loadConfigList()
    ElMessage.success(updated.enabled ? '已保存并开始' : '已保存')
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  } finally {
    saving.value = false
  }
}

async function stopNow() {
  config.value.enabled = false
  await saveConfig()
}

function syncLive(cfg: AutoComposeConfig) {
  live.value = {
    enabled: cfg.enabled, batchCount: cfg.batchCount, status: cfg.status,
    lastError: cfg.lastError, lastRunAt: cfg.lastRunAt, lastGroups: cfg.lastGroups ?? [],
  }
}

// 拉一次运行状态到 live。只读不写表单。任务停了就关掉轮询。返回是否仍在跑。
async function refreshStatus(): Promise<boolean> {
  if (!selectedName.value) return false
  try {
    const res = await fetch(`/api/auto-compose/configs/${encodeURIComponent(selectedName.value)}`)
    if (res.status === 404) { live.value = emptyLive(); stopStatusPoll(); return false }
    if (!res.ok) return false
    const cfg = (await res.json()) as AutoComposeConfig
    syncLive(cfg)
    const active = cfg.enabled || cfg.status === 'running'
    if (!active) stopStatusPoll()
    return active
  } catch {
    return false
  }
}
function startStatusPoll() {
  stopStatusPoll()
  statusTimer = setInterval(refreshStatus, 1000)
}
function stopStatusPoll() {
  if (statusTimer !== null) { clearInterval(statusTimer); statusTimer = null }
}

watch(selectedName, async () => {
  live.value = emptyLive()
  stopStatusPoll()
  await reload()
  const active = await refreshStatus()
  if (active) startStatusPoll()
})

onMounted(async () => {
  await loadConfigList()
  if (!selectedName.value && selectedInstance.value?.characterName) {
    selectedName.value = selectedInstance.value.characterName
  }
})

onUnmounted(() => {
  stopStatusPoll()
})
</script>

<style scoped>
.compose-view {
  max-width: 1000px;
}
.config-card {
  margin-bottom: 16px;
}
.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
}
.empty-hint {
  color: var(--el-text-color-placeholder);
  padding: 24px 0;
  text-align: center;
}
.status-card {
  margin-bottom: 16px;
}
</style>

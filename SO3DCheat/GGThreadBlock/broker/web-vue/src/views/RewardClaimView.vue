<template>
  <div class="reward-claim-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>每日奖励领取 — 在线奖励 + 签到奖励</span>
          <el-button size="small" @click="refresh" :loading="loading">刷新</el-button>
        </div>
      </template>

      <div class="hint" style="margin-bottom:12px">
        勾选要开启的角色后点「应用」。开启的角色上线后每分钟只读探一次「现在有没有可领奖励」(读游戏内闹钟/红心的可领标记,不开窗不发包),
        探到可领才开窗去领。在线奖励按在线时长分档解锁(30/60/90/120/150/180 分钟),后面解锁的档下一轮会自动补领,没有可领时不会开窗。
        下方表格里可对每个账号单独选「在线奖励 / 签到奖励」只领某一种还是都领。
      </div>

      <el-form label-width="110px" size="default">
        <el-form-item label="开启领取的角色">
          <el-select
            v-model="selectedNames"
            multiple
            filterable
            allow-create
            default-first-option
            placeholder="选择(或手填)角色名"
            style="width: 100%; max-width: 560px"
          >
            <el-option
              v-for="opt in characterOptions"
              :key="opt.value"
              :label="opt.label"
              :value="opt.value"
            />
          </el-select>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="applySelection" :loading="applying">应用</el-button>
          <span class="hint" style="margin-left:8px">勾掉再应用即可关闭该角色。</span>
        </el-form-item>
      </el-form>
    </el-card>

    <el-card shadow="never" class="status-card">
      <template #header>
        <div class="card-header">
          <span>角色状态</span>
        </div>
      </template>

      <div v-if="configs.length === 0" class="empty-hint">还没有任何角色配置。上面选角色并应用即可开启。</div>
      <el-table v-else :data="configs" size="small" border style="width:100%">
        <el-table-column label="角色" min-width="140">
          <template #default="{ row }">
            {{ row.characterName }}
            <el-tag v-if="isOnline(row.characterName)" size="small" type="success" effect="plain" style="margin-left:6px">在线</el-tag>
            <el-tag v-else size="small" type="info" effect="plain" style="margin-left:6px">离线</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="开启" width="80">
          <template #default="{ row }">
            <el-switch
              :model-value="row.enabled"
              size="small"
              @change="(v: boolean) => toggleEnabled(row, v)"
            />
          </template>
        </el-table-column>
        <el-table-column label="在线奖励" width="90">
          <template #default="{ row }">
            <el-switch
              :model-value="row.claimOnline"
              size="small"
              @change="(v: boolean) => toggleClaim(row, 'claimOnline', v)"
            />
          </template>
        </el-table-column>
        <el-table-column label="签到奖励" width="90">
          <template #default="{ row }">
            <el-switch
              :model-value="row.claimSignin"
              size="small"
              @change="(v: boolean) => toggleClaim(row, 'claimSignin', v)"
            />
          </template>
        </el-table-column>
        <el-table-column label="今天" width="90">
          <template #default="{ row }">
            <el-tag v-if="row.doneDate === today" size="small" type="success">已领完</el-tag>
            <el-tag v-else size="small" type="warning" effect="plain">待领</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="上次结果" min-width="240">
          <template #default="{ row }">
            <span class="result-text">{{ row.lastResult || '—' }}</span>
          </template>
        </el-table-column>
        <el-table-column label="上次时间" width="160">
          <template #default="{ row }">{{ formatTime(row.lastAttempt) }}</template>
        </el-table-column>
        <el-table-column label="操作" width="160">
          <template #default="{ row }">
            <el-button
              link
              type="primary"
              size="small"
              :loading="runningName === row.characterName"
              :disabled="!isOnline(row.characterName)"
              @click="runNow(row.characterName)"
            >立即领取</el-button>
            <el-button link type="danger" size="small" @click="removeConfig(row.characterName)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useInstances } from '@/composables/useInstances'
import { ElMessage } from 'element-plus'

interface RewardClaimConfig {
  characterName: string
  enabled: boolean
  claimOnline: boolean
  claimSignin: boolean
  doneDate: string
  lastAttempt: number
  lastResult: string
}

const { instances } = useInstances()

const configs = ref<RewardClaimConfig[]>([])
const selectedNames = ref<string[]>([])
const loading = ref(false)
const applying = ref(false)
const runningName = ref<string>('')
let pollTimer: ReturnType<typeof setInterval> | null = null

const today = computed(() => dayKey(Date.now()))

const characterOptions = computed(() => {
  const set = new Set<string>()
  for (const i of instances.value) if (i.characterName) set.add(i.characterName)
  for (const c of configs.value) set.add(c.characterName)
  return Array.from(set).map((n) => {
    const online = instances.value.some((i) => i.characterName === n)
    return { value: n, label: online ? `${n} (在线)` : `${n} (离线)` }
  })
})

function dayKey(ms: number): string {
  const d = new Date(ms)
  const y = d.getFullYear()
  const m = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  return `${y}-${m}-${day}`
}

function isOnline(name: string): boolean {
  return instances.value.some((i) => i.characterName === name)
}

function formatTime(ms: number): string {
  if (!ms) return '—'
  return new Date(ms).toLocaleString()
}

async function refresh() {
  loading.value = true
  try {
    const list = (await fetch('/api/reward-claim/configs').then((r) => r.json())) as RewardClaimConfig[]
    configs.value = Array.isArray(list) ? list : []
    selectedNames.value = configs.value.filter((c) => c.enabled).map((c) => c.characterName)
  } catch (e: any) {
    ElMessage.error(`加载失败: ${e.message}`)
  } finally {
    loading.value = false
  }
}

async function patchConfig(name: string, body: Partial<RewardClaimConfig>): Promise<void> {
  const res = await fetch(`/api/reward-claim/configs/${encodeURIComponent(name)}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
}

async function applySelection() {
  applying.value = true
  try {
    const desired = new Set(selectedNames.value)
    const names = new Set<string>([...desired, ...configs.value.map((c) => c.characterName)])
    const ops: Promise<void>[] = []
    for (const name of names) {
      const cur = configs.value.find((c) => c.characterName === name)
      const want = desired.has(name)
      if (!cur && !want) continue
      if (!cur || cur.enabled !== want) ops.push(patchConfig(name, { enabled: want }))
    }
    await Promise.all(ops)
    await refresh()
    ElMessage.success('已应用')
  } catch (e: any) {
    ElMessage.error(`应用失败: ${e.message}`)
  } finally {
    applying.value = false
  }
}

async function toggleEnabled(row: RewardClaimConfig, enabled: boolean) {
  try {
    await patchConfig(row.characterName, { enabled })
    await refresh()
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  }
}

// 在线/签到选择:每账号可单独选只领一种还是都领。不允许两种都关(那样开启了却什么都不领)。
async function toggleClaim(row: RewardClaimConfig, field: 'claimOnline' | 'claimSignin', value: boolean) {
  const other = field === 'claimOnline' ? row.claimSignin : row.claimOnline
  if (!value && !other) {
    ElMessage.warning('在线/签到至少要保留一种')
    await refresh()
    return
  }
  try {
    await patchConfig(row.characterName, { [field]: value })
    await refresh()
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  }
}

async function runNow(name: string) {
  runningName.value = name
  try {
    const r = await fetch(`/api/reward-claim/run-now/${encodeURIComponent(name)}`, { method: 'POST' }).then((x) => x.json())
    if (r.ok) ElMessage.success(`领取完成: ${r.detail ?? ''}`)
    else ElMessage.warning(`领取: ${r.detail ?? '失败'}`)
    await refresh()
  } catch (e: any) {
    ElMessage.error(`领取失败: ${e.message}`)
  } finally {
    runningName.value = ''
  }
}

async function removeConfig(name: string) {
  try {
    await fetch(`/api/reward-claim/configs/${encodeURIComponent(name)}`, { method: 'DELETE' })
    await refresh()
  } catch (e: any) {
    ElMessage.error(`删除失败: ${e.message}`)
  }
}

onMounted(async () => {
  await refresh()
  pollTimer = setInterval(refresh, 5000)
})

onUnmounted(() => {
  if (pollTimer !== null) clearInterval(pollTimer)
})
</script>

<style scoped>
.reward-claim-view {
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
.result-text {
  font-size: 12px;
  color: var(--el-text-color-regular);
}
</style>

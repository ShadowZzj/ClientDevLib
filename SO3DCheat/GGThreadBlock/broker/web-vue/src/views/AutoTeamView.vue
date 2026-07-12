<template>
  <div class="auto-team-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>自动组队 — 选中角色,输入创建人名,自动开队伍列表并加入对方队伍</span>
          <el-tag :type="onlineCount > 0 ? 'success' : 'info'" size="small">
            {{ onlineCount > 0 ? `${onlineCount} 个在线` : '无在线角色' }}
          </el-tag>
        </div>
      </template>

      <el-form label-width="100px" size="default">
        <el-form-item label="参与角色">
          <div style="width: 100%; max-width: 560px">
            <el-select
              v-model="selectedNames"
              placeholder="选择要加入队伍的角色(可多选)"
              multiple
              filterable
              collapse-tags
              collapse-tags-tooltip
              style="width: 100%"
            >
              <el-option
                v-for="opt in characterOptions"
                :key="opt.value"
                :label="opt.label"
                :value="opt.value"
              />
            </el-select>
            <div class="select-actions">
              <el-button size="small" :disabled="onlineCount === 0" @click="selectAllOnline">
                全选在线 ({{ onlineCount }})
              </el-button>
              <el-button size="small" :disabled="selectedNames.length === 0" @click="selectedNames = []">
                清空
              </el-button>
            </div>
          </div>
          <span class="hint">这些角色会各自加入下面指定创建人的队伍。离线角色仍可移除,但加入时会被跳过。创建人本人若在名单里也会自动跳过,不加入自己。</span>
        </el-form-item>

        <el-form-item label="创建人名">
          <el-input
            v-model="leaderName"
            placeholder="队长 / 创建人角色名"
            clearable
            style="width: 280px"
          />
          <span class="hint">在队伍列表里按队长名精确匹配。可在下方列表点「设为目标」自动填入。</span>
        </el-form-item>

        <el-form-item label="列表数据源">
          <el-select
            v-model="sourcePid"
            placeholder="用哪个在线角色拉取列表"
            filterable
            style="width: 280px"
          >
            <el-option
              v-for="i in onlineInstances"
              :key="i.pid"
              :label="`${i.characterName || '(未知)'} · pid ${i.pid}`"
              :value="i.pid"
            />
          </el-select>
          <span class="hint">刷新列表会让这个角色短暂开/关组队窗口。</span>
        </el-form-item>

        <el-form-item label="加入间隔">
          <el-input-number v-model="intervalSec" :min="0" :max="60" :step="1" controls-position="right" style="width: 140px" />
          <span class="hint">多个角色依次加入,每个之间间隔的秒数(默认 3 秒),太快队长处理不过来。</span>
        </el-form-item>

        <el-form-item>
          <el-button type="primary" @click="refresh" :loading="loading" :disabled="!sourcePid">
            刷新队伍列表
          </el-button>
          <el-button
            type="success"
            @click="join"
            :loading="joining"
            :disabled="selectedNames.length === 0 || !leaderName.trim()"
          >
            加入({{ selectedNames.length }})
          </el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <el-card v-if="parties.length > 0 || listFetched" shadow="never" class="list-card">
      <template #header>
        <div class="card-header">
          <span>队伍列表</span>
          <el-tag size="small" type="info">
            {{ parties.length }} 支队伍 · {{ listMaxPage }} 页
          </el-tag>
        </div>
      </template>

      <el-table :data="parties" size="small" stripe empty-text="没有队伍">
        <el-table-column label="创建人" prop="leaderName" min-width="120">
          <template #default="{ row }">
            <span :class="{ 'leader-hit': isTarget(row.leaderName) }">{{ row.leaderName || '—' }}</span>
          </template>
        </el-table-column>
        <el-table-column label="队伍名" prop="partyName" min-width="140" show-overflow-tooltip />
        <el-table-column label="人数" width="90">
          <template #default="{ row }">{{ row.curMembers }}/{{ row.maxMembers }}</template>
        </el-table-column>
        <el-table-column label="分配" width="80">
          <template #default="{ row }">{{ distributionLabel(row.distribution) }}</template>
        </el-table-column>
        <el-table-column label="地图" prop="mapId" width="80" />
        <el-table-column label="队伍ID" prop="partyId" width="100" />
        <el-table-column label="操作" width="170" fixed="right">
          <template #default="{ row }">
            <el-button link type="primary" size="small" @click="setTarget(row.leaderName)">
              设为目标
            </el-button>
            <el-button
              link
              type="success"
              size="small"
              :disabled="selectedNames.length === 0"
              @click="joinPartyId(row.partyId, row.leaderName)"
            >
              直接加入
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-card v-if="results.length > 0" shadow="never" class="result-card">
      <template #header><span>加入结果</span></template>
      <el-table :data="results" size="small">
        <el-table-column label="角色" prop="characterName" min-width="120">
          <template #default="{ row }">{{ row.characterName || `pid ${row.pid}` }}</template>
        </el-table-column>
        <el-table-column label="结果" width="90">
          <template #default="{ row }">
            <el-tag :type="row.ok ? 'success' : 'danger'" size="small">
              {{ row.ok ? '成功' : '失败' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="队伍ID" prop="partyId" width="100" />
        <el-table-column label="说明" prop="error" min-width="160" show-overflow-tooltip />
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import { useInstances } from '@/composables/useInstances'
import { ElMessage } from 'element-plus'

interface PartyBoardEntry {
  partyId: number
  distribution: number
  curMembers: number
  maxMembers: number
  mapId: number
  partyName: string
  leaderName: string
}

interface JoinResult {
  pid: number
  characterName: string
  ok: boolean
  partyId: number
  error?: string
}

const { instances } = useInstances()

const selectedNames = ref<string[]>([])
const leaderName = ref('')
const intervalSec = ref(3)
const sourcePid = ref<number | null>(null)
const parties = ref<PartyBoardEntry[]>([])
const results = ref<JoinResult[]>([])
const listMaxPage = ref(0)
const listFetched = ref(false)
const loading = ref(false)
const joining = ref(false)

const onlineInstances = computed(() => instances.value.filter((i) => !!i.characterName))
const onlineCount = computed(() => onlineInstances.value.length)

// 在线角色 + 已选但当前离线的角色都列出来,离线的标注但不禁用(否则其标签无法移除)。
const characterOptions = computed(() => {
  const set = new Set<string>()
  for (const i of onlineInstances.value) set.add(i.characterName)
  for (const n of selectedNames.value) set.add(n)
  return Array.from(set).map((n) => {
    const online = onlineInstances.value.some((i) => i.characterName === n)
    return { value: n, label: online ? `${n} (在线)` : `${n} (离线)` }
  })
})

// 分配方式枚举来自引擎原始值,具体含义尚未逐一逆向 —— 已知 1=平均分配,其余先显示原始值。
function distributionLabel(d: number): string {
  if (d === 1) return '平均'
  return String(d)
}

// 清空当前选择,再把所有在线角色全部放进去。
function selectAllOnline() {
  selectedNames.value = onlineInstances.value.map((i) => i.characterName).filter(Boolean)
}

function isTarget(name: string): boolean {
  return !!name && name.trim() === leaderName.value.trim()
}

function setTarget(name: string) {
  leaderName.value = (name || '').trim()
}

// 默认数据源 = 第一个选中的在线角色,否则第一个在线角色。
function ensureSourcePid() {
  const stillOnline = onlineInstances.value.some((i) => i.pid === sourcePid.value)
  if (sourcePid.value && stillOnline) return
  const firstSelected = onlineInstances.value.find((i) => selectedNames.value.includes(i.characterName))
  sourcePid.value = firstSelected?.pid ?? onlineInstances.value[0]?.pid ?? null
}

async function refresh() {
  ensureSourcePid()
  if (!sourcePid.value) {
    ElMessage.warning('没有可用的在线角色拉取列表')
    return
  }
  loading.value = true
  try {
    const res = await fetch(`/api/auto-team/party-list/${sourcePid.value}`)
    const data = await res.json()
    if (!res.ok || data?.ok === false) {
      throw new Error(data?.error || `HTTP ${res.status}`)
    }
    parties.value = Array.isArray(data.parties) ? data.parties : []
    listMaxPage.value = Number(data.maxPage) || 0
    listFetched.value = true
    ElMessage.success(`已拉取 ${parties.value.length} 支队伍`)
  } catch (e: any) {
    ElMessage.error(`刷新失败: ${e.message}`)
  } finally {
    loading.value = false
  }
}

async function doJoin(body: Record<string, unknown>) {
  joining.value = true
  try {
    const res = await fetch('/api/auto-team/join', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
    const data = await res.json()
    if (!res.ok) throw new Error(data?.error || `HTTP ${res.status}`)
    results.value = Array.isArray(data.results) ? data.results : []
    const okCount = results.value.filter((r) => r.ok).length
    if (okCount === results.value.length) ElMessage.success(`全部加入成功 (${okCount})`)
    else ElMessage.warning(`成功 ${okCount}/${results.value.length},详见结果`)
  } catch (e: any) {
    ElMessage.error(`加入失败: ${e.message}`)
  } finally {
    joining.value = false
  }
}

function join() {
  doJoin({
    characterNames: selectedNames.value,
    leaderName: leaderName.value.trim(),
    intervalMs: Math.max(0, intervalSec.value) * 1000,
  })
}

function joinPartyId(partyId: number, leader = '') {
  if (!partyId) return
  doJoin({
    characterNames: selectedNames.value,
    partyId,
    leaderName: leader.trim(),
    intervalMs: Math.max(0, intervalSec.value) * 1000,
  })
}

// ---- 轻量持久化:记住选中角色 + 创建人名,刷新页面后回填 ----
let saveTimer: number | undefined
function scheduleSave() {
  if (saveTimer) clearTimeout(saveTimer)
  saveTimer = window.setTimeout(async () => {
    try {
      await fetch('/api/auto-team/config', {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          leaderName: leaderName.value.trim(),
          selectedNames: selectedNames.value,
          intervalSec: Math.max(0, intervalSec.value),
        }),
      })
    } catch { /* silent */ }
  }, 500)
}

let loaded = false
watch([selectedNames, leaderName, intervalSec], () => {
  if (loaded) scheduleSave()
})

onMounted(async () => {
  try {
    const res = await fetch('/api/auto-team/config')
    if (res.ok) {
      const cfg = await res.json()
      if (typeof cfg?.leaderName === 'string') leaderName.value = cfg.leaderName
      if (Array.isArray(cfg?.selectedNames)) selectedNames.value = cfg.selectedNames.map((n: unknown) => String(n))
      if (Number.isFinite(Number(cfg?.intervalSec))) intervalSec.value = Math.max(0, Math.floor(Number(cfg.intervalSec)))
    }
  } catch { /* silent */ }
  ensureSourcePid()
  loaded = true
})
</script>

<style scoped>
.auto-team-view {
  max-width: 900px;
}
.config-card,
.list-card,
.result-card {
  margin-bottom: 16px;
}
.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.select-actions {
  margin-top: 6px;
  display: flex;
  gap: 8px;
}
.hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-left: 8px;
}
.leader-hit {
  color: var(--el-color-success);
  font-weight: 600;
}
</style>

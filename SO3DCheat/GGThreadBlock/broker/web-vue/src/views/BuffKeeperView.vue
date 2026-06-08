<template>
  <div class="buff-keeper-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>Buff 守护 — 缺 buff 自动放技能</span>
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

      <div v-if="!selectedName" class="empty-hint">先选一个角色。在线角色直接选,也可手填离线角色名。</div>

      <template v-else>
        <el-form label-width="110px" size="default" class="head-form">
          <el-form-item label="角色总开关">
            <el-switch v-model="config.enabled" />
            <span class="hint" style="margin-left:8px">关掉就完全停这个角色的守护。</span>
          </el-form-item>
          <el-form-item label="轮询间隔">
            <el-input-number v-model="config.pollMs" :min="1500" :max="60000" :step="500" />
            <span class="hint" style="margin-left:8px">毫秒。每隔这么久查一次 buff 快照。</span>
          </el-form-item>
        </el-form>

        <el-divider content-position="left">规则</el-divider>

        <el-table :data="config.rules" size="small" border style="width:100%">
          <el-table-column label="启用" width="60">
            <template #default="{ row }">
              <el-switch v-model="row.enabled" size="small" />
            </template>
          </el-table-column>
          <el-table-column label="匹配 buffId" width="130">
            <template #default="{ row }">
              <el-input-number v-model="row.buffId" :min="0" :controls="false" size="small" style="width:100px" />
            </template>
          </el-table-column>
          <el-table-column label="或 buff 名" min-width="140">
            <template #default="{ row }">
              <el-input v-model="row.buffName" size="small" placeholder="buffId=0 时按名字匹配" />
            </template>
          </el-table-column>
          <el-table-column label="放技能 skillId" width="130">
            <template #default="{ row }">
              <el-input-number v-model="row.skillId" :min="0" :controls="false" size="small" style="width:100px" />
            </template>
          </el-table-column>
          <el-table-column label="目标Id" width="110">
            <template #default="{ row }">
              <el-input-number v-model="row.targetId" :min="0" :controls="false" size="small" style="width:90px" />
            </template>
          </el-table-column>
          <el-table-column label="最小间隔ms" width="120">
            <template #default="{ row }">
              <el-input-number v-model="row.minRecastMs" :min="0" :step="500" :controls="false" size="small" style="width:100px" />
            </template>
          </el-table-column>
          <el-table-column label="操作" width="70">
            <template #default="{ $index }">
              <el-button link type="danger" size="small" @click="config.rules.splice($index, 1)">删除</el-button>
            </template>
          </el-table-column>
        </el-table>
        <div class="rule-actions">
          <el-button size="small" @click="addRule">新增规则</el-button>
          <div class="hint">buffId &gt; 0 优先按 id 匹配;否则按 buff 名精确匹配。最小间隔应 ≥ 技能冷却,避免冷却期间刷包。</div>
        </div>

        <el-form-item style="margin-top:16px">
          <el-button type="primary" @click="saveConfig" :loading="saving">保存配置</el-button>
          <el-button @click="reload">重新加载</el-button>
        </el-form-item>
      </template>
    </el-card>

    <el-card shadow="never" class="buffs-card" v-if="selectedName">
      <template #header>
        <div class="card-header">
          <span>当前 Buff 快照</span>
          <el-button size="small" @click="refreshBuffs" :loading="loadingBuffs" :disabled="!selectedPidValue">
            刷新
          </el-button>
        </div>
      </template>
      <div v-if="!selectedPidValue" class="empty-hint">该角色不在线,无法读取实时 buff。</div>
      <el-table v-else :data="buffs" size="small" border style="width:100%" max-height="320">
        <el-table-column prop="buffId" label="buffId" width="100" />
        <el-table-column prop="name" label="名称" min-width="140" />
        <el-table-column prop="kind" label="类型" width="80" />
        <el-table-column label="剩余" width="100">
          <template #default="{ row }">{{ formatRemain(row.remainingMs) }}</template>
        </el-table-column>
        <el-table-column prop="skillId" label="来源技能" width="100" />
        <el-table-column label="用此条建规则" width="120">
          <template #default="{ row }">
            <el-button link type="primary" size="small" @click="ruleFromBuff(row)">+ 规则</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-card shadow="never" class="party-card" v-if="selectedName">
      <template #header>
        <div class="card-header">
          <span>
            组队成员
            <el-tag v-if="party && party.inParty" size="small" type="success" style="margin-left:8px">
              {{ roleLabel(party.role) }} · {{ party.members.length }} 人
            </el-tag>
            <el-tag v-else size="small" type="info" style="margin-left:8px">未组队</el-tag>
          </span>
          <div class="party-head-right">
            <el-switch
              v-model="partyAutoRefresh"
              size="small"
              active-text="自动"
              style="margin-right:12px"
              :disabled="!selectedPidValue"
            />
            <el-button size="small" @click="refreshParty" :loading="loadingParty" :disabled="!selectedPidValue">
              刷新
            </el-button>
          </div>
        </div>
      </template>

      <div v-if="!selectedPidValue" class="empty-hint">该角色不在线,无法读取组队信息。</div>
      <div v-else-if="!party || !party.inParty" class="empty-hint">当前不在任何组队中。</div>
      <div v-else class="party-list">
        <div v-for="m in party.members" :key="m.index" class="party-member">
          <div class="member-head">
            <span class="member-name">
              {{ m.name || '(未知)' }}
              <el-tag v-if="m.isSelf" size="small" type="primary" effect="plain">我</el-tag>
              <el-tag v-if="!m.online" size="small" type="info" effect="plain">离线</el-tag>
            </span>
            <span class="member-hp" v-if="m.maxHp > 0">
              <el-progress
                :percentage="hpPercent(m.hp, m.maxHp)"
                :status="hpStatus(m.hp, m.maxHp)"
                :stroke-width="14"
                :text-inside="true"
                style="width:160px"
              />
              <span class="hp-text">{{ m.hp }}/{{ m.maxHp }}</span>
            </span>
            <span class="member-hp" v-else-if="m.hp >= 0">HP {{ m.hp }}</span>
          </div>
          <div class="member-buffs">
            <template v-if="m.buffs && m.buffs.length">
              <el-tag
                v-for="b in m.buffs"
                :key="b.buffId + '-' + b.name"
                size="small"
                :type="b.kind === 'cash' ? 'warning' : 'success'"
                effect="light"
                class="buff-tag"
              >
                {{ b.name || ('#' + b.buffId) }}
                <span class="buff-remain">{{ formatRemain(b.remainingMs) }}</span>
              </el-tag>
            </template>
            <span v-else class="no-buff">无 buff</span>
          </div>
        </div>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useInstances } from '@/composables/useInstances'
import { ElMessage } from 'element-plus'

interface BuffRule {
  id: string
  enabled: boolean
  buffId: number
  buffName: string
  skillId: number
  targetId: number
  minRecastMs: number
  lastCast?: number
}
interface BuffKeeperConfig {
  characterName: string
  enabled: boolean
  pollMs: number
  rules: BuffRule[]
}
interface BuffSnapshotItem {
  buffId: number
  name: string
  kind: string
  remainingMs: number
  skillId: number
}
interface PartyMember {
  index: number
  name: string
  userId: number
  isSelf: boolean
  online: boolean
  hp: number
  maxHp: number
  buffs: BuffSnapshotItem[]
}
interface PartySnapshot {
  inParty: boolean
  role: number
  selfIndex: number
  members: PartyMember[]
}

const { instances, selectedInstance } = useInstances()

const selectedName = ref<string>('')
const config = ref<BuffKeeperConfig>(emptyConfig(''))
const saving = ref(false)
const buffs = ref<BuffSnapshotItem[]>([])
const loadingBuffs = ref(false)
const knownNames = ref<string[]>([])
const party = ref<PartySnapshot | null>(null)
const loadingParty = ref(false)
const partyAutoRefresh = ref(true)
let partyTimer: ReturnType<typeof setInterval> | null = null

function emptyConfig(name: string): BuffKeeperConfig {
  return { characterName: name, enabled: false, pollMs: 4000, rules: [] }
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

const selectedPidValue = computed(() => {
  const inst = instances.value.find((i) => i.characterName === selectedName.value)
  return inst?.pid ?? null
})

function genId(): string {
  return Math.random().toString(36).slice(2)
}

function addRule() {
  config.value.rules.push({
    id: genId(),
    enabled: true,
    buffId: 0,
    buffName: '',
    skillId: 0,
    targetId: 0,
    minRecastMs: 3000,
  })
}

function ruleFromBuff(row: BuffSnapshotItem) {
  config.value.rules.push({
    id: genId(),
    enabled: true,
    buffId: row.buffId,
    buffName: row.name,
    skillId: row.skillId || 0,
    targetId: 0,
    minRecastMs: 3000,
  })
  ElMessage.success(`已添加规则:buffId=${row.buffId}${row.skillId ? ` skill=${row.skillId}` : '(技能待填)'}`)
}

function formatRemain(ms: number): string {
  if (ms < 0) return '常驻'
  const s = Math.round(ms / 1000)
  if (s < 60) return `${s}s`
  return `${Math.floor(s / 60)}m${s % 60}s`
}

async function loadConfigList() {
  try {
    const list = (await fetch('/api/buff-keeper/configs').then((r) => r.json())) as BuffKeeperConfig[]
    knownNames.value = Array.isArray(list) ? list.map((c) => c.characterName) : []
  } catch { /* silent */ }
}

async function reload() {
  if (!selectedName.value) return
  try {
    const res = await fetch(`/api/buff-keeper/configs/${encodeURIComponent(selectedName.value)}`)
    if (res.status === 404) {
      config.value = emptyConfig(selectedName.value)
      return
    }
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const cfg = (await res.json()) as BuffKeeperConfig
    config.value = { ...emptyConfig(selectedName.value), ...cfg, rules: cfg.rules ?? [] }
  } catch (e: any) {
    ElMessage.error(`加载失败: ${e.message}`)
  }
}

async function saveConfig() {
  if (!selectedName.value) return
  saving.value = true
  try {
    const res = await fetch(`/api/buff-keeper/configs/${encodeURIComponent(selectedName.value)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const updated = (await res.json()) as BuffKeeperConfig
    config.value = { ...emptyConfig(selectedName.value), ...updated, rules: updated.rules ?? [] }
    await loadConfigList()
    ElMessage.success('已保存')
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  } finally {
    saving.value = false
  }
}

async function refreshBuffs() {
  const pid = selectedPidValue.value
  if (!pid) return
  loadingBuffs.value = true
  try {
    const res = await fetch(`/api/command/${pid}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'queryBuffs', args: { kind: -1 } }),
    })
    const r = await res.json()
    if (!r.ok) throw new Error(r.detail || r.error || '查询失败')
    buffs.value = JSON.parse(r.detail || '[]') as BuffSnapshotItem[]
  } catch (e: any) {
    ElMessage.error(`读取 buff 失败: ${e.message}`)
  } finally {
    loadingBuffs.value = false
  }
}

function roleLabel(role: number): string {
  if (role === 1) return '队长'
  if (role === 2) return '队员'
  return '组队'
}

function hpPercent(hp: number, maxHp: number): number {
  if (maxHp <= 0) return 0
  const p = Math.round((hp / maxHp) * 100)
  return Math.max(0, Math.min(100, p))
}

function hpStatus(hp: number, maxHp: number): '' | 'success' | 'warning' | 'exception' {
  const p = hpPercent(hp, maxHp)
  if (p <= 25) return 'exception'
  if (p <= 50) return 'warning'
  return 'success'
}

async function refreshParty(manual = false) {
  const pid = selectedPidValue.value
  if (!pid) {
    party.value = null
    return
  }
  loadingParty.value = true
  try {
    const res = await fetch(`/api/command/${pid}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'queryParty', args: { buffs: true } }),
    })
    const r = await res.json()
    if (!r.ok) throw new Error(r.detail || r.error || '查询失败')
    party.value = JSON.parse(r.detail || '{}') as PartySnapshot
  } catch (e: any) {
    party.value = null
    if (manual) ElMessage.error(`读取组队失败: ${e.message}`)
  } finally {
    loadingParty.value = false
  }
}

function stopPartyPoll() {
  if (partyTimer !== null) {
    clearInterval(partyTimer)
    partyTimer = null
  }
}

function startPartyPoll() {
  stopPartyPoll()
  if (!partyAutoRefresh.value || !selectedPidValue.value) return
  const interval = Math.max(2000, config.value.pollMs || 4000)
  partyTimer = setInterval(() => refreshParty(false), interval)
}

watch(selectedName, () => {
  buffs.value = []
  party.value = null
  reload()
  if (selectedPidValue.value) {
    refreshBuffs()
    refreshParty(false)
  }
  startPartyPoll()
})

watch([partyAutoRefresh, selectedPidValue, () => config.value.pollMs], () => {
  startPartyPoll()
  if (partyAutoRefresh.value && selectedPidValue.value && !party.value) refreshParty(false)
})

onMounted(async () => {
  await loadConfigList()
  if (!selectedName.value && selectedInstance.value?.characterName) {
    selectedName.value = selectedInstance.value.characterName
  }
})

onUnmounted(() => {
  stopPartyPoll()
})
</script>

<style scoped>
.buff-keeper-view {
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
.head-form {
  margin-bottom: 8px;
}
.rule-actions {
  margin-top: 10px;
}
.hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-top: 4px;
}
.empty-hint {
  color: var(--el-text-color-placeholder);
  padding: 24px 0;
  text-align: center;
}
.party-card {
  margin-top: 16px;
}
.party-head-right {
  display: flex;
  align-items: center;
}
.party-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.party-member {
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 6px;
  padding: 10px 12px;
}
.member-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 8px;
}
.member-name {
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 6px;
}
.member-hp {
  display: flex;
  align-items: center;
  gap: 8px;
}
.hp-text {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  min-width: 90px;
  text-align: right;
}
.member-buffs {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.buff-tag {
  display: inline-flex;
  align-items: center;
  gap: 4px;
}
.buff-remain {
  opacity: 0.7;
  font-size: 11px;
}
.no-buff {
  color: var(--el-text-color-placeholder);
  font-size: 12px;
}
</style>

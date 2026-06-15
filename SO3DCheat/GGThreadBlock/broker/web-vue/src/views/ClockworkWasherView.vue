<template>
  <div class="clockwork-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>自动洗发条 — 按等级/属性条件反复洗到满足为止</span>
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

      <div v-if="!selectedName" class="empty-hint">先选一个角色(需在线才能读背包/洗发条)。</div>

      <template v-else>
        <!-- 1. 选要洗的装备 -->
        <el-divider content-position="left">① 选要洗的装备(普通背包)</el-divider>
        <div class="row">
          <el-button size="small" @click="refreshBag" :loading="loadingBag" :disabled="!selectedPidValue">
            刷新背包
          </el-button>
          <el-select
            v-model="config.slotIndex"
            placeholder="选装备格子"
            size="small"
            style="width: 320px; margin-left: 10px"
            filterable
            @change="onPickEquip"
          >
            <el-option
              v-for="it in bagItems"
              :key="it.slotIndex"
              :label="`[${it.slotIndex}] ${it.name || ('#'+it.itemId)}`"
              :value="it.slotIndex"
            />
          </el-select>
          <el-button size="small" style="margin-left:10px" @click="refreshState" :loading="loadingState" :disabled="!selectedPidValue">
            读当前发条
          </el-button>
        </div>
        <div class="cur-state" v-if="curState">
          <el-tag size="small" :type="curState.valid ? 'success' : 'info'">
            等级 {{ gradeLabel(curState.grade) }}
          </el-tag>
          <el-tag
            v-for="(a, i) in curState.attrs"
            :key="i"
            size="small"
            effect="plain"
            style="margin-left:6px"
          >
            {{ attrLabel(a) }}
          </el-tag>
          <span v-if="!curState.valid" class="hint" style="margin-left:8px">该格子没有可读发条的装备</span>
        </div>

        <!-- 2. 选发条 + 条件 -->
        <el-divider content-position="left">② 用哪种发条 + 停止条件</el-divider>
        <el-form label-width="120px" size="default">
          <el-form-item label="发条种类">
            <el-radio-group v-model="config.springType" @change="onSpringTypeChange">
              <el-radio-button :value="0">實習生</el-radio-button>
              <el-radio-button :value="1">高手</el-radio-button>
              <el-radio-button :value="2">武爾坎努斯</el-radio-button>
            </el-radio-group>
            <span class="hint" style="margin-left:8px">
              自动在普通/cash 背包里找这种发条来洗。最高可洗到 {{ GRADE_LABELS[springMaxGrade] }} 级。
            </span>
          </el-form-item>

          <el-form-item label="目标等级">
            <el-select v-model="config.targetGrade" style="width: 160px">
              <el-option v-for="o in gradeOptions" :key="o.value" :value="o.value" :label="o.label" />
            </el-select>
            <span class="hint" style="margin-left:8px">等级优先:没到目标等级会一直洗。等级只升不降。</span>
          </el-form-item>

          <el-alert
            v-if="configWarnings.length"
            type="warning"
            :closable="false"
            show-icon
            style="margin: 0 0 12px"
          >
            <div v-for="(msg, i) in configWarnings" :key="i">⚠ {{ msg }}</div>
          </el-alert>

          <el-form-item label="需要属性">
            <div style="width:100%">
              <el-table :data="config.requiredAttrs" size="small" border style="width:100%; max-width:680px">
                <el-table-column label="属性" min-width="200">
                  <template #default="{ row }">
                    <el-select v-model="row.id" size="small" filterable placeholder="选属性" style="width:100%">
                      <el-option
                        v-for="a in attrTable"
                        :key="a.id"
                        :label="attrSelectLabel(a)"
                        :value="a.id"
                      />
                    </el-select>
                  </template>
                </el-table-column>
                <el-table-column label="数值/M ≥" width="120">
                  <template #default="{ row }">
                    <el-input-number v-model="row.minValue" :min="0" :controls="false" size="small" style="width:100px" />
                  </template>
                </el-table-column>
                <el-table-column label="每N級 N ≤" width="130">
                  <template #default="{ row }">
                    <el-input-number
                      v-if="isComposite(row.id)"
                      v-model="row.maxN"
                      :min="0"
                      :controls="false"
                      size="small"
                      style="width:100px"
                      placeholder="不限"
                    />
                    <span v-else class="hint">—</span>
                  </template>
                </el-table-column>
                <el-table-column label="操作" width="70">
                  <template #default="{ $index }">
                    <el-button link type="danger" size="small" @click="config.requiredAttrs.splice($index, 1)">删除</el-button>
                  </template>
                </el-table-column>
              </el-table>
              <div class="rule-actions">
                <el-button size="small" @click="addAttr" :disabled="!attrTable.length">新增需要属性</el-button>
                <span class="hint" v-if="!attrTable.length" style="margin-left:8px">属性表为空,点上面"读当前发条"旁的刷新或确认角色在线。</span>
                <div class="hint">复合属性(每N等級增加X)用「数值M≥」管增加量、「每N級 N≤」管等级间隔(N 越小越好,留 0=不限);普通属性只看「数值≥」。</div>
              </div>
            </div>
          </el-form-item>

          <el-form-item label="至少命中">
            <el-input-number v-model="config.minMatchCount" :min="0" :max="3" :step="1" style="width:120px" />
            <span class="hint" style="margin-left:8px">3 条属性里至少有几条命中需求(数值达标才算)。发条属性可重复,两条魔法力=命中2、两条智力每级=命中2。0 = 列出的每种需求都至少出现一次。</span>
          </el-form-item>

          <el-form-item label="两次洗间隔">
            <el-input-number v-model="config.pollMs" :min="0" :max="10000" :step="100" style="width:140px" />
            <span class="hint" style="margin-left:8px">毫秒。每次洗已等服务器回包确认,这里是额外节流;0=收到回包立刻洗下一次(按网络往返自限速,最快)。</span>
          </el-form-item>

          <el-form-item label="最多洗次数">
            <el-input-number v-model="config.maxWashes" :min="0" :step="10" style="width:140px" />
            <span class="hint" style="margin-left:8px">0 = 不限(洗到满足为止或发条用完)。防失控可设个上限。</span>
          </el-form-item>

          <el-form-item label="启用(开始洗)">
            <el-switch v-model="config.enabled" />
            <span class="hint" style="margin-left:8px">打开并保存后立即开始;满足条件/发条用完会自动停。</span>
          </el-form-item>

          <el-form-item>
            <el-button type="primary" @click="saveConfig" :loading="saving">保存 / 开始</el-button>
            <el-button @click="stopNow" :disabled="!config.enabled && config.status !== 'running'">停止</el-button>
            <el-button @click="reload">重新加载</el-button>
          </el-form-item>
        </el-form>
      </template>
    </el-card>

    <!-- 3. 运行状态 -->
    <el-card shadow="never" v-if="selectedName" class="status-card">
      <template #header>
        <div class="card-header">
          <span>运行状态</span>
          <el-tag size="small" :type="statusTagType(live.status)">{{ statusLabel(live.status) }}</el-tag>
        </div>
      </template>
      <el-descriptions :column="2" border size="small">
        <el-descriptions-item label="已洗次数">{{ live.washCount }}</el-descriptions-item>
        <el-descriptions-item label="当前等级">{{ gradeLabel(live.lastGrade) }}</el-descriptions-item>
        <el-descriptions-item label="当前属性" :span="2">
          <template v-if="live.lastAttrs && live.lastAttrs.length">
            <el-tag v-for="(a,i) in live.lastAttrs" :key="i" size="small" effect="plain" style="margin-right:6px">
              {{ attrLabel(a) }}
            </el-tag>
          </template>
          <span v-else class="hint">—</span>
        </el-descriptions-item>
        <el-descriptions-item label="提示" :span="2" v-if="live.lastError">
          <span style="color: var(--el-color-danger)">{{ live.lastError }}</span>
        </el-descriptions-item>
      </el-descriptions>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useInstances } from '@/composables/useInstances'
import { ElMessage } from 'element-plus'

interface RequiredAttr { id: number; minValue: number; maxN?: number }
interface SpringAttrView { id: number; value: number; name?: string; percent?: boolean; composite?: boolean; n?: number; m?: number }
interface BagItem { bagId: number; slotIndex: number; itemId: number; count: number; name: string }
interface AttrTableEntry { id: number; name: string; percent: boolean; composite?: boolean }
interface ClockworkConfig {
  characterName: string
  enabled: boolean
  slotIndex: number
  itemName: string
  springType: number
  targetGrade: number
  requiredAttrs: RequiredAttr[]
  minMatchCount: number
  pollMs: number
  maxWashes: number
  washCount: number
  status: string
  lastGrade: number
  lastAttrs: SpringAttrView[]
  lastError: string
  lastWashAt: number
}

// 发条等级原始值: 0=未洗, 1=N, 2=G, 3=DG, 4=XG, 5=SG (实证 2026-06-14)
const GRADE_LABELS = ['—', 'N', 'G', 'DG', 'XG', 'SG']

// 发条系统硬限制 (来源 2usealol 发条系统说明):
//   发条最高等级: 0實習生=DG(3), 1高手=SG(5), 2武爾坎努斯=SG(5)。等级是棘轮,洗到不降。
const SPRING_MAX_GRADE = [3, 5, 5]
const SPRING_LABELS = ['實習生', '高手', '武爾坎努斯']
//   属性按等级解锁: 该属性要物品达到此等级才可能洗出。
//   N(1): 1攻击力 2魔法力 3命中 4回避 5防御 6必杀 7攻速 8移速 9HP值 10AP值 21减道具限制
//   G(2): +11HP% 12AP% 15-20每级属性 22经验值 23副本伤害
//   DG(3): +13增加伤害 14减少伤害
const ATTR_MIN_GRADE: Record<number, number> = {
  1: 1, 2: 1, 3: 1, 4: 1, 5: 1, 6: 1, 7: 1, 8: 1, 9: 1, 10: 1, 21: 1,
  11: 2, 12: 2, 15: 2, 16: 2, 17: 2, 18: 2, 19: 2, 20: 2, 22: 2, 23: 2,
  13: 3, 14: 3,
}
//   各属性在各等级 (index 0..4 = N/G/DG/XG/SG) 能洗出的最大数值;0=该等级未解锁。
//   超过此值(在该发条能到的最高等级下)就永远洗不出来。来源:发条属性一览的数值范围表。
const ATTR_VALUE_MAX: Record<number, number[]> = {
  1: [25, 45, 75, 135, 210], 2: [25, 45, 75, 135, 210], 5: [25, 45, 75, 135, 210],
  3: [5, 10, 15, 20, 25], 4: [5, 10, 15, 20, 25], 6: [5, 10, 15, 20, 25],
  7: [5, 10, 15, 20, 25], 8: [5, 10, 15, 20, 25],
  9: [50, 200, 500, 1000, 1500], 10: [50, 200, 500, 1000, 1500],
  11: [0, 1, 2, 3, 4], 12: [0, 1, 2, 3, 4], 13: [0, 0, 1, 3, 4], 14: [0, 0, 1, 3, 4],
  21: [3, 5, 10, 15, 20], 22: [0, 3, 5, 10, 15], 23: [0, 1, 1, 3, 4],
}
//   复合属性(15-20):每级点数 M 上限 + 最小等级间隔 N(越小越好)。index 0..4 = N..SG。
const COMP_M_MAX: Record<number, number[]> = {
  15: [0, 1, 1, 2, 2], 16: [0, 1, 1, 2, 2], 17: [0, 1, 1, 2, 2], 18: [0, 1, 1, 2, 2],
  19: [0, 1, 1, 1, 1], 20: [0, 1, 1, 1, 1],
}
const COMP_N_MIN: Record<number, number[]> = {
  15: [0, 6, 4, 4, 3], 16: [0, 6, 4, 4, 3], 17: [0, 6, 4, 4, 3], 18: [0, 6, 4, 4, 3],
  19: [0, 40, 30, 15, 13], 20: [0, 40, 30, 15, 13],
}

const { instances, selectedInstance } = useInstances()

const selectedName = ref('')
const config = ref<ClockworkConfig>(emptyConfig(''))
const saving = ref(false)
const bagItems = ref<BagItem[]>([])
const loadingBag = ref(false)
const attrTable = ref<AttrTableEntry[]>([])
const curState = ref<{ valid: boolean; grade: number; attrs: SpringAttrView[] } | null>(null)
const loadingState = ref(false)
const knownNames = ref<string[]>([])

// 实时运行状态(由轮询更新),与上面的编辑表单 config 分开 —— 轮询绝不碰 config,
// 否则会把用户刚拨开的「启用」开关又同步回 false。
interface LiveStatus {
  enabled: boolean
  washCount: number
  status: string
  lastGrade: number
  lastAttrs: SpringAttrView[]
  lastError: string
}
function emptyLive(): LiveStatus {
  return { enabled: false, washCount: 0, status: 'idle', lastGrade: 0, lastAttrs: [], lastError: '' }
}
const live = ref<LiveStatus>(emptyLive())
let statusTimer: ReturnType<typeof setInterval> | null = null

function emptyConfig(name: string): ClockworkConfig {
  return {
    characterName: name, enabled: false, slotIndex: 0, itemName: '',
    springType: 0, targetGrade: 0, requiredAttrs: [], minMatchCount: 0,
    pollMs: 0, maxWashes: 0, washCount: 0, status: 'idle',
    lastGrade: 0, lastAttrs: [], lastError: '', lastWashAt: 0,
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

const selectedPidValue = computed(() => {
  const inst = instances.value.find((i) => i.characterName === selectedName.value)
  return inst?.pid ?? null
})

function gradeLabel(g: number): string {
  return GRADE_LABELS[g] ?? `#${g}`
}

function attrLabel(a: SpringAttrView): string {
  if (!a || !a.id) return '空'
  const name = a.name || `属性#${a.id}`
  if (a.composite) return `${name} 每${a.n ?? 0}級+${a.m ?? 0}`
  return `${name} +${a.value}${a.percent ? '%' : ''}`
}

function isComposite(id: number): boolean {
  return attrTable.value.find((a) => a.id === id)?.composite === true
}

// 当前发条的最高可洗等级
const springMaxGrade = computed(() => SPRING_MAX_GRADE[config.value.springType] ?? 5)

// 目标等级下拉:只列到当前发条能洗出的最高等级
const gradeOptions = computed(() => {
  const opts = [{ value: 0, label: '不限等级' }]
  for (let g = 1; g <= springMaxGrade.value; g++) {
    opts.push({ value: g, label: `洗到 ≥ ${GRADE_LABELS[g]}` })
  }
  return opts
})

// 切发条种类时,目标等级若超过新发条上限就钳回去
function onSpringTypeChange() {
  if (config.value.targetGrade > springMaxGrade.value) {
    config.value.targetGrade = springMaxGrade.value
  }
}

// 在「目标等级」下取某数值表的上限。设了目标等级就按目标等级判(用户冲的就是这个等级,
// 比如 XG 的 减伤 上限是 3 而不是 SG 的 4);该等级该属性还没解锁(查到 0)才回退到发条最高
// 等级(等级是棘轮会一路冲到最高,所以未解锁属性的真实上限在最高等级)。
function ceilingAt(table: Record<number, number[]>, id: number): { val: number; grade: number } {
  const sm = springMaxGrade.value
  const tg = config.value.targetGrade
  const eff = tg > 0 ? Math.min(tg, sm) : sm
  const arr = table[id]
  if (!arr) return { val: 0, grade: eff }
  let g = eff
  let val = arr[g - 1] ?? 0
  if (val === 0) {
    g = sm
    val = arr[sm - 1] ?? 0
  }
  return { val, grade: g }
}

// 属性下拉标注最低出现等级 + 目标等级下能洗到的数值上限,如 "攻擊力 最高75 (XG)" / "減少傷害力 (需DG+) 最高3% (XG)"
function attrSelectLabel(a: AttrTableEntry): string {
  let label = a.name + (a.percent ? ' (%)' : '')
  const mg = ATTR_MIN_GRADE[a.id] ?? 1
  if (mg > 1) label += ` (需${GRADE_LABELS[mg]}+)`
  if (a.id in COMP_M_MAX) {
    const m = ceilingAt(COMP_M_MAX, a.id)
    const n = ceilingAt(COMP_N_MIN, a.id)
    if (m.val > 0) label += `  最高 每${n.val}级+${m.val} (${GRADE_LABELS[m.grade]})`
  } else if (a.id in ATTR_VALUE_MAX) {
    const v = ceilingAt(ATTR_VALUE_MAX, a.id)
    if (v.val > 0) label += `  最高 ${v.val}${a.percent ? '%' : ''} (${GRADE_LABELS[v.grade]})`
  }
  return label
}

// 不可行配置告警(发条上限 / 属性最低等级超过发条上限 / 数值超过目标等级上限)
const configWarnings = computed(() => {
  const w: string[] = []
  const max = springMaxGrade.value
  const sn = SPRING_LABELS[config.value.springType] ?? ''
  if (config.value.targetGrade > max) {
    w.push(`${sn}的發條最高只能洗到 ${GRADE_LABELS[max]},目标等级 ${GRADE_LABELS[config.value.targetGrade]} 永远洗不出来。`)
  }
  for (const r of config.value.requiredAttrs) {
    const nm = attrTable.value.find((a) => a.id === r.id)?.name ?? `#${r.id}`
    const mg = ATTR_MIN_GRADE[r.id]
    if (mg && mg > max) {
      w.push(`属性「${nm}」要 ${GRADE_LABELS[mg]}+ 才会出现,但${sn}最高 ${GRADE_LABELS[max]},洗不出来。`)
      continue
    }
    if (r.id in COMP_M_MAX) {
      const m = ceilingAt(COMP_M_MAX, r.id)
      if (m.val > 0 && r.minValue > m.val) {
        w.push(`「${nm}」每级点数最高 +${m.val}(${GRADE_LABELS[m.grade]}),要求 +${r.minValue} 洗不出来。`)
      }
      const n = ceilingAt(COMP_N_MIN, r.id)
      if (r.maxN && r.maxN > 0 && n.val > 0 && r.maxN < n.val) {
        w.push(`「${nm}」最小等级间隔为 每${n.val}级(${GRADE_LABELS[n.grade]}),要求 ≤每${r.maxN}级 洗不出来。`)
      }
    } else if (r.id in ATTR_VALUE_MAX) {
      const v = ceilingAt(ATTR_VALUE_MAX, r.id)
      if (v.val > 0 && r.minValue > v.val) {
        w.push(`「${nm}」数值最高 ${v.val}(${GRADE_LABELS[v.grade]}),要求 ≥${r.minValue} 洗不出来。`)
      }
    }
  }
  return w
})

function statusLabel(s: string): string {
  switch (s) {
    case 'running': return '运行中'
    case 'done': return '已达成,已停止'
    case 'no-spring': return '发条用完,已停止'
    case 'reached-max': return '到达次数上限,已停止'
    case 'error': return '出错,已停止'
    default: return '空闲'
  }
}
function statusTagType(s: string): 'success' | 'info' | 'warning' | 'danger' {
  if (s === 'running') return 'warning'
  if (s === 'done') return 'success'
  if (s === 'error') return 'danger'
  return 'info'
}

function addAttr() {
  config.value.requiredAttrs.push({ id: attrTable.value[0]?.id ?? 0, minValue: 1, maxN: 0 })
}

async function command(pid: number, action: string, args: any): Promise<any> {
  const res = await fetch(`/api/command/${pid}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action, args }),
  })
  const r = await res.json()
  if (!r.ok) throw new Error(r.detail || r.error || '命令失败')
  return r.detail
}

async function refreshBag() {
  const pid = selectedPidValue.value
  if (!pid) return
  loadingBag.value = true
  try {
    const detail = await command(pid, 'getBagItems', {})
    bagItems.value = JSON.parse(detail || '[]') as BagItem[]
  } catch (e: any) {
    ElMessage.error(`读背包失败: ${e.message}`)
  } finally {
    loadingBag.value = false
  }
}

async function loadAttrTable() {
  const pid = selectedPidValue.value
  if (!pid) return
  try {
    const detail = await command(pid, 'getSpringAttrTable', {})
    attrTable.value = JSON.parse(detail || '[]') as AttrTableEntry[]
  } catch { /* silent */ }
}

async function refreshState() {
  const pid = selectedPidValue.value
  if (!pid) return
  loadingState.value = true
  try {
    const detail = await command(pid, 'queryClockwork', { slotIndex: config.value.slotIndex })
    curState.value = JSON.parse(detail || '{}')
  } catch (e: any) {
    ElMessage.error(`读发条失败: ${e.message}`)
  } finally {
    loadingState.value = false
  }
}

function onPickEquip() {
  const it = bagItems.value.find((b) => b.slotIndex === config.value.slotIndex)
  if (it) config.value.itemName = it.name || `#${it.itemId}`
  refreshState()
}

async function loadConfigList() {
  try {
    const list = (await fetch('/api/clockwork-washer/configs').then((r) => r.json())) as ClockworkConfig[]
    knownNames.value = Array.isArray(list) ? list.map((c) => c.characterName) : []
  } catch { /* silent */ }
}

async function reload() {
  if (!selectedName.value) return
  try {
    const res = await fetch(`/api/clockwork-washer/configs/${encodeURIComponent(selectedName.value)}`)
    if (res.status === 404) {
      config.value = emptyConfig(selectedName.value)
      return
    }
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const cfg = (await res.json()) as ClockworkConfig
    config.value = { ...emptyConfig(selectedName.value), ...cfg, requiredAttrs: cfg.requiredAttrs ?? [], lastAttrs: cfg.lastAttrs ?? [] }
  } catch (e: any) {
    ElMessage.error(`加载失败: ${e.message}`)
  }
}

async function saveConfig() {
  if (!selectedName.value) return
  saving.value = true
  try {
    const res = await fetch(`/api/clockwork-washer/configs/${encodeURIComponent(selectedName.value)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const updated = (await res.json()) as ClockworkConfig
    config.value = { ...emptyConfig(selectedName.value), ...updated, requiredAttrs: updated.requiredAttrs ?? [], lastAttrs: updated.lastAttrs ?? [] }
    live.value = {
      enabled: updated.enabled, washCount: updated.washCount, status: updated.status,
      lastGrade: updated.lastGrade, lastAttrs: updated.lastAttrs ?? [], lastError: updated.lastError,
    }
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

// 拉一次运行状态到 live。只读不写表单。任务停了(未启用且非运行中)就关掉轮询,
// 避免空闲时一直发请求。返回当前是否仍在跑。
async function refreshStatus(): Promise<boolean> {
  if (!selectedName.value) return false
  try {
    const res = await fetch(`/api/clockwork-washer/configs/${encodeURIComponent(selectedName.value)}`)
    if (res.status === 404) { live.value = emptyLive(); stopStatusPoll(); return false }
    if (!res.ok) return false
    const cfg = (await res.json()) as ClockworkConfig
    live.value = {
      enabled: cfg.enabled,
      washCount: cfg.washCount,
      status: cfg.status,
      lastGrade: cfg.lastGrade,
      lastAttrs: cfg.lastAttrs ?? [],
      lastError: cfg.lastError,
    }
    const active = cfg.enabled || cfg.status === 'running'
    if (!active) stopStatusPoll()
    return active
  } catch {
    return false
  }
}
function startStatusPoll() {
  stopStatusPoll()
  statusTimer = setInterval(refreshStatus, 1500)
}
function stopStatusPoll() {
  if (statusTimer !== null) { clearInterval(statusTimer); statusTimer = null }
}

watch(selectedName, async () => {
  curState.value = null
  bagItems.value = []
  attrTable.value = []
  live.value = emptyLive()
  stopStatusPoll()
  await reload()
  const active = await refreshStatus()
  if (active) startStatusPoll()
  if (selectedPidValue.value) {
    await refreshBag()
    await loadAttrTable()
    await refreshState()
  }
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
.clockwork-view {
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
.row {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
}
.cur-state {
  margin-top: 10px;
}
.rule-actions {
  margin-top: 8px;
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

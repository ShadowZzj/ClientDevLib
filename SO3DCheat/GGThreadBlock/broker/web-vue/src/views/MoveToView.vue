<template>
  <div class="moveto-view">
    <h3>寻路移动</h3>

    <p class="hint">
      调用引擎 CLocalUser::SetAfterAction (0x7539E0) —— 跟手动点地走是同一条路径。
      坐标是<strong>世界坐标</strong>，跟 CUser+0x3C/+0x40 同坐标系。
      战斗或施法中可能被拒；如果走不动请打开「攻击/技能时移动」开关。
    </p>

    <el-form label-width="100px" style="max-width: 520px">
      <el-form-item label="当前位置">
        <el-input :model-value="currentPosText" readonly placeholder="点 [读取当前]" style="width: 280px" />
        <el-button :loading="reading" @click="doReadPosition" style="margin-left: 8px">
          读取当前
        </el-button>
        <el-button :disabled="!lastPos" @click="copyCurrentToTarget" style="margin-left: 4px">
          复制到目标
        </el-button>
      </el-form-item>

      <el-form-item label="目标 X">
        <el-input-number v-model="targetX" :step="50" :precision="2" :controls-position="'right'" style="width: 200px" />
      </el-form-item>
      <el-form-item label="目标 Y">
        <el-input-number v-model="targetY" :step="50" :precision="2" :controls-position="'right'" style="width: 200px" />
      </el-form-item>

      <el-form-item label="动作">
        <el-radio-group v-model="action">
          <el-radio :label="1">纯走路</el-radio>
          <el-radio :label="3">走过去 + 攻击</el-radio>
        </el-radio-group>
      </el-form-item>
      <el-form-item v-if="action === 3" label="目标 ID">
        <el-input-number v-model="targetId" :min="0" :step="1" style="width: 200px" />
        <span class="sub-hint">creature/user id，0 表示自动选最近</span>
      </el-form-item>

      <el-form-item>
        <el-button type="primary" :loading="moving" @click="doMove">执行</el-button>
        <el-button :disabled="!lastPos" @click="quickStep('north')">↑ 北 100</el-button>
        <el-button :disabled="!lastPos" @click="quickStep('south')">↓ 南 100</el-button>
        <el-button :disabled="!lastPos" @click="quickStep('east')">→ 东 100</el-button>
        <el-button :disabled="!lastPos" @click="quickStep('west')">← 西 100</el-button>
      </el-form-item>
    </el-form>

    <el-divider />

    <!-- 城市传送（复刻聊天框 "/城市名"） -->
    <h4>城市传送</h4>
    <p class="hint">
      复刻聊天框输入「/狮子城」的城市传送 —— 客户端本地用 UIManager 传送表把城名
      解析成目的地 id，再发 411076。城名按游戏菜单里的写法填（此 build 为繁体）。
      等级 / 金钱不够的城会解析失败；死亡 / 地图限制由服务端校验。
    </p>
    <el-form label-width="100px" style="max-width: 520px">
      <el-form-item label="城市名">
        <el-input
          v-model="teleportCity"
          placeholder="如 狮子城"
          style="width: 220px"
          clearable
          @keyup.enter="doTeleport"
        />
        <el-button type="primary" :loading="teleporting" @click="doTeleport" style="margin-left: 8px">
          传送
        </el-button>
      </el-form-item>
      <el-form-item v-if="teleportHistory.length" label="最近">
        <el-tag
          v-for="c in teleportHistory"
          :key="c"
          class="tp-tag"
          @click="quickTeleport(c)"
        >{{ c }}</el-tag>
      </el-form-item>
    </el-form>

    <el-divider />

    <h4>历史目标</h4>
    <el-table :data="historyRows" size="small" style="max-width: 720px">
      <el-table-column prop="x" label="X" width="120" />
      <el-table-column prop="y" label="Y" width="120" />
      <el-table-column prop="actionText" label="动作" width="140" />
      <el-table-column prop="when" label="时间" />
      <el-table-column label="操作" width="120">
        <template #default="{ row }">
          <el-button size="small" @click="applyHistory(row)">填回</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-divider />

    <!-- 走到 NPC / 怪物（统一按钮：跟手动点一样，引擎自己判断对话/开打） -->
    <h4>附近 NPC / 怪物</h4>
    <p class="hint">
      跟手动鼠标点 creature 一样的效果 —— DLL 内部用引擎自己的 Npc__LoadDialogScript
      判断:有对话脚本就走过去开对话,没有就走过去开打。
    </p>
    <div class="npc-toolbar">
      <el-button @click="refreshNpcs" :loading="loadingNpcs">刷新</el-button>
      <span class="bar-label">距离上限</span>
      <el-input-number v-model="npcMaxDistance" :min="0" :max="2000" :step="50" style="width: 130px" />
      <el-checkbox v-model="onlyRealNpcs">只看 NPC</el-checkbox>
      <el-input v-model="nameFilter" placeholder="按名字过滤" clearable size="small" style="width: 200px" />
      <span class="npc-count">{{ filteredNpcs.length }} 条</span>
    </div>
    <el-table :data="filteredNpcs" size="small" style="max-width: 1100px" max-height="360">
      <el-table-column prop="id" label="ID" width="70" />
      <el-table-column label="名字" min-width="180">
        <template #default="{ row }">
          <el-tag v-if="row.hasDialog" size="small" type="success" style="margin-right: 6px">NPC</el-tag>
          <el-tag v-else size="small" type="warning" style="margin-right: 6px">怪</el-tag>
          <span v-if="row.name">{{ row.name }}</span>
          <span v-else class="muted">(无名)</span>
        </template>
      </el-table-column>
      <el-table-column prop="monsterTblId" label="模板ID" width="80" />
      <el-table-column label="距离" width="70">
        <template #default="{ row }">
          {{ row.distance.toFixed(1) }}
        </template>
      </el-table-column>
      <el-table-column label="坐标" width="140">
        <template #default="{ row }">
          ({{ row.x.toFixed(0) }}, {{ row.y.toFixed(0) }})
        </template>
      </el-table-column>
      <el-table-column label="操作" width="160">
        <template #default="{ row }">
          <el-button size="small" type="primary" @click="doTalkOrAttack(row)">
            {{ row.hasDialog ? '走过去 + 对话' : '走过去 + 攻击' }}
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-divider />

    <!-- 当前 NPC 对话框 -->
    <h4>当前 NPC 对话</h4>
    <p class="hint">
      跟 NPC 说话后,游戏里的对话框选项会同步显示到这里。每点一个选项就发一次
      411026 给服务器,服务器再推下层菜单回来(自动刷新)。
      <strong>多级菜单</strong>是引擎自己的「点 1 → server 推下一屏 → 点 2 → server 再推」
      —— 客户端只是发选项编号 + 显示 server 推的菜单。
    </p>
    <div class="dialog-toolbar">
      <el-button @click="refreshDialog" :loading="loadingDialog">刷新</el-button>
      <el-checkbox v-model="autoRefreshDialog">每秒自动刷新</el-checkbox>
      <span v-if="dialog?.open" class="dialog-meta">
        NPC interactId={{ dialog.npcInteractId }} · monsterTblId={{ dialog.monsterTblId }} · mode={{ dialog.mode }}
      </span>
      <span v-else class="muted dialog-meta">无对话</span>
    </div>
    <div v-if="dialog?.open" class="dialog-box">
      <div v-if="dialog.body" class="dialog-body">{{ dialog.body }}</div>
      <div v-if="dialog.options?.length" class="dialog-options">
        <el-button
          v-for="opt in dialog.options"
          :key="opt.index"
          :loading="sendingOption === opt.index"
          @click="doSelectDialogOption(opt.index)"
          class="dialog-option-btn"
        >
          <span class="opt-num">{{ opt.index + 1 }}.</span>
          <span class="opt-text">{{ opt.text || '(空选项)' }}</span>
          <span class="opt-code" v-if="opt.opt">[opt {{ opt.opt }}]</span>
          <span class="opt-tag" v-if="opt.tag">[tag {{ opt.tag }}]</span>
        </el-button>
      </div>
      <div v-else class="muted">(没有可点选项)</div>
    </div>
    <div v-else class="muted dialog-empty">
      暂无对话 —— 先点附近 NPC 的「走过去 + 对话」,等 dialog 框出现后再回来看这里。
    </div>
  </div>
</template>

<script setup lang="ts">
// 寻路目标输入 + 调 SetAfterAction 的小工具。坐标系跟 CUser+0x3C/+0x44 同 —
// 用「读取当前」按钮取个起点，UI 上以 ±N 步进调或者直接键入。历史用 localStorage
// 留 20 条，方便反复走同一个 NPC / 刷怪点。

import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { useInstances } from '@/composables/useInstances'
import { useLocalHistory } from '@/composables/useLocalHistory'

const { selectedPid } = useInstances()

const targetX = ref(0)
const targetY = ref(0)
const action = ref<1 | 3>(1)
const targetId = ref(0)

const lastPos = ref<{ x: number; y: number; z: number } | null>(null)
const reading = ref(false)
const moving = ref(false)

interface MoveHistoryEntry {
  x: number
  y: number
  action: number
  targetId: number
  when: string
}
// useLocalHistory 是 string-based；这里用 JSON.stringify 走它的存储，自己再解一层。
const historyStore = useLocalHistory('ggtb.moveto')
const historyRows = computed(() =>
  historyStore.getAll().map((s) => {
    try {
      const obj = JSON.parse(s) as MoveHistoryEntry
      return { ...obj, actionText: obj.action === 3 ? `走+攻击 #${obj.targetId}` : '纯走路' }
    } catch {
      return null
    }
  }).filter(Boolean) as (MoveHistoryEntry & { actionText: string })[]
)

const currentPosText = computed(() =>
  lastPos.value
    ? `${lastPos.value.x.toFixed(2)}, ${lastPos.value.y.toFixed(2)} (z=${lastPos.value.z.toFixed(1)})`
    : ''
)

async function postCommand(actionName: string, args: Record<string, any>) {
  if (!selectedPid.value) {
    ElMessage.warning('未选择实例')
    return null
  }
  const res = await fetch(`/api/command/${selectedPid.value}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ action: actionName, args }),
  })
  return res.json()
}

async function doReadPosition() {
  reading.value = true
  try {
    const r = await postCommand('getLocalPosition', {})
    if (!r) return
    if (!r.ok) {
      ElMessage.error(`读取失败: ${r.detail || r.error || 'unknown'}`)
      return
    }
    // detail 是 JSON string（broker handler 拼好的）
    const obj = typeof r.detail === 'string' ? JSON.parse(r.detail) : r.detail
    lastPos.value = obj
    ElMessage.success(`当前位置: ${currentPosText.value}`)
  } catch (e: any) {
    ElMessage.error(`读取失败: ${e.message}`)
  } finally {
    reading.value = false
  }
}

function copyCurrentToTarget() {
  if (!lastPos.value) return
  targetX.value = Number(lastPos.value.x.toFixed(2))
  targetY.value = Number(lastPos.value.y.toFixed(2))
}

async function doMove() {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  moving.value = true
  try {
    const args: Record<string, any> = {
      x: Number(targetX.value),
      y: Number(targetY.value),
      action: action.value,
    }
    if (action.value === 3) args.targetId = Number(targetId.value) || 0
    const r = await postCommand('moveTo', args)
    if (!r) return
    if (r.ok) {
      ElMessage.success(`已下发: (${targetX.value}, ${targetY.value})`)
      historyStore.push(JSON.stringify({
        x: Number(targetX.value),
        y: Number(targetY.value),
        action: action.value,
        targetId: action.value === 3 ? Number(targetId.value) || 0 : 0,
        when: new Date().toLocaleTimeString(),
      } satisfies MoveHistoryEntry))
    } else {
      ElMessage.error(`失败: ${r.detail || r.error || 'unknown'}`)
    }
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    moving.value = false
  }
}

function quickStep(dir: 'north' | 'south' | 'east' | 'west') {
  if (!lastPos.value) return
  const step = 100
  let { x, y } = lastPos.value
  if (dir === 'north') y += step
  else if (dir === 'south') y -= step
  else if (dir === 'east') x += step
  else if (dir === 'west') x -= step
  targetX.value = Number(x.toFixed(2))
  targetY.value = Number(y.toFixed(2))
  doMove()
}

function applyHistory(row: MoveHistoryEntry & { actionText: string }) {
  targetX.value = row.x
  targetY.value = row.y
  action.value = (row.action === 3 ? 3 : 1) as 1 | 3
  targetId.value = row.targetId
}

// ---------- 城市传送 ----------
// 复刻聊天框 "/城市名"：broker teleport handler 先用传送表把城名解析成 destId
// 再发 411076。城名是 UTF-8，DLL 内部转 Big5（此 build 为 TW 包）。最近城市存
// localStorage，点 tag 直接重发。
const teleportCity = ref('')
const teleporting = ref(false)
const teleportStore = useLocalHistory('ggtb.teleport')
const teleportHistory = ref<string[]>(teleportStore.getAll())

async function doTeleport() {
  const city = teleportCity.value.trim()
  if (!city) { ElMessage.warning('请输入城市名'); return }
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  teleporting.value = true
  try {
    const r = await postCommand('teleport', { cityName: city })
    if (r?.ok) {
      ElMessage.success(`已下发传送: ${city}`)
      teleportStore.push(city)
      teleportHistory.value = teleportStore.getAll()
    } else {
      ElMessage.error(`失败: ${r?.detail || r?.error || 'unknown'}`)
    }
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    teleporting.value = false
  }
}

function quickTeleport(city: string) {
  teleportCity.value = city
  doTeleport()
}

// ---------- NPC / 怪物 ----------
interface NpcRow {
  id: number
  kind: number
  monsterTblId: number
  hasDialog: boolean
  level: number
  distance: number
  x: number
  y: number
  z: number
  hp: number
  name: string
  isNpc: boolean
}

const npcList = ref<NpcRow[]>([])
const npcMaxDistance = ref(200)
const onlyRealNpcs = ref(false)
const nameFilter = ref('')
const loadingNpcs = ref(false)

const filteredNpcs = computed(() => {
  let xs = npcList.value
  if (onlyRealNpcs.value) xs = xs.filter((n) => n.hasDialog)
  const q = nameFilter.value.trim()
  if (q) xs = xs.filter((n) => n.name.includes(q) || String(n.monsterTblId).includes(q))
  return xs
})

async function refreshNpcs() {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  loadingNpcs.value = true
  try {
    const r = await postCommand('getNearbyNpcs', { maxDistance: Number(npcMaxDistance.value) })
    if (!r || !r.ok) {
      ElMessage.error(`失败: ${r?.detail || r?.error || 'unknown'}`)
      return
    }
    const arr = typeof r.detail === 'string' ? JSON.parse(r.detail) : r.detail
    npcList.value = Array.isArray(arr) ? arr : []
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    loadingNpcs.value = false
  }
}

async function doTalkOrAttack(row: NpcRow) {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  try {
    const r = await postCommand('talkOrAttack', { creatureId: row.id })
    const label = row.name || `#${row.id}`
    if (r?.ok) {
      ElMessage.success(`已下发: ${row.hasDialog ? '对话' : '攻击'} ${label}`)
      // 对 NPC 点完之后等一小会儿(server 还要推菜单),自动刷新一次对话状态。
      if (row.hasDialog) {
        setTimeout(() => { refreshDialog() }, 800)
      }
    } else {
      ElMessage.error(`失败: ${r?.detail || r?.error || 'unknown'}`)
    }
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  }
}

// ---------- NPC dialog (多级菜单) ----------
//
// 引擎本身的多级对话:每点一个选项就发 411026 (CG_NPC_DIALOG_SELECT)。server
// 推回 521603 (OnNpcDialogConfirm),把新选项码写进 g_NpcDialogState 的选项链表
// (state+1052 / state+1056),然后客户端在本地 rebuild 菜单。我们在 DLL 那边
// 把这个 state 链表读出来当 JSON 返回 —— 前端拿到选项就显示按钮,点哪个就
// selectDialogOption(index) 把 index 发回服务器。整个流程是 server-driven。
interface DialogOption {
  index: number
  text: string
  tag: number
  opt?: number    // *(*(option+396)+12)+332 — 真正发出去的 opt 数值。
                  // 0 表示这个选项没 quest tag(纯本地子菜单),点了不会发包。
}
interface DialogState {
  open: boolean
  mode?: number       // 0=closed, 1=confirm, 2=choice
  npcInteractId?: number
  monsterTblId?: number
  body?: string
  options?: DialogOption[]
}

const dialog = ref<DialogState | null>(null)
const loadingDialog = ref(false)
const sendingOption = ref<number | null>(null)
const autoRefreshDialog = ref(true)
let dialogTimer: number | null = null

async function refreshDialog() {
  if (!selectedPid.value) return
  loadingDialog.value = true
  try {
    const r = await postCommand('getDialog', {})
    if (!r || !r.ok) {
      // 没拿到 — 多半是 broker 还没连上,别弹错,留空。
      return
    }
    const obj = typeof r.detail === 'string' ? JSON.parse(r.detail) : r.detail
    dialog.value = obj as DialogState
  } catch {
    // 静默 — 自动刷新时不想刷屏
  } finally {
    loadingDialog.value = false
  }
}

async function doSelectDialogOption(index: number) {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  sendingOption.value = index
  try {
    const r = await postCommand('selectDialogOption', { option: index })
    if (r?.ok) {
      ElMessage.success(`已选: ${dialog.value?.options?.[index]?.text || `选项 ${index + 1}`}`)
      // server 推下一菜单大概 200–600ms,稍微等一下再刷新。
      setTimeout(() => { refreshDialog() }, 500)
    } else {
      ElMessage.error(`失败: ${r?.detail || r?.error || 'unknown'}`)
    }
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    sendingOption.value = null
  }
}

function startDialogPoll() {
  stopDialogPoll()
  if (!autoRefreshDialog.value) return
  // 1Hz 已经够 — 选项变更最早也是 server 推下一菜单后,人手点也至少几百 ms。
  dialogTimer = window.setInterval(() => { refreshDialog() }, 1000)
}
function stopDialogPoll() {
  if (dialogTimer !== null) {
    window.clearInterval(dialogTimer)
    dialogTimer = null
  }
}
onMounted(() => { startDialogPoll() })
onUnmounted(() => { stopDialogPoll() })
watch(autoRefreshDialog, () => { startDialogPoll() })
</script>

<style scoped>
.moveto-view { padding: 8px; }
.hint {
  color: var(--el-text-color-secondary);
  font-size: 13px;
  margin: 4px 0 16px;
  line-height: 1.6;
}
.sub-hint { color: var(--el-text-color-secondary); margin-left: 8px; font-size: 12px; }
h4 { margin: 12px 0 8px; }

.tp-tag { margin: 0 6px 6px 0; cursor: pointer; }

.npc-toolbar {
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 8px 0;
}
.bar-label { color: var(--el-text-color-secondary); font-size: 12px; }
.npc-count { color: var(--el-text-color-secondary); font-size: 12px; margin-left: auto; }
.muted { color: var(--el-text-color-secondary); font-style: italic; }

.dialog-toolbar {
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 8px 0;
}
.dialog-meta {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-left: auto;
}
.dialog-box {
  border: 1px solid var(--el-border-color);
  border-radius: 6px;
  padding: 12px 14px;
  background: var(--el-fill-color-light);
  max-width: 720px;
}
.dialog-body {
  white-space: pre-wrap;
  font-size: 14px;
  line-height: 1.7;
  margin-bottom: 10px;
  color: var(--el-text-color-primary);
}
.dialog-options {
  display: flex;
  flex-direction: column;
  gap: 6px;
  align-items: stretch;
}
.dialog-option-btn {
  justify-content: flex-start;
  text-align: left;
  white-space: normal;
  height: auto;
  padding: 8px 12px;
}
.dialog-option-btn .opt-num {
  font-weight: 600;
  margin-right: 6px;
  color: var(--el-color-primary);
}
.dialog-option-btn .opt-text { flex: 1; }
.dialog-option-btn .opt-code {
  font-size: 11px;
  color: var(--el-color-success);
  margin-left: 8px;
  font-family: ui-monospace, 'SF Mono', Menlo, monospace;
}
.dialog-option-btn .opt-tag {
  font-size: 11px;
  color: var(--el-text-color-secondary);
  margin-left: 8px;
}
.dialog-empty {
  padding: 10px 0;
  font-size: 13px;
}
</style>

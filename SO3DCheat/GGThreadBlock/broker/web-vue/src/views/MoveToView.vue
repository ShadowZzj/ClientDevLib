<template>
  <div class="moveto-view">
    <h3>寻路移动</h3>

    <!-- 账号清单(与「复活传送」共用)。批量自动寻路 / 城市传送下发到这里勾选的全部账号。 -->
    <el-card shadow="never" class="acct-card">
      <template #header>
        <span>下发账号</span>
      </template>
      <AccountMultiSelect />
    </el-card>

    <el-divider />

    <!-- 城市传送(复刻聊天框 "/城市名",批量下发) -->
    <h4>城市传送(批量)</h4>
    <p class="hint">
      复刻聊天框输入「/狮子城」的城市传送 —— 客户端本地用 UIManager 传送表把城名
      解析成目的地 id,再发 411076。城名按游戏菜单里的写法填(此 build 为繁体)。
      点「传送」会下发给上方勾选的全部在线账号。等级 / 金钱不够的城会解析失败。
    </p>
    <el-form label-width="100px" style="max-width: 560px">
      <el-form-item label="城市名">
        <el-input
          v-model="teleportCity"
          placeholder="如 狮子城"
          style="width: 220px"
          clearable
          @keyup.enter="doTeleport"
        />
        <el-button type="primary" :loading="teleporting" @click="doTeleport" style="margin-left: 8px">
          传送 ({{ selectedTargets.length }})
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
    <BatchResultTable v-if="teleportResults.length" :rows="teleportResults" />

    <el-divider />

    <!-- 自动寻路(网格 A* 绕障,批量下发) -->
    <h4>自动寻路(自动绕障,批量)</h4>
    <p class="hint">
      远距离目标用这个 —— 在 DLL 里起 worker 线程跑网格 A* 算绕障路径并平滑后逐拐点走,
      凸障碍、U 型凹墙都能绕。点「走过去」会把同一个目标坐标下发给上方勾选的全部在线账号
      (各自独立寻路);「停止」对全部账号下发取消。
    </p>
    <el-form label-width="100px" style="max-width: 560px">
      <el-form-item label="目标 X">
        <el-input-number v-model="pathX" :step="50" :precision="2" :controls-position="'right'" style="width: 200px" />
        <el-button :disabled="!lastPos" @click="pathUseCurrent" style="margin-left: 8px">填当前</el-button>
      </el-form-item>
      <el-form-item label="目标 Y">
        <el-input-number v-model="pathY" :step="50" :precision="2" :controls-position="'right'" style="width: 200px" />
      </el-form-item>
      <el-form-item>
        <el-button type="primary" :loading="pathStarting" @click="doPathTo">走过去 ({{ selectedTargets.length }})</el-button>
        <el-button type="danger" :loading="pathStopping" @click="doStopPath">停止全部</el-button>
      </el-form-item>
    </el-form>
    <BatchResultTable v-if="pathResults.length" :rows="pathResults" />

    <el-divider />

    <!-- 传送移动(瞬移,批量下发):发 CG 411597 直接把角色挪到目标格,不走路 -->
    <h4>传送移动(瞬移,批量)</h4>
    <p class="hint">
      和上面「自动寻路」走路不同,这个直接发坐标传送包(CG 411597)把角色<strong>瞬移</strong>到
      目标地图格,无视障碍、不走路。坐标是整数地图格(非世界浮点);「填当前」读首个选中账号
      的当前位置并取整。点「传送」把同一目标格下发给上方勾选的全部在线账号。
    </p>
    <el-form label-width="100px" style="max-width: 560px">
      <el-form-item label="目标 X">
        <el-input-number v-model="warpX" :step="1" :precision="0" :controls-position="'right'" style="width: 200px" />
        <el-button :disabled="!lastPos" @click="warpUseCurrent" style="margin-left: 8px">填当前</el-button>
      </el-form-item>
      <el-form-item label="目标 Y">
        <el-input-number v-model="warpY" :step="1" :precision="0" :controls-position="'right'" style="width: 200px" />
      </el-form-item>
      <el-form-item>
        <el-button type="primary" :loading="warping" @click="doWarpTo">传送 ({{ selectedTargets.length }})</el-button>
      </el-form-item>
    </el-form>
    <BatchResultTable v-if="warpResults.length" :rows="warpResults" />

    <el-divider />

    <p class="single-note">
      以下为<strong>单账号工具</strong>(读取位置 / 单次移动 / NPC 对话),作用于
      <el-tag v-if="primaryName" size="small" type="success">{{ primaryName }}</el-tag>
      <el-tag v-else size="small" type="info">未选账号</el-tag>
      ,即勾选列表里的第一个在线账号。
    </p>

    <!-- 单账号:读取位置 + 单次 SetAfterAction -->
    <h4>单次移动 (SetAfterAction)</h4>
    <p class="hint">
      调用引擎 CLocalUser::SetAfterAction —— 跟手动点地走是同一条路径,只 call 一次,
      引擎朝目标直线推进,撞到第一个阻挡 tile 就停。远目标请用上面的「自动寻路」。
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
        <span class="sub-hint">creature/user id,0 表示自动选最近</span>
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

    <!-- 走到 NPC / 怪物(单账号:跟手动点一样,引擎自己判断对话/开打) -->
    <h4>附近 NPC / 怪物</h4>
    <p class="hint">
      跟手动鼠标点 creature 一样的效果 —— DLL 内部用引擎自己的 Npc__LoadDialogScript
      判断:有对话脚本就走过去开对话,没有就走过去开打。作用于上方首个选中账号。
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
          <el-tag v-if="row.isNpc" size="small" type="success" style="margin-right: 6px">NPC</el-tag>
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
            {{ row.isNpc ? '走过去 + 对话' : '走过去 + 攻击' }}
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-divider />

    <!-- 当前 NPC 对话框(单账号) -->
    <h4>当前 NPC 对话</h4>
    <p class="hint">
      跟 NPC 说话后,游戏里的对话框选项会同步显示到这里。每点一个选项就发一次
      411026 给服务器,服务器再推下层菜单回来(自动刷新)。作用于上方首个选中账号。
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
// 寻路移动:批量(自动寻路 / 城市传送,下发到上方勾选的全部账号)+ 单账号工具
// (读取位置 / 单次 SetAfterAction / NPC 对话,作用于首个选中账号)。账号名单与
// 「复活传送」共用,见 useSelectedAccounts。

import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { useSelectedAccounts } from '@/composables/useSelectedAccounts'
import { useLocalHistory } from '@/composables/useLocalHistory'
import AccountMultiSelect from '@/components/AccountMultiSelect.vue'
import BatchResultTable, { type BatchRow } from '@/components/BatchResultTable.vue'

const { selectedTargets, primaryPid } = useSelectedAccounts()
const primaryName = computed(() => selectedTargets.value[0]?.characterName ?? '')

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
// useLocalHistory 是 string-based;这里用 JSON.stringify 走它的存储,自己再解一层。
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

// 单账号命令:打首个选中账号(primaryPid)。
async function postCommand(actionName: string, args: Record<string, any>) {
  if (!primaryPid.value) {
    ElMessage.warning('未选择账号(请在上方勾选)')
    return null
  }
  const res = await fetch(`/api/command/${primaryPid.value}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ action: actionName, args }),
  })
  return res.json()
}

// 批量命令:对全部选中在线账号并行下发同一条命令,汇总每账号结果。
async function dispatchBatch(actionName: string, args: Record<string, any>): Promise<BatchRow[]> {
  const targets = selectedTargets.value
  const settled = await Promise.all(
    targets.map(async (t): Promise<BatchRow> => {
      try {
        const res = await fetch(`/api/command/${t.pid}`, {
          method: 'POST',
          headers: { 'content-type': 'application/json' },
          body: JSON.stringify({ action: actionName, args }),
        })
        const r = await res.json()
        return {
          pid: t.pid,
          characterName: t.characterName,
          ok: !!r?.ok,
          error: r?.ok ? '' : (r?.detail || r?.error || `HTTP ${res.status}`),
        }
      } catch (e: any) {
        return { pid: t.pid, characterName: t.characterName, ok: false, error: e?.message || String(e) }
      }
    })
  )
  return settled
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
  if (!primaryPid.value) { ElMessage.warning('未选择账号(请在上方勾选)'); return }
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

// ---------- 自动寻路(批量)----------
// pathTo 在每个账号的 DLL 里起 worker 线程跑网格 A*,立即返回。批量下发只汇总「是否
// 接受并开始走」,各账号的实时进度不在这里逐一轮询(N 账号轮询不现实)。
const pathX = ref(0)
const pathY = ref(0)
const pathStarting = ref(false)
const pathStopping = ref(false)
const pathResults = ref<BatchRow[]>([])

function pathUseCurrent() {
  if (!lastPos.value) return
  pathX.value = Number(lastPos.value.x.toFixed(2))
  pathY.value = Number(lastPos.value.y.toFixed(2))
}

async function doPathTo() {
  if (selectedTargets.value.length === 0) { ElMessage.warning('未选择账号(请在上方勾选)'); return }
  pathStarting.value = true
  try {
    pathResults.value = await dispatchBatch('pathTo', { x: Number(pathX.value), y: Number(pathY.value), action: 1 })
    const ok = pathResults.value.filter((r) => r.ok).length
    if (ok === pathResults.value.length) ElMessage.success(`已下发寻路: ${ok} 个账号`)
    else ElMessage.warning(`寻路下发 ${ok}/${pathResults.value.length},详见结果`)
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    pathStarting.value = false
  }
}

async function doStopPath() {
  if (selectedTargets.value.length === 0) { ElMessage.warning('未选择账号(请在上方勾选)'); return }
  pathStopping.value = true
  try {
    pathResults.value = await dispatchBatch('stopPath', {})
    ElMessage.info('已对全部选中账号下发停止')
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    pathStopping.value = false
  }
}

// ---------- 传送移动(瞬移,批量)----------
// warpTo 直接发 CG 411597 坐标传送包,把角色瞬移到目标地图格(整数),不走路。批量
// 下发同一目标格给全部选中账号。warpTo 的 x/y 是地图格整数,「填当前」从 lastPos(浮点
// 世界坐标,与格坐标同刻度)取整。
const warpX = ref(0)
const warpY = ref(0)
const warping = ref(false)
const warpResults = ref<BatchRow[]>([])

function warpUseCurrent() {
  if (!lastPos.value) return
  warpX.value = Math.round(lastPos.value.x)
  warpY.value = Math.round(lastPos.value.y)
}

async function doWarpTo() {
  if (selectedTargets.value.length === 0) { ElMessage.warning('未选择账号(请在上方勾选)'); return }
  warping.value = true
  try {
    warpResults.value = await dispatchBatch('warpTo', {
      x: Math.round(Number(warpX.value)),
      y: Math.round(Number(warpY.value)),
    })
    const ok = warpResults.value.filter((r) => r.ok).length
    if (ok === warpResults.value.length) ElMessage.success(`已下发传送: ${ok} 个账号`)
    else ElMessage.warning(`传送下发 ${ok}/${warpResults.value.length},详见结果`)
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    warping.value = false
  }
}

// ---------- 城市传送(批量)----------
const teleportCity = ref('')
const teleporting = ref(false)
const teleportResults = ref<BatchRow[]>([])
const teleportStore = useLocalHistory('ggtb.teleport')
const teleportHistory = ref<string[]>(teleportStore.getAll())

async function doTeleport() {
  const city = teleportCity.value.trim()
  if (!city) { ElMessage.warning('请输入城市名'); return }
  if (selectedTargets.value.length === 0) { ElMessage.warning('未选择账号(请在上方勾选)'); return }
  teleporting.value = true
  try {
    teleportResults.value = await dispatchBatch('teleport', { cityName: city })
    const ok = teleportResults.value.filter((r) => r.ok).length
    if (ok === teleportResults.value.length) ElMessage.success(`已下发传送 ${city}: ${ok} 个账号`)
    else ElMessage.warning(`传送下发 ${ok}/${teleportResults.value.length},详见结果`)
    teleportStore.push(city)
    teleportHistory.value = teleportStore.getAll()
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

// ---------- NPC / 怪物(单账号)----------
interface NpcRow {
  id: number
  kind: number
  monsterTblId: number
  isNpc: boolean
  attackable: boolean
  level: number
  distance: number
  x: number
  y: number
  z: number
  hp: number
  name: string
}

const npcList = ref<NpcRow[]>([])
const npcMaxDistance = ref(200)
const onlyRealNpcs = ref(false)
const nameFilter = ref('')
const loadingNpcs = ref(false)

const filteredNpcs = computed(() => {
  let xs = npcList.value
  if (onlyRealNpcs.value) xs = xs.filter((n) => n.isNpc)
  const q = nameFilter.value.trim()
  if (q) xs = xs.filter((n) => n.name.includes(q) || String(n.monsterTblId).includes(q))
  return xs
})

async function refreshNpcs() {
  if (!primaryPid.value) { ElMessage.warning('未选择账号(请在上方勾选)'); return }
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
  if (!primaryPid.value) { ElMessage.warning('未选择账号(请在上方勾选)'); return }
  try {
    const r = await postCommand('talkOrAttack', { creatureId: row.id })
    const label = row.name || `#${row.id}`
    if (r?.ok) {
      ElMessage.success(`已下发: ${row.isNpc ? '对话' : '攻击'} ${label}`)
      if (row.isNpc) {
        setTimeout(() => { refreshDialog() }, 800)
      }
    } else {
      ElMessage.error(`失败: ${r?.detail || r?.error || 'unknown'}`)
    }
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  }
}

// ---------- NPC dialog (多级菜单,单账号) ----------
interface DialogOption {
  index: number
  text: string
  tag: number
  opt?: number
}
interface DialogState {
  open: boolean
  mode?: number
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
  if (!primaryPid.value) return
  loadingDialog.value = true
  try {
    const r = await postCommand('getDialog', {})
    if (!r || !r.ok) {
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
  if (!primaryPid.value) { ElMessage.warning('未选择账号(请在上方勾选)'); return }
  sendingOption.value = index
  try {
    const r = await postCommand('selectDialogOption', { option: index })
    if (r?.ok) {
      ElMessage.success(`已选: ${dialog.value?.options?.[index]?.text || `选项 ${index + 1}`}`)
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
.acct-card { max-width: 720px; margin-bottom: 8px; }
.hint {
  color: var(--el-text-color-secondary);
  font-size: 13px;
  margin: 4px 0 16px;
  line-height: 1.6;
}
.single-note {
  font-size: 13px;
  margin: 4px 0 12px;
  color: var(--el-text-color-regular);
}
.single-note .el-tag { margin: 0 4px; }
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

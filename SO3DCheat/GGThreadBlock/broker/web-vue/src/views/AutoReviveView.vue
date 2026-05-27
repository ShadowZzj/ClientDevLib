<template>
  <div class="autorevive-view">
    <h3>自动复活</h3>

    <p class="hint">
      <strong>broker 端无人值守</strong>:配置存在 broker(auto_revive.json),
      不再依赖浏览器。broker 每 5 秒轮询一次 <code>getStatus</code>,检测到
      HP=0 → 按 <em>死亡后延迟</em> 排程 → 到点自动跑脚本。<br />
      浏览器关掉、切到别的 tab、选别的实例都不会影响 —— 这里只是个遥控器。
    </p>
    <p class="hint">
      角色名为 key,broker 重启会从 <code>auto_revive.json</code> 恢复配置 + 排程。
      想暂停某个角色 → 取消勾选「自动运行」并保存。
    </p>

    <el-form label-width="100px" style="max-width: 720px">
      <el-form-item label="当前实例">
        <span v-if="selectedInstance">
          <strong>{{ selectedInstance.characterName || `pid=${selectedInstance.pid}` }}</strong>
          (pid {{ selectedInstance.pid }})
        </span>
        <span v-else class="muted">未选择实例</span>
      </el-form-item>

      <el-form-item label="状态">
        <el-tag v-if="liveStatus?.isDead" type="danger">死亡 HP=0</el-tag>
        <el-tag v-else-if="liveStatus" type="success">存活 HP={{ liveStatus.hp ?? '?' }}</el-tag>
        <el-tag v-else type="info">未刷新</el-tag>
        <el-tag v-if="liveStatus" type="info" style="margin-left: 8px">
          地图 {{ liveStatus.mapId ?? '?' }}
          <span v-if="mapName" class="muted">({{ mapName }})</span>
        </el-tag>
        <el-tag v-if="liveStatus" type="info" style="margin-left: 8px">userId {{ liveStatus.userId ?? '?' }}</el-tag>
        <el-button :loading="loadingStatus" @click="refreshStatus" size="small" style="margin-left: 12px">
          刷新
        </el-button>
        <el-tag :type="phaseTagType" style="margin-left: 12px">
          broker: {{ state?.phase || 'idle' }}
        </el-tag>
        <el-tag v-if="state?.phase === 'running' && state.currentStepIdx >= 0"
                type="warning" style="margin-left: 6px">
          step {{ state.currentStepIdx + 1 }}/{{ config.steps.length }}
        </el-tag>
      </el-form-item>

      <el-form-item label="自动运行">
        <el-checkbox v-model="config.autoRun">
          broker 检测到 HP=0 时自动执行复活脚本
        </el-checkbox>
        <span class="sub-hint">改完点「保存配置」生效。关掉会清掉当前 pending 排程。</span>
      </el-form-item>

      <el-form-item label="死亡后延迟">
        <el-input-number v-model="config.delayMinMin" :min="0" :step="1" style="width: 110px" />
        <span style="margin: 0 8px">~</span>
        <el-input-number v-model="config.delayMinMax" :min="0" :step="1" style="width: 110px" />
        <span class="sub-hint">
          分钟,范围。检测到死亡后 broker 在 [min, max] 内<strong>随机挑一个</strong>具体值
          再排程(防止固定节奏)。两端相等 = 固定延迟;0 = 立刻。
          <span v-if="pendingRemainingSec > 0" style="color: var(--el-color-warning);">
            <strong>broker 已排程,剩 {{ Math.ceil(pendingRemainingSec) }}s 执行</strong>
            <el-button link type="primary" size="small" @click="cancelPending">取消排程</el-button>
          </span>
        </span>
      </el-form-item>
    </el-form>

    <el-divider />

    <div class="run-bar">
      <el-button type="danger" size="large" :disabled="running" :loading="running" @click="runNow">
        {{ running ? '执行中...' : '立即执行脚本(不看死活)' }}
      </el-button>
      <el-button :disabled="!running" @click="abortScript">中止</el-button>
      <span class="muted" style="margin-left: 12px">
        共 {{ config.steps.length }} 步 · 估计 {{ totalDurationSec.toFixed(1) }}s
      </span>
    </div>

    <h4>脚本步骤
      <span class="sub-hint">
        (broker 持久化 — 按角色名:
        <strong>{{ characterKey || '(无角色)' }}</strong>)
      </span>
    </h4>

    <el-table :data="config.steps" size="small" style="max-width: 1100px">
      <el-table-column type="index" label="#" width="50" />
      <el-table-column prop="type" label="动作" width="170">
        <template #default="{ row, $index }">
          <el-select v-model="row.type" size="small" @change="onStepTypeChange($index)">
            <el-option label="reviveToTown (回主城)" value="reviveToTown" />
            <el-option label="moveTo (走到 x,y)" value="moveTo" />
            <el-option label="waitInTown (等到达主城)" value="waitInTown" />
            <el-option label="waitOutOfTown (等离开主城)" value="waitOutOfTown" />
            <el-option label="sendDialogSelectRaw (411026 NPC 跳图)" value="sendDialogSelectRaw" />
            <el-option label="pressHookedKey (调用 123.dll HOOKPROC 按键)" value="pressHookedKey" />
            <el-option label="sleep (空等)" value="sleep" />
          </el-select>
        </template>
      </el-table-column>

      <el-table-column label="参数" min-width="320">
        <template #default="{ row }">
          <template v-if="row.type === 'moveTo'">
            x<el-input-number v-model="row.x" :precision="2" :step="50" size="small" style="width:110px; margin-left:4px" />
            y<el-input-number v-model="row.y" :precision="2" :step="50" size="small" style="width:110px; margin-left:4px" />
            <el-select v-model="row.action" size="small" style="width:120px; margin-left:6px">
              <el-option label="纯走路" :value="1" />
              <el-option label="走+攻击" :value="3" />
            </el-select>
          </template>
          <template v-else-if="row.type === 'sendDialogSelectRaw'">
            npcId<el-input-number v-model="row.npcId" :min="0" :step="1" size="small" style="width:140px; margin-left:4px" />
            option<el-input-number v-model="row.option" :min="0" :step="1" size="small" style="width:140px; margin-left:4px" />
            <el-button v-if="dialog?.open" size="small" link type="primary" @click="fillFromCurrentNpc(row)">
              从当前 NPC 填入
            </el-button>
          </template>
          <template v-else-if="row.type === 'pressHookedKey'">
            vkey<el-input-number v-model="row.vkey" :min="1" :max="255" :step="1" size="small" style="width:110px; margin-left:4px" />
            <el-checkbox v-model="row.alt" style="margin-left:8px">Alt</el-checkbox>
            <el-checkbox v-model="row.ctrl">Ctrl</el-checkbox>
            <el-checkbox v-model="row.shift">Shift</el-checkbox>
          </template>
          <template v-else-if="row.type === 'waitInTown' || row.type === 'waitOutOfTown'">
            目标mapId<el-input-number v-model="row.mapId" :min="0" :step="1" size="small" style="width:120px; margin-left:4px" />
            超时(ms)<el-input-number v-model="row.timeoutMs" :min="500" :step="500" size="small" style="width:120px; margin-left:4px" />
          </template>
          <template v-else-if="row.type === 'sleep'">
            <span class="muted">只等下方 delay 时长</span>
          </template>
          <template v-else>
            <span class="muted">(无参数)</span>
          </template>
        </template>
      </el-table-column>

      <el-table-column label="后置延迟ms" width="130">
        <template #default="{ row }">
          <el-input-number v-model="row.delayMs" :min="0" :step="100" size="small" style="width:110px" />
        </template>
      </el-table-column>

      <el-table-column label="操作" width="170">
        <template #default="{ $index }">
          <el-button size="small" :disabled="$index === 0" @click="moveStep($index, -1)">↑</el-button>
          <el-button size="small" :disabled="$index === config.steps.length - 1" @click="moveStep($index, 1)">↓</el-button>
          <el-button size="small" type="danger" @click="removeStep($index)">删</el-button>
        </template>
      </el-table-column>
    </el-table>

    <div class="step-toolbar">
      <el-button @click="addStep">+ 加一步</el-button>
      <el-button @click="addPressKeyStep">+ 按键(Alt+W)</el-button>
      <el-button @click="loadDefaultScript">加载默认脚本</el-button>
      <el-button type="success" @click="saveConfig">保存配置</el-button>
      <el-button type="warning" @click="resetConfig">重置当前角色</el-button>
      <el-button link type="primary" @click="showCapturedHooks">查看截获的 HOOKPROC</el-button>
    </div>

    <h4>broker 执行日志</h4>
    <pre class="run-log" ref="logRef">{{ logText }}</pre>

    <el-divider />

    <!-- 当前 NPC 对话 — 跟「寻路移动」那边一模一样,方便: 走到 NPC 后看引擎
         推下来的选项,直接知道 npcInteractId / option,再回去配 sendDialogSelectRaw。
         也支持点选项实测当前对话路径。 -->
    <h4>当前 NPC 对话</h4>
    <p class="hint">
      跟 NPC 说话后,游戏里的对话框选项会同步显示到这里。每点一个选项就发一次
      411026 给服务器,服务器再推下层菜单回来(自动刷新)。配脚本时用「从当前 NPC 填入」
      可以直接把 <code>npcInteractId</code> 灌到 sendDialogSelectRaw 步骤。
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
      暂无对话 —— 先到「寻路移动」点附近 NPC 的「走过去 + 对话」,等 dialog 框
      出现后再回来看这里。
    </div>
  </div>
</template>

<script setup lang="ts">
// 自动复活 — 完全由 broker 跑,前端只是配置 UI + 状态显示。
//
// 数据流:
//   config 编辑 → 「保存配置」→ PUT /api/auto-revive/configs/<name>
//   broker 自己 5s 轮询 getStatus,推进 idle / pending / running 状态机。
//   WS 收 autoRevive.state + autoRevive.log,前端只读、显示。
//
// 迁移老的 localStorage(ggtb.autorevive.<name>):
//   组件 mount 时,若 broker 还没这个角色的配置,但本机 localStorage 里有 →
//   自动 PUT 上去,然后删掉本地 key,避免下次再迁。一次性完成。

import { computed, onMounted, onUnmounted, ref, watch, nextTick } from 'vue'
import { ElMessage } from 'element-plus'
import { useInstances } from '@/composables/useInstances'
import { useWebSocket } from '@/composables/useWebSocket'

const { selectedPid, selectedInstance } = useInstances()
const { onMessage } = useWebSocket()

interface StepBase { delayMs: number }
interface StepReviveToTown extends StepBase { type: 'reviveToTown' }
interface StepMoveTo extends StepBase { type: 'moveTo'; x: number; y: number; action: 1 | 3 }
interface StepWaitInTown extends StepBase { type: 'waitInTown'; mapId: number; timeoutMs: number }
interface StepWaitOutOfTown extends StepBase { type: 'waitOutOfTown'; mapId: number; timeoutMs: number }
interface StepSendDialogSelectRaw extends StepBase { type: 'sendDialogSelectRaw'; npcId: number; option: number }
interface StepPressHookedKey extends StepBase { type: 'pressHookedKey'; vkey: number; alt: boolean; ctrl: boolean; shift: boolean }
interface StepSleep extends StepBase { type: 'sleep' }
type Step = StepReviveToTown | StepMoveTo | StepWaitInTown | StepWaitOutOfTown
          | StepSendDialogSelectRaw | StepPressHookedKey | StepSleep

interface ReviveConfig {
  characterName: string
  autoRun: boolean
  delayMinMin: number
  delayMinMax: number
  steps: Step[]
}

interface BrokerState {
  characterName: string
  phase: 'idle' | 'pending' | 'running' | 'armed'
  deadAt: number
  scheduledAt: number
  currentStepIdx: number
  lastError?: string
  lastRunAt?: number
}

interface LiveStatus {
  hp: number
  isDead: boolean
  mapId: number
  userId: number
}

const liveStatus = ref<LiveStatus | null>(null)
const loadingStatus = ref(false)
const logText = ref('')
const logRef = ref<HTMLPreElement | null>(null)

const defaultConfig = (name: string): ReviveConfig => ({
  characterName: name,
  autoRun: false,
  delayMinMin: 5,
  delayMinMax: 5,
  steps: [],
})
const defaultScript = (): Step[] => ([
  { type: 'reviveToTown', delayMs: 5000 },
  { type: 'waitInTown', mapId: 11, timeoutMs: 5000, delayMs: 5000 },
  { type: 'moveTo', x: 252, y: 286, action: 1, delayMs: 5000 },
  { type: 'sendDialogSelectRaw', npcId: 19811, option: 10245, delayMs: 5000 },
  { type: 'pressHookedKey', vkey: 0x57, alt: true, ctrl: false, shift: false, delayMs: 1000 },
])

const config = ref<ReviveConfig>(defaultConfig(''))
const state = ref<BrokerState | null>(null)

const characterKey = computed(() =>
  selectedInstance.value?.characterName || ''
)

const running = computed(() => state.value?.phase === 'running')
const phaseTagType = computed(() => {
  switch (state.value?.phase) {
    case 'running': return 'warning'
    case 'pending': return 'danger'
    default:        return 'success'
  }
})

const mapNames: Record<number, string> = {
  7: '新手村', 400: 'Square / 主城', 200: '老挂机点', 580: '怪物狩猎',
}
const mapName = computed(() =>
  liveStatus.value ? (mapNames[liveStatus.value.mapId] ?? '') : ''
)

const totalDurationSec = computed(() => {
  let ms = 0
  for (const s of config.value.steps) {
    ms += s.delayMs ?? 0
    if (s.type === 'waitInTown' || s.type === 'waitOutOfTown') ms += (s.timeoutMs ?? 0) / 4
  }
  return ms / 1000
})

// pending 倒计时:每秒 tick 重新算
const tickNow = ref<number>(Date.now())
const pendingRemainingSec = computed(() => {
  if (!state.value || state.value.phase !== 'pending') return 0
  const rem = state.value.scheduledAt - tickNow.value
  return rem > 0 ? rem / 1000 : 0
})

// ---------- broker config CRUD ----------
async function loadConfigFromBroker() {
  if (!characterKey.value) {
    config.value = defaultConfig('')
    state.value = null
    logText.value = ''
    return
  }
  try {
    const r = await fetch(`/api/auto-revive/configs/${encodeURIComponent(characterKey.value)}`)
    if (r.ok) {
      config.value = await r.json()
    } else if (r.status === 404) {
      // broker 没有 → 检查本地 localStorage 老格式做一次性迁移
      const migrated = await migrateFromLocalStorage(characterKey.value)
      if (!migrated) {
        const cfg = defaultConfig(characterKey.value)
        cfg.steps = defaultScript()
        config.value = cfg
      }
    }
  } catch (e: any) {
    ElMessage.error(`加载 broker 配置失败: ${e.message}`)
  }
  // state + logs
  try {
    const [sr, lr] = await Promise.all([
      fetch(`/api/auto-revive/states/${encodeURIComponent(characterKey.value)}`),
      fetch(`/api/auto-revive/logs/${encodeURIComponent(characterKey.value)}`),
    ])
    if (sr.ok) state.value = await sr.json()
    if (lr.ok) {
      const lines = (await lr.json()) as string[]
      logText.value = lines.join('\n') + (lines.length ? '\n' : '')
    }
  } catch { /* silent */ }
}

async function migrateFromLocalStorage(name: string): Promise<boolean> {
  const oldKey = `ggtb.autorevive.${name}`
  const raw = localStorage.getItem(oldKey)
  if (!raw) return false
  try {
    const parsed = JSON.parse(raw)
    // 老 schema: delayMin 单值 / 还有 townMapId / delegateNpcId / delegateOption。
    // 单值迁移成 [min,max] 两端相等;另外几个字段丢弃(不再有这些 UI)。
    const single =
      Number.isFinite(parsed.delayMin) ? parsed.delayMin :
      Number.isFinite(parsed.cooldownMin) ? parsed.cooldownMin : 5
    const cfg: ReviveConfig = {
      characterName: name,
      autoRun: !!parsed.autoRun,
      delayMinMin: Number.isFinite(parsed.delayMinMin) ? parsed.delayMinMin : single,
      delayMinMax: Number.isFinite(parsed.delayMinMax) ? parsed.delayMinMax : single,
      steps: Array.isArray(parsed.steps) && parsed.steps.length ? parsed.steps : defaultScript(),
    }
    const r = await fetch(`/api/auto-revive/configs/${encodeURIComponent(name)}`, {
      method: 'PUT',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(cfg),
    })
    if (!r.ok) return false
    config.value = await r.json()
    // 迁移成功 → 清掉本地 + 老的 deadAt key,以后只读 broker
    localStorage.removeItem(oldKey)
    localStorage.removeItem(`${oldKey}.deadAt`)
    ElMessage.success(`已从本地迁移 ${name} 的配置到 broker`)
    return true
  } catch {
    return false
  }
}

async function saveConfig() {
  if (!characterKey.value) {
    ElMessage.warning('当前未选角色,无法保存')
    return
  }
  try {
    const r = await fetch(`/api/auto-revive/configs/${encodeURIComponent(characterKey.value)}`, {
      method: 'PUT',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!r.ok) throw new Error(`HTTP ${r.status}`)
    config.value = await r.json()
    ElMessage.success(`已保存到 broker (${characterKey.value})`)
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  }
}

async function resetConfig() {
  if (!characterKey.value) return
  try {
    await fetch(`/api/auto-revive/configs/${encodeURIComponent(characterKey.value)}`, { method: 'DELETE' })
    const cfg = defaultConfig(characterKey.value)
    cfg.steps = defaultScript()
    config.value = cfg
    ElMessage.info('已重置当前角色配置(broker 端已删除)')
  } catch (e: any) {
    ElMessage.error(`重置失败: ${e.message}`)
  }
}

function loadDefaultScript() {
  config.value.steps = defaultScript()
}

async function cancelPending() {
  if (!characterKey.value) return
  await fetch(`/api/auto-revive/cancel/${encodeURIComponent(characterKey.value)}`, { method: 'POST' })
}

async function runNow() {
  if (!characterKey.value) return
  const r = await fetch(`/api/auto-revive/run-now/${encodeURIComponent(characterKey.value)}`, { method: 'POST' })
  const obj = await r.json()
  if (!obj.ok) ElMessage.error(`无法启动: ${obj.detail || '?'}`)
}

async function abortScript() {
  if (!characterKey.value) return
  await fetch(`/api/auto-revive/abort/${encodeURIComponent(characterKey.value)}`, { method: 'POST' })
}

// ---------- 步骤编辑 ----------
function addStep() {
  config.value.steps.push({ type: 'sleep', delayMs: 1000 } as StepSleep)
}
function addPressKeyStep() {
  config.value.steps.push({
    type: 'pressHookedKey', vkey: 0x57, alt: true, ctrl: false, shift: false, delayMs: 200,
  } as StepPressHookedKey)
}
function removeStep(i: number) {
  config.value.steps.splice(i, 1)
}
function moveStep(i: number, dir: number) {
  const j = i + dir
  if (j < 0 || j >= config.value.steps.length) return
  const [s] = config.value.steps.splice(i, 1)
  config.value.steps.splice(j, 0, s)
}
function onStepTypeChange(idx: number) {
  const s = config.value.steps[idx]
  if (s.type === 'moveTo') {
    const ms = s as StepMoveTo
    if (ms.x == null) ms.x = 0
    if (ms.y == null) ms.y = 0
    if (ms.action == null) ms.action = 1
  } else if (s.type === 'sendDialogSelectRaw') {
    const ds = s as StepSendDialogSelectRaw
    if (ds.npcId == null) ds.npcId = 0
    if (ds.option == null) ds.option = 0
  } else if (s.type === 'pressHookedKey') {
    const ks = s as StepPressHookedKey
    if (ks.vkey == null) ks.vkey = 0x57
    if (ks.alt == null)  ks.alt  = true
    if (ks.ctrl == null) ks.ctrl = false
    if (ks.shift == null) ks.shift = false
  } else if (s.type === 'waitInTown' || s.type === 'waitOutOfTown') {
    const ws = s as StepWaitInTown
    // 默认 11 — 用户改 step 时如果想要别的可以直接改 row.mapId
    if (ws.mapId == null) ws.mapId = 11
    if (ws.timeoutMs == null) ws.timeoutMs = 15000
  }
  if (s.delayMs == null) s.delayMs = 500
}

// 把当前在对话的 NPC 的 interactId 灌进去 — 配 sendDialogSelectRaw 步骤的时候
// 不用回头去抓包,看到对话框打开了直接「填入」省事。option 字段保留用户原值。
function fillFromCurrentNpc(row: any) {
  if (!dialog.value?.open) return
  if (typeof dialog.value.npcInteractId === 'number')
    row.npcId = dialog.value.npcInteractId
}

// ---------- 实时 status 显示(只是 UI,不参与判定) ----------
async function postCommand(action: string, args: Record<string, any>): Promise<any> {
  if (!selectedPid.value) return null
  const res = await fetch(`/api/command/${selectedPid.value}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ action, args }),
  })
  return res.json()
}

async function refreshStatus() {
  if (!selectedPid.value) return
  loadingStatus.value = true
  try {
    const r = await postCommand('getStatus', {})
    if (r?.ok) {
      const obj = typeof r.detail === 'string' ? JSON.parse(r.detail) : r.detail
      liveStatus.value = obj as LiveStatus
    }
  } catch { /* silent */ }
  finally {
    loadingStatus.value = false
  }
}

async function showCapturedHooks() {
  const r = await postCommand('listCapturedHooks', {})
  if (!r?.ok) {
    ElMessage.error('listCapturedHooks 失败: ' + (r?.detail || r?.error))
    return
  }
  const list = typeof r.detail === 'string' ? JSON.parse(r.detail) : r.detail
  appendLocalLog('[captured hooks]')
  if (!list || list.length === 0) {
    appendLocalLog('  (空) — 第三方 DLL 还没装 hook')
    return
  }
  for (const h of list)
    appendLocalLog(`  idHook=${h.idHook}(${h.idHookName}) lpfn=${h.lpfn} hMod=${h.hMod} tid=${h.tid}`)
}

function appendLocalLog(line: string) {
  const ts = new Date().toLocaleTimeString()
  logText.value += `[${ts}] ${line}\n`
  nextTick(() => {
    if (logRef.value) logRef.value.scrollTop = logRef.value.scrollHeight
  })
}

// ---------- WS 监听 broker 推送 ----------
const offMsg = onMessage((msg) => {
  if (msg.type === 'autoRevive.state' && msg.characterName === characterKey.value) {
    state.value = msg.state
  } else if (msg.type === 'autoRevive.log' && msg.characterName === characterKey.value) {
    logText.value += msg.line + '\n'
    nextTick(() => {
      if (logRef.value) logRef.value.scrollTop = logRef.value.scrollHeight
    })
  } else if (msg.type === 'autoRevive.states') {
    // 首连 snapshot,如果当前角色在里头就直接刷新
    const s = (msg.states as BrokerState[] | undefined)?.find(
      (x) => x.characterName === characterKey.value
    )
    if (s) state.value = s
  }
})

// 切角色 → 重新拉 broker 配置;UI 上的实时 hp/map 也刷一下
watch(characterKey, () => {
  logText.value = ''
  loadConfigFromBroker()
  refreshStatus()
}, { immediate: true })

let statusTimer: number | null = null
let tickTimer: number | null = null
onMounted(() => {
  refreshStatus()
  // 1Hz 刷 UI hp + pendingRemainingSec 重算。注:这只是 UI 显示,broker
  // 自己也在 5s 轮询。前端关掉这个不会影响 broker 自动模式工作。
  statusTimer = window.setInterval(refreshStatus, 2000)
  tickTimer   = window.setInterval(() => { tickNow.value = Date.now() }, 1000)
})
onUnmounted(() => {
  if (statusTimer !== null) { window.clearInterval(statusTimer); statusTimer = null }
  if (tickTimer   !== null) { window.clearInterval(tickTimer);   tickTimer   = null }
  offMsg && (offMsg as any)()
})

watch(selectedPid, () => { refreshStatus() })

// ---------- NPC dialog (跟 MoveToView 那份 1:1 复用) ----------
// 引擎本身的多级对话:每点一个选项就发 411026 (CG_NPC_DIALOG_SELECT)。server
// 推回 521603,把新选项码写进 g_NpcDialogState 链表,客户端 rebuild 菜单。
// 我们这一侧只是把 state 链表读出来当 JSON,前端按钮发回 selectDialogOption。
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
  if (!selectedPid.value) return
  loadingDialog.value = true
  try {
    const r = await postCommand('getDialog', {})
    if (!r || !r.ok) return
    const obj = typeof r.detail === 'string' ? JSON.parse(r.detail) : r.detail
    dialog.value = obj as DialogState
  } catch {
    // 静默
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
.autorevive-view { padding: 8px; }
.hint {
  color: var(--el-text-color-secondary);
  font-size: 13px;
  margin: 4px 0 16px;
  line-height: 1.6;
}
.sub-hint { color: var(--el-text-color-secondary); font-size: 12px; margin-left: 8px; }
.muted { color: var(--el-text-color-secondary); font-style: italic; }
h4 { margin: 16px 0 8px; }
.run-bar {
  display: flex;
  align-items: center;
  margin: 16px 0;
  gap: 6px;
}
.step-toolbar {
  display: flex;
  gap: 8px;
  margin: 10px 0;
}
.run-log {
  max-height: 320px;
  overflow-y: auto;
  background: var(--el-fill-color-light);
  border: 1px solid var(--el-border-color);
  border-radius: 4px;
  padding: 10px 12px;
  font-size: 12px;
  line-height: 1.5;
  white-space: pre-wrap;
  margin: 4px 0;
}
code {
  background: var(--el-fill-color);
  padding: 1px 4px;
  border-radius: 3px;
  font-size: 12px;
}

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

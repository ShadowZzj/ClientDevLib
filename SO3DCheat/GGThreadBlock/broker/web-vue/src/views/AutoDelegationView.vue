<template>
  <div class="autodelegation-view">
    <h3>自动委托</h3>
    <p class="hint">
      自动委托 = 死循环发两条 <code>CG_NPC_DIALOG_SELECT (411026)</code>:
      <code>(npcId, opt1)</code> → 间隔 → <code>(npcId, opt2)</code> → 间隔 → 循环。
      DLL 那边的 <code>sendDialogSelectRaw</code> handler 直接调引擎的
      <code>Net__SendDialogSelect</code> — 跟 imgui 那个 AutoDelegation 模块完全等价,
      只是把循环放到这里跑,方便保存「预设组合」和一键切换。
    </p>
    <p class="hint">
      <strong>预设</strong> = 给一组 (npcId, opt1, opt2, intervalMs) 起个名字存下来。
      存在浏览器 <code>localStorage</code> 里,key:
      <code>{{ presetStorageKey }}</code>(<strong>全局共享</strong>,不绑角色 —— 不同角色调同一个委托 NPC 的概率最高)。
      历史 npcId / option 也会自动记录到下拉建议里,直接点选不用重输。
    </p>

    <el-form label-width="100px" style="max-width: 720px">
      <el-form-item label="当前实例">
        <span v-if="selectedInstance">
          <strong>{{ selectedInstance.characterName || `pid=${selectedInstance.pid}` }}</strong>
          (pid {{ selectedInstance.pid }})
        </span>
        <span v-else class="muted">未选择实例</span>
      </el-form-item>

      <el-form-item label="预设">
        <el-select v-model="selectedPresetName" placeholder="(直接编辑下方,或选已保存预设)"
                   clearable style="width: 260px" @change="onPresetChange">
          <el-option v-for="p in presets" :key="p.name" :label="p.name" :value="p.name">
            <span>{{ p.name }}</span>
            <span class="muted" style="margin-left: 8px">
              npc={{ p.npcId }} opt={{ p.opt1 }}/{{ p.opt2 }}
            </span>
          </el-option>
        </el-select>
        <el-input v-model="newPresetName" placeholder="新预设名" size="default"
                  style="width:160px; margin-left:8px" />
        <el-button :disabled="!newPresetName.trim()" @click="savePreset" size="default"
                   type="success" style="margin-left:6px">
          保存为预设
        </el-button>
        <el-button :disabled="!selectedPresetName" @click="deletePreset" type="danger"
                   size="default" style="margin-left:6px">
          删除当前
        </el-button>
      </el-form-item>

      <el-form-item label="npcId">
        <el-select
          v-model.number="form.npcId"
          filterable allow-create
          placeholder="选历史值 / 直接输入"
          style="width: 200px">
          <el-option v-for="v in historyNpcIds" :key="v" :label="String(v)" :value="v" />
        </el-select>
        <span class="sub-hint">CG_NPC_DIALOG_SELECT 包里的 npc 字段。</span>
      </el-form-item>

      <el-form-item label="对话ID 1">
        <el-select
          v-model.number="form.opt1"
          filterable allow-create
          placeholder="选历史值 / 直接输入"
          style="width: 200px">
          <el-option v-for="v in historyOptions" :key="v" :label="String(v)" :value="v" />
        </el-select>
        <span class="sub-hint">第一步选项编号。</span>
      </el-form-item>

      <el-form-item label="对话ID 2">
        <el-select
          v-model.number="form.opt2"
          filterable allow-create
          placeholder="选历史值 / 直接输入"
          style="width: 200px">
          <el-option v-for="v in historyOptions" :key="v" :label="String(v)" :value="v" />
        </el-select>
        <span class="sub-hint">第二步选项编号。</span>
      </el-form-item>

      <el-form-item label="间隔">
        <el-input-number v-model="form.intervalMs" :min="100" :max="60000" :step="100"
                         style="width: 160px" />
        <span class="sub-hint">ms。两次发包之间的等待。imgui 默认 1000ms。</span>
      </el-form-item>
    </el-form>

    <el-divider />

    <div class="run-bar">
      <el-button type="primary" size="large" :disabled="running || !canRun" @click="start">
        {{ running ? '运行中...' : '开始' }}
      </el-button>
      <el-button :disabled="!running" @click="stop" size="large" type="danger">停止</el-button>
      <span class="muted" style="margin-left: 12px">
        <span v-if="running">
          已发 <strong>{{ sentTotal }}</strong> 次 · 上一步 = step {{ lastStep || '?' }} ·
          距上次 {{ lastSendAgeMs }}ms
        </span>
        <span v-else>未运行</span>
      </span>
    </div>

    <h4>执行日志</h4>
    <pre class="run-log" ref="logRef">{{ logText }}</pre>
  </div>
</template>

<script setup lang="ts">
// 自动委托 — imgui AutoDelegationModule 的 web 版。
// 用 setInterval 死循环发两条 CG_NPC_DIALOG_SELECT(411026),
// 直接复用 dllmain 里已经注册的 sendDialogSelectRaw handler。
//
// 持久化:
//   ggtb.autodelegation.presets   — 全局预设列表 [{name,npcId,opt1,opt2,intervalMs}]
//   ggtb.autodelegation.history   — 全局历史值,用于下拉建议 {npcIds:[], options:[]}
// 故意不绑角色:不同角色用同一个委托 NPC 的概率高,把缓存全局共享更好用。
// 真要按角色,改 storageKey 加个 characterKey 即可。

import { computed, onMounted, onUnmounted, ref, watch, nextTick } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useInstances } from '@/composables/useInstances'

const { selectedPid, selectedInstance } = useInstances()

interface Preset {
  name: string
  npcId: number
  opt1: number
  opt2: number
  intervalMs: number
}

const presetStorageKey  = 'ggtb.autodelegation.presets'
const historyStorageKey = 'ggtb.autodelegation.history'

const presets = ref<Preset[]>([])
const selectedPresetName = ref<string>('')
const newPresetName = ref<string>('')

const form = ref({ npcId: 0, opt1: 0, opt2: 0, intervalMs: 1000 })

const historyNpcIds = ref<number[]>([])
const historyOptions = ref<number[]>([])

const running = ref(false)
const sentTotal = ref(0)
const lastStep = ref(0)
const lastSendMs = ref(0)
const tickNow = ref(Date.now())
const lastSendAgeMs = computed(() => lastSendMs.value ? (tickNow.value - lastSendMs.value) : 0)

const logText = ref('')
const logRef = ref<HTMLPreElement | null>(null)
let tickTimer: number | null = null
let loopTimer: number | null = null

const canRun = computed(() =>
  form.value.npcId > 0 && form.value.opt1 > 0 && form.value.opt2 > 0 &&
  !!selectedPid.value
)

// ---------- 持久化 ----------
function loadPresets() {
  try {
    const raw = localStorage.getItem(presetStorageKey)
    presets.value = raw ? JSON.parse(raw) : []
  } catch { presets.value = [] }
}
function savePresetsToStorage() {
  localStorage.setItem(presetStorageKey, JSON.stringify(presets.value))
}
function loadHistory() {
  try {
    const raw = localStorage.getItem(historyStorageKey)
    if (raw) {
      const obj = JSON.parse(raw)
      historyNpcIds.value  = Array.isArray(obj.npcIds)  ? obj.npcIds  : []
      historyOptions.value = Array.isArray(obj.options) ? obj.options : []
    }
  } catch { /* ignore */ }
}
function saveHistory() {
  localStorage.setItem(historyStorageKey, JSON.stringify({
    npcIds:  historyNpcIds.value,
    options: historyOptions.value,
  }))
}
function pushHistory(arr: number[], v: number, cap = 30): number[] {
  if (!Number.isFinite(v) || v <= 0) return arr
  const without = arr.filter(x => x !== v)
  without.unshift(v) // 最近用的排最前
  return without.slice(0, cap)
}
function recordHistoryFromForm() {
  historyNpcIds.value  = pushHistory(historyNpcIds.value, form.value.npcId)
  historyOptions.value = pushHistory(historyOptions.value, form.value.opt1)
  historyOptions.value = pushHistory(historyOptions.value, form.value.opt2)
  saveHistory()
}

// ---------- 预设 ----------
function onPresetChange(name: string) {
  if (!name) return
  const p = presets.value.find(x => x.name === name)
  if (!p) return
  form.value = { npcId: p.npcId, opt1: p.opt1, opt2: p.opt2, intervalMs: p.intervalMs }
  log(`加载预设「${name}」: npc=${p.npcId} opt=${p.opt1}/${p.opt2} interval=${p.intervalMs}ms`)
}
function savePreset() {
  const name = newPresetName.value.trim()
  if (!name) return
  if (form.value.npcId <= 0 || form.value.opt1 <= 0 || form.value.opt2 <= 0) {
    ElMessage.warning('请先填好 npcId / opt1 / opt2')
    return
  }
  const existing = presets.value.findIndex(p => p.name === name)
  const entry: Preset = {
    name,
    npcId: form.value.npcId,
    opt1:  form.value.opt1,
    opt2:  form.value.opt2,
    intervalMs: form.value.intervalMs,
  }
  if (existing >= 0) presets.value[existing] = entry
  else               presets.value.push(entry)
  savePresetsToStorage()
  selectedPresetName.value = name
  newPresetName.value = ''
  recordHistoryFromForm()
  ElMessage.success(`已保存预设「${name}」`)
}
async function deletePreset() {
  const name = selectedPresetName.value
  if (!name) return
  try { await ElMessageBox.confirm(`删除预设「${name}」?`, '确认', { type: 'warning' }) }
  catch { return }
  presets.value = presets.value.filter(p => p.name !== name)
  savePresetsToStorage()
  selectedPresetName.value = ''
  ElMessage.info(`已删除「${name}」`)
}

// ---------- 网络命令 ----------
async function postCommand(action: string, args: Record<string, any>): Promise<any> {
  if (!selectedPid.value) {
    ElMessage.warning('未选择实例')
    return null
  }
  const res = await fetch(`/api/command/${selectedPid.value}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ action, args }),
  })
  return res.json()
}

// ---------- 主循环 ----------
function log(line: string) {
  const ts = new Date().toLocaleTimeString()
  logText.value += `[${ts}] ${line}\n`
  // 限制日志长度,避免长时间运行后 DOM 爆炸。
  const lines = logText.value.split('\n')
  if (lines.length > 400) logText.value = lines.slice(-300).join('\n')
  nextTick(() => {
    if (logRef.value) logRef.value.scrollTop = logRef.value.scrollHeight
  })
}

async function sendStep(opt: number, stepNum: number) {
  const r = await postCommand('sendDialogSelectRaw', { npcId: form.value.npcId, option: opt })
  lastSendMs.value = Date.now()
  lastStep.value = stepNum
  sentTotal.value += 1
  if (!r?.ok) log(`step${stepNum} FAIL: ${r?.detail || r?.error}`)
}

// 串行循环:发 opt1 → 等 intervalMs → 发 opt2 → 等 intervalMs → ...
// 用 setTimeout 链而不是 setInterval,这样:
//   1) 每个 send 完成才开始计时,不会因为 send 慢导致并发;
//   2) 用户 stop 时清掉 loopTimer 一次就够,不用 abort flag。
async function tickOnce() {
  if (!running.value) return
  if (!canRun.value) { stop(); return }
  const step = (sentTotal.value % 2 === 0) ? 1 : 2
  const opt  = step === 1 ? form.value.opt1 : form.value.opt2
  await sendStep(opt, step)
  if (!running.value) return
  loopTimer = window.setTimeout(tickOnce, form.value.intervalMs)
}

function start() {
  if (running.value) return
  if (!canRun.value) {
    ElMessage.warning('请先选择实例并填好 npcId / opt1 / opt2')
    return
  }
  running.value = true
  sentTotal.value = 0
  lastStep.value = 0
  lastSendMs.value = 0
  recordHistoryFromForm()
  log(`启动: npc=${form.value.npcId} opt=${form.value.opt1}/${form.value.opt2} interval=${form.value.intervalMs}ms`)
  void tickOnce()
}
function stop() {
  if (!running.value && loopTimer === null) return
  running.value = false
  if (loopTimer !== null) {
    window.clearTimeout(loopTimer)
    loopTimer = null
  }
  log(`停止: 共发 ${sentTotal.value} 次`)
}

// 切实例(或离开页面)时自动停 — 否则会继续给前一个角色发包。
watch(selectedPid, (newPid, oldPid) => {
  if (running.value && newPid !== oldPid) {
    log('切换实例 → 自动停止')
    stop()
  }
})

onMounted(() => {
  loadPresets()
  loadHistory()
  tickTimer = window.setInterval(() => { tickNow.value = Date.now() }, 500)
})
onUnmounted(() => {
  stop()
  if (tickTimer !== null) { window.clearInterval(tickTimer); tickTimer = null }
})
</script>

<style scoped>
.autodelegation-view { padding: 8px; }
.hint {
  color: var(--el-text-color-secondary);
  font-size: 13px;
  margin: 4px 0 12px;
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
</style>

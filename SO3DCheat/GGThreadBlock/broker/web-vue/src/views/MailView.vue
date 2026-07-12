<template>
  <div class="mail-view">
    <!-- 金币邮寄 -->
    <h3>金币邮寄</h3>
    <el-form label-width="80px" style="max-width: 500px">
      <el-form-item label="收件人">
        <el-autocomplete
          v-model="recipient"
          :fetch-suggestions="recipientSuggestions"
          placeholder="角色名 (ASCII, 1-15字符)"
          style="width: 100%"
          @select="(item: any) => recipient = item.value"
        />
      </el-form-item>
      <el-form-item label="金额">
        <el-input-number v-model="amount" :min="1" :step="1000" style="width: 200px" />
      </el-form-item>
      <el-form-item label="附言">
        <el-input v-model="body" placeholder="可选" />
      </el-form-item>
      <el-form-item>
        <el-button type="primary" :loading="moneySending" @click="doSendMoney">发送金币</el-button>
      </el-form-item>
    </el-form>

    <el-divider />

    <!-- 背包物品邮寄 / 丢弃 -->
    <h3>背包物品</h3>
    <div class="bag-toolbar">
      <el-button @click="refreshBag" :loading="bagLoading">刷新背包</el-button>
      <span class="bag-count" v-if="bagItems.length">{{ bagItems.length }} 件物品</span>
      <span class="bag-count" v-if="bagItems.length">已选 {{ selectedRows.length }}</span>
    </div>

    <div class="bag-action-bar" v-if="bagItems.length">
      <el-autocomplete
        v-model="bagRecipient"
        :fetch-suggestions="recipientSuggestions"
        placeholder="收件人"
        style="width: 200px"
        @select="(item: any) => bagRecipient = item.value"
      />
      <el-button
        type="warning"
        :disabled="selectedRows.length === 0 || !bagRecipient.trim()"
        :loading="batchSending"
        @click="doBatchSend"
      >
        邮寄选中 ({{ selectedRows.length }})
      </el-button>
      <el-divider direction="vertical" />
      <span class="bar-label">丢弃间隔(ms)</span>
      <el-input-number v-model="dropIntervalMs" :min="100" :step="100" :max="60000" style="width: 130px" />
      <el-button
        type="danger"
        :disabled="selectedRows.length === 0"
        :loading="batchDropping"
        @click="doBatchDrop"
      >
        丢弃选中 ({{ selectedRows.length }})
      </el-button>
      <el-divider direction="vertical" />
      <el-input
        v-model="dropByName"
        placeholder="按名称丢弃 (精确匹配)"
        clearable
        style="width: 220px"
      />
      <el-button
        type="danger"
        plain
        :disabled="!dropByName.trim() || dropByNameCount === 0"
        :loading="batchDropping"
        @click="doDropByName"
      >
        丢弃同名 ({{ dropByNameCount }})
      </el-button>
      <el-button
        v-if="batchDropping"
        type="info"
        :disabled="dropAbort"
        @click="stopDrop"
      >
        {{ dropAbort ? '停止中…' : '停止' }}
      </el-button>
      <span class="batch-status">{{ batchStatus }}</span>
    </div>

    <el-table
      v-if="bagItems.length"
      :data="bagItems"
      @selection-change="onSelectionChange"
      ref="bagTableRef"
      row-key="bagId"
      style="margin-top: 12px"
      @row-contextmenu="onRowContext"
    >
      <el-table-column type="selection" width="40" reserve-selection />
      <el-table-column prop="slotIndex" label="Slot" width="60" />
      <el-table-column prop="bagId" label="BagId" width="80" />
      <el-table-column prop="name" label="名称" min-width="120" />
      <el-table-column prop="itemId" label="ItemId" width="80" />
      <el-table-column prop="count" label="数量" width="70" />
    </el-table>

    <el-divider />

    <!-- 全号邮寄:所有在线角色把指定物品邮寄给一个目标角色 -->
    <h3>全号邮寄</h3>
    <p class="all-mail-hint">
      当前所有<strong>在线角色</strong>把名称匹配的物品邮寄给目标角色。某角色有多格该物品就邮寄多次
      (和单格勾选一样,按整格数量发);同一角色多格之间延迟
      <strong>{{ Math.round(allMailIntervalMs / 1000) }}s</strong>。
      默认邮寄全部匹配格(每角色最多格数=0 表示不限)。
      <strong>目标角色若在线,自身不会邮给自己。</strong>
    </p>
    <el-form label-width="110px" style="max-width: 560px">
      <el-form-item label="物品名称">
        <el-input v-model="allMailItemName" placeholder="精确匹配背包物品名称" clearable />
      </el-form-item>
      <el-form-item label="目标收件人">
        <el-autocomplete
          v-model="allMailRecipient"
          :fetch-suggestions="recipientSuggestions"
          placeholder="角色名 (ASCII, 1-15字符)"
          style="width: 100%"
          @select="(item: any) => allMailRecipient = item.value"
        />
      </el-form-item>
      <el-form-item label="每角色最多格数">
        <el-input-number v-model="allMailMaxSlots" :min="0" :step="1" style="width: 160px" />
        <span class="bar-label" style="margin-left: 8px">0 = 邮寄全部匹配格</span>
      </el-form-item>
      <el-form-item label="格间隔(ms)">
        <el-input-number v-model="allMailIntervalMs" :min="1000" :step="1000" :max="120000" style="width: 160px" />
      </el-form-item>
      <el-form-item>
        <el-button
          type="warning"
          :loading="allMailSending"
          :disabled="!allMailItemName.trim() || !allMailRecipient.trim()"
          @click="doAllCharsMail"
        >
          全号邮寄
        </el-button>
        <span class="batch-status" style="margin-left: 10px">{{ allMailStatus }}</span>
      </el-form-item>
    </el-form>
    <pre v-if="allMailLog" class="all-mail-log">{{ allMailLog }}</pre>

    <!-- 右键菜单 -->
    <!-- PLACEHOLDER_CONTEXT_MENU -->
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useInstances } from '@/composables/useInstances'
import { useLocalHistory } from '@/composables/useLocalHistory'
import type { BagItem } from '@/types'

const { instances, selectedPid, selectedInstance } = useInstances()
const recipientHistory = useLocalHistory('ggtb.recipients')

const recipient = ref('')
const amount = ref(1000)
const body = ref('')
const moneySending = ref(false)

const bagItems = ref<BagItem[]>([])
const bagLoading = ref(false)
const bagRecipient = ref('')
const selectedRows = ref<BagItem[]>([])
const batchSending = ref(false)
const batchDropping = ref(false)
const dropAbort = ref(false)
const dropIntervalMs = ref(1000)
const batchStatus = ref('')
const bagTableRef = ref<any>(null)
const dropByName = ref('')

// 全号邮寄
const allMailItemName = ref('')
const allMailRecipient = ref('')
const allMailMaxSlots = ref(0)      // 0 = 邮寄全部匹配格
const allMailIntervalMs = ref(10000)
const allMailSending = ref(false)
const allMailStatus = ref('')
const allMailLog = ref('')

// 当前背包里和输入框名称完全相同的格子数(用于按钮显示数量 + 提前防呆)
const dropByNameCount = computed(() => {
  const q = dropByName.value.trim()
  if (!q) return 0
  return bagItems.value.filter((it) => (it.name || '') === q).length
})

function recipientSuggestions(query: string, cb: (results: any[]) => void) {
  const all = recipientHistory.getAll().map((v) => ({ value: v }))
  cb(query ? all.filter((i) => i.value.includes(query)) : all)
}

async function doSendMoney() {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  const r = recipient.value.trim()
  if (!r || r.length > 15 || /[^\x20-\x7e]/.test(r)) {
    ElMessage.warning('收件人无效（1-15 ASCII）'); return
  }
  if (!Number.isFinite(amount.value) || amount.value <= 0) {
    ElMessage.warning('金额必须 > 0'); return
  }
  const wallet = selectedInstance.value?.money
  if (typeof wallet === 'number' && amount.value > wallet) {
    ElMessage.warning('金额超过钱包余额'); return
  }

  moneySending.value = true
  try {
    const res = await fetch(`/api/command/${selectedPid.value}`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ action: 'sendMoneyMail', args: { recipient: r, amount: amount.value, body: body.value } }),
    })
    const data = await res.json()
    if (data.ok) {
      ElMessage.success(`已发送 ${amount.value} → ${r}`)
      recipientHistory.push(r)
    } else {
      ElMessage.error(`失败: ${data.detail || data.error || 'unknown'}`)
    }
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    moneySending.value = false
  }
}

async function refreshBag() {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  bagLoading.value = true
  try {
    const res = await fetch(`/api/command/${selectedPid.value}`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ action: 'getBagItems', args: {} }),
    })
    const data = await res.json()
    if (data.ok) {
      bagItems.value = JSON.parse(data.detail || '[]')
      selectedRows.value = []
    } else {
      ElMessage.error(`失败: ${data.detail || data.error || ''}`)
    }
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    bagLoading.value = false
  }
}

function onSelectionChange(rows: BagItem[]) {
  selectedRows.value = rows
}

async function doBatchSend() {
  if (!selectedPid.value) return
  const r = bagRecipient.value.trim()
  if (!r || r.length > 15) { ElMessage.warning('收件人无效'); return }
  const toSend = [...selectedRows.value]
  if (toSend.length === 0) return

  batchSending.value = true
  batchStatus.value = `发送中 (0/${toSend.length})…`
  let ok = 0, fail = 0

  for (let idx = 0; idx < toSend.length; idx++) {
    const item = toSend[idx]
    batchStatus.value = `发送中 (${idx + 1}/${toSend.length})…`
    try {
      const res = await fetch(`/api/command/${selectedPid.value}`, {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ action: 'sendItemMail', args: { recipient: r, bagId: item.bagId, count: item.count } }),
      })
      const data = await res.json()
      if (data.ok) ok++; else fail++
    } catch { fail++ }
    if (idx < toSend.length - 1) await new Promise((resolve) => setTimeout(resolve, 6000))
  }

  batchStatus.value = `完成: 成功 ${ok}, 失败 ${fail}`
  ElMessage[fail === 0 ? 'success' : 'warning'](`邮寄完成: ${ok} 成功, ${fail} 失败`)
  batchSending.value = false
  recipientHistory.push(r)
}

async function doBatchDrop() {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  const toDrop = [...selectedRows.value]
  if (toDrop.length === 0) return

  const interval = Math.max(100, Number(dropIntervalMs.value) || 1000)

  try {
    await ElMessageBox.confirm(
      `将一格一格丢弃 ${toDrop.length} 件物品，间隔 ${interval}ms，确认？`,
      '确认丢弃',
      { confirmButtonText: '丢弃', cancelButtonText: '取消', type: 'warning' }
    )
  } catch { return }

  batchDropping.value = true
  dropAbort.value = false
  batchStatus.value = `丢弃中 (0/${toDrop.length})…`
  let ok = 0, fail = 0, aborted = false

  for (let idx = 0; idx < toDrop.length; idx++) {
    if (dropAbort.value) { aborted = true; break }
    const item = toDrop[idx]
    batchStatus.value = `丢弃中 (${idx + 1}/${toDrop.length}) ${item.name || item.itemId}…`
    try {
      const res = await fetch(`/api/command/${selectedPid.value}`, {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ action: 'dropBagItem', args: { bagId: item.bagId, count: item.count } }),
      })
      const data = await res.json()
      if (data.ok) ok++; else fail++
    } catch { fail++ }
    if (idx < toDrop.length - 1) await new Promise((resolve) => setTimeout(resolve, interval))
  }

  batchStatus.value = `${aborted ? '已停止' : '完成'}: 成功 ${ok}, 失败 ${fail}`
  ElMessage[aborted ? 'info' : (fail === 0 ? 'success' : 'warning')](`丢弃${aborted ? '已停止' : '完成'}: ${ok} 成功, ${fail} 失败`)
  batchDropping.value = false
  dropAbort.value = false
  // 丢完刷新一下背包,把空了的格子去掉
  await refreshBag()
}

async function doDropByName() {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  const q = dropByName.value.trim()
  if (!q) return

  // 实时从背包列表里挑;不依赖表格选中状态,也避免漏掉用户没勾的同名格子
  const toDrop = bagItems.value.filter((it) => (it.name || '') === q)
  if (toDrop.length === 0) {
    ElMessage.warning(`背包中没有名为 "${q}" 的物品`)
    return
  }

  const interval = Math.max(100, Number(dropIntervalMs.value) || 1000)

  try {
    await ElMessageBox.confirm(
      `将一格一格丢弃 ${toDrop.length} 件 "${q}"，间隔 ${interval}ms，确认？`,
      '确认丢弃同名物品',
      { confirmButtonText: '丢弃', cancelButtonText: '取消', type: 'warning' }
    )
  } catch { return }

  batchDropping.value = true
  dropAbort.value = false
  batchStatus.value = `丢弃中 (0/${toDrop.length})…`
  let ok = 0, fail = 0, aborted = false

  for (let idx = 0; idx < toDrop.length; idx++) {
    if (dropAbort.value) { aborted = true; break }
    const item = toDrop[idx]
    batchStatus.value = `丢弃中 (${idx + 1}/${toDrop.length}) ${item.name || item.itemId}…`
    try {
      const res = await fetch(`/api/command/${selectedPid.value}`, {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ action: 'dropBagItem', args: { bagId: item.bagId, count: item.count } }),
      })
      const data = await res.json()
      if (data.ok) ok++; else fail++
    } catch { fail++ }
    if (idx < toDrop.length - 1) await new Promise((resolve) => setTimeout(resolve, interval))
  }

  batchStatus.value = `${aborted ? '已停止' : '完成'}: 成功 ${ok}, 失败 ${fail}`
  ElMessage[aborted ? 'info' : (fail === 0 ? 'success' : 'warning')](`丢弃${aborted ? '已停止' : '完成'}: ${ok} 成功, ${fail} 失败`)
  batchDropping.value = false
  dropAbort.value = false
  await refreshBag()
}

function stopDrop() {
  if (!batchDropping.value) return
  dropAbort.value = true
  batchStatus.value = '正在停止…'
}

function onRowContext(row: BagItem, _col: any, event: MouseEvent) {
  event.preventDefault()
  const r = bagRecipient.value.trim()
  ElMessageBox.confirm(
    `邮寄 "${row.name || row.itemId}" x${row.count} 给 ${r || '(请先填收件人)'}？`,
    '确认邮寄',
    { confirmButtonText: '发送', cancelButtonText: '取消', type: 'info' }
  ).then(async () => {
    if (!r) { ElMessage.warning('请先填写收件人'); return }
    if (!selectedPid.value) return
    try {
      const res = await fetch(`/api/command/${selectedPid.value}`, {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ action: 'sendItemMail', args: { recipient: r, bagId: row.bagId, count: row.count } }),
      })
      const data = await res.json()
      if (data.ok) ElMessage.success('已发送')
      else ElMessage.error(`失败: ${data.detail || data.error}`)
    } catch (e: any) {
      ElMessage.error(`失败: ${e.message}`)
    }
  }).catch(() => {})
}

function appendAllMailLog(line: string) {
  const ts = new Date().toLocaleTimeString()
  allMailLog.value += `[${ts}] ${line}\n`
}

// 全号邮寄:遍历所有在线角色,把名称匹配 allMailItemName 的格子逐个邮给目标。
// 目标角色自身(若在线)跳过,避免自己邮给自己。每次邮寄之间(含跨角色)延迟
// allMailIntervalMs,满足"多格之间延迟 10s"且不刷爆邮寄命令。
async function doAllCharsMail() {
  const target = allMailRecipient.value.trim()
  const itemName = allMailItemName.value.trim()
  if (!target || target.length > 15 || /[^\x20-\x7e]/.test(target)) {
    ElMessage.warning('目标收件人无效（1-15 ASCII）'); return
  }
  if (!itemName) { ElMessage.warning('请填写物品名称'); return }

  // 在线角色,排除目标自己。
  const senders = instances.value.filter(
    (i) => !!i.characterName && i.characterName !== target
  )
  if (senders.length === 0) {
    ElMessage.warning(`没有可邮寄的在线角色（已排除目标 ${target}）`); return
  }

  const interval = Math.max(1000, Number(allMailIntervalMs.value) || 10000)
  const maxSlots = Math.max(0, Math.floor(Number(allMailMaxSlots.value) || 0))

  try {
    await ElMessageBox.confirm(
      `让 ${senders.length} 个在线角色把 "${itemName}" 邮寄给 ${target}` +
        `（每角色最多 ${maxSlots === 0 ? '全部' : maxSlots} 格，每次间隔 ${Math.round(interval / 1000)}s），确认？`,
      '确认全号邮寄',
      { confirmButtonText: '开始', cancelButtonText: '取消', type: 'warning' }
    )
  } catch { return }

  allMailSending.value = true
  allMailLog.value = ''
  let ok = 0, fail = 0, mailedChars = 0
  let firstMail = true

  try {
    for (const inst of senders) {
      const who = inst.characterName || `pid=${inst.pid}`
      allMailStatus.value = `处理 ${who}…`

      // 取该角色背包,找匹配格。
      let slots: BagItem[] = []
      try {
        const res = await fetch(`/api/command/${inst.pid}`, {
          method: 'POST',
          headers: { 'content-type': 'application/json' },
          body: JSON.stringify({ action: 'getBagItems', args: {} }),
        })
        const data = await res.json()
        if (!data.ok) { appendAllMailLog(`${who}: 读背包失败 (${data.detail || data.error || ''})`); continue }
        const all: BagItem[] = JSON.parse(data.detail || '[]')
        slots = all.filter((it) => (it.name || '') === itemName)
      } catch (e: any) {
        appendAllMailLog(`${who}: 读背包异常 (${e.message})`); continue
      }

      if (slots.length === 0) { appendAllMailLog(`${who}: 无 "${itemName}"，跳过`); continue }
      if (maxSlots > 0 && slots.length > maxSlots) slots = slots.slice(0, maxSlots)
      mailedChars++
      appendAllMailLog(`${who}: 匹配 ${slots.length} 格，开始邮寄 → ${target}`)

      for (const item of slots) {
        // 每次邮寄前(除第一次)等间隔,满足"多格之间延迟"。
        if (!firstMail) await new Promise((r) => setTimeout(r, interval))
        firstMail = false
        allMailStatus.value = `${who}: 邮寄 ${item.name || item.itemId} x${item.count}…`
        try {
          const res = await fetch(`/api/command/${inst.pid}`, {
            method: 'POST',
            headers: { 'content-type': 'application/json' },
            body: JSON.stringify({ action: 'sendItemMail', args: { recipient: target, bagId: item.bagId, count: item.count } }),
          })
          const data = await res.json()
          if (data.ok) { ok++; appendAllMailLog(`  ✓ ${who} ${item.name || item.itemId} x${item.count}`) }
          else { fail++; appendAllMailLog(`  ✗ ${who} ${item.name || item.itemId}: ${data.detail || data.error || ''}`) }
        } catch (e: any) {
          fail++; appendAllMailLog(`  ✗ ${who} ${item.name || item.itemId}: ${e.message}`)
        }
      }
    }

    allMailStatus.value = `完成: ${mailedChars} 角色, 成功 ${ok}, 失败 ${fail}`
    appendAllMailLog(allMailStatus.value)
    ElMessage[fail === 0 ? 'success' : 'warning'](`全号邮寄完成: ${ok} 成功, ${fail} 失败`)
    if (ok > 0) recipientHistory.push(target)
  } finally {
    allMailSending.value = false
  }
}
</script>

<style scoped>
.mail-view { padding: 8px; }
.bag-toolbar { display: flex; align-items: center; gap: 12px; }
.bag-count { font-size: 13px; color: var(--el-text-color-secondary); }
.bag-action-bar { display: flex; align-items: center; gap: 12px; margin-top: 10px; flex-wrap: wrap; }
.batch-status { font-size: 13px; color: var(--el-text-color-secondary); }
.bar-label { font-size: 13px; color: var(--el-text-color-secondary); }
.all-mail-hint {
  color: var(--el-text-color-secondary);
  font-size: 13px;
  line-height: 1.6;
  margin: 4px 0 12px;
  max-width: 760px;
}
.all-mail-log {
  max-height: 280px;
  overflow-y: auto;
  background: var(--el-fill-color-light);
  border: 1px solid var(--el-border-color);
  border-radius: 4px;
  padding: 10px 12px;
  font-size: 12px;
  line-height: 1.5;
  white-space: pre-wrap;
  margin: 4px 0;
  max-width: 760px;
}
</style>

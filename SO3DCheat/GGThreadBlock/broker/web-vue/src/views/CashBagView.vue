<template>
  <div class="cash-bag">
    <h3>商城背包</h3>
    <el-tabs v-model="activeSubTab" class="cash-tabs">
      <el-tab-pane label="当前角色" name="current">
        <el-button :loading="loading" @click="refresh" size="small" style="margin-bottom: 12px">
          刷新背包
        </el-button>

        <el-table :data="items" size="small" stripe v-loading="loading" max-height="300">
          <el-table-column prop="slotIndex" label="格" width="50" />
          <el-table-column prop="name" label="物品名" />
          <el-table-column prop="count" label="数量" width="60" />
          <el-table-column label="操作" width="200">
            <template #default="{ row }">
              <el-button size="small" @click="useItem(row)">使用</el-button>
              <el-button size="small" type="primary" @click="openSchedule(row)">定时</el-button>
            </template>
          </el-table-column>
        </el-table>

        <h3 style="margin-top: 24px">定时使用任务</h3>
        <el-table :data="schedules" size="small" stripe max-height="250">
          <el-table-column prop="characterName" label="角色" width="120" />
          <el-table-column prop="itemName" label="物品" />
          <el-table-column label="间隔" width="100">
            <template #default="{ row }">{{ formatInterval(row.intervalMs) }}</template>
          </el-table-column>
          <el-table-column label="上次使用" width="160">
            <template #default="{ row }">{{ row.lastUsed ? new Date(row.lastUsed).toLocaleString() : '从未' }}</template>
          </el-table-column>
          <el-table-column label="操作" width="80">
            <template #default="{ row }">
              <el-button size="small" type="danger" @click="removeSchedule(row)">删除</el-button>
            </template>
          </el-table-column>
        </el-table>
      </el-tab-pane>

      <el-tab-pane label="总览" name="overview">
        <div class="overview-toolbar">
          <el-button :loading="overviewLoading" @click="refreshOverview" size="small">
            刷新所有角色
          </el-button>
          <el-input
            v-model.trim="overviewFilter"
            placeholder="过滤物品名 / ItemId，例如：摊贩"
            clearable
            size="small"
            style="width: 260px"
          />
          <span class="overview-summary">
            {{ overviewRows.length }} 类，{{ overviewSlotCount }} 格，合计 {{ overviewTotalCount }} 个
          </span>
          <span v-if="overviewLastRefresh" class="overview-summary">
            {{ new Date(overviewLastRefresh).toLocaleTimeString() }}
          </span>
        </div>

        <el-table :data="overviewRows" size="small" stripe v-loading="overviewLoading" max-height="420">
          <el-table-column prop="itemName" label="物品名" min-width="220" />
          <el-table-column prop="itemIds" label="ItemId" width="150" />
          <el-table-column prop="characterCount" label="角色数" width="80" align="right" />
          <el-table-column prop="slotCount" label="格数" width="80" align="right" />
          <el-table-column prop="totalCount" label="总数量" width="100" align="right" />
          <el-table-column label="角色分布" min-width="260">
            <template #default="{ row }">
              <el-tag
                v-for="owner in row.characterCounts"
                :key="owner.characterName"
                size="small"
                class="owner-tag"
              >
                {{ owner.characterName }} x{{ owner.count }}
              </el-tag>
            </template>
          </el-table-column>
        </el-table>

        <h3 style="margin-top: 20px">角色刷新状态</h3>
        <el-table :data="overviewSnapshotRows" size="small" stripe max-height="220">
          <el-table-column prop="characterName" label="角色" min-width="160" />
          <el-table-column prop="accountName" label="账号" min-width="140" />
          <el-table-column label="状态" width="100">
            <template #default="{ row }">
              <el-tag :type="row.status === 'ok' ? 'success' : row.status === 'loading' ? 'info' : 'danger'" size="small">
                {{ row.status === 'ok' ? '完成' : row.status === 'loading' ? '刷新中' : '失败' }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column label="格数" width="80" align="right">
            <template #default="{ row }">{{ row.filteredSlotCount }}</template>
          </el-table-column>
          <el-table-column label="物品数量" width="100" align="right">
            <template #default="{ row }">{{ row.filteredTotalCount }}</template>
          </el-table-column>
          <el-table-column label="购买" width="90">
            <template #default="{ row }">
              <el-button
                size="small"
                type="primary"
                :disabled="row.status !== 'ok' || !row.accountName || row.purchaseOptions.length === 0"
                :loading="purchasingKey === row.characterName"
                @click="openOverviewPurchase(row)"
              >
                购买
              </el-button>
            </template>
          </el-table-column>
          <el-table-column prop="error" label="错误" min-width="220" />
        </el-table>
      </el-tab-pane>

      <el-tab-pane label="监控购买" name="monitor">
        <div class="overview-toolbar">
          <el-button :loading="monitorLoading" @click="loadMonitor" size="small">刷新</el-button>
          <el-button type="primary" size="small" @click="openMonitorDialog()">新增监控</el-button>
          <el-switch
            v-model="monitorConfig.autoEnabled"
            active-text="自动监控"
            @change="saveMonitorConfig"
          />
          <span class="overview-summary">轮询间隔</span>
          <el-select
            v-model="monitorConfig.pollIntervalMs"
            size="small"
            style="width: 110px"
            @change="saveMonitorConfig"
          >
            <el-option :value="30 * 1000" label="30 秒" />
            <el-option :value="60 * 1000" label="1 分钟" />
            <el-option :value="5 * 60 * 1000" label="5 分钟" />
            <el-option :value="15 * 60 * 1000" label="15 分钟" />
          </el-select>
        </div>

        <el-table :data="monitorEntries" size="small" stripe v-loading="monitorLoading" max-height="320">
          <el-table-column prop="characterName" label="角色" width="110" />
          <el-table-column prop="itemName" label="物品" min-width="120" />
          <el-table-column prop="threshold" label="阈值<" width="70" align="right" />
          <el-table-column prop="buyCount" label="购买量" width="80" align="right" />
          <el-table-column label="最小间隔" width="90">
            <template #default="{ row }">{{ formatInterval(row.minIntervalMs) }}</template>
          </el-table-column>
          <el-table-column label="启用" width="70">
            <template #default="{ row }">
              <el-switch v-model="row.enabled" @change="() => toggleEntry(row)" />
            </template>
          </el-table-column>
          <el-table-column label="上次触发" width="150">
            <template #default="{ row }">{{ row.lastTriggered ? new Date(row.lastTriggered).toLocaleString() : '从未' }}</template>
          </el-table-column>
          <el-table-column label="cash数量" width="80" align="right">
            <template #default="{ row }">{{ row.lastChecked ? row.lastCashCount : '-' }}</template>
          </el-table-column>
          <el-table-column label="上次探测" width="150">
            <template #default="{ row }">{{ row.lastChecked ? new Date(row.lastChecked).toLocaleString() : '从未' }}</template>
          </el-table-column>
          <el-table-column label="状态" min-width="170">
            <template #default="{ row }">
              <el-tag
                v-if="row.lastStatus"
                :type="row.lastStatus === 'ok' ? 'success' : row.lastStatus === 'running' ? 'info' : 'danger'"
                size="small"
              >
                {{ row.lastResult || row.lastStatus }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column label="操作" width="150">
            <template #default="{ row }">
              <el-button size="small" @click="openMonitorDialog(row)">编辑</el-button>
              <el-button size="small" type="danger" @click="removeMonitorEntry(row)">删除</el-button>
            </template>
          </el-table-column>
        </el-table>

        <h3 style="margin-top: 24px">手动测试(立即买并转移,不影响自动间隔)</h3>
        <el-form :inline="true" class="overview-toolbar">
          <el-form-item label="角色">
            <el-select v-model="manualCharacter" filterable placeholder="选择在线角色" style="width: 160px" size="small">
              <el-option v-for="inst in onlineCharacters" :key="inst.pid" :label="inst.label" :value="inst.characterName" />
            </el-select>
          </el-form-item>
          <el-form-item label="物品名">
            <el-input v-model.trim="manualItemName" placeholder="如 攤販呼叫券" style="width: 160px" size="small" />
          </el-form-item>
          <el-form-item label="数量">
            <el-input-number v-model="manualCount" :min="1" :step="1" size="small" style="width: 130px" />
          </el-form-item>
          <el-form-item>
            <el-button type="primary" :loading="manualRunning" @click="runManual" size="small">测试购买并转移</el-button>
          </el-form-item>
        </el-form>

        <el-table v-if="manualResult" :data="manualResult.steps" size="small" stripe max-height="240" style="margin-top: 8px">
          <el-table-column prop="action" label="步骤" width="170" />
          <el-table-column label="结果" width="70">
            <template #default="{ row }">
              <el-tag :type="row.ok ? 'success' : 'danger'" size="small">{{ row.ok ? 'OK' : 'X' }}</el-tag>
            </template>
          </el-table-column>
          <el-table-column prop="detail" label="详情" min-width="220" />
        </el-table>
        <div v-if="manualResult" class="overview-summary" style="margin-top: 6px">
          {{ manualResult.ok ? '成功' : '失败' }}: 买{{ manualResult.buyCount }} 搬{{ manualResult.moved }}
          cash {{ manualResult.cashBefore }}→{{ manualResult.cashAfter }}{{ manualResult.error ? ' | ' + manualResult.error : '' }}
        </div>
      </el-tab-pane>
    </el-tabs>

    <!-- Schedule dialog -->
    <el-dialog v-model="dlgVisible" title="设置定时使用" width="360px">
      <el-form label-width="80px">
        <el-form-item label="物品">{{ dlgItem?.name }} (格 {{ dlgItem?.slotIndex }})</el-form-item>
        <el-form-item label="间隔">
          <el-select v-model="dlgInterval" style="width: 100%">
            <el-option :value="30 * 60 * 1000" label="30 分钟" />
            <el-option :value="60 * 60 * 1000" label="1 小时" />
            <el-option :value="2 * 60 * 60 * 1000" label="2 小时" />
            <el-option :value="4 * 60 * 60 * 1000" label="4 小时" />
            <el-option :value="8 * 60 * 60 * 1000" label="8 小时" />
            <el-option :value="12 * 60 * 60 * 1000" label="12 小时" />
            <el-option :value="24 * 60 * 60 * 1000" label="24 小时" />
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dlgVisible = false">取消</el-button>
        <el-button type="primary" @click="confirmSchedule">确定</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="overviewPurchaseVisible" title="购买商城物品" width="420px">
      <el-form label-width="86px">
        <el-form-item label="角色">
          {{ overviewPurchaseTarget?.characterName || '-' }}
        </el-form-item>
        <el-form-item label="窗口账号">
          {{ overviewPurchaseTarget?.accountName || '-' }}
        </el-form-item>
        <el-form-item label="购买账号">
          <el-select
            v-model="overviewPurchaseUsername"
            filterable
            placeholder="选择泡点账号"
            style="width: 100%"
          >
            <el-option
              v-for="account in paodianAccounts"
              :key="account.username"
              :label="account.username"
              :value="account.username"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="物品">
          <el-select v-model="overviewPurchaseItemId" style="width: 100%">
            <el-option
              v-for="item in overviewPurchaseOptions"
              :key="item.itemId"
              :label="`${item.name} (${item.itemId})`"
              :value="item.itemId"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="数量">
          <el-input-number
            v-model="overviewPurchaseCount"
            :min="1"
            :step="1"
            style="width: 160px"
          />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="overviewPurchaseVisible = false">取消</el-button>
        <el-button type="primary" :loading="!!purchasingKey" @click="confirmOverviewPurchase">
          购买
        </el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="monitorDlgVisible" :title="monitorDlgEditing ? '编辑监控' : '新增监控'" width="440px">
      <el-form label-width="92px">
        <el-form-item label="角色">
          <el-input v-if="monitorDlgEditing" :model-value="monitorForm.characterName" disabled />
          <div v-else style="display: flex; gap: 8px; width: 100%">
            <el-select
              v-model="monitorCharacters"
              multiple
              filterable
              collapse-tags
              collapse-tags-tooltip
              placeholder="选择在线角色(可多选)"
              style="flex: 1"
            >
              <el-option v-for="inst in onlineCharacters" :key="inst.pid" :label="inst.label" :value="inst.characterName" />
            </el-select>
            <el-button @click="selectAllOnline">全选在线</el-button>
          </div>
        </el-form-item>
        <el-form-item label="物品名">
          <el-input v-model.trim="monitorForm.itemName" placeholder="如 攤販呼叫券" />
        </el-form-item>
        <el-form-item label="itemId">
          <el-input-number v-model="monitorForm.itemId" :min="0" :step="1" style="width: 160px" />
          <span class="overview-summary" style="margin-left: 8px">0=按名字在泡点表解析</span>
        </el-form-item>
        <el-form-item label="低于阈值">
          <el-input-number v-model="monitorForm.threshold" :min="0" :step="1" style="width: 160px" />
        </el-form-item>
        <el-form-item label="每次购买">
          <el-input-number v-model="monitorForm.buyCount" :min="1" :step="1" style="width: 160px" />
        </el-form-item>
        <el-form-item label="最小间隔">
          <el-select v-model="monitorForm.minIntervalMs" style="width: 160px">
            <el-option :value="60 * 60 * 1000" label="1 小时" />
            <el-option :value="6 * 60 * 60 * 1000" label="6 小时" />
            <el-option :value="12 * 60 * 60 * 1000" label="12 小时" />
            <el-option :value="24 * 60 * 60 * 1000" label="1 天" />
            <el-option :value="3 * 24 * 60 * 60 * 1000" label="3 天" />
            <el-option :value="7 * 24 * 60 * 60 * 1000" label="7 天" />
          </el-select>
        </el-form-item>
        <el-form-item label="启用">
          <el-switch v-model="monitorForm.enabled" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="monitorDlgVisible = false">取消</el-button>
        <el-button type="primary" @click="confirmMonitorEntry">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { useInstances } from '@/composables/useInstances'

const { instances, selectedPid, selectedInstance } = useInstances()

interface BagItem {
  bagId: number
  slotIndex: number
  itemId: number
  count: number
  name: string
}

interface ScheduleEntry {
  pid: number
  characterName: string
  itemName: string
  intervalMs: number
  lastUsed: number
}

interface CashBagSnapshot {
  pid: number
  characterName: string
  accountName?: string
  windowTitle?: string
  status: 'loading' | 'ok' | 'error'
  items: BagItem[]
  error?: string
}

interface CharacterItemCount {
  characterName: string
  count: number
  slots: number
}

interface OverviewRow {
  itemName: string
  itemIds: string
  totalCount: number
  slotCount: number
  characterCount: number
  characterCounts: CharacterItemCount[]
}

interface PurchaseOption {
  itemId: number
  name: string
  count: number
  slots: number
}

interface OverviewSnapshotRow extends CashBagSnapshot {
  filteredSlotCount: number
  filteredTotalCount: number
  purchaseOptions: PurchaseOption[]
}

interface PaodianAccount {
  username: string
}

interface MonitorEntry {
  characterName: string
  itemName: string
  itemId?: number
  threshold: number
  buyCount: number
  minIntervalMs: number
  enabled: boolean
  lastTriggered: number
  lastStatus: '' | 'ok' | 'error' | 'running'
  lastResult: string
  lastCashCount: number
  lastChecked: number
}

interface MonitorConfig {
  pollIntervalMs: number
  autoEnabled: boolean
}

interface RunStep {
  action: string
  ok: boolean
  detail?: string
}

interface RunResult {
  ok: boolean
  characterName: string
  account: string
  itemName: string
  itemId: number
  buyCount: number
  moved: number
  cashBefore: number
  cashAfter: number
  steps: RunStep[]
  error?: string
}

const activeSubTab = ref<'current' | 'overview' | 'monitor'>('current')
const items = ref<BagItem[]>([])
const allSchedules = ref<ScheduleEntry[]>([])
const loading = ref(false)
const overviewLoading = ref(false)
const overviewFilter = ref('')
const overviewSnapshots = ref<CashBagSnapshot[]>([])
const overviewLastRefresh = ref(0)
const overviewPurchaseVisible = ref(false)
const overviewPurchaseTarget = ref<OverviewSnapshotRow | null>(null)
const overviewPurchaseItemId = ref<number | null>(null)
const overviewPurchaseUsername = ref('')
const overviewPurchaseCount = ref(1)
const purchasingKey = ref('')
const paodianAccounts = ref<PaodianAccount[]>([])

// 监控购买
const monitorEntries = ref<MonitorEntry[]>([])
const monitorConfig = ref<MonitorConfig>({ pollIntervalMs: 60 * 1000, autoEnabled: true })
const monitorLoading = ref(false)
const monitorDlgVisible = ref(false)
const monitorDlgEditing = ref(false)
const monitorForm = ref<MonitorEntry>(emptyMonitorForm())
const monitorCharacters = ref<string[]>([]) // 新增模式下的多选角色
const manualCharacter = ref('')
const manualItemName = ref('')
const manualCount = ref(1)
const manualRunning = ref(false)
const manualResult = ref<RunResult | null>(null)

const onlineCharacters = computed(() =>
  instances.value
    .filter((inst) => inst.pid && inst.characterName)
    .map((inst) => ({
      pid: inst.pid,
      characterName: inst.characterName as string,
      label: inst.accountName ? `${inst.characterName} (${inst.accountName})` : (inst.characterName as string),
    }))
)

function emptyMonitorForm(): MonitorEntry {
  return {
    characterName: '',
    itemName: '',
    itemId: 0,
    threshold: 10,
    buyCount: 999,
    minIntervalMs: 24 * 60 * 60 * 1000,
    enabled: true,
    lastTriggered: 0,
    lastStatus: '',
    lastResult: '',
    lastCashCount: 0,
    lastChecked: 0,
  }
}

const schedules = computed(() => {
  const name = selectedInstance.value?.characterName
  if (!name) return allSchedules.value
  return allSchedules.value.filter((e) => e.characterName === name)
})

const dlgVisible = ref(false)
const dlgItem = ref<BagItem | null>(null)
const dlgInterval = ref(24 * 60 * 60 * 1000)

const overviewRows = computed<OverviewRow[]>(() => {
  const byName = new Map<string, {
    itemName: string
    itemIds: Set<number>
    totalCount: number
    slotCount: number
    characterCounts: Map<string, CharacterItemCount>
  }>()

  for (const snapshot of overviewSnapshots.value) {
    if (snapshot.status !== 'ok') continue
    for (const item of snapshot.items) {
      if (!matchesOverviewFilter(item)) continue
      const name = item.name || `Item ${item.itemId}`
      const count = normalizeCount(item.count)
      let row = byName.get(name)
      if (!row) {
        row = {
          itemName: name,
          itemIds: new Set<number>(),
          totalCount: 0,
          slotCount: 0,
          characterCounts: new Map<string, CharacterItemCount>(),
        }
        byName.set(name, row)
      }
      row.itemIds.add(item.itemId)
      row.totalCount += count
      row.slotCount += 1
      const owner = row.characterCounts.get(snapshot.characterName) || {
        characterName: snapshot.characterName,
        count: 0,
        slots: 0,
      }
      owner.count += count
      owner.slots += 1
      row.characterCounts.set(snapshot.characterName, owner)
    }
  }

  return Array.from(byName.values())
    .map((row) => ({
      itemName: row.itemName,
      itemIds: Array.from(row.itemIds).sort((a, b) => a - b).join(', '),
      totalCount: row.totalCount,
      slotCount: row.slotCount,
      characterCount: row.characterCounts.size,
      characterCounts: Array.from(row.characterCounts.values()).sort((a, b) => b.count - a.count || a.characterName.localeCompare(b.characterName)),
    }))
    .sort((a, b) => b.totalCount - a.totalCount || a.itemName.localeCompare(b.itemName))
})

const overviewTotalCount = computed(() => overviewRows.value.reduce((sum, row) => sum + row.totalCount, 0))
const overviewSlotCount = computed(() => overviewRows.value.reduce((sum, row) => sum + row.slotCount, 0))
const overviewSnapshotRows = computed<OverviewSnapshotRow[]>(() => overviewSnapshots.value.map((snapshot) => {
  const items = filteredItems(snapshot.items)
  return {
    ...snapshot,
    filteredSlotCount: items.length,
    filteredTotalCount: items.reduce((sum, item) => sum + normalizeCount(item.count), 0),
    purchaseOptions: buildPurchaseOptions(items),
  }
}))

async function refresh() {
  const pid = selectedPid.value
  if (!pid) { ElMessage.warning('未选择实例'); return }
  loading.value = true
  try {
    const res = await fetch(`/api/command/${pid}`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ action: 'getCashBagItems', args: {} }),
    })
    const r = await res.json()
    if (r.ok && r.detail) {
      items.value = JSON.parse(r.detail)
    } else {
      ElMessage.error(r.detail || r.error || '获取失败')
    }
    await refreshSchedules()
  } catch (e: any) {
    ElMessage.error(e.message)
  } finally {
    loading.value = false
  }
}

async function fetchCashBag(pid: number): Promise<BagItem[]> {
  const res = await fetch(`/api/command/${pid}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ action: 'getCashBagItems', args: {} }),
  })
  const r = await res.json()
  if (!r.ok) throw new Error(r.detail || r.error || '获取失败')
  return JSON.parse(r.detail || '[]')
}

async function readJson(res: Response) {
  const text = await res.text()
  let data: any = {}
  if (text) {
    try { data = JSON.parse(text) } catch { data = { error: text } }
  }
  if (!res.ok) throw new Error(data.error || data.message || res.statusText)
  return data
}

async function loadPaodianAccounts() {
  const res = await fetch('/api/paodian/accounts')
  paodianAccounts.value = await readJson(res)
}

async function refreshOverview() {
  const live = instances.value
    .filter((inst) => inst.pid)
    .map((inst) => ({
      pid: inst.pid,
      characterName: inst.characterName || `PID ${inst.pid}`,
      accountName: inst.accountName,
      windowTitle: inst.windowTitle,
    }))

  if (live.length === 0) {
    ElMessage.warning('暂无在线实例')
    overviewSnapshots.value = []
    return
  }

  overviewLoading.value = true
  overviewSnapshots.value = live.map((inst) => ({
    ...inst,
    status: 'loading',
    items: [],
  }))

  const snapshots = await Promise.all(live.map(async (inst): Promise<CashBagSnapshot> => {
    try {
      const items = await fetchCashBag(inst.pid)
      return { ...inst, status: 'ok', items }
    } catch (e: any) {
      return { ...inst, status: 'error', items: [], error: e.message || String(e) }
    }
  }))

  overviewSnapshots.value = snapshots.sort((a, b) => a.characterName.localeCompare(b.characterName))
  overviewLastRefresh.value = Date.now()
  overviewLoading.value = false
}

async function refreshSchedules() {
  try {
    const res = await fetch('/api/cash-schedule')
    allSchedules.value = await res.json()
  } catch { /* ignore */ }
}

async function useItem(row: BagItem) {
  const pid = selectedPid.value
  if (!pid) return
  try {
    const res = await fetch(`/api/command/${pid}`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ action: 'useCashItem', args: { slotIndex: row.slotIndex } }),
    })
    const r = await res.json()
    if (r.ok) ElMessage.success(`已使用: ${row.name}`)
    else ElMessage.error(r.detail || '失败')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

function openSchedule(row: BagItem) {
  dlgItem.value = row
  dlgInterval.value = 24 * 60 * 60 * 1000
  dlgVisible.value = true
}

async function confirmSchedule() {
  const pid = selectedPid.value
  if (!pid || !dlgItem.value) return
  const inst = selectedInstance.value
  if (!inst?.characterName) { ElMessage.warning('当前实例无角色名'); return }
  try {
    const res = await fetch('/api/cash-schedule', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        characterName: inst.characterName,
        itemName: dlgItem.value.name,
        intervalMs: dlgInterval.value,
      }),
    })
    const r = await res.json()
    if (r.ok) {
      ElMessage.success('定时任务已设置')
      dlgVisible.value = false
      await refreshSchedules()
    } else {
      ElMessage.error('设置失败')
    }
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function removeSchedule(row: ScheduleEntry) {
  try {
    await fetch('/api/cash-schedule', {
      method: 'DELETE',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ characterName: row.characterName, itemName: row.itemName }),
    })
    await refreshSchedules()
    ElMessage.success('已删除')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

function openOverviewPurchase(row: OverviewSnapshotRow) {
  if (row.purchaseOptions.length === 0) {
    ElMessage.warning('当前过滤条件下没有可购买的物品')
    return
  }
  if (!row.accountName) {
    ElMessage.warning('还没有解析到窗口账号,请稍后刷新')
    return
  }
  overviewPurchaseTarget.value = row
  overviewPurchaseItemId.value = row.purchaseOptions[0].itemId
  overviewPurchaseUsername.value =
    (row.accountName && paodianAccounts.value.find((a) => a.username === row.accountName)?.username) ||
    paodianAccounts.value[0]?.username ||
    ''
  overviewPurchaseCount.value = 1
  overviewPurchaseVisible.value = true
  if (paodianAccounts.value.length === 0) {
    loadPaodianAccounts()
      .then(() => {
        if (!overviewPurchaseUsername.value) {
          overviewPurchaseUsername.value =
            (row.accountName && paodianAccounts.value.find((a) => a.username === row.accountName)?.username) ||
            paodianAccounts.value[0]?.username ||
            ''
        }
      })
      .catch((e: any) => ElMessage.error(e.message))
  }
}

async function confirmOverviewPurchase() {
  const target = overviewPurchaseTarget.value
  const itemID = Number(overviewPurchaseItemId.value)
  const username = overviewPurchaseUsername.value.trim()
  const count = Math.max(1, Math.round(Number(overviewPurchaseCount.value) || 1))
  if (!target || !itemID) return
  if (!username) {
    ElMessage.warning('请选择泡点账号')
    return
  }

  purchasingKey.value = target.characterName
  try {
    const res = await fetch(`/api/paodian/purchase/${encodeURIComponent(username)}`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ itemID, itemCount: count }),
    })
    const result = await readJson(res)
    ElMessage.success(result.message || '购买成功')
    overviewPurchaseVisible.value = false
  } catch (e: any) {
    ElMessage.error(e.message)
  } finally {
    purchasingKey.value = ''
  }
}

// ---------- 监控购买 ----------

async function loadMonitor() {
  monitorLoading.value = true
  try {
    const [entriesRes, configRes] = await Promise.all([
      fetch('/api/purchase-monitor/entries'),
      fetch('/api/purchase-monitor/config'),
    ])
    monitorEntries.value = await readJson(entriesRes)
    monitorConfig.value = await readJson(configRes)
  } catch (e: any) {
    ElMessage.error(e.message)
  } finally {
    monitorLoading.value = false
  }
}

async function saveMonitorConfig() {
  try {
    const res = await fetch('/api/purchase-monitor/config', {
      method: 'PUT',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(monitorConfig.value),
    })
    monitorConfig.value = await readJson(res)
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

function openMonitorDialog(row?: MonitorEntry) {
  if (row) {
    monitorDlgEditing.value = true
    monitorForm.value = { ...row, itemId: row.itemId || 0 }
    monitorCharacters.value = []
  } else {
    monitorDlgEditing.value = false
    monitorForm.value = emptyMonitorForm()
    // 新增模式默认预选当前选中的角色(若有)
    monitorCharacters.value = selectedInstance.value?.characterName
      ? [selectedInstance.value.characterName]
      : []
  }
  monitorDlgVisible.value = true
}

function selectAllOnline() {
  monitorCharacters.value = onlineCharacters.value.map((inst) => inst.characterName)
}

async function confirmMonitorEntry() {
  const form = monitorForm.value
  if (!form.itemName) {
    ElMessage.warning('物品名必填')
    return
  }
  // 编辑模式只改这一条;新增模式可对多个角色批量创建相同配置。
  const targets = monitorDlgEditing.value ? [form.characterName] : monitorCharacters.value
  if (!targets.length) {
    ElMessage.warning('请至少选择一个角色')
    return
  }
  try {
    let ok = 0
    const errors: string[] = []
    for (const characterName of targets) {
      try {
        const res = await fetch('/api/purchase-monitor/entries', {
          method: 'POST',
          headers: { 'content-type': 'application/json' },
          body: JSON.stringify({
            characterName,
            itemName: form.itemName,
            itemId: form.itemId || undefined,
            threshold: form.threshold,
            buyCount: form.buyCount,
            minIntervalMs: form.minIntervalMs,
            enabled: form.enabled,
          }),
        })
        await readJson(res)
        ok++
      } catch (e: any) {
        errors.push(`${characterName}: ${e.message}`)
      }
    }
    if (ok > 0) ElMessage.success(`已保存 ${ok} 个监控`)
    if (errors.length) ElMessage.error(errors.join('; '))
    if (ok > 0) {
      monitorDlgVisible.value = false
      await loadMonitor()
    }
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function toggleEntry(row: MonitorEntry) {
  try {
    await fetch('/api/purchase-monitor/entries', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        characterName: row.characterName,
        itemName: row.itemName,
        itemId: row.itemId || undefined,
        threshold: row.threshold,
        buyCount: row.buyCount,
        minIntervalMs: row.minIntervalMs,
        enabled: row.enabled,
      }),
    })
  } catch (e: any) {
    ElMessage.error(e.message)
    await loadMonitor()
  }
}

async function removeMonitorEntry(row: MonitorEntry) {
  try {
    const q = new URLSearchParams({ characterName: row.characterName, itemName: row.itemName })
    await fetch(`/api/purchase-monitor/entries?${q.toString()}`, { method: 'DELETE' })
    ElMessage.success('已删除')
    await loadMonitor()
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

async function runManual() {
  if (!manualCharacter.value) { ElMessage.warning('请选择角色'); return }
  if (!manualItemName.value) { ElMessage.warning('请输入物品名'); return }
  manualRunning.value = true
  manualResult.value = null
  try {
    const res = await fetch('/api/purchase-monitor/run', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        characterName: manualCharacter.value,
        itemName: manualItemName.value,
        count: manualCount.value,
      }),
    })
    const result = await readJson(res)
    manualResult.value = result
    if (result.ok) ElMessage.success(`成功: 搬运 ${result.moved} 进 cash`)
    else ElMessage.error(result.error || '失败')
  } catch (e: any) {
    ElMessage.error(e.message)
  } finally {
    manualRunning.value = false
  }
}

function formatInterval(ms: number): string {
  const d = ms / (24 * 60 * 60 * 1000)
  if (d >= 1) return Number.isInteger(d) ? `${d}天` : `${d.toFixed(1)}天`
  const h = ms / (60 * 60 * 1000)
  if (h >= 1) return `${h}小时`
  return `${ms / (60 * 1000)}分钟`
}

function normalizeCount(count: number): number {
  return Number.isFinite(count) ? Math.max(1, Math.round(count)) : 1
}

function matchesOverviewFilter(item: BagItem): boolean {
  const q = overviewFilter.value.trim().toLowerCase()
  if (!q) return true
  const name = item.name || `Item ${item.itemId}`
  return `${name} ${item.itemId}`.toLowerCase().includes(q)
}

function filteredItems(items: BagItem[]): BagItem[] {
  return items.filter(matchesOverviewFilter)
}

function buildPurchaseOptions(items: BagItem[]): PurchaseOption[] {
  const byId = new Map<number, PurchaseOption>()
  for (const item of items) {
    const option = byId.get(item.itemId) || {
      itemId: item.itemId,
      name: item.name || `Item ${item.itemId}`,
      count: 0,
      slots: 0,
    }
    option.count += normalizeCount(item.count)
    option.slots += 1
    byId.set(item.itemId, option)
  }
  return Array.from(byId.values()).sort((a, b) => a.itemId - b.itemId)
}

const overviewPurchaseOptions = computed(() => overviewPurchaseTarget.value?.purchaseOptions || [])

onMounted(() => {
  if (selectedPid.value) refresh()
  loadPaodianAccounts().catch(() => { /* ignore */ })
})
watch(selectedPid, (v) => { if (v && activeSubTab.value === 'current') refresh() })
watch(activeSubTab, (tab) => {
  if (tab === 'overview' && overviewSnapshots.value.length === 0) refreshOverview()
  if (tab === 'monitor') loadMonitor()
})
</script>

<style scoped>
.cash-bag { padding: 8px; }
.cash-tabs { margin-top: 8px; }
.overview-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  margin-bottom: 12px;
}
.overview-summary {
  font-size: 13px;
  color: var(--el-text-color-secondary);
}
.owner-tag {
  margin-right: 6px;
  margin-bottom: 4px;
}
</style>

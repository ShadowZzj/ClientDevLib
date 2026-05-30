<template>
  <div class="paodian-view">
    <div class="toolbar">
      <el-form :inline="true" @submit.prevent>
        <el-form-item label="账号">
          <el-input
            v-model.trim="form.username"
            placeholder="username"
            clearable
            style="width: 180px"
          />
        </el-form-item>
        <el-form-item label="密码">
          <el-input
            v-model="form.password"
            type="password"
            show-password
            placeholder="password"
            style="width: 180px"
            @keyup.enter="addAccount"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="adding" @click="addAccount">添加/更新</el-button>
          <el-button :loading="refreshing" @click="refreshAll">刷新全部</el-button>
        </el-form-item>
        <el-form-item label="自动刷新(分钟)">
          <el-switch
            v-model="autoRefreshEnabled"
            active-text="开"
            inactive-text="关"
            style="margin-right: 12px"
          />
          <el-input-number
            v-model="refreshIntervalMinutes"
            :disabled="!autoRefreshEnabled"
            :min="1"
            :max="1440"
            :step="1"
            :precision="0"
            controls-position="right"
            style="width: 130px"
          />
          <el-button :loading="savingConfig" @click="saveConfig" style="margin-left: 8px">保存</el-button>
        </el-form-item>
      </el-form>
      <div class="meta">
        <span>账号 {{ accounts.length }}</span>
        <span>{{ refreshIntervalText }}</span>
      </div>
    </div>

    <el-table :data="accounts" stripe size="small" v-loading="loading" class="account-table">
      <el-table-column prop="username" label="账号" min-width="150" />
      <el-table-column label="泡点" width="120" align="right">
        <template #default="{ row }">
          <span class="paodian">{{ formatNumber(row.paodian) }}</span>
        </template>
      </el-table-column>
      <el-table-column prop="point" label="积分" width="90" align="right" />
      <el-table-column prop="vip" label="VIP" width="70" align="center" />
      <el-table-column prop="paymoney" label="充值" width="90" align="right" />
      <el-table-column label="下级 VIP" width="130">
        <template #default="{ row }">
          {{ row.next_vip_level || '-' }} / {{ row.next_vip_need || '-' }}
        </template>
      </el-table-column>
      <el-table-column label="状态" width="110">
        <template #default="{ row }">
          <el-tag :type="statusType(row.status)" size="small">
            {{ statusText(row.status) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="更新时间" width="170">
        <template #default="{ row }">
          {{ formatTime(row.lastUpdated) }}
        </template>
      </el-table-column>
      <el-table-column label="错误" min-width="180" show-overflow-tooltip>
        <template #default="{ row }">
          <span class="error-text">{{ row.error }}</span>
        </template>
      </el-table-column>
      <el-table-column label="操作" width="220" fixed="right">
        <template #default="{ row }">
          <el-button size="small" :loading="row.status === 'loading'" @click="refreshAccount(row.username)">
            刷新
          </el-button>
          <el-button size="small" type="primary" @click="openPurchase(row)">
            购买
          </el-button>
          <el-button size="small" type="danger" @click="removeAccount(row.username)">
            删除
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog
      v-model="purchaseDialogVisible"
      :title="purchaseAccount ? `购买 - ${purchaseAccount.username}` : '购买'"
      width="920px"
      destroy-on-close
    >
      <div class="purchase-toolbar">
        <el-input
          v-model.trim="shopSearch"
          placeholder="搜索 itemid / 名称 / 描述"
          clearable
          style="width: 320px"
        />
        <el-button :loading="shopLoading" @click="loadShopItems(true)">刷新商品</el-button>
        <span class="purchase-summary">共 {{ filteredShopItems.length }} 件</span>
      </div>
      <el-table
        :data="filteredShopItems"
        stripe
        size="small"
        v-loading="shopLoading"
        max-height="520"
      >
        <el-table-column prop="itemid" label="ID" width="80" />
        <el-table-column prop="name" label="名称" min-width="180" show-overflow-tooltip />
        <el-table-column prop="text" label="说明" min-width="260" show-overflow-tooltip />
        <el-table-column prop="bubble_price" label="泡点" width="90" align="right" />
        <el-table-column prop="price" label="价格" width="80" align="right" />
        <el-table-column label="数量" width="150">
          <template #default="{ row }">
            <el-input-number
              v-model="purchaseCounts[row.itemid]"
              :min="1"
              :max="999"
              :step="1"
              :precision="0"
              controls-position="right"
              style="width: 116px"
            />
          </template>
        </el-table-column>
        <el-table-column label="合计泡点" width="100" align="right">
          <template #default="{ row }">
            {{ formatNumber(row.bubble_price * getPurchaseCount(row.itemid)) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="100" fixed="right">
          <template #default="{ row }">
            <el-button
              size="small"
              type="primary"
              :loading="purchasingKey === row.itemid"
              @click="purchaseItem(row)"
            >
              购买
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'

interface PaodianAccount {
  username: string
  hasPassword: boolean
  id: string
  point: number
  paodian: number
  vip: number
  paymoney: number
  next_vip_level: number
  next_vip_need: number
  eps_endtime: string | null
  status: 'idle' | 'loading' | 'ok' | 'error'
  lastUpdated: number
  error: string
}

interface PaodianConfig {
  refreshIntervalMs: number
  autoRefreshEnabled: boolean
}

interface PaodianShopItem {
  itemid: number
  price: number
  bubble_price: number
  pr: string
  name: string
  text: string
  class: number
  pd: number
  qx: number
}

interface PaodianPurchaseResult {
  message: string
  account: PaodianAccount
}

const LIST_POLL_MS = 10 * 1000
const DEFAULT_REFRESH_INTERVAL_MS = 10 * 60 * 1000

const accounts = ref<PaodianAccount[]>([])
const loading = ref(false)
const adding = ref(false)
const refreshing = ref(false)
const savingConfig = ref(false)
const refreshIntervalMinutes = ref(DEFAULT_REFRESH_INTERVAL_MS / 60_000)
const autoRefreshEnabled = ref(true)
const form = reactive({ username: '', password: '' })
const purchaseDialogVisible = ref(false)
const purchaseAccount = ref<PaodianAccount | null>(null)
const shopItems = ref<PaodianShopItem[]>([])
const shopLoading = ref(false)
const shopSearch = ref('')
const purchaseCounts = reactive<Record<number, number>>({})
const purchasingKey = ref<number | null>(null)

let listTimer: number | undefined

const refreshIntervalText = computed(() => {
  if (!autoRefreshEnabled.value) return '仅手动刷新'
  const minutes = Math.max(1, Math.round(Number(refreshIntervalMinutes.value) || 1))
  if (minutes < 60) return `${minutes} 分钟自动刷新`
  const hours = minutes / 60
  return Number.isInteger(hours) ? `${hours} 小时自动刷新` : `${minutes} 分钟自动刷新`
})

const filteredShopItems = computed(() => {
  const q = shopSearch.value.trim().toLowerCase()
  if (!q) return shopItems.value
  return shopItems.value.filter((item) => {
    return String(item.itemid).includes(q) ||
      item.name.toLowerCase().includes(q) ||
      item.text.toLowerCase().includes(q)
  })
})

async function readJson(res: Response) {
  const text = await res.text()
  if (!res.ok) {
    try {
      const parsed = JSON.parse(text)
      throw new Error(parsed.error || text || `HTTP ${res.status}`)
    } catch (e: any) {
      if (e instanceof SyntaxError) throw new Error(text || `HTTP ${res.status}`)
      throw e
    }
  }
  return text ? JSON.parse(text) : null
}

async function loadAccounts(silent = false) {
  if (!silent) loading.value = true
  try {
    const res = await fetch('/api/paodian/accounts')
    accounts.value = await readJson(res)
  } catch (e: any) {
    if (!silent) ElMessage.error(e.message)
  } finally {
    if (!silent) loading.value = false
  }
}

async function loadConfig(silent = false) {
  try {
    const res = await fetch('/api/paodian/config')
    const cfg = await readJson(res) as PaodianConfig
    refreshIntervalMinutes.value = Math.max(1, Math.round((cfg.refreshIntervalMs || DEFAULT_REFRESH_INTERVAL_MS) / 60_000))
    autoRefreshEnabled.value = cfg.autoRefreshEnabled !== false
  } catch (e: any) {
    if (!silent) ElMessage.error(e.message)
  }
}

async function saveConfig() {
  const minutes = Math.max(1, Math.round(Number(refreshIntervalMinutes.value) || 1))
  refreshIntervalMinutes.value = minutes
  savingConfig.value = true
  try {
    const res = await fetch('/api/paodian/config', {
      method: 'PUT',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        refreshIntervalMs: minutes * 60_000,
        autoRefreshEnabled: autoRefreshEnabled.value,
      }),
    })
    const cfg = await readJson(res) as PaodianConfig
    refreshIntervalMinutes.value = Math.max(1, Math.round(cfg.refreshIntervalMs / 60_000))
    autoRefreshEnabled.value = cfg.autoRefreshEnabled !== false
    ElMessage.success('已保存')
  } catch (e: any) {
    ElMessage.error(e.message)
  } finally {
    savingConfig.value = false
  }
}

async function addAccount() {
  if (!form.username || !form.password) {
    ElMessage.warning('账号和密码都要填')
    return
  }
  adding.value = true
  try {
    const res = await fetch('/api/paodian/accounts', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ username: form.username, password: form.password }),
    })
    await readJson(res)
    ElMessage.success('已添加')
    form.password = ''
    await loadAccounts(true)
  } catch (e: any) {
    ElMessage.error(e.message)
  } finally {
    adding.value = false
  }
}

async function refreshAll() {
  refreshing.value = true
  try {
    const res = await fetch('/api/paodian/refresh', { method: 'POST' })
    accounts.value = await readJson(res)
    ElMessage.success('已刷新')
  } catch (e: any) {
    ElMessage.error(e.message)
  } finally {
    refreshing.value = false
  }
}

async function refreshAccount(username: string) {
  try {
    const res = await fetch(`/api/paodian/refresh/${encodeURIComponent(username)}`, { method: 'POST' })
    const updated = await readJson(res) as PaodianAccount
    const idx = accounts.value.findIndex((a) => a.username === username)
    if (idx >= 0) accounts.value[idx] = updated
    else accounts.value.push(updated)
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

function upsertAccount(account: PaodianAccount) {
  const idx = accounts.value.findIndex((a) => a.username === account.username)
  if (idx >= 0) accounts.value[idx] = account
  else accounts.value.push(account)
}

function getPurchaseCount(itemid: number) {
  return Math.max(1, Math.round(Number(purchaseCounts[itemid]) || 1))
}

async function loadShopItems(force = false) {
  shopLoading.value = true
  try {
    const suffix = force ? '?force=1' : ''
    const res = await fetch(`/api/paodian/shop-items${suffix}`)
    shopItems.value = await readJson(res)
    for (const item of shopItems.value) {
      if (!purchaseCounts[item.itemid]) purchaseCounts[item.itemid] = 1
    }
  } catch (e: any) {
    ElMessage.error(e.message)
  } finally {
    shopLoading.value = false
  }
}

async function openPurchase(account: PaodianAccount) {
  purchaseAccount.value = account
  purchaseDialogVisible.value = true
  if (shopItems.value.length === 0) {
    await loadShopItems()
  }
}

async function purchaseItem(item: PaodianShopItem) {
  if (!purchaseAccount.value) return
  const count = getPurchaseCount(item.itemid)
  const total = item.bubble_price * count
  try {
    await ElMessageBox.confirm(
      `使用 ${formatNumber(total)} 泡点购买 ${count} 个「${item.name}」？`,
      '确认购买',
      { type: 'warning' }
    )
  } catch {
    return
  }

  purchasingKey.value = item.itemid
  try {
    const res = await fetch(`/api/paodian/purchase/${encodeURIComponent(purchaseAccount.value.username)}`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ itemID: item.itemid, itemCount: count }),
    })
    const result = await readJson(res) as PaodianPurchaseResult
    upsertAccount(result.account)
    purchaseAccount.value = result.account
    ElMessage.success(result.message || '购买成功')
  } catch (e: any) {
    ElMessage.error(e.message)
  } finally {
    purchasingKey.value = null
  }
}

async function removeAccount(username: string) {
  try {
    await ElMessageBox.confirm(`删除账号 ${username}？`, '确认', { type: 'warning' })
  } catch {
    return
  }
  try {
    const res = await fetch(`/api/paodian/accounts/${encodeURIComponent(username)}`, { method: 'DELETE' })
    await readJson(res)
    await loadAccounts(true)
    ElMessage.success('已删除')
  } catch (e: any) {
    ElMessage.error(e.message)
  }
}

function statusType(status: PaodianAccount['status']) {
  if (status === 'ok') return 'success'
  if (status === 'error') return 'danger'
  if (status === 'loading') return 'warning'
  return 'info'
}

function statusText(status: PaodianAccount['status']) {
  if (status === 'ok') return '正常'
  if (status === 'error') return '失败'
  if (status === 'loading') return '刷新中'
  return '未刷新'
}

function formatNumber(value: number) {
  return Number(value || 0).toLocaleString('zh-CN')
}

function formatTime(value: number) {
  return value ? new Date(value).toLocaleString() : '-'
}

onMounted(() => {
  loadConfig()
  loadAccounts()
  listTimer = window.setInterval(() => loadAccounts(true), LIST_POLL_MS)
})

onUnmounted(() => {
  if (listTimer) window.clearInterval(listTimer)
})
</script>

<style scoped>
.paodian-view {
  padding: 8px;
}

.toolbar {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 12px;
}

.meta {
  display: flex;
  gap: 12px;
  color: var(--el-text-color-secondary);
  font-size: 12px;
  line-height: 32px;
  white-space: nowrap;
}

.account-table {
  width: 100%;
}

.purchase-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 12px;
}

.purchase-summary {
  color: var(--el-text-color-secondary);
  font-size: 12px;
}

.paodian {
  color: var(--el-color-warning);
  font-weight: 700;
  font-variant-numeric: tabular-nums;
}

.error-text {
  color: var(--el-color-danger);
}
</style>

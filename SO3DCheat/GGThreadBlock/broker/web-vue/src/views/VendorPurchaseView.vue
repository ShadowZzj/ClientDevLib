<template>
  <div class="vendor-purchase">
    <div class="toolbar">
      <el-select v-model="pid" placeholder="角色" size="small" class="pid-select">
        <el-option
          v-for="inst in instances"
          :key="inst.pid"
          :label="inst.characterName || `PID ${inst.pid}`"
          :value="inst.pid"
        />
      </el-select>
      <el-input v-model.trim="form.itemName" placeholder="物品名" size="small" class="item-input" />
      <label class="field">
        <span class="field-label">组数</span>
        <el-input-number
          v-model="form.groups"
          :min="1"
          :step="1"
          size="small"
          class="groups-input"
          aria-label="组数"
        />
      </label>
      <label class="field">
        <span class="field-label">每组数量</span>
        <el-input-number
          v-model="form.perGroupCount"
          :min="1"
          :step="1"
          size="small"
          class="count-input"
          aria-label="每组数量"
        />
      </label>
      <label class="field">
        <span class="field-label">现金格(可空)</span>
        <el-input-number
          v-model="form.summonSlotIndex"
          :min="0"
          :step="1"
          :controls="false"
          size="small"
          class="slot-input"
          aria-label="现金格"
        />
      </label>
      <el-button :loading="purchasing" type="primary" size="small" @click="purchase">
        <el-icon><Goods /></el-icon>
        打开摊贩购买
      </el-button>
      <el-button :loading="shopLoading" size="small" @click="refreshShopItems">
        <el-icon><Refresh /></el-icon>
        刷新摊贩商品
      </el-button>
    </div>

    <el-table :data="shopItems" size="small" stripe v-loading="shopLoading" max-height="360" @row-click="pickShopItem">
      <el-table-column prop="shopIndex" label="Index" width="80" />
      <el-table-column prop="itemId" label="ItemId" width="100" />
      <el-table-column prop="name" label="物品名" min-width="220" />
      <el-table-column prop="unitPrice" label="单价" width="100" align="right" />
      <el-table-column label="操作" width="90">
        <template #default="{ row }">
          <el-button size="small" @click.stop="pickShopItem(row)">填入</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-table v-if="lastResult" :data="lastResult.steps" size="small" stripe class="steps-table">
      <el-table-column prop="action" label="步骤" width="160" />
      <el-table-column label="结果" width="80">
        <template #default="{ row }">
          <el-tag :type="row.ok ? 'success' : 'danger'" size="small">{{ row.ok ? 'OK' : 'FAIL' }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="detail" label="详情" min-width="260" show-overflow-tooltip />
    </el-table>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, reactive, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { Goods, Refresh } from '@element-plus/icons-vue'
import { useInstances } from '@/composables/useInstances'

interface VendorConfigItem {
  itemName: string
  aliases?: string[]
  itemId?: number
  defaultCount: number
  vendorId?: number
  token?: string | number
}

interface VendorConfig {
  defaultVendorId: number
  summonSlotIndex?: number
  items: VendorConfigItem[]
}

interface VendorShopItem {
  shopIndex: number
  itemId: number
  unitPrice?: number
  name: string
}

interface VendorPurchaseResult {
  ok: boolean
  itemName: string
  itemId: number
  shopIndex: number
  count: number
  groups: number
  perGroupCount: number
  steps: Array<{ action: string; ok: boolean; detail?: string }>
}

const { instances, selectedPid } = useInstances()

const pid = computed({
  get: () => selectedPid.value,
  set: (value: number | null) => { selectedPid.value = value },
})

const config = ref<VendorConfig | null>(null)
const shopItems = ref<VendorShopItem[]>([])
const lastResult = ref<VendorPurchaseResult | null>(null)
const purchasing = ref(false)
const shopLoading = ref(false)

const form = reactive({
  itemName: '红标枪',
  itemId: undefined as number | undefined,
  shopIndex: undefined as number | undefined,
  groups: 1,
  perGroupCount: 300,
  vendorId: 2,
  summonSlotIndex: undefined as number | undefined,
})

onMounted(async () => {
  await loadConfig()
})

watch(selectedPid, (newPid, oldPid) => {
  if (newPid === oldPid) return
  shopItems.value = []
  lastResult.value = null
  form.itemId = undefined
  form.shopIndex = undefined
})

watch(() => form.itemName, (name) => {
  const picked = shopItems.value.find((it) => it.itemId === form.itemId && it.shopIndex === form.shopIndex)
  if (picked && normalizeName(picked.name) !== normalizeName(name)) {
    form.itemId = undefined
    form.shopIndex = undefined
  }
})

async function loadConfig() {
  const res = await fetch('/api/vendor-purchase/config')
  if (!res.ok) throw new Error(await res.text())
  const data = (await res.json()) as VendorConfig
  config.value = data
  const first = data.items?.[0]
  form.itemName = first?.itemName || form.itemName
  form.perGroupCount = first?.defaultCount || form.perGroupCount
  form.vendorId = first?.vendorId ?? data.defaultVendorId ?? form.vendorId
  form.summonSlotIndex = data.summonSlotIndex
}

async function refreshShopItems() {
  if (!pid.value) {
    ElMessage.warning('未选择角色')
    return
  }
  shopLoading.value = true
  try {
    const res = await fetch(`/api/command/${pid.value}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        action: 'getVendorShopItems',
        args: { vendorId: form.vendorId },
      }),
    })
    const data = await res.json()
    if (!res.ok || !data.ok) throw new Error(data.error || data.detail || `HTTP ${res.status}`)
    shopItems.value = parseJson<VendorShopItem[]>(data.detail, [])
    const matched = findShopItemByName(form.itemName)
    if (matched) applyShopItem(matched)
  } catch (e: any) {
    ElMessage.error(e.message || String(e))
  } finally {
    shopLoading.value = false
  }
}

async function purchase() {
  if (!pid.value) {
    ElMessage.warning('未选择角色')
    return
  }
  if (!form.itemName.trim()) {
    ElMessage.warning('请输入物品名')
    return
  }
  purchasing.value = true
  try {
    const payload: Record<string, unknown> = {
      itemName: form.itemName.trim(),
      groups: form.groups,
      perGroupCount: form.perGroupCount,
      vendorId: form.vendorId,
    }
    const shopItem = currentOrMatchedShopItem()
    if (shopItem) {
      form.itemId = shopItem.itemId
      form.shopIndex = shopItem.shopIndex
      payload.itemId = shopItem.itemId
      payload.shopIndex = shopItem.shopIndex
    }
    if (form.summonSlotIndex !== undefined && form.summonSlotIndex !== null) {
      payload.summonSlotIndex = form.summonSlotIndex
    }
    const res = await fetch(`/api/vendor-purchase/${pid.value}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    })
    const data = await res.json()
    if (!res.ok || !data.ok) {
      if (Array.isArray(data.steps)) {
        lastResult.value = {
          ok: false,
          itemName: form.itemName.trim(),
          itemId: form.itemId ?? 0,
          shopIndex: form.shopIndex ?? 0,
          count: form.groups * form.perGroupCount,
          groups: form.groups,
          perGroupCount: form.perGroupCount,
          steps: data.steps,
        }
      }
      throw new Error(data.error || `HTTP ${res.status}`)
    }
    lastResult.value = data as VendorPurchaseResult
    ElMessage.success(`${data.itemName} ${data.groups ?? form.groups}组 x${data.perGroupCount ?? form.perGroupCount}`)
  } catch (e: any) {
    ElMessage.error(e.message || String(e))
  } finally {
    purchasing.value = false
  }
}

function pickShopItem(row: VendorShopItem) {
  applyShopItem(row)
}

function applyShopItem(row: VendorShopItem) {
  form.itemId = row.itemId
  form.shopIndex = row.shopIndex
  form.itemName = row.name || String(row.itemId)
}

function currentOrMatchedShopItem(): VendorShopItem | undefined {
  if (form.itemId !== undefined && form.shopIndex !== undefined) {
    return shopItems.value.find((it) => it.itemId === form.itemId && it.shopIndex === form.shopIndex) ?? {
      itemId: form.itemId,
      shopIndex: form.shopIndex,
      name: form.itemName.trim(),
    }
  }
  return findShopItemByName(form.itemName)
}

function findShopItemByName(name: string): VendorShopItem | undefined {
  const needle = normalizeName(name)
  if (!needle) return undefined
  return shopItems.value.find((it) => {
    const shopName = normalizeName(it.name || '')
    return shopName === needle || shopName.includes(needle) || needle.includes(shopName)
  })
}

function parseJson<T>(raw: unknown, fallback: T): T {
  try {
    if (typeof raw !== 'string') return fallback
    return JSON.parse(raw) as T
  } catch {
    return fallback
  }
}

function normalizeName(value: string): string {
  return String(value || '')
    .normalize('NFKC')
    .trim()
    .toLocaleLowerCase()
    .replace(/\s+/g, '')
    .replace(/紅/g, '红')
    .replace(/標/g, '标')
    .replace(/槍/g, '枪')
    .replace(/捲/g, '卷')
    .replace(/軸/g, '轴')
    .replace(/級/g, '级')
    .replace(/藍/g, '蓝')
    .replace(/綠/g, '绿')
}
</script>

<style scoped>
.vendor-purchase {
  display: flex;
  flex-direction: column;
  gap: 14px;
  min-width: 0;
}
.toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}
.field {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  white-space: nowrap;
}
.field-label {
  color: var(--el-text-color-regular);
  font-size: 12px;
  line-height: 1;
}
.pid-select {
  width: 160px;
}
.item-input {
  width: 220px;
}
.groups-input,
.count-input {
  width: 120px;
}
.slot-input {
  width: 90px;
}
.steps-table {
  margin-top: 6px;
}
</style>

<template>
  <div class="cash-bag">
    <h3>商城背包</h3>
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
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { useInstances } from '@/composables/useInstances'

const { selectedPid, selectedInstance } = useInstances()

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

const items = ref<BagItem[]>([])
const allSchedules = ref<ScheduleEntry[]>([])
const loading = ref(false)

const schedules = computed(() => {
  const name = selectedInstance.value?.characterName
  if (!name) return allSchedules.value
  return allSchedules.value.filter((e) => e.characterName === name)
})

const dlgVisible = ref(false)
const dlgItem = ref<BagItem | null>(null)
const dlgInterval = ref(24 * 60 * 60 * 1000)

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

function formatInterval(ms: number): string {
  const h = ms / (60 * 60 * 1000)
  if (h >= 1) return `${h}小时`
  return `${ms / (60 * 1000)}分钟`
}

onMounted(() => { if (selectedPid.value) refresh() })
watch(selectedPid, (v) => { if (v) refresh() })
</script>

<style scoped>
.cash-bag { padding: 8px; }
</style>

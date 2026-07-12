<template>
  <div class="daily-task-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>每日猎杀任务</span>
          <el-button size="small" :loading="loading" @click="refresh()">刷新</el-button>
        </div>
      </template>

      <el-alert type="info" :closable="false" show-icon class="description">
        <template #title>
          只处理任务列表后五个、且游戏明确标记为猎杀及可更换的任务。每个动作确认后会立即重新查询并继续：
          先完成已达成任务，再从未领取任务中随机更换当前最差等级；次数耗尽或全部达到最低等级后，连续逐个领取。
          自动动作时请关闭游戏内每日任务窗口；检测到窗口打开会暂停，避免与手动更换并发。
        </template>
      </el-alert>

      <el-form label-width="120px">
        <el-form-item label="自动处理角色">
          <el-select
            v-model="selectedNames"
            multiple
            filterable
            allow-create
            default-first-option
            placeholder="选择或输入角色名"
            style="width: 100%; max-width: 620px"
            @change="selectionDirty = true"
          >
            <el-option
              v-for="option in characterOptions"
              :key="option.value"
              :label="option.label"
              :value="option.value"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="新角色最低等级">
          <el-select v-model="defaultMinGrade" style="width: 160px">
            <el-option v-for="grade in grades" :key="grade" :label="grade" :value="grade" />
          </el-select>
          <span class="hint">SSS 最强，C 最低。已有角色可在下方单独调整。</span>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="applying" @click="applySelection">应用</el-button>
          <span class="hint">取消勾选并应用，会关闭对应角色的自动处理。</span>
        </el-form-item>
      </el-form>
    </el-card>

    <el-card shadow="never">
      <template #header>
        <div class="card-header">
          <span>角色与任务状态</span>
          <span class="hint">状态每 3 秒刷新</span>
        </div>
      </template>

      <el-empty v-if="configs.length === 0" description="还没有每日任务配置" :image-size="72" />
      <el-table v-else :data="configs" border size="small" style="width: 100%">
        <el-table-column label="角色" min-width="130">
          <template #default="{ row }">
            <span>{{ row.characterName }}</span>
            <el-tag
              size="small"
              :type="isOnline(row.characterName) ? 'success' : 'info'"
              effect="plain"
              class="online-tag"
            >
              {{ isOnline(row.characterName) ? '在线' : '离线' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="开启" width="64" align="center">
          <template #default="{ row }">
            <el-switch
              :model-value="row.enabled"
              size="small"
              @change="(value: boolean) => updateConfig(row.characterName, { enabled: value })"
            />
          </template>
        </el-table-column>
        <el-table-column label="最低等级" width="96">
          <template #default="{ row }">
            <el-select
              :model-value="row.minGrade"
              size="small"
              @change="(value: DailyTaskGrade) => updateConfig(row.characterName, { minGrade: value })"
            >
              <el-option v-for="grade in grades" :key="grade" :label="grade" :value="grade" />
            </el-select>
          </template>
        </el-table-column>
        <el-table-column label="剩余更换" width="82" align="center">
          <template #default="{ row }">{{ row.changeChance }}</template>
        </el-table-column>
        <el-table-column label="后五个猎杀任务" min-width="300">
          <template #default="{ row }">
            <div v-if="row.tasks.length" class="task-list">
              <el-tooltip
                v-for="task in huntTasks(row)"
                :key="task.index"
                :content="`任务 ${task.taskId} · ${stateLabel(task.state)} · 进度 ${task.progress}`"
                placement="top"
              >
                <el-tag :type="gradeTagType(task.gradeRaw)" effect="plain" class="task-tag">
                  #{{ task.index + 1 }} {{ gradeLabel(task) }} · {{ stateLabel(task.state) }}
                </el-tag>
              </el-tooltip>
            </div>
            <span v-else class="hint">尚未读取</span>
          </template>
        </el-table-column>
        <el-table-column label="运行状态" min-width="180">
          <template #default="{ row }">
            <div class="runtime-cell">
              <el-tag size="small" :type="statusTagType(row.status)">{{ statusLabel(row.status) }}</el-tag>
              <span class="result-text">{{ row.lastResult || '—' }}</span>
              <span v-if="row.lastRunAt" class="time-text">{{ formatTime(row.lastRunAt) }}</span>
            </div>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="135">
          <template #default="{ row }">
            <el-button
              link
              type="primary"
              size="small"
              :disabled="!isOnline(row.characterName)"
              :loading="runningName === row.characterName"
              @click="runNow(row.characterName)"
            >立即执行一轮</el-button>
            <el-button link type="danger" size="small" @click="removeConfig(row.characterName)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { useInstances } from '@/composables/useInstances'

const grades = ['SSS', 'SS', 'S', 'A', 'B', 'C'] as const
type DailyTaskGrade = (typeof grades)[number]

interface DailyTaskEntry {
  index: number
  taskId: number
  state: number
  progress: number
  gradeRaw: number
  gradeName: string
  replaceable: boolean
  hunt: boolean
}

interface DailyTaskConfig {
  characterName: string
  enabled: boolean
  minGrade: DailyTaskGrade
  status: string
  lastAction: string
  lastResult: string
  lastRunAt: number
  changeChance: number
  completedCount: number
  tasks: DailyTaskEntry[]
}

const { instances } = useInstances()
const configs = ref<DailyTaskConfig[]>([])
const selectedNames = ref<string[]>([])
const defaultMinGrade = ref<DailyTaskGrade>('SS')
const loading = ref(false)
const applying = ref(false)
const runningName = ref('')
const selectionDirty = ref(false)
let pollTimer: ReturnType<typeof setInterval> | null = null

const characterOptions = computed(() => {
  const names = new Set<string>()
  for (const instance of instances.value) if (instance.characterName) names.add(instance.characterName)
  for (const config of configs.value) names.add(config.characterName)
  return Array.from(names)
    .sort((a, b) => a.localeCompare(b))
    .map((name) => ({
      value: name,
      label: `${name} (${isOnline(name) ? '在线' : '离线'})`,
    }))
})

async function refresh(showLoading = true): Promise<void> {
  if (showLoading) loading.value = true
  try {
    const response = await fetch('/api/daily-task/configs')
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    const value = await response.json()
    configs.value = Array.isArray(value) ? value : []
    if (!selectionDirty.value) {
      selectedNames.value = configs.value.filter((config) => config.enabled).map((config) => config.characterName)
    }
  } catch (e: any) {
    if (showLoading) ElMessage.error(`加载每日任务配置失败: ${e.message}`)
  } finally {
    if (showLoading) loading.value = false
  }
}

async function patchConfig(name: string, body: Partial<Pick<DailyTaskConfig, 'enabled' | 'minGrade'>>): Promise<void> {
  const response = await fetch(`/api/daily-task/configs/${encodeURIComponent(name)}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
  if (!response.ok) throw new Error(`HTTP ${response.status}`)
}

async function applySelection(): Promise<void> {
  applying.value = true
  try {
    const selected = new Set(selectedNames.value)
    const names = new Set([...selected, ...configs.value.map((config) => config.characterName)])
    const changes: Promise<void>[] = []
    for (const name of names) {
      const current = configs.value.find((config) => config.characterName === name)
      const enabled = selected.has(name)
      if (!current && enabled) changes.push(patchConfig(name, { enabled: true, minGrade: defaultMinGrade.value }))
      else if (current && current.enabled !== enabled) changes.push(patchConfig(name, { enabled }))
    }
    await Promise.all(changes)
    selectionDirty.value = false
    await refresh(false)
    ElMessage.success('每日任务配置已应用')
  } catch (e: any) {
    ElMessage.error(`应用失败: ${e.message}`)
  } finally {
    applying.value = false
  }
}

async function updateConfig(
  name: string,
  body: Partial<Pick<DailyTaskConfig, 'enabled' | 'minGrade'>>
): Promise<void> {
  try {
    await patchConfig(name, body)
    await refresh(false)
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
    await refresh(false)
  }
}

async function runNow(name: string): Promise<void> {
  runningName.value = name
  try {
    const response = await fetch(`/api/daily-task/run-now/${encodeURIComponent(name)}`, { method: 'POST' })
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    const result = await response.json()
    if (result.ok) ElMessage.success(result.detail || '本轮执行完成')
    else ElMessage.warning(result.detail || '本轮没有执行动作')
    await refresh(false)
  } catch (e: any) {
    ElMessage.error(`执行失败: ${e.message}`)
  } finally {
    runningName.value = ''
  }
}

async function removeConfig(name: string): Promise<void> {
  try {
    const response = await fetch(`/api/daily-task/configs/${encodeURIComponent(name)}`, { method: 'DELETE' })
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    await refresh(false)
  } catch (e: any) {
    ElMessage.error(`删除失败: ${e.message}`)
  }
}

function isOnline(name: string): boolean {
  return instances.value.some((instance) => instance.characterName === name)
}

function huntTasks(config: DailyTaskConfig): DailyTaskEntry[] {
  return config.tasks.filter((task) => task.index >= 3 && task.index <= 7)
}

function gradeLabel(task: DailyTaskEntry): string {
  return task.gradeName || grades[task.gradeRaw] || '?'
}

function stateLabel(state: number): string {
  return ['未领取', '进行中', '可完成', '已完成'][state] || `状态 ${state}`
}

function gradeTagType(raw: number): 'danger' | 'warning' | 'success' | 'info' {
  if (raw === 0) return 'danger'
  if (raw === 1) return 'warning'
  if (raw === 2) return 'success'
  return 'info'
}

function statusLabel(status: string): string {
  const labels: Record<string, string> = {
    idle: '等待',
    offline: '离线',
    running: '运行中',
    done: '已完成',
    unsafe: '安全检查失败',
    error: '失败',
    uncertain: '结果待确认',
  }
  return labels[status] || status
}

function statusTagType(status: string): 'success' | 'warning' | 'danger' | 'info' | 'primary' {
  if (status === 'done') return 'success'
  if (status === 'running') return 'primary'
  if (status === 'unsafe' || status === 'error') return 'danger'
  if (status === 'uncertain') return 'warning'
  return 'info'
}

function formatTime(timestamp: number): string {
  return new Date(timestamp).toLocaleString()
}

onMounted(async () => {
  await refresh()
  pollTimer = setInterval(() => refresh(false), 3000)
})

onUnmounted(() => {
  if (pollTimer !== null) clearInterval(pollTimer)
})
</script>

<style scoped>
.daily-task-view {
  max-width: 1280px;
}
.config-card {
  margin-bottom: 16px;
}
.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.description {
  margin-bottom: 18px;
  line-height: 1.6;
}
.hint,
.time-text {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-left: 10px;
}
.online-tag {
  margin-left: 6px;
}
.task-list {
  display: flex;
  flex-wrap: wrap;
  gap: 5px;
}
.task-tag {
  cursor: default;
}
.runtime-cell {
  display: flex;
  align-items: flex-start;
  flex-direction: column;
  gap: 4px;
}
.result-text {
  color: var(--el-text-color-regular);
  font-size: 12px;
  line-height: 1.4;
}
.time-text {
  margin-left: 0;
}
</style>

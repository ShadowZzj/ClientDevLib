<template>
  <div class="online-whitelist">
    <div class="page-head">
      <h3>在线白名单</h3>
      <div class="actions">
        <el-button :loading="loading" @click="load">刷新</el-button>
        <el-button type="primary" :loading="saving" @click="save">保存</el-button>
        <el-button :loading="syncing" @click="syncNow">立即同步</el-button>
      </div>
    </div>

    <el-form :model="form" label-width="150px" class="config-form">
      <el-form-item label="启用同步">
        <el-switch v-model="form.enabled" />
      </el-form-item>
      <el-form-item label="中心服务器">
        <el-input v-model="form.serverUrl" placeholder="http://81.70.213.20:8787" />
      </el-form-item>
      <el-form-item label="主机名">
        <el-input v-model="form.hostName" />
      </el-form-item>
      <el-form-item label="上报间隔(ms)">
        <el-input-number v-model="form.pushIntervalMs" :min="1000" :step="1000" />
      </el-form-item>
      <el-form-item label="拉取间隔(ms)">
        <el-input-number v-model="form.pullIntervalMs" :min="1000" :step="1000" />
      </el-form-item>
      <el-form-item label="白名单有效期(ms)">
        <el-input-number v-model="form.whitelistTtlMs" :min="1000" :step="1000" />
      </el-form-item>
      <el-form-item label="同步文件">
        <el-input v-model="form.outputFile" />
      </el-form-item>
    </el-form>

    <el-divider />

    <div class="status-grid">
      <div>
        <span>配置文件</span>
        <code>{{ status.configFile || '-' }}</code>
      </div>
      <div>
        <span>运行状态</span>
        <el-tag :type="status.running ? 'success' : 'info'" size="small">
          {{ status.running ? '运行中' : '未运行' }}
        </el-tag>
      </div>
      <div>
        <span>上次上报</span>
        <code>{{ fmtTime(status.lastPushAt) }}</code>
      </div>
      <div>
        <span>上次拉取</span>
        <code>{{ fmtTime(status.lastPullOkAt) }}</code>
      </div>
      <div v-if="status.lastError" class="full">
        <span>最近错误</span>
        <code>{{ status.lastError }}</code>
      </div>
    </div>

    <el-divider />

    <h4>当前文件中的白名单</h4>
    <el-table :data="characters" size="small" stripe max-height="420">
      <el-table-column prop="name" label="角色" min-width="150" />
      <el-table-column prop="host" label="主机" min-width="140" />
      <el-table-column label="剩余有效期" width="130" align="right">
        <template #default="{ row }">{{ remainingMs(row) }} ms</template>
      </el-table-column>
      <el-table-column label="过期时间" width="190">
        <template #default="{ row }">{{ fmtTime(row.expiresAt) }}</template>
      </el-table-column>
      <el-table-column prop="pid" label="PID" width="90" />
    </el-table>
  </div>
</template>

<script setup lang="ts">
import { ElMessage } from 'element-plus'
import { onMounted, onUnmounted, reactive, ref } from 'vue'

interface OnlineWhitelistConfig {
  enabled: boolean
  serverUrl: string
  hostName: string
  pushIntervalMs: number
  pullIntervalMs: number
  whitelistTtlMs: number
  outputFile: string
}

interface StatusSnapshot {
  configFile?: string
  outputFile?: string
  running?: boolean
  lastPushAt?: number
  lastPullAt?: number
  lastPullOkAt?: number
  lastError?: string
}

interface FileCharacter {
  host?: string
  name?: string
  pid?: number
  expiresAt?: number
}

const form = reactive<OnlineWhitelistConfig>({
  enabled: true,
  serverUrl: '',
  hostName: '',
  pushIntervalMs: 10000,
  pullIntervalMs: 15000,
  whitelistTtlMs: 30000,
  outputFile: '',
})
const status = reactive<StatusSnapshot>({})
const characters = ref<FileCharacter[]>([])
const loading = ref(false)
const saving = ref(false)
const syncing = ref(false)
let timer: number | undefined

async function load() {
  loading.value = true
  try {
    const res = await fetch('/api/online-whitelist')
    if (!res.ok) throw new Error(await res.text())
    const data = await res.json()
    Object.assign(form, data.config || {})
    Object.assign(status, data.status || {})
    characters.value = Array.isArray(data.file?.characters) ? data.file.characters : []
  } finally {
    loading.value = false
  }
}

async function save() {
  saving.value = true
  try {
    const res = await fetch('/api/online-whitelist', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(form),
    })
    if (!res.ok) throw new Error(await res.text())
    ElMessage.success('已保存')
    await load()
  } finally {
    saving.value = false
  }
}

async function syncNow() {
  syncing.value = true
  try {
    const res = await fetch('/api/online-whitelist/sync-now', { method: 'POST' })
    if (!res.ok) throw new Error(await res.text())
    await load()
  } finally {
    syncing.value = false
  }
}

function remainingMs(row: FileCharacter): number {
  const expiresAt = Number(row.expiresAt || 0)
  return Math.max(0, Math.round(expiresAt - Date.now()))
}

function fmtTime(ts?: number): string {
  if (!ts) return '-'
  return new Date(ts).toLocaleString()
}

onMounted(async () => {
  await load()
  timer = window.setInterval(load, 5000)
})

onUnmounted(() => {
  if (timer) window.clearInterval(timer)
})
</script>

<style scoped>
.online-whitelist {
  max-width: 980px;
}
.page-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}
.actions {
  display: flex;
  gap: 8px;
}
.config-form {
  max-width: 780px;
}
.status-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px 24px;
}
.status-grid > div {
  min-width: 0;
}
.status-grid .full {
  grid-column: 1 / -1;
}
.status-grid span {
  display: block;
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-bottom: 4px;
}
code {
  word-break: break-all;
}
</style>

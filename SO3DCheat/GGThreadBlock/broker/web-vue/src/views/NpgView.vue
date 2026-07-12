<template>
  <div class="npg-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>附近玩家停手 — 全局共享配置(所有角色共用一份)</span>
          <el-tag :type="config.enabled ? 'success' : 'info'" size="small">
            {{ config.enabled ? '已启用' : '已停用' }}
          </el-tag>
        </div>
      </template>

      <p class="hint">
        附近(视野/距离阈值内)出现非白名单玩家时,自动把加速类模块拍暂停;人走后<strong>保持停手</strong>一段时间再恢复。
        期间只要再看到人就<strong>刷新计时</strong>——这样有人来回卡视野时,缝隙里不会出现短暂加速被旁人看出来。
        改完<strong>立即推给所有在线实例</strong>;新上线的实例也会自动补推。
      </p>

      <el-form label-width="140px" size="default" style="max-width: 760px">
        <el-form-item label="总开关">
          <el-switch v-model="config.enabled" />
          <span class="hint">关掉则所有角色都不再扫描、不再自动停手。</span>
        </el-form-item>

        <el-form-item label="保持停手时长">
          <el-input-number v-model="config.holdSeconds" :min="0" :max="7200" :step="30" />
          <span class="hint">秒。看到人后至少停手这么久;期间再看到人会刷新计时。300 = 5 分钟,0 = 无人即恢复(旧行为)。</span>
        </el-form-item>

        <el-form-item>
          <el-button type="primary" @click="save" :loading="saving">保存并下发</el-button>
          <el-button @click="reload">重新加载</el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <el-card shadow="never" class="status-card" style="margin-top: 12px">
      <template #header>
        <div class="card-header">
          <span>各实例运行状态</span>
          <el-button size="small" text @click="refreshStatus" :loading="statusLoading">刷新</el-button>
        </div>
      </template>
      <el-table :data="statusRows" size="small" v-loading="statusLoading">
        <el-table-column label="角色" min-width="140">
          <template #default="{ row }">
            {{ row.characterName || `pid=${row.pid}` }}
          </template>
        </el-table-column>
        <el-table-column label="启用" width="80">
          <template #default="{ row }">
            <el-tag size="small" :type="row.enabled ? 'success' : 'info'">
              {{ row.enabled ? '是' : '否' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="状态" width="160">
          <template #default="{ row }">
            <el-tag size="small" :type="stateTagType(row)">{{ stateText(row) }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="附近人数" width="90" prop="playerCount" />
        <el-table-column label="保持时长(秒)" width="120" prop="holdSeconds" />
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useInstances } from '@/composables/useInstances'
import { ElMessage } from 'element-plus'

interface NpgConfig {
  enabled: boolean
  holdSeconds: number
}

interface StatusRow {
  pid: number
  characterName?: string
  enabled: boolean
  guardHeld: boolean
  someoneVisible: boolean
  playerCount: number
  holdSeconds: number
  ok: boolean
}

const { instances } = useInstances()

const config = ref<NpgConfig>(defaultConfig())
const saving = ref(false)
const statusLoading = ref(false)
const statusRows = ref<StatusRow[]>([])

function defaultConfig(): NpgConfig {
  return { enabled: true, holdSeconds: 300 }
}

function normalize(cfg: Partial<NpgConfig>): NpgConfig {
  const d = defaultConfig()
  return {
    enabled: cfg.enabled ?? d.enabled,
    holdSeconds: Number(cfg.holdSeconds ?? d.holdSeconds),
  }
}

function stateTagType(row: StatusRow): string {
  if (!row.ok || !row.enabled) return 'info'
  if (row.guardHeld && row.someoneVisible) return 'danger'
  if (row.guardHeld) return 'warning'
  return 'success'
}

function stateText(row: StatusRow): string {
  if (!row.ok) return '未就绪'
  if (!row.enabled) return '已停用'
  if (row.guardHeld && row.someoneVisible) return '附近有人,停手中'
  if (row.guardHeld) return '保持停手中'
  return '周围无人'
}

async function reload() {
  try {
    const res = await fetch('/api/npg/config')
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    config.value = normalize((await res.json()) as Partial<NpgConfig>)
  } catch (e: any) {
    ElMessage.error(`加载失败: ${e.message}`)
  }
}

async function save() {
  saving.value = true
  try {
    const res = await fetch('/api/npg/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    config.value = normalize((await res.json()) as Partial<NpgConfig>)
    ElMessage.success('已保存并下发到所有在线实例')
    void refreshStatus()
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  } finally {
    saving.value = false
  }
}

// 逐个实例发 getNpgConfig,汇总每个角色的运行态。
async function refreshStatus() {
  statusLoading.value = true
  try {
    const rows = await Promise.all(
      instances.value.map(async (inst): Promise<StatusRow> => {
        const base: StatusRow = {
          pid: inst.pid,
          characterName: inst.characterName,
          enabled: false,
          guardHeld: false,
          someoneVisible: false,
          playerCount: 0,
          holdSeconds: 0,
          ok: false,
        }
        try {
          const res = await fetch(`/api/command/${inst.pid}`, {
            method: 'POST',
            headers: { 'content-type': 'application/json' },
            body: JSON.stringify({ action: 'getNpgConfig', args: {} }),
          })
          const r = await res.json()
          if (r?.ok && typeof r.detail === 'string') {
            const s = JSON.parse(r.detail)
            base.enabled = !!s.enabled
            base.guardHeld = !!s.guardHeld
            base.someoneVisible = !!s.someoneVisible
            base.playerCount = Number(s.playerCount) || 0
            base.holdSeconds = Number(s.holdSeconds) || 0
            base.ok = true
          }
        } catch { /* 实例没回就保留默认 */ }
        return base
      })
    )
    statusRows.value = rows
  } finally {
    statusLoading.value = false
  }
}

onMounted(async () => {
  await reload()
  void refreshStatus()
})
</script>

<style scoped>
.npg-view { padding: 8px; }
.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-left: 8px;
  line-height: 1.6;
}
</style>

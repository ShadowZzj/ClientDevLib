<template>
  <div class="autotrade-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>自动交易 — 全局共享配置(所有角色共用一份开关 / 白名单)</span>
          <el-tag :type="config.enabled ? 'success' : 'info'" size="small">
            {{ config.enabled ? '已启用' : '已停用' }}
          </el-tag>
        </div>
      </template>

      <p class="hint">
        交易请求 → 同意 → 对方锁定 → 自动锁定 + 确认 的状态机仍在 DLL 里(收包触发,管道往返太慢)。
        这里只下发开关 / 白名单 / 延时,改完<strong>立即推给所有在线实例</strong>;新上线的实例也会自动补推。
        接受方永远不放物品/钱,对自己零风险。
      </p>

      <el-form label-width="120px" size="default" style="max-width: 760px">
        <el-form-item label="总开关">
          <el-switch v-model="config.enabled" />
          <span class="hint">关掉则所有角色都不自动接受交易。</span>
        </el-form-item>

        <el-form-item label="来者不拒">
          <el-switch v-model="config.acceptAll" />
          <span class="hint">
            开 = 接受任何人的交易请求(忽略白名单)。关 = 仅接受白名单里的角色。
          </span>
        </el-form-item>

        <el-form-item label="白名单" v-if="!config.acceptAll">
          <el-select
            v-model="config.whitelist"
            multiple filterable allow-create default-first-option
            placeholder="选在线角色 / 直接输入名字回车"
            style="width: 480px">
            <el-option v-for="n in onlineNames" :key="n" :label="label(n)" :value="n" />
          </el-select>
          <div class="hint" style="margin-top: 4px">
            只有这些角色发来的交易才会被接受。白名单模式下读不到对方名字(视野外/读取失败)一律拒绝。
          </div>
        </el-form-item>

        <el-form-item label="自动锁定确认">
          <el-switch v-model="config.autoLockConfirm" />
          <span class="hint">
            关 = 同意后停在交易窗,锁定/确认需手动。开 = 对方锁定后自动跟着锁定并确认。
          </span>
        </el-form-item>

        <el-form-item label="接受延时">
          <el-input-number v-model="config.acceptDelayMs" :min="0" :max="10000" :step="100" />
          <span class="hint">毫秒。收到请求后等这么久再同意,拟人,避免瞬秒应答。</span>
        </el-form-item>

        <el-form-item label="确认延时">
          <el-input-number v-model="config.confirmDelayMs" :min="100" :max="5000" :step="100" />
          <span class="hint">毫秒。对方锁定后,本端锁定到确认之间的节流。</span>
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
        <el-table-column label="状态" width="110">
          <template #default="{ row }">
            <el-tag size="small" :type="row.state === 1 ? 'warning' : 'info'">
              {{ row.state === 1 ? '交易中' : '空闲' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="已同意" width="80" prop="tradesAccepted" />
        <el-table-column label="已完成" width="80" prop="tradesDone" />
        <el-table-column label="最近消息" min-width="220">
          <template #default="{ row }">
            <span class="muted">{{ row.status || '—' }}</span>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useInstances } from '@/composables/useInstances'
import { ElMessage } from 'element-plus'

interface AutoTradeConfig {
  enabled: boolean
  acceptAll: boolean
  autoLockConfirm: boolean
  acceptDelayMs: number
  confirmDelayMs: number
  whitelist: string[]
}

interface StatusRow {
  pid: number
  characterName?: string
  state: number
  tradesAccepted: number
  tradesDone: number
  status: string
}

const { instances } = useInstances()

const config = ref<AutoTradeConfig>(defaultConfig())
const saving = ref(false)
const statusLoading = ref(false)
const statusRows = ref<StatusRow[]>([])

function defaultConfig(): AutoTradeConfig {
  return {
    enabled: false,
    acceptAll: false,
    autoLockConfirm: true,
    acceptDelayMs: 600,
    confirmDelayMs: 300,
    whitelist: [],
  }
}

const onlineNames = computed(() => {
  const set = new Set<string>()
  for (const i of instances.value) if (i.characterName) set.add(i.characterName)
  return Array.from(set)
})

function isOnline(name: string): boolean {
  return instances.value.some((i) => i.characterName === name)
}

function label(name: string): string {
  return isOnline(name) ? `${name} (在线)` : name
}

function normalize(cfg: Partial<AutoTradeConfig>): AutoTradeConfig {
  const d = defaultConfig()
  return {
    enabled: cfg.enabled ?? d.enabled,
    acceptAll: cfg.acceptAll ?? d.acceptAll,
    autoLockConfirm: cfg.autoLockConfirm ?? d.autoLockConfirm,
    acceptDelayMs: Number(cfg.acceptDelayMs ?? d.acceptDelayMs),
    confirmDelayMs: Number(cfg.confirmDelayMs ?? d.confirmDelayMs),
    whitelist: Array.isArray(cfg.whitelist) ? cfg.whitelist.filter(Boolean) : d.whitelist,
  }
}

async function reload() {
  try {
    const res = await fetch('/api/auto-trade/config')
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    config.value = normalize((await res.json()) as Partial<AutoTradeConfig>)
  } catch (e: any) {
    ElMessage.error(`加载失败: ${e.message}`)
  }
}

async function save() {
  saving.value = true
  try {
    const res = await fetch('/api/auto-trade/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    config.value = normalize((await res.json()) as Partial<AutoTradeConfig>)
    ElMessage.success('已保存并下发到所有在线实例')
    void refreshStatus()
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  } finally {
    saving.value = false
  }
}

// 逐个实例发 getAutoTradeStatus,汇总每个角色的运行态。
async function refreshStatus() {
  statusLoading.value = true
  try {
    const rows = await Promise.all(
      instances.value.map(async (inst): Promise<StatusRow> => {
        const base: StatusRow = {
          pid: inst.pid,
          characterName: inst.characterName,
          state: 0,
          tradesAccepted: 0,
          tradesDone: 0,
          status: '',
        }
        try {
          const res = await fetch(`/api/command/${inst.pid}`, {
            method: 'POST',
            headers: { 'content-type': 'application/json' },
            body: JSON.stringify({ action: 'getAutoTradeStatus', args: {} }),
          })
          const r = await res.json()
          if (r?.ok && typeof r.detail === 'string') {
            const s = JSON.parse(r.detail)
            base.state = Number(s.state) || 0
            base.tradesAccepted = Number(s.tradesAccepted) || 0
            base.tradesDone = Number(s.tradesDone) || 0
            base.status = String(s.status || '')
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
.autotrade-view { padding: 8px; }
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
.muted { color: var(--el-text-color-secondary); }
</style>

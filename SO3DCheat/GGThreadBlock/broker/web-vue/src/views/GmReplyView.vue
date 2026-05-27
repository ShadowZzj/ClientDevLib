<template>
  <div class="gm-reply-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <span>GM 自动回复</span>
        <el-tag :type="config.enabled ? 'success' : 'info'" size="small" style="margin-left:8px">
          {{ config.enabled ? '已启用' : '已停用' }}
        </el-tag>
      </template>

      <el-form label-width="120px" size="default">
        <el-form-item label="启用">
          <el-switch v-model="config.enabled" />
        </el-form-item>
        <el-form-item label="GM 角色名">
          <div style="width:100%">
            <div v-for="(name, i) in config.gmNames" :key="i" style="display:flex; margin-bottom:6px">
              <el-input v-model="config.gmNames[i]" placeholder="GM 的游戏角色名" />
              <el-button link type="danger" @click="config.gmNames.splice(i, 1)" style="margin-left:8px">删除</el-button>
            </div>
            <el-button size="small" @click="config.gmNames.push('')">新增 GM</el-button>
            <div class="hint">所有挂机角色共用这个 GM 名单;sender 必须精确匹配才会触发。</div>
          </div>
        </el-form-item>
        <el-form-item label="API Base URL">
          <el-input v-model="config.apiBaseUrl" placeholder="https://timicc.com" />
        </el-form-item>
        <el-form-item label="API Key">
          <el-input
            v-model="apiKeyInput"
            type="password"
            show-password
            :placeholder="config.apiKey ? `已保存: ${config.apiKey}` : 'sk-...'"
          />
          <div class="hint">填入新值会覆盖保存。空值或带 *** 的掩码值不会更新。</div>
        </el-form-item>
        <el-form-item label="模型">
          <el-input v-model="config.model" placeholder="claude-sonnet-4-6" />
        </el-form-item>
        <el-form-item label="出站代理">
          <el-input v-model="config.proxy" placeholder="http://127.0.0.1:7890 (可空,默认走环境变量)" />
        </el-form-item>
        <el-form-item label="冷却 (毫秒)">
          <el-input-number v-model="config.cooldownMs" :min="0" :max="600000" :step="1000" />
          <span class="hint" style="margin-left:8px">同 GM 同实例两次回复最小间隔。</span>
        </el-form-item>
        <el-form-item label="超时 (毫秒)">
          <el-input-number v-model="config.timeoutMs" :min="1000" :max="120000" :step="1000" />
        </el-form-item>
        <el-form-item label="System Prompt">
          <el-input v-model="config.systemPrompt" type="textarea" :rows="3" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="saveConfig" :loading="saving">保存配置</el-button>
          <el-button @click="reload">重新加载</el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <el-card shadow="never" class="log-card">
      <template #header>
        <span>触发记录</span>
        <el-button size="small" link style="margin-left:8px" @click="doClear">清空</el-button>
      </template>
      <div class="log-list">
        <div v-for="(e, i) in displayLogs" :key="i" class="log-item">
          <div class="log-row">
            <span class="log-time">{{ formatTime(e.timestamp) }}</span>
            <el-tag size="small" type="info" class="log-pid">{{ e.characterName || `PID ${e.pid}` }}</el-tag>
            <el-tag v-if="e.error" size="small" type="danger">错误</el-tag>
            <el-tag v-else-if="e.skipped" size="small" type="warning">跳过</el-tag>
            <el-tag v-else size="small" type="success">已回复</el-tag>
          </div>
          <div class="log-incoming"><b>{{ e.sender }}:</b> {{ e.incoming }}</div>
          <div v-if="e.outgoing" class="log-outgoing">→ {{ e.outgoing }}</div>
          <div v-if="e.error" class="log-error">{{ e.error }}</div>
          <div v-if="e.skipped" class="log-skipped">{{ e.skipped }}</div>
        </div>
        <div v-if="displayLogs.length === 0" class="log-empty">暂无触发记录</div>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useWebSocket } from '@/composables/useWebSocket'
import { ElMessage } from 'element-plus'

interface GmReplierConfig {
  enabled: boolean
  gmNames: string[]
  apiBaseUrl: string
  apiKey: string
  model: string
  proxy: string
  cooldownMs: number
  systemPrompt: string
  timeoutMs: number
}

interface GmReplyLogEntry {
  timestamp: number
  pid: number
  characterName?: string
  sender: string
  incoming: string
  outgoing?: string
  error?: string
  skipped?: string
}

const config = ref<GmReplierConfig>({
  enabled: false,
  gmNames: [],
  apiBaseUrl: 'https://timicc.com',
  apiKey: '',
  model: 'claude-sonnet-4-6',
  proxy: '',
  cooldownMs: 8000,
  systemPrompt: '',
  timeoutMs: 20000,
})
const apiKeyInput = ref('')
const logs = ref<GmReplyLogEntry[]>([])
const saving = ref(false)

const displayLogs = computed(() => [...logs.value].reverse())

const { onMessage } = useWebSocket()

function formatTime(ts: number): string {
  return new Date(ts).toLocaleTimeString('zh-CN', { hour12: false })
}

async function reload() {
  try {
    const [cfg, lg] = await Promise.all([
      fetch('/api/gm-replier/config').then((r) => r.json()),
      fetch('/api/gm-replier/logs').then((r) => r.json()),
    ])
    config.value = { ...config.value, ...cfg }
    logs.value = Array.isArray(lg) ? lg : []
    apiKeyInput.value = ''
  } catch (e: any) {
    ElMessage.error(`加载失败: ${e.message}`)
  }
}

async function saveConfig() {
  saving.value = true
  try {
    const payload: any = { ...config.value }
    // 只在用户输入了新 key 时才覆盖,否则保留服务端
    if (apiKeyInput.value && !apiKeyInput.value.includes('***')) {
      payload.apiKey = apiKeyInput.value
    } else {
      delete payload.apiKey
    }
    const res = await fetch('/api/gm-replier/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const updated = await res.json()
    config.value = { ...config.value, ...updated }
    apiKeyInput.value = ''
    ElMessage.success('已保存')
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  } finally {
    saving.value = false
  }
}

async function doClear() {
  try {
    await fetch('/api/gm-replier/logs/clear', { method: 'POST' })
    logs.value = []
  } catch (e: any) {
    ElMessage.error(`清空失败: ${e.message}`)
  }
}

const unsub = onMessage((msg: any) => {
  if (msg.type === 'gmReplier.log' && msg.entry) {
    logs.value.push(msg.entry as GmReplyLogEntry)
    if (logs.value.length > 200) logs.value.splice(0, logs.value.length - 200)
  }
})

onMounted(() => { reload() })
onUnmounted(() => { unsub() })
</script>

<style scoped>
.gm-reply-view {
  max-width: 900px;
}
.config-card {
  margin-bottom: 16px;
}
.hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-top: 4px;
}
.log-list {
  max-height: 400px;
  overflow-y: auto;
}
.log-item {
  border-bottom: 1px solid var(--el-border-color-lighter);
  padding: 8px 0;
  font-size: 13px;
}
.log-row {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 4px;
}
.log-time {
  color: var(--el-text-color-secondary);
}
.log-pid {
  margin-right: 4px;
}
.log-incoming {
  color: var(--el-text-color-primary);
  word-break: break-all;
}
.log-outgoing {
  color: var(--el-color-success);
  margin-left: 16px;
  word-break: break-all;
}
.log-error {
  color: var(--el-color-danger);
  margin-left: 16px;
}
.log-skipped {
  color: var(--el-text-color-secondary);
  margin-left: 16px;
}
.log-empty {
  text-align: center;
  color: var(--el-text-color-placeholder);
  padding: 32px 0;
}
</style>

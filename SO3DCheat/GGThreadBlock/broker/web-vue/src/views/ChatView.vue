<template>
  <div class="chat-view">
    <el-card shadow="never">
      <template #header>
        <span>公屏聊天</span>
        <el-tag :type="instances.length ? 'success' : 'info'" size="small" style="margin-left:8px">
          {{ instances.length }} 在线
        </el-tag>
        <el-select
          v-model="filterPid"
          size="small"
          style="width:180px; margin-left:12px"
          placeholder="筛选接收角色"
        >
          <el-option label="全部角色" :value="0" />
          <el-option
            v-for="inst in instances"
            :key="inst.pid"
            :label="inst.characterName || `PID ${inst.pid}`"
            :value="inst.pid"
          />
        </el-select>
        <el-button size="small" link style="margin-left:8px" @click="messages = []">清空</el-button>
      </template>

      <div ref="msgContainer" class="msg-list">
        <div v-for="(m, i) in filteredMessages" :key="i" class="msg-item">
          <span class="msg-time">{{ formatTime(m.timestamp) }}</span>
          <el-tag size="small" type="info" class="msg-recv">{{ m.characterName || `PID ${m.pid}` }}</el-tag>
          <span class="msg-sender">{{ m.sender }}</span>
          <span class="msg-text">{{ m.message }}</span>
        </div>
        <div v-if="filteredMessages.length === 0" class="msg-empty">暂无消息</div>
      </div>

      <div class="send-bar">
        <el-select v-model="sendPid" placeholder="选择发送角色" size="default" style="width:160px">
          <el-option
            v-for="inst in instances"
            :key="inst.pid"
            :label="inst.characterName || `PID ${inst.pid}`"
            :value="inst.pid"
          />
        </el-select>
        <el-input
          v-model="inputMsg"
          placeholder="输入消息..."
          @keyup.enter="doSend"
          :disabled="sending"
          style="flex:1; margin: 0 8px"
        />
        <el-button type="primary" @click="doSend" :loading="sending" :disabled="!inputMsg.trim() || !sendPid">
          发送
        </el-button>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, nextTick, onMounted, onUnmounted } from 'vue'
import { useInstances } from '@/composables/useInstances'
import { useWebSocket } from '@/composables/useWebSocket'
import { ElMessage } from 'element-plus'

interface ChatMsg {
  pid: number
  sender: string
  message: string
  timestamp: number
  characterName?: string
}

const { instances } = useInstances()
const { onMessage } = useWebSocket()

const messages = ref<ChatMsg[]>([])
const inputMsg = ref('')
const sendPid = ref<number | null>(null)
const filterPid = ref<number>(0)  // 0 = all
const sending = ref(false)
const msgContainer = ref<HTMLElement | null>(null)

const MAX_MESSAGES = 500

const filteredMessages = computed(() =>
  filterPid.value === 0
    ? messages.value
    : messages.value.filter((m) => m.pid === filterPid.value)
)

function formatTime(ts: number): string {
  const d = new Date(ts)
  return d.toLocaleTimeString('zh-CN', { hour12: false })
}

function scrollToBottom() {
  nextTick(() => {
    if (msgContainer.value) {
      msgContainer.value.scrollTop = msgContainer.value.scrollHeight
    }
  })
}

const unsub = onMessage((msg: any) => {
  if (msg.type === 'chat') {
    messages.value.push({
      pid: msg.pid,
      sender: msg.sender || '???',
      message: msg.message || '',
      timestamp: msg.timestamp || Date.now(),
      characterName: msg.characterName,
    })
    if (messages.value.length > MAX_MESSAGES) {
      messages.value.splice(0, messages.value.length - MAX_MESSAGES)
    }
    scrollToBottom()
  }
})

onUnmounted(() => { unsub() })

onMounted(() => {
  if (instances.value.length > 0 && !sendPid.value) {
    sendPid.value = instances.value[0].pid
  }
})

async function doSend() {
  const msg = inputMsg.value.trim()
  if (!msg || !sendPid.value) return
  sending.value = true
  try {
    const res = await fetch(`/api/command/${sendPid.value}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'sendPublicChat', args: { message: msg } }),
    })
    const data = await res.json()
    if (data.ok) {
      inputMsg.value = ''
    } else {
      ElMessage.error(data.detail || '发送失败')
    }
  } catch (e: any) {
    ElMessage.error(e.message || '网络错误')
  } finally {
    sending.value = false
  }
}
</script>

<style scoped>
.chat-view {
  max-width: 800px;
}
.msg-list {
  height: 400px;
  overflow-y: auto;
  border: 1px solid var(--el-border-color);
  border-radius: 4px;
  padding: 8px;
  margin-bottom: 12px;
  background: var(--el-bg-color-page);
}
.msg-item {
  margin-bottom: 4px;
  font-size: 13px;
  line-height: 1.6;
}
.msg-time {
  color: var(--el-text-color-secondary);
  margin-right: 6px;
}
.msg-recv {
  margin-right: 6px;
  vertical-align: middle;
}
.msg-sender {
  font-weight: 600;
  color: var(--el-color-primary);
  margin-right: 6px;
}
.msg-sender::after {
  content: ':';
}
.msg-text {
  word-break: break-all;
}
.msg-empty {
  text-align: center;
  color: var(--el-text-color-placeholder);
  padding: 40px 0;
}
.send-bar {
  display: flex;
  align-items: center;
}
</style>

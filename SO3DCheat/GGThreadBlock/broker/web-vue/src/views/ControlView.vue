<template>
  <div class="control">
    <h3>发送按键</h3>
    <el-form label-width="80px" style="max-width: 500px">
      <el-form-item label="按键组合">
        <el-autocomplete
          v-model="keysText"
          :fetch-suggestions="keySuggestions"
          placeholder="例: alt, w 或 ctrl+shift+f5"
          style="width: 100%"
          @select="(item: any) => keysText = item.value"
        />
      </el-form-item>
      <el-form-item label="按住(ms)">
        <el-input-number v-model="holdMs" :min="20" :max="5000" :step="10" />
      </el-form-item>
      <el-form-item>
        <el-button type="primary" :loading="sending" @click="doSendKeys">发送</el-button>
      </el-form-item>
    </el-form>

    <el-divider />

    <h3>升级测试</h3>
    <el-form label-width="80px" style="max-width: 500px">
      <el-form-item label="payload">
        <el-input-number
          v-model="levelUpPayload"
          :min="0"
          :max="2147483647"
          :step="1"
          style="width: 180px"
        />
      </el-form-item>
      <el-form-item>
        <el-button
          type="warning"
          :loading="levelUpSending"
          :disabled="!selectedPid"
          @click="doRequestLevelUp"
        >
          发送 CG_LEVEL_UP_CHECK
        </el-button>
      </el-form-item>
    </el-form>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import { useInstances } from '@/composables/useInstances'
import { useLocalHistory } from '@/composables/useLocalHistory'

const { selectedPid } = useInstances()
const keysHistory = useLocalHistory('ggtb.keys')

const keysText = ref('')
const holdMs = ref(80)
const sending = ref(false)
const levelUpPayload = ref(412016)
const levelUpSending = ref(false)

function keySuggestions(query: string, cb: (results: any[]) => void) {
  const all = keysHistory.getAll().map((v) => ({ value: v }))
  cb(query ? all.filter((i) => i.value.includes(query)) : all)
}

function parseKeyCombo(text: string): (string | number)[] {
  return text
    .split(/[,+\s]+/)
    .map((s) => s.trim())
    .filter((s) => s.length > 0)
    .map((p) => (/^\d+$/.test(p) ? Number(p) : p))
}

async function doSendKeys() {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  const text = keysText.value.trim()
  if (!text) { ElMessage.warning('请输入按键组合'); return }
  const vks = parseKeyCombo(text)
  if (vks.length === 0) { ElMessage.warning('解析不到任何按键'); return }

  sending.value = true
  try {
    const res = await fetch(`/api/command/${selectedPid.value}`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ action: 'sendInput', args: { vks, holdMs: holdMs.value } }),
    })
    const r = await res.json()
    if (r.ok) {
      ElMessage.success(`已发送 ${text}`)
      keysHistory.push(text)
    } else {
      ElMessage.error(`失败: ${r.detail || r.error || 'unknown'}`)
    }
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    sending.value = false
  }
}

async function doRequestLevelUp() {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }

  levelUpSending.value = true
  try {
    const res = await fetch(`/api/level-up/${selectedPid.value}`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ payload: levelUpPayload.value }),
    })
    const r = await res.json()
    if (r.ok) {
      ElMessage.success(`已发送升级请求 payload=${r.payload ?? levelUpPayload.value}`)
    } else {
      ElMessage.error(`失败: ${r.detail || r.error || 'unknown'}`)
    }
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    levelUpSending.value = false
  }
}
</script>

<style scoped>
.control {
  padding: 8px;
}
</style>

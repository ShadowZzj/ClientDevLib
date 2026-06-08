<template>
  <div class="sync-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>同步 — 副角色镜像主角色的移动与 NPC 对话</span>
          <el-tag :type="config.enabled ? 'success' : 'info'" size="small">
            {{ config.enabled ? '运行中' : '已停止' }}
          </el-tag>
        </div>
      </template>

      <el-form label-width="120px" size="default">
        <el-form-item label="总开关">
          <el-switch v-model="config.enabled" />
          <span class="hint">关掉就完全停止同步,副角色不再跟随。</span>
        </el-form-item>

        <el-form-item label="主角色">
          <el-select
            v-model="config.masterName"
            placeholder="选择主角色"
            filterable
            style="width: 260px"
          >
            <el-option
              v-for="opt in characterOptions"
              :key="opt.value"
              :label="opt.label"
              :value="opt.value"
            />
          </el-select>
          <span class="hint">被监控的角色,它的动作会被同步给副角色。</span>
        </el-form-item>

        <el-form-item label="副角色">
          <el-select
            v-model="config.slaveNames"
            placeholder="选择一个或多个副角色"
            multiple
            filterable
            style="width: 100%; max-width: 520px"
          >
            <el-option
              v-for="opt in slaveOptions"
              :key="opt.value"
              :label="opt.label"
              :value="opt.value"
            />
          </el-select>
          <span class="hint">这些角色会镜像主角色。主角色不会被选进来。</span>
        </el-form-item>

        <el-divider content-position="left">移动同步</el-divider>

        <el-form-item label="镜像移动">
          <el-switch v-model="config.mirrorPosition" />
          <span class="hint">按间隔把主角色当前坐标 moveTo 给副角色(引擎自动寻路)。</span>
        </el-form-item>
        <el-form-item label="跟随间隔">
          <el-input-number v-model="config.followIntervalMs" :min="800" :max="10000" :step="100" />
          <span class="hint">毫秒。每隔这么久对齐一次位置。主角色靠 status 每 1.5s 上报坐标。</span>
        </el-form-item>
        <el-form-item label="位移阈值">
          <el-input-number v-model="config.posEpsilon" :min="0" :max="100" :step="0.5" />
          <span class="hint">主角色移动超过这个距离才重发 moveTo,站桩时不刷命令。</span>
        </el-form-item>

        <el-divider content-position="left">NPC 对话同步</el-divider>

        <el-form-item label="镜像对话">
          <el-switch v-model="config.mirrorDialog" />
          <span class="hint">主角色每发一次对话选择(411026),立即并发重放给所有在线副角色,npcId/选项/sub 一致。</span>
        </el-form-item>

        <el-form-item>
          <el-button type="primary" @click="save" :loading="saving">保存配置</el-button>
          <el-button @click="reload">重新加载</el-button>
        </el-form-item>
      </el-form>

      <el-alert
        v-if="config.enabled && !masterOnline"
        type="warning"
        :closable="false"
        show-icon
        title="主角色当前不在线 — 同步会等到它上线后才开始。"
      />
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useInstances } from '@/composables/useInstances'
import { ElMessage } from 'element-plus'

interface SyncConfig {
  enabled: boolean
  masterName: string
  slaveNames: string[]
  followIntervalMs: number
  mirrorPosition: boolean
  mirrorDialog: boolean
  posEpsilon: number
}

const { instances } = useInstances()

const config = ref<SyncConfig>(defaultConfig())
const saving = ref(false)

function defaultConfig(): SyncConfig {
  return {
    enabled: false,
    masterName: '',
    slaveNames: [],
    followIntervalMs: 1500,
    mirrorPosition: true,
    mirrorDialog: true,
    posEpsilon: 1.0,
  }
}

const characterOptions = computed(() => {
  const set = new Set<string>()
  for (const i of instances.value) if (i.characterName) set.add(i.characterName)
  if (config.value.masterName) set.add(config.value.masterName)
  for (const n of config.value.slaveNames) set.add(n)
  return Array.from(set).map((n) => {
    const online = instances.value.some((i) => i.characterName === n)
    return { value: n, label: online ? `${n} (在线)` : `${n} (离线)` }
  })
})

// 副角色候选排除主角色自身。
const slaveOptions = computed(() =>
  characterOptions.value.filter((o) => o.value !== config.value.masterName)
)

const masterOnline = computed(() =>
  instances.value.some((i) => i.characterName === config.value.masterName)
)

async function reload() {
  try {
    const res = await fetch('/api/sync/config')
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const cfg = (await res.json()) as Partial<SyncConfig>
    config.value = { ...defaultConfig(), ...cfg, slaveNames: cfg.slaveNames ?? [] }
  } catch (e: any) {
    ElMessage.error(`加载失败: ${e.message}`)
  }
}

async function save() {
  // 主角色不能同时是副角色。
  config.value.slaveNames = config.value.slaveNames.filter((n) => n !== config.value.masterName)
  saving.value = true
  try {
    const res = await fetch('/api/sync/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const updated = (await res.json()) as Partial<SyncConfig>
    config.value = { ...defaultConfig(), ...updated, slaveNames: updated.slaveNames ?? [] }
    ElMessage.success('已保存')
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  } finally {
    saving.value = false
  }
}

onMounted(reload)
</script>

<style scoped>
.sync-view {
  max-width: 900px;
}
.config-card {
  margin-bottom: 16px;
}
.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-left: 8px;
}
</style>

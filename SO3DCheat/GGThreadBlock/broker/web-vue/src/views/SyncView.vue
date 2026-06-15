<template>
  <div class="sync-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>同步 — 多分组,副角色镜像主角色的移动 / NPC 对话 / 城市传送</span>
          <el-tag :type="enabledGroupCount > 0 ? 'success' : 'info'" size="small">
            {{ enabledGroupCount > 0 ? `${enabledGroupCount} 组运行中` : '全部已停止' }}
          </el-tag>
        </div>
      </template>

      <el-form label-width="120px" size="default">
        <el-form-item label="跟随间隔">
          <el-input-number v-model="config.followIntervalMs" :min="800" :max="10000" :step="100" />
          <span class="hint">毫秒,全局共享。每隔这么久对齐一次各组位置。</span>
        </el-form-item>
        <el-form-item label="位移阈值">
          <el-input-number v-model="config.posEpsilon" :min="0" :max="100" :step="0.5" />
          <span class="hint">主角色移动超过这个距离才重发 moveTo,站桩时不刷命令。</span>
        </el-form-item>
        <el-form-item label="错峰抖动">
          <el-input-number v-model="config.followJitterMs" :min="0" :max="5000" :step="100" />
          <span class="hint">
            毫秒。每个副角色发 moveTo 前各等一个 0~此值的随机延迟,打散起步时刻,
            避免几个副角色齐步走被看穿。最终坐标不变,0=关闭。
          </span>
        </el-form-item>
        <el-form-item label="路径随机">
          <el-switch v-model="config.pathRandomize" />
          <span class="hint">
            开了之后副角色不直线寻路到目标,而是先走几个随机途经点再到终点,每个号
            路线各不相同。最终落点精确不变。
          </span>
        </el-form-item>
        <template v-if="config.pathRandomize">
          <el-form-item label="途经点数">
            <el-input-number v-model="config.pathWaypoints" :min="1" :max="5" :step="1" />
            <span class="hint">每条路径插几个随机途经点,越多越绕。</span>
          </el-form-item>
          <el-form-item label="偏移幅度">
            <el-input-number v-model="config.pathOffsetMax" :min="0" :max="20" :step="0.5" />
            <span class="hint">途经点垂直于直线方向的最大随机偏移(世界坐标),越大绕得越开。</span>
          </el-form-item>
          <el-form-item label="分段间隔">
            <el-input-number v-model="config.pathSegDelayMs" :min="0" :max="3000" :step="100" />
            <span class="hint">
              毫秒。发完一段 moveTo 后等多久再发下一段(给引擎时间往途经点走)。
              建议小于跟随间隔,否则主角色跑图时折线容易被下一拍打断。
            </span>
          </el-form-item>
        </template>
        <el-form-item>
          <el-button type="primary" @click="save" :loading="saving">保存配置</el-button>
          <el-button @click="reload">重新加载</el-button>
          <el-button type="success" plain @click="addGroup">+ 新建分组</el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <el-empty v-if="config.groups.length === 0" description="还没有分组,点「新建分组」开始" />

    <el-card
      v-for="(group, gi) in config.groups"
      :key="group.id"
      shadow="never"
      class="group-card"
    >
      <template #header>
        <div class="card-header">
          <div class="group-title">
            <el-switch v-model="group.enabled" />
            <el-input
              v-model="group.name"
              size="small"
              style="width: 200px"
              placeholder="分组名"
            />
          </div>
          <div>
            <el-tag v-if="!group.enabled" type="info" size="small">已停止</el-tag>
            <el-tag v-else-if="masterOnline(group)" type="success" size="small">主在线</el-tag>
            <el-tag v-else type="warning" size="small">主离线</el-tag>
            <el-button
              type="danger"
              link
              size="small"
              style="margin-left: 8px"
              @click="removeGroup(gi)"
            >删除分组</el-button>
          </div>
        </div>
      </template>

      <el-form label-width="100px" size="default">
        <el-form-item label="主角色">
          <el-select
            v-model="group.masterName"
            placeholder="选择主角色"
            filterable
            clearable
            style="width: 260px"
            @change="onMasterChange(group)"
          >
            <el-option
              v-for="opt in masterOptionsFor(group)"
              :key="opt.value"
              :label="opt.label"
              :value="opt.value"
              :disabled="opt.disabled"
            />
          </el-select>
          <span class="hint">它的动作会被同步给本组副角色。</span>
        </el-form-item>

        <el-form-item label="副角色">
          <el-select
            v-model="group.slaveNames"
            placeholder="选择一个或多个副角色"
            multiple
            filterable
            style="width: 100%; max-width: 520px"
          >
            <el-option
              v-for="opt in slaveOptionsFor(group)"
              :key="opt.value"
              :label="opt.label"
              :value="opt.value"
              :disabled="opt.disabled"
            />
          </el-select>
          <span class="hint">这些角色镜像主角色。已属于其它分组的角色不可选。</span>
        </el-form-item>

        <el-form-item label="镜像移动">
          <el-switch v-model="group.mirrorPosition" />
          <span class="hint">按间隔把主角色坐标 moveTo 给副角色。</span>
        </el-form-item>

        <template v-if="group.mirrorPosition">
          <el-form-item label="站位方式">
            <el-radio-group v-model="group.formation">
              <el-radio label="overlap">重叠站位</el-radio>
              <el-radio label="line">排队站位</el-radio>
            </el-radio-group>
            <span class="hint">
              重叠=副角色最终和主角色坐标重合;排队=沿一个方向依次排开。
            </span>
          </el-form-item>

          <template v-if="group.formation === 'line'">
            <el-form-item label="排队方向">
              <el-select v-model="group.lineDirection" style="width: 160px">
                <el-option label="X 增 (向东)" value="x+" />
                <el-option label="X 减 (向西)" value="x-" />
                <el-option label="Y 增 (向北)" value="y+" />
                <el-option label="Y 减 (向南)" value="y-" />
              </el-select>
              <span class="hint">副角色按选中角色列表顺序,沿此方向逐个排开。</span>
            </el-form-item>
            <el-form-item label="格间距">
              <el-input-number v-model="group.lineSpacing" :min="0.1" :max="50" :step="0.5" />
              <span class="hint">相邻站位的世界坐标间距,默认 1。</span>
            </el-form-item>
            <el-form-item label="避免重合">
              <el-switch v-model="group.lineAvoidPlayers" />
              <span class="hint">
                勾上则排队时遍历主角色附近玩家(含自己),已被站住的格子跳过往后站,
                保证不和任何玩家重合。
              </span>
            </el-form-item>
          </template>
        </template>

        <el-form-item label="镜像对话">
          <el-switch v-model="group.mirrorDialog" />
          <span class="hint">主角色发对话选择(411026)时,立即重放给副角色。</span>
        </el-form-item>
        <el-form-item label="镜像传送">
          <el-switch v-model="group.mirrorTeleport" />
          <span class="hint">主角色城市传送(411076)时,把同一目的地重放给副角色。</span>
        </el-form-item>
      </el-form>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useInstances } from '@/composables/useInstances'
import { ElMessage } from 'element-plus'

interface SyncGroup {
  id: string
  name: string
  enabled: boolean
  masterName: string
  slaveNames: string[]
  mirrorPosition: boolean
  mirrorDialog: boolean
  mirrorTeleport: boolean
  formation: 'overlap' | 'line'
  lineDirection: 'x+' | 'x-' | 'y+' | 'y-'
  lineSpacing: number
  lineAvoidPlayers: boolean
}

interface SyncConfig {
  followIntervalMs: number
  posEpsilon: number
  followJitterMs: number
  pathRandomize: boolean
  pathWaypoints: number
  pathOffsetMax: number
  pathSegDelayMs: number
  groups: SyncGroup[]
}

const { instances } = useInstances()

const config = ref<SyncConfig>(defaultConfig())
const saving = ref(false)

function defaultConfig(): SyncConfig {
  return {
    followIntervalMs: 1500,
    posEpsilon: 1.0,
    followJitterMs: 600,
    pathRandomize: false,
    pathWaypoints: 2,
    pathOffsetMax: 2.0,
    pathSegDelayMs: 700,
    groups: [],
  }
}

function newGroup(): SyncGroup {
  return {
    id: `g${Date.now().toString(36)}${Math.floor(Math.random() * 1e4).toString(36)}`,
    name: `分组 ${config.value.groups.length + 1}`,
    enabled: true,
    masterName: '',
    slaveNames: [],
    mirrorPosition: true,
    mirrorDialog: true,
    mirrorTeleport: true,
    formation: 'overlap',
    lineDirection: 'x+',
    lineSpacing: 1,
    lineAvoidPlayers: false,
  }
}

// 所有已知角色名(在线 + 配置里出现过的)。
const allCharacters = computed(() => {
  const set = new Set<string>()
  for (const i of instances.value) if (i.characterName) set.add(i.characterName)
  for (const g of config.value.groups) {
    if (g.masterName) set.add(g.masterName)
    for (const n of g.slaveNames) set.add(n)
  }
  return Array.from(set)
})

function isOnline(name: string): boolean {
  return instances.value.some((i) => i.characterName === name)
}

// 头部状态标:有几个分组开着开关。
const enabledGroupCount = computed(
  () => config.value.groups.filter((g) => g.enabled).length
)

function label(name: string): string {
  return isOnline(name) ? `${name} (在线)` : `${name} (离线)`
}

// name -> 占用它的分组 id(主或副)。用来禁用其它组里的同名选项。
const ownerByName = computed(() => {
  const m = new Map<string, string>()
  for (const g of config.value.groups) {
    if (g.masterName && !m.has(g.masterName)) m.set(g.masterName, g.id)
    for (const n of g.slaveNames) if (!m.has(n)) m.set(n, g.id)
  }
  return m
})

// 主角色候选:本组当前 master 始终可选;其余按是否被别组占用禁用。
function masterOptionsFor(group: SyncGroup) {
  return allCharacters.value.map((n) => {
    const owner = ownerByName.value.get(n)
    const takenByOther = owner !== undefined && owner !== group.id
    // 本组副角色也不能当本组主
    const isOwnSlave = group.slaveNames.includes(n)
    return { value: n, label: label(n), disabled: (takenByOther && n !== group.masterName) || isOwnSlave }
  })
}

// 副角色候选:排除本组主;被别组占用的禁用。
function slaveOptionsFor(group: SyncGroup) {
  return allCharacters.value
    .filter((n) => n !== group.masterName)
    .map((n) => {
      const owner = ownerByName.value.get(n)
      const takenByOther = owner !== undefined && owner !== group.id
      const alreadyMine = group.slaveNames.includes(n)
      return { value: n, label: label(n), disabled: takenByOther && !alreadyMine }
    })
}

function masterOnline(group: SyncGroup): boolean {
  return !!group.masterName && isOnline(group.masterName)
}

// 改主角色时,如果新主在副列表里,从副列表剔除。
function onMasterChange(group: SyncGroup) {
  group.slaveNames = group.slaveNames.filter((n) => n !== group.masterName)
}

function addGroup() {
  config.value.groups.push(newGroup())
}

function removeGroup(idx: number) {
  config.value.groups.splice(idx, 1)
}

function normalize(cfg: Partial<SyncConfig>): SyncConfig {
  const groups = Array.isArray(cfg.groups) ? cfg.groups : []
  return {
    followIntervalMs: cfg.followIntervalMs ?? 1500,
    posEpsilon: cfg.posEpsilon ?? 1.0,
    followJitterMs: cfg.followJitterMs ?? 600,
    pathRandomize: cfg.pathRandomize ?? false,
    pathWaypoints: cfg.pathWaypoints ?? 2,
    pathOffsetMax: cfg.pathOffsetMax ?? 2.0,
    pathSegDelayMs: cfg.pathSegDelayMs ?? 700,
    groups: groups.map((g: any) => ({
      id: g.id,
      name: g.name,
      enabled: g.enabled ?? true,
      masterName: g.masterName ?? '',
      slaveNames: Array.isArray(g.slaveNames) ? g.slaveNames : [],
      mirrorPosition: g.mirrorPosition ?? true,
      mirrorDialog: g.mirrorDialog ?? true,
      mirrorTeleport: g.mirrorTeleport ?? true,
      formation: g.formation === 'line' ? 'line' : 'overlap',
      lineDirection: ['x+', 'x-', 'y+', 'y-'].includes(g.lineDirection) ? g.lineDirection : 'x+',
      lineSpacing: Number(g.lineSpacing) > 0 ? Number(g.lineSpacing) : 1,
      lineAvoidPlayers: !!g.lineAvoidPlayers,
    })),
  }
}

async function reload() {
  try {
    const res = await fetch('/api/sync/config')
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    config.value = normalize((await res.json()) as Partial<SyncConfig>)
  } catch (e: any) {
    ElMessage.error(`加载失败: ${e.message}`)
  }
}

async function save() {
  // 提交前本地清一遍:主不在副里。服务端还会做跨组去重归一化。
  for (const g of config.value.groups) {
    g.slaveNames = g.slaveNames.filter((n) => n !== g.masterName)
  }
  saving.value = true
  try {
    const res = await fetch('/api/sync/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    config.value = normalize((await res.json()) as Partial<SyncConfig>)
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
.config-card,
.group-card {
  margin-bottom: 16px;
}
.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.group-title {
  display: flex;
  align-items: center;
  gap: 10px;
}
.hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-left: 8px;
}
</style>

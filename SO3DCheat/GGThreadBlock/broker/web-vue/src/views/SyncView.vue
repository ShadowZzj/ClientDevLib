<template>
  <div class="sync-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>同步 — 多分组,副角色镜像主角色的移动 / NPC 对话 / 城市传送</span>
          <div>
            <el-tag v-if="conflictNames.size > 0" type="danger" size="small" style="margin-right: 8px">
              {{ conflictNames.size }} 个角色冲突
            </el-tag>
            <el-tag :type="enabledGroupCount > 0 ? 'success' : 'info'" size="small">
              {{ enabledGroupCount > 0 ? `${enabledGroupCount} 组运行中` : '全部已停止' }}
            </el-tag>
          </div>
        </div>
      </template>

      <el-form label-width="120px" size="default">
        <el-form-item label="跟随间隔">
          <el-input-number v-model="config.followIntervalMs" :min="300" :max="10000" :step="100" />
          <span class="hint">毫秒,全局共享。每隔这么久对齐一次各组位置。坐标源(遥测)每 500ms 刷新,设 300~500 最跟手,再低是空转。</span>
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
            <el-tag v-if="groupHasConflict(group)" type="danger" size="small" style="margin-right: 8px">角色冲突</el-tag>
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
          <span class="hint">这些角色镜像主角色。同一角色可分到多个组,但「同时启用」的组之间不能共用。</span>
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
    followIntervalMs: 500,
    posEpsilon: 1.0,
    followJitterMs: 150,
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

// 角色可以出现在多个分组里。约束只针对「同时启用(enabled)」的分组:同一个角色
// 不能在两个都启用的组里出现。下面算出「被冲突占用」的角色名集合 —— 即出现在 2 个
// 以上 enabled 组里的名字。用来在 UI 上高亮提示,并在保存前拦截。
const conflictNames = computed(() => {
  const countByName = new Map<string, number>()
  for (const g of config.value.groups) {
    if (!g.enabled) continue
    const mine = new Set<string>()
    if (g.masterName) mine.add(g.masterName)
    for (const n of g.slaveNames) mine.add(n)
    for (const n of mine) countByName.set(n, (countByName.get(n) ?? 0) + 1)
  }
  const set = new Set<string>()
  for (const [n, c] of countByName) if (c > 1) set.add(n)
  return set
})

// 某个启用组是否和别的启用组撞了角色(用于组头上的红标)。
function groupHasConflict(group: SyncGroup): boolean {
  if (!group.enabled) return false
  if (group.masterName && conflictNames.value.has(group.masterName)) return true
  return group.slaveNames.some((n) => conflictNames.value.has(n))
}

// 主角色候选:任何已知角色都能选。本组副不能当本组主。其它组(含启用组)不再禁用
// —— 只有「都启用」才算冲突,交给保存校验/UI 提示,不在选项层面一刀切禁掉。
function masterOptionsFor(group: SyncGroup) {
  return allCharacters.value.map((n) => {
    const isOwnSlave = group.slaveNames.includes(n)
    const dup = group.enabled && conflictNames.value.has(n) && n !== group.masterName
    return { value: n, label: label(n) + (dup ? ' ⚠冲突' : ''), disabled: isOwnSlave }
  })
}

// 副角色候选:排除本组主;其余都可选(同名可跨组)。启用组间撞名加 ⚠ 提示。
function slaveOptionsFor(group: SyncGroup) {
  return allCharacters.value
    .filter((n) => n !== group.masterName)
    .map((n) => {
      const dup = group.enabled && conflictNames.value.has(n) && !group.slaveNames.includes(n)
      return { value: n, label: label(n) + (dup ? ' ⚠冲突' : ''), disabled: false }
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
    followIntervalMs: cfg.followIntervalMs ?? 500,
    posEpsilon: cfg.posEpsilon ?? 1.0,
    followJitterMs: cfg.followJitterMs ?? 150,
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
  // 提交前本地清一遍:主不在副里。服务端会做「同时启用组」唯一性校验。
  for (const g of config.value.groups) {
    g.slaveNames = g.slaveNames.filter((n) => n !== g.masterName)
  }
  // 记下提交前哪些组是启用的,保存后跟服务端返回比对,看哪些被自动停用。
  const wantEnabled = new Map<string, boolean>()
  for (const g of config.value.groups) wantEnabled.set(g.id, g.enabled)

  saving.value = true
  try {
    const res = await fetch('/api/sync/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    config.value = normalize((await res.json()) as Partial<SyncConfig>)
    // 服务端把和已启用组撞角色的组自动停用了 —— 提示用户是哪些。
    const forced = config.value.groups
      .filter((g) => wantEnabled.get(g.id) === true && !g.enabled)
      .map((g) => g.name)
    if (forced.length) {
      ElMessage.warning(`已保存。这些分组与已启用分组共用角色,已自动停用: ${forced.join('、')}`)
    } else {
      ElMessage.success('已保存')
    }
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

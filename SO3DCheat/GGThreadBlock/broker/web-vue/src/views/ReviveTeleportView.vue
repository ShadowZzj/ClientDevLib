<template>
  <div class="revive-teleport-view">
    <h3>复活传送</h3>

    <!-- 账号清单(与「寻路移动」共用)。下方按钮把传送下发到这里勾选的全部账号。 -->
    <el-card shadow="never" class="acct-card">
      <template #header>
        <span>下发账号</span>
      </template>
      <AccountMultiSelect />
    </el-card>

    <!-- 一键传送:固定城镇 + 废墟(废墟来自下方可配置列表) -->
    <el-card shadow="never" class="action-card">
      <template #header>
        <div class="card-header">
          <span>一键复活传送</span>
          <el-tag size="small" type="info">下发到 {{ selectedTargets.length }} 个在线账号</el-tag>
        </div>
      </template>

      <div class="group-label">固定城镇</div>
      <p class="hint">直接发 reviveToTown(412017),由服务端决定落点。狮子城 / 乐园镇是固定包,无需配置。</p>
      <div class="btn-row">
        <el-button
          type="primary"
          :loading="dispatchingKey === 'town:2'"
          :disabled="busy"
          @click="dispatchTown(2, '狮子城')"
        >狮子城</el-button>
        <el-button
          type="primary"
          :loading="dispatchingKey === 'town:1'"
          :disabled="busy"
          @click="dispatchTown(1, '乐园镇')"
        >乐园镇</el-button>
      </div>

      <el-divider />

      <div class="group-label">废墟传送</div>
      <p class="hint">
        聚合序列:先回「经由城」→ 等场景加载 → 走到城内 NPC 落点 → 发废墟对应的对话包(411026)。
        每个账号独立跑,单条序列约需「等待 + 寻路」十几秒,请耐心等待结果。
      </p>
      <div v-if="ruins.length" class="btn-row">
        <el-button
          v-for="r in ruins"
          :key="r.id"
          type="success"
          :loading="dispatchingKey === `ruins:${r.id}`"
          :disabled="busy"
          @click="dispatchRuins(r)"
        >{{ r.name || '(未命名)' }}</el-button>
      </div>
      <el-empty v-else description="还没有废墟地图,去下面添加" :image-size="60" />

      <BatchResultTable v-if="results.length" :rows="resultRows" />
    </el-card>

    <!-- 废墟地图配置:可增删改。狮子城/乐园镇不在此(固定包)。 -->
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>废墟地图配置</span>
          <div>
            <el-button size="small" @click="addRuins">新增废墟</el-button>
            <el-button size="small" type="primary" :loading="saving" @click="saveConfig">保存配置</el-button>
          </div>
        </div>
      </template>
      <p class="hint">
        一条废墟 = 先回哪个城(经由城) + 城内落点坐标(走到哪) + 与哪个 NPC 发什么对话包。
        参考默认两条:NPC 19129,落点 (226,155),经由狮子城;回声谷 Option=10282,乐园镇 Option=10281。
        改完点「保存配置」,上方废墟按钮会随之更新。
      </p>

      <el-table :data="ruins" size="small" border>
        <el-table-column label="名称" min-width="130">
          <template #default="{ row }">
            <el-input v-model="row.name" size="small" placeholder="废墟回声谷" />
          </template>
        </el-table-column>
        <el-table-column label="经由城" width="110">
          <template #default="{ row }">
            <el-select v-model="row.viaCityMode" size="small">
              <el-option label="狮子城" :value="2" />
              <el-option label="乐园镇" :value="1" />
            </el-select>
          </template>
        </el-table-column>
        <el-table-column label="落点 X" width="110">
          <template #default="{ row }">
            <el-input-number v-model="row.walkX" size="small" :precision="2" :controls="false" style="width: 96px" />
          </template>
        </el-table-column>
        <el-table-column label="落点 Y" width="110">
          <template #default="{ row }">
            <el-input-number v-model="row.walkY" size="small" :precision="2" :controls="false" style="width: 96px" />
          </template>
        </el-table-column>
        <el-table-column label="NPC ID" width="110">
          <template #default="{ row }">
            <el-input-number v-model="row.npcId" size="small" :min="0" :controls="false" style="width: 96px" />
          </template>
        </el-table-column>
        <el-table-column label="对话 Option" width="120">
          <template #default="{ row }">
            <el-input-number v-model="row.dialogOption" size="small" :min="0" :controls="false" style="width: 100px" />
          </template>
        </el-table-column>
        <el-table-column label="Sub" width="80">
          <template #default="{ row }">
            <el-input-number v-model="row.dialogSub" size="small" :min="0" :controls="false" style="width: 64px" />
          </template>
        </el-table-column>
        <el-table-column label="等待ms" width="100">
          <template #default="{ row }">
            <el-input-number v-model="row.settleMs" size="small" :min="0" :step="500" :controls="false" style="width: 84px" />
          </template>
        </el-table-column>
        <el-table-column label="寻路超时ms" width="120">
          <template #default="{ row }">
            <el-input-number v-model="row.walkTimeoutMs" size="small" :min="1000" :step="1000" :controls="false" style="width: 96px" />
          </template>
        </el-table-column>
        <el-table-column label="到达距离" width="100">
          <template #default="{ row }">
            <el-input-number v-model="row.arriveDist" size="small" :min="0.1" :step="0.5" :precision="1" :controls="false" style="width: 80px" />
          </template>
        </el-table-column>
        <el-table-column label="操作" width="80" fixed="right">
          <template #default="{ $index }">
            <el-button link type="danger" size="small" @click="removeRuins($index)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
// 复活传送:固定城镇(狮子城/乐园镇,reviveToTown 固定包)+ 废墟聚合序列(回城->等->
// 寻路->发对话,由 broker 编排,定义可在本页增删改)。账号名单与「寻路移动」共用。
import { computed, onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { useSelectedAccounts } from '@/composables/useSelectedAccounts'
import AccountMultiSelect from '@/components/AccountMultiSelect.vue'
import BatchResultTable, { type BatchRow } from '@/components/BatchResultTable.vue'

interface RuinsDef {
  id: string
  name: string
  viaCityMode: number
  walkX: number
  walkY: number
  npcId: number
  dialogOption: number
  dialogSub: number
  settleMs: number
  walkTimeoutMs: number
  reissueMs: number
  arriveDist: number
}

interface DispatchResult {
  pid: number
  characterName: string
  ok: boolean
  phase?: string
  error?: string
}

const { selectedTargets } = useSelectedAccounts()

const ruins = ref<RuinsDef[]>([])
const saving = ref(false)
const dispatchingKey = ref<string | null>(null)
const busy = computed(() => dispatchingKey.value !== null)
const results = ref<DispatchResult[]>([])

// 把后端结果映射到 BatchResultTable 行(失败时把 phase 拼进说明)。
const resultRows = computed<BatchRow[]>(() =>
  results.value.map((r) => ({
    pid: r.pid,
    characterName: r.characterName,
    ok: r.ok,
    error: r.ok ? '' : `${r.phase ? `[${r.phase}] ` : ''}${r.error || ''}`,
  }))
)

function selectedNames(): string[] {
  return selectedTargets.value.map((t) => t.characterName)
}

async function dispatch(body: Record<string, unknown>, key: string) {
  if (selectedTargets.value.length === 0) { ElMessage.warning('未选择账号(请在上方勾选)'); return }
  dispatchingKey.value = key
  results.value = []
  try {
    const res = await fetch('/api/revive-teleport/dispatch', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ...body, characterNames: selectedNames() }),
    })
    const data = await res.json()
    if (!res.ok && !Array.isArray(data?.results)) throw new Error(data?.error || `HTTP ${res.status}`)
    results.value = Array.isArray(data.results) ? data.results : []
    const ok = results.value.filter((r) => r.ok).length
    if (ok === results.value.length) ElMessage.success(`全部成功 (${ok})`)
    else ElMessage.warning(`成功 ${ok}/${results.value.length},详见结果`)
  } catch (e: any) {
    ElMessage.error(`下发失败: ${e.message}`)
  } finally {
    dispatchingKey.value = null
  }
}

function dispatchTown(mode: number, label: string) {
  dispatch({ kind: 'town', townMode: mode }, `town:${mode}`)
  ElMessage.info(`已下发 ${label}`)
}

function dispatchRuins(r: RuinsDef) {
  dispatch({ kind: 'ruins', ruinsId: r.id }, `ruins:${r.id}`)
}

function addRuins() {
  ruins.value.push({
    id: '',
    name: '新废墟',
    viaCityMode: 2,
    walkX: 226,
    walkY: 155,
    npcId: 19129,
    dialogOption: 0,
    dialogSub: 1,
    settleMs: 3000,
    walkTimeoutMs: 15000,
    reissueMs: 2000,
    arriveDist: 1,
  })
}

function removeRuins(index: number) {
  ruins.value.splice(index, 1)
}

async function saveConfig() {
  saving.value = true
  try {
    const res = await fetch('/api/revive-teleport/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ruins: ruins.value }),
    })
    const data = await res.json()
    if (!res.ok) throw new Error(data?.error || `HTTP ${res.status}`)
    ruins.value = Array.isArray(data?.ruins) ? data.ruins : []
    ElMessage.success('配置已保存')
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  } finally {
    saving.value = false
  }
}

async function loadConfig() {
  try {
    const res = await fetch('/api/revive-teleport/config')
    if (res.ok) {
      const data = await res.json()
      ruins.value = Array.isArray(data?.ruins) ? data.ruins : []
    }
  } catch { /* silent */ }
}

onMounted(loadConfig)
</script>

<style scoped>
.revive-teleport-view {
  max-width: 1080px;
}
.acct-card,
.action-card,
.config-card {
  margin-bottom: 16px;
}
.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.group-label {
  font-weight: 600;
  margin: 4px 0;
}
.btn-row {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  margin: 8px 0;
}
.hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  line-height: 1.6;
  margin: 4px 0 8px;
}
</style>

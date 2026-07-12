<template>
  <div class="enchant-stone-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <div>
            <div class="card-title">石头附魔自动洗</div>
            <div class="card-subtitle">按颜色逐格洗属性、固定数值，并在达标后自动尝试开放下一格</div>
          </div>
          <el-select
            v-model="selectedName"
            placeholder="选择在线角色"
            filterable
            :disabled="saving || starting || stopping"
            style="width: 220px"
          >
            <el-option
              v-for="option in characterOptions"
              :key="option.value"
              :label="option.label"
              :value="option.value"
            />
          </el-select>
        </div>
      </template>

      <el-empty
        v-if="characterOptions.length === 0"
        description="当前没有在线角色"
        :image-size="72"
      />
      <div v-else-if="!selectedName" class="empty-hint">请先选择一个在线角色。</div>

      <template v-else>
        <el-alert
          v-if="!selectedOnline"
          type="warning"
          :closable="false"
          show-icon
          title="角色已离线；可以查看状态或停止任务，重新上线后才能启动。"
          class="section-gap"
        />
        <el-alert
          v-if="readError"
          type="error"
          :closable="false"
          show-icon
          :title="`读取游戏石头状态失败：${readError}`"
          class="section-gap"
        />

        <div class="settings-row">
          <span class="settings-label">全局操作间隔</span>
          <el-input-number
            v-model="config.waitMs"
            :min="100"
            :max="4000"
            :step="100"
            :disabled="state.running"
            controls-position="right"
          />
          <span class="hint">毫秒；洗属性、固定洗值、充值和强化共用。游戏状态确认变化且距上次操作达到此间隔后，才会执行下一步。</span>
        </div>

        <el-alert
          type="info"
          :closable="false"
          show-icon
          class="section-gap"
        >
          每种颜色只配置普通格与特殊格两套目标。启用颜色后，只处理当前最高格；达标就强化下一格，洗完第 12 格后自动切换到下一种已启用颜色。
        </el-alert>

        <el-tabs v-model="activeColor" type="border-card" class="color-tabs">
          <el-tab-pane
            v-for="colorConfig in config.colors"
            :key="colorConfig.color"
            :name="String(colorConfig.color)"
          >
            <template #label>
              <span class="tab-label">
                <span
                  class="color-dot"
                  :style="{ backgroundColor: colorMeta(colorConfig.color).hex }"
                />
                {{ colorMeta(colorConfig.color).name }}
                <el-tag v-if="colorConfig.enabled" size="small" type="success" effect="plain">
                  本次洗
                </el-tag>
              </span>
            </template>

            <div class="color-session-row">
              <div class="color-session-switch">
                <span class="settings-label">本次处理{{ colorMeta(colorConfig.color).name }}石头</span>
                <el-switch
                  v-model="colorConfig.enabled"
                  :disabled="state.running"
                  active-text="启用"
                  inactive-text="不处理"
                />
              </div>
              <span class="hint">启用后会从当前最高格连续处理到第 12 格。</span>
            </div>

            <div class="rule-grid">
              <div class="rule-card">
                <div class="rule-title">普通格目标</div>
                <div class="rule-slots">第 1、2、4、5、7、8、10、11 格</div>
                <div class="rule-editor">
                  <el-select
                    v-model="colorConfig.normalTarget.attrId"
                    :disabled="state.running"
                    filterable
                    style="width: 190px"
                  >
                    <el-option
                      v-for="option in targetAttrOptions(colorConfig.color, false)"
                      :key="option.id"
                      :label="option.name"
                      :value="option.id"
                    />
                  </el-select>
                  <el-input-number
                    v-model="colorConfig.normalTarget.minValue"
                    :min="0"
                    :step="1"
                    :controls="false"
                    :disabled="state.running"
                    style="width: 135px"
                  />
                  <span class="hint">达到或超过</span>
                </div>
              </div>

              <div class="rule-card special-rule-card">
                <div class="rule-title">特殊格目标</div>
                <div class="rule-slots">第 3、6、9、12 格</div>
                <div class="rule-editor">
                  <el-select
                    v-model="colorConfig.specialTarget.attrId"
                    :disabled="state.running"
                    filterable
                    style="width: 190px"
                  >
                    <el-option
                      v-for="option in targetAttrOptions(colorConfig.color, true)"
                      :key="option.id"
                      :label="option.name"
                      :value="option.id"
                    />
                  </el-select>
                  <el-input-number
                    v-model="colorConfig.specialTarget.minValue"
                    :min="0"
                    :step="1"
                    :controls="false"
                    :disabled="state.running"
                    style="width: 135px"
                  />
                  <span class="hint">达到或超过</span>
                </div>
              </div>
            </div>

            <div class="stone-summary">
              <div>
                <span class="summary-label">当前阶级</span>
                <strong>{{ stoneForColor(colorConfig.color)?.grade ?? '—' }}</strong>
              </div>
              <div>
                <span class="summary-label">已开放</span>
                <strong>{{ openedCount(colorConfig.color) }} / 12</strong>
              </div>
              <div>
                <span class="summary-label">剩余变更次数</span>
                <strong>{{ remainingText(colorConfig.color) }}</strong>
              </div>
              <div>
                <span class="summary-label">本次处理</span>
                <strong>{{ colorConfig.enabled ? '已启用' : '未启用' }}</strong>
              </div>
            </div>

            <el-table
              :data="slotIndexes"
              border
              size="small"
              :row-class-name="targetRowClass"
              class="target-table"
            >
              <el-table-column label="格子" width="92" align="center">
                <template #default="{ $index }">
                  <span>第 {{ $index + 1 }} 格</span>
                </template>
              </el-table-column>

              <el-table-column label="类型" width="82" align="center">
                <template #default="{ $index }">
                  <el-tag v-if="isSpecialSlot($index)" size="small" type="warning" effect="plain">特殊</el-tag>
                  <el-tag v-else size="small" type="info" effect="plain">普通</el-tag>
                </template>
              </el-table-column>

              <el-table-column label="当前属性与数值" min-width="205">
                <template #default="{ $index }">
                  <template v-if="slotForColor(colorConfig.color, $index)?.opened">
                    <el-tag
                      size="small"
                      effect="plain"
                      :type="currentValueTagType(colorConfig.color, $index)"
                    >
                      {{ currentSlotText(colorConfig.color, $index) }}
                    </el-tag>
                  </template>
                  <span v-else class="muted">尚未开放</span>
                </template>
              </el-table-column>

              <el-table-column label="本格目标" min-width="190">
                <template #default="{ $index }">
                  {{ targetText(colorConfig.color, $index) }}
                </template>
              </el-table-column>

              <el-table-column label="状态" width="100" align="center">
                <template #default="{ $index }">
                  <el-tag
                    size="small"
                    :type="slotProgress(colorConfig.color, $index).type"
                    effect="plain"
                  >
                    {{ slotProgress(colorConfig.color, $index).text }}
                  </el-tag>
                </template>
              </el-table-column>
            </el-table>
          </el-tab-pane>
        </el-tabs>

        <div class="actions">
          <el-button
            type="primary"
            :loading="saving"
            :disabled="state.running"
            @click="saveConfig"
          >
            保存配置
          </el-button>
          <el-button
            type="success"
            :loading="starting"
            :disabled="state.running || !selectedOnline || enabledTotal === 0"
            @click="startTask"
          >
            启动
          </el-button>
          <el-button
            type="danger"
            plain
            :loading="stopping"
            :disabled="!state.running"
            @click="stopTask"
          >
            停止
          </el-button>
          <span v-if="enabledTotal === 0" class="hint">至少启用一种颜色后才能启动。</span>
        </div>
      </template>
    </el-card>

    <el-card v-if="selectedName" shadow="never" class="status-card">
      <template #header>
        <div class="card-header compact">
          <span class="card-title">运行状态</span>
          <div class="status-actions">
            <el-tag :type="statusTagType(state.status)" effect="dark">
              {{ statusLabel(state.status) }}
            </el-tag>
            <el-button link type="primary" :loading="refreshing" @click="refreshLive(true)">刷新</el-button>
          </div>
        </div>
      </template>

      <el-descriptions :column="4" border size="small" class="status-descriptions">
        <el-descriptions-item label="当前阶段" :span="2">{{ stageText }}</el-descriptions-item>
        <el-descriptions-item label="操作次数">{{ state.actionCount }}</el-descriptions-item>
        <el-descriptions-item label="洗属性 / 强化">
          {{ state.optionChangeCount }} / {{ state.upgradeCount }}
        </el-descriptions-item>
        <el-descriptions-item label="最近消息" :span="4">
          {{ state.lastMessage || '—' }}
        </el-descriptions-item>
        <el-descriptions-item v-if="state.lastError" label="错误" :span="4">
          <span class="error-text">
            {{ state.lastErrorCode ? `[${state.lastErrorCode}] ` : '' }}{{ state.lastError }}
          </span>
        </el-descriptions-item>
      </el-descriptions>

      <template v-if="hasResourceInfo">
        <el-divider content-position="left">材料与金币</el-divider>
        <div class="resource-grid">
          <div v-if="money !== null" class="resource-item">
            <span>金币</span>
            <strong>{{ formatNumber(money) }}</strong>
          </div>
          <div v-if="shiningCrystalStock" class="resource-item">
            <span>闪亮结晶库存</span>
            <strong>{{ resourceAmount(shiningCrystalStock) }}</strong>
          </div>
          <div v-if="rainbowPowderStock" class="resource-item">
            <span>七彩粉末库存</span>
            <strong>{{ resourceAmount(rainbowPowderStock) }}</strong>
          </div>
          <div v-if="fixedCoupon" class="resource-item">
            <span>属性固定券</span>
            <el-tag :type="resourceTagType(fixedCoupon)">{{ resourceAmount(fixedCoupon) }}</el-tag>
          </div>
          <div v-if="rechargeTicket" class="resource-item">
            <span>次数充值券</span>
            <el-tag :type="resourceTagType(rechargeTicket)">{{ resourceAmount(rechargeTicket) }}</el-tag>
          </div>
        </div>

        <div v-if="requirementVisible(upgradeRequirement)" class="requirement-line">
          <strong>{{ colorMeta(activeColorNumber).name }}强化：</strong>
          <span v-if="requirementResource(upgradeRequirement, 'crystal')">
            闪亮结晶 {{ resourceAmount(requirementResource(upgradeRequirement, 'crystal')) }}
          </span>
          <span v-if="requirementResource(upgradeRequirement, 'rainbowPowder')">
            七彩粉末 {{ resourceAmount(requirementResource(upgradeRequirement, 'rainbowPowder')) }}
          </span>
          <span v-if="requirementMoney(upgradeRequirement) !== null">
            金币 {{ formatNumber(requirementMoney(upgradeRequirement)) }}
          </span>
        </div>
        <div v-if="requirementVisible(changeRequirement)" class="requirement-line">
          <strong>{{ colorMeta(activeColorNumber).name }}替换：</strong>
          <span v-if="requirementResource(changeRequirement, 'crystal')">
            闪亮结晶 {{ resourceAmount(requirementResource(changeRequirement, 'crystal')) }}
          </span>
          <span v-if="requirementMoney(changeRequirement) !== null">
            金币 {{ formatNumber(requirementMoney(changeRequirement)) }}
          </span>
        </div>
      </template>
    </el-card>

    <el-card v-if="selectedName" shadow="never" class="log-card">
      <template #header>
        <div class="card-header compact">
          <span class="card-title">运行日志</span>
          <span class="hint">最近 {{ logs.length }} 条</span>
        </div>
      </template>
      <pre ref="logRef" class="log-output">{{ logs.length ? logs.join('\n') : '暂无日志' }}</pre>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { useInstances } from '@/composables/useInstances'

interface AttrOption {
  id: number
  name: string
}

interface StoneTarget {
  attrId: number
  minValue: number
}

interface StoneColorConfig {
  color: number
  enabled: boolean
  normalTarget: StoneTarget
  specialTarget: StoneTarget
}

interface WasherConfig {
  characterName: string
  waitMs: number
  colors: StoneColorConfig[]
}

interface StoneSlotSnapshot {
  index: number
  opened: boolean
  attrId: number
  value: number
}

interface StoneSnapshot {
  color: number
  grade: number
  remaining: number
  max: number
  topIndex: number
  slots: StoneSlotSnapshot[]
  [key: string]: unknown
}

interface QuerySnapshot {
  stones: StoneSnapshot[]
  resources?: unknown
  queriedAt: number
  [key: string]: unknown
}

interface WasherState {
  characterName: string
  running: boolean
  status: string
  currentColor: number | null
  currentSlot: number | null
  lastAction: string
  lastMessage: string
  lastErrorCode: string
  lastError: string
  actionCount: number
  optionChangeCount: number
  upgradeCount: number
  extendCount: number
  startedAt: number
  stoppedAt: number
  lastOperationStartedAt: number
  lastConfirmedAt: number
  nextOperationAt: number
  awaitingStateChange: boolean
  updatedAt: number
  lastSnapshot?: QuerySnapshot
}

type JsonRecord = Record<string, unknown>
type TagType = 'success' | 'warning' | 'danger' | 'info'

const COLOR_META = [
  { id: 0, name: '红色', hex: '#e34b4b', extra: { id: 7, name: '攻击速度' }, special: { id: 10, name: '增加伤害力' } },
  { id: 1, name: '橙色', hex: '#f28c28', extra: { id: 8, name: '移动速度' }, special: { id: 11, name: '减少伤害力' } },
  { id: 2, name: '黄色', hex: '#d6ad18', extra: { id: 9, name: '必杀' }, special: { id: 12, name: 'HP %' } },
  { id: 3, name: '绿色', hex: '#3ba272', extra: { id: 9, name: '必杀' }, special: { id: 13, name: '副本伤害' } },
] as const

const COMMON_ATTRS: AttrOption[] = [
  { id: 1, name: '攻击力' },
  { id: 2, name: '魔法力' },
  { id: 3, name: '防御力' },
  { id: 4, name: '命中' },
  { id: 5, name: '回避' },
  { id: 6, name: 'AP %' },
]

const PERCENT_ATTRS = new Set([6, 10, 11, 12, 13])
const slotIndexes = Array.from({ length: 12 }, (_, index) => index)
const { instances, selectedInstance } = useInstances()

const selectedName = ref('')
const savedNames = ref<string[]>([])
const activeColor = ref('0')
const config = ref<WasherConfig>(emptyConfig(''))
const state = ref<WasherState>(emptyState(''))
const logs = ref<string[]>([])
const readError = ref('')
const hasConfig = ref(false)
const saving = ref(false)
const starting = ref(false)
const stopping = ref(false)
const refreshing = ref(false)
const logRef = ref<HTMLPreElement | null>(null)
let pollTimer: ReturnType<typeof setInterval> | null = null
let selectionVersion = 0

const characterOptions = computed(() => {
  const options: Array<{ value: string; label: string }> = []
  const seen = new Set<string>()
  for (const instance of instances.value) {
    const name = instance.characterName
    if (!name || seen.has(name)) continue
    seen.add(name)
    options.push({ value: name, label: `${name}（PID ${instance.pid}）` })
  }
  for (const name of savedNames.value) {
    if (!name || seen.has(name)) continue
    seen.add(name)
    options.push({ value: name, label: `${name}（离线配置）` })
  }
  return options
})

const selectedOnline = computed(() =>
  instances.value.some((instance) => instance.characterName === selectedName.value),
)

const enabledTotal = computed(() =>
  config.value.colors.filter((color) => color.enabled).length,
)

const activeColorNumber = computed(() => Number(activeColor.value) || 0)
const stageText = computed(() => {
  if (state.value.running && state.value.awaitingStateChange) {
    const remaining = Math.max(0, state.value.nextOperationAt - Date.now())
    if (remaining > 0) return `状态已确认 · 全局间隔还需约 ${Math.ceil(remaining / 100) * 100} 毫秒`
    return '全局间隔已满足 · 等待游戏状态变化'
  }
  if (state.value.currentColor === null || state.value.currentSlot === null) {
    return state.value.running ? '正在读取石头状态' : '—'
  }
  const actionNames: Record<string, string> = {
    enchantStoneOptionChange: '洗属性',
    enchantStoneUpgrade: '强化开格',
    enchantStoneExtend: '充值变更次数',
  }
  const action = actionNames[state.value.lastAction] || state.value.lastAction || '处理中'
  return `${colorMeta(state.value.currentColor).name}第 ${state.value.currentSlot + 1} 格 · ${action}`
})

const snapshotRecord = computed<JsonRecord>(() => asRecord(state.value.lastSnapshot) ?? {})
const resourceRoot = computed<JsonRecord>(() => {
  const resources = asRecord(state.value.lastSnapshot?.resources)
  return resources && Object.keys(resources).length > 0 ? resources : snapshotRecord.value
})
const money = computed(() => firstNumber(resourceRoot.value.money, snapshotRecord.value.money))
const fixedCoupon = computed(() =>
  asRecord(resourceRoot.value.fixedCoupon) ?? asRecord(snapshotRecord.value.fixedCoupon),
)
const rechargeTicket = computed(() =>
  asRecord(resourceRoot.value.rechargeTicket) ?? asRecord(snapshotRecord.value.rechargeTicket),
)
const shiningCrystalStock = computed(() =>
  asRecord(resourceRoot.value.shiningCrystal) ?? asRecord(snapshotRecord.value.shiningCrystal),
)
const rainbowPowderStock = computed(() =>
  asRecord(resourceRoot.value.rainbowPowder) ?? asRecord(snapshotRecord.value.rainbowPowder),
)
const activeResourceStone = computed<JsonRecord | null>(() => {
  for (const root of [resourceRoot.value, snapshotRecord.value]) {
    const requirements = Array.isArray(root.requirements) ? root.requirements : []
    const requirement = requirements.find((entry) => Number(asRecord(entry)?.color) === activeColorNumber.value)
    const requirementRecord = asRecord(requirement)
    if (requirementRecord) return requirementRecord
    const stones = Array.isArray(root.stones) ? root.stones : Array.isArray(root.colors) ? root.colors : []
    const match = stones.find((entry) => Number(asRecord(entry)?.color) === activeColorNumber.value)
    const record = asRecord(match)
    if (record) return record
  }
  return asRecord(stoneForColor(activeColorNumber.value))
})
const upgradeRequirement = computed(() => asRecord(activeResourceStone.value?.upgrade))
const changeRequirement = computed(() => asRecord(activeResourceStone.value?.change))
const hasResourceInfo = computed(() =>
  money.value !== null || !!shiningCrystalStock.value || !!rainbowPowderStock.value ||
  !!fixedCoupon.value || !!rechargeTicket.value ||
  requirementVisible(upgradeRequirement.value) || requirementVisible(changeRequirement.value),
)

function emptyConfig(characterName: string): WasherConfig {
  return {
    characterName,
    waitMs: 1500,
    colors: COLOR_META.map((color) => ({
      color: color.id,
      enabled: false,
      normalTarget: { attrId: 1, minValue: 0 },
      specialTarget: { attrId: color.special.id, minValue: 0 },
    })),
  }
}

function emptyState(characterName: string): WasherState {
  return {
    characterName,
    running: false,
    status: 'idle',
    currentColor: null,
    currentSlot: null,
    lastAction: '',
    lastMessage: '',
    lastErrorCode: '',
    lastError: '',
    actionCount: 0,
    optionChangeCount: 0,
    upgradeCount: 0,
    extendCount: 0,
    startedAt: 0,
    stoppedAt: 0,
    lastOperationStartedAt: 0,
    lastConfirmedAt: 0,
    nextOperationAt: 0,
    awaitingStateChange: false,
    updatedAt: 0,
  }
}

function normalizeConfig(characterName: string, raw: Partial<WasherConfig>): WasherConfig {
  const base = emptyConfig(characterName)
  const rawColors = Array.isArray(raw.colors) ? raw.colors : []
  base.waitMs = Number.isFinite(Number(raw.waitMs)) ? Number(raw.waitMs) : base.waitMs
  for (const color of base.colors) {
    const source = rawColors.find((entry) => Number(entry?.color) === color.color)
    if (!source) continue
    const record = source as unknown as {
      enabled?: unknown
      normalTarget?: Partial<StoneTarget>
      specialTarget?: Partial<StoneTarget>
      targets?: Array<Partial<StoneTarget> & { enabled?: boolean }>
    }
    const legacyTargets = Array.isArray(record.targets) ? record.targets : []
    const legacyNormal = legacyTargets.find((target, index) => target.enabled === true && !isSpecialSlot(index))
    const legacySpecial = legacyTargets.find((target, index) => target.enabled === true && isSpecialSlot(index))
    color.enabled = record.enabled === true
    color.normalTarget = normalizeTargetInput(
      color.color,
      false,
      record.normalTarget ?? legacyNormal,
      color.normalTarget,
    )
    color.specialTarget = normalizeTargetInput(
      color.color,
      true,
      record.specialTarget ?? legacySpecial,
      color.specialTarget,
    )
  }
  return base
}

function colorMeta(color: number) {
  return COLOR_META[color] ?? COLOR_META[0]
}

function isSpecialSlot(index: number): boolean {
  return (index + 1) % 3 === 0
}

function targetAttrOptions(color: number, special: boolean): AttrOption[] {
  const meta = colorMeta(color)
  const options: AttrOption[] = [...COMMON_ATTRS, meta.extra]
  if (special) options.push(meta.special)
  return options
}

function normalizeTargetInput(
  color: number,
  special: boolean,
  input: Partial<StoneTarget> | undefined,
  fallback: StoneTarget,
): StoneTarget {
  const options = targetAttrOptions(color, special)
  const attrId = Number(input?.attrId)
  const minValue = Number(input?.minValue)
  return {
    attrId: options.some((option) => option.id === attrId) ? attrId : fallback.attrId,
    minValue: Number.isFinite(minValue) ? Math.max(0, Math.trunc(minValue)) : fallback.minValue,
  }
}

function attrName(attrId: number): string {
  for (const meta of COLOR_META) {
    const option = [...COMMON_ATTRS, meta.extra, meta.special].find((entry) => entry.id === attrId)
    if (option) return option.name
  }
  return attrId > 0 ? `属性 #${attrId}` : '无属性'
}

function applyWasherState(next: WasherState) {
  state.value = next
}

function stoneForColor(color: number): StoneSnapshot | undefined {
  return state.value.lastSnapshot?.stones?.find((stone) => stone.color === color)
}

function slotForColor(color: number, index: number): StoneSlotSnapshot | undefined {
  return stoneForColor(color)?.slots?.find((slot) => slot.index === index) ?? stoneForColor(color)?.slots?.[index]
}

function openedCount(color: number): number {
  return stoneForColor(color)?.slots?.filter((slot) => slot.opened).length ?? 0
}

function remainingText(color: number): string {
  const stone = stoneForColor(color)
  if (!stone) return '— / —'
  return `${stone.remaining} / ${stone.max}`
}

function colorConfigFor(color: number): StoneColorConfig | undefined {
  return config.value.colors.find((entry) => entry.color === color)
}

function targetForSlot(color: number, index: number): StoneTarget {
  const colorConfig = colorConfigFor(color)
  if (!colorConfig) return { attrId: 1, minValue: 0 }
  return isSpecialSlot(index) ? colorConfig.specialTarget : colorConfig.normalTarget
}

function targetMet(color: number, index: number): boolean {
  const current = slotForColor(color, index)
  const target = targetForSlot(color, index)
  return !!current?.opened && current.attrId === target.attrId && current.value >= target.minValue
}

function targetText(color: number, index: number): string {
  const target = targetForSlot(color, index)
  const suffix = PERCENT_ATTRS.has(target.attrId) ? '%' : ''
  return `${attrName(target.attrId)} ≥ ${target.minValue}${suffix}`
}

function currentValueTagType(color: number, index: number): TagType {
  const stone = stoneForColor(color)
  const colorConfig = colorConfigFor(color)
  if (!stone || !colorConfig) return 'info'
  if (index < stone.topIndex || targetMet(color, index)) return 'success'
  if (colorConfig.enabled && index === stone.topIndex) return 'warning'
  return 'info'
}

function slotProgress(color: number, index: number): { text: string; type: TagType } {
  const stone = stoneForColor(color)
  const colorConfig = colorConfigFor(color)
  if (!stone || !colorConfig) return { text: '未读取', type: 'info' }
  if (index < stone.topIndex) return { text: '已固定', type: 'info' }
  if (index === stone.topIndex) {
    if (!colorConfig.enabled) return { text: '保持当前', type: 'info' }
    if (targetMet(color, index)) {
      return index === 11
        ? { text: '已完成', type: 'success' }
        : { text: '已达标', type: 'success' }
    }
    return { text: '待洗', type: 'warning' }
  }
  if (!colorConfig.enabled) return { text: '未开放', type: 'info' }
  if (index === stone.topIndex + 1) return { text: '待强化', type: 'warning' }
  return { text: '等待前序', type: 'info' }
}

function currentSlotText(color: number, index: number): string {
  const slot = slotForColor(color, index)
  if (!slot) return '尚未读取'
  const suffix = PERCENT_ATTRS.has(slot.attrId) ? '%' : ''
  return `${attrName(slot.attrId)} ${slot.value}${suffix}`
}

function targetRowClass({ rowIndex }: { rowIndex: number }): string {
  if (state.value.currentColor === activeColorNumber.value && state.value.currentSlot === rowIndex) {
    return 'active-target-row'
  }
  return ''
}

function statusLabel(status: string): string {
  const labels: Record<string, string> = {
    idle: '空闲',
    running: '运行中',
    stopped: '已停止',
    done: '已完成',
    'no-fixed-coupon': '固定券不足',
    'no-recharge-ticket': '充值券不足',
    'insufficient-money': '金币不足',
    'insufficient-change-material': '替换材料不足',
    'insufficient-upgrade-crystal': '闪亮结晶不足',
    'insufficient-rainbow-powder': '七彩粉末不足',
    'max-recharge': '变更次数已达上限',
    'state-not-applied': '状态未生效',
    'incompatible-existing-slot': '已有格子不符合目标',
    error: '出错',
  }
  return labels[status] || status || '空闲'
}

function statusTagType(status: string): TagType {
  if (status === 'running') return 'warning'
  if (status === 'done') return 'success'
  if (status === 'idle' || status === 'stopped') return 'info'
  return 'danger'
}

function asRecord(value: unknown): JsonRecord | null {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
    ? value as JsonRecord
    : null
}

function firstNumber(...values: unknown[]): number | null {
  for (const value of values) {
    if (value === null || value === undefined || value === '') continue
    const number = Number(value)
    if (Number.isFinite(number)) return number
  }
  return null
}

function resourceAmount(resource: JsonRecord | null): string {
  if (!resource) return '—'
  const owned = firstNumber(resource.owned, resource.count)
  const required = firstNumber(resource.required)
  if (owned === null) return '—'
  return required === null ? formatNumber(owned) : `${formatNumber(owned)} / ${formatNumber(required)}`
}

function resourceTagType(resource: JsonRecord | null): TagType {
  if (!resource) return 'info'
  if (resource.enough === true) return 'success'
  if (resource.enough === false) return 'danger'
  const owned = firstNumber(resource.owned, resource.count)
  const required = firstNumber(resource.required)
  return owned !== null && required !== null && owned >= required ? 'success' : 'info'
}

function requirementResource(requirement: JsonRecord | null, key: string): JsonRecord | null {
  return requirement ? asRecord(requirement[key]) : null
}

function requirementMoney(requirement: JsonRecord | null): number | null {
  return requirement ? firstNumber(requirement.moneyRequired, requirement.money) : null
}

function requirementVisible(requirement: JsonRecord | null): boolean {
  if (!requirement || requirement.resolved === false) return false
  return !!requirementResource(requirement, 'crystal') ||
    !!requirementResource(requirement, 'rainbowPowder') ||
    requirementMoney(requirement) !== null
}

function formatNumber(value: number | null): string {
  return value === null ? '—' : value.toLocaleString('zh-CN')
}

async function responseError(response: Response): Promise<string> {
  try {
    const body = await response.json() as JsonRecord
    return String(body.error || body.detail || body.message || `HTTP ${response.status}`)
  } catch {
    return `HTTP ${response.status}`
  }
}

async function loadSelected() {
  const name = selectedName.value
  const version = ++selectionVersion
  stopPoll()
  config.value = emptyConfig(name)
  state.value = emptyState(name)
  logs.value = []
  readError.value = ''
  hasConfig.value = false
  if (!name) return

  try {
    const response = await fetch(`/api/enchant-stone/configs/${encodeURIComponent(name)}`)
    if (version !== selectionVersion || name !== selectedName.value) return
    if (response.status === 404) {
      await refreshLive(true)
      startPoll()
      return
    }
    if (!response.ok) throw new Error(await responseError(response))
    config.value = normalizeConfig(name, await response.json() as WasherConfig)
    hasConfig.value = true
    await refreshLive(true)
  } catch (error: unknown) {
    ElMessage.error(`加载配置失败：${errorMessage(error)}`)
  } finally {
    if (version === selectionVersion && name === selectedName.value) startPoll()
  }
}

async function persistConfig(
  showSuccess: boolean,
  name = selectedName.value,
  version = selectionVersion,
): Promise<boolean> {
  if (!name) return false
  const payload = JSON.stringify({ ...config.value, characterName: name })
  saving.value = true
  try {
    const response = await fetch(`/api/enchant-stone/configs/${encodeURIComponent(name)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: payload,
    })
    if (!response.ok) throw new Error(await responseError(response))
    const persisted = await response.json() as WasherConfig
    if (version !== selectionVersion || name !== selectedName.value) return false
    config.value = normalizeConfig(name, persisted)
    hasConfig.value = true
    if (!savedNames.value.includes(name)) savedNames.value = [...savedNames.value, name]
    if (showSuccess) ElMessage.success('配置已保存')
    return true
  } catch (error: unknown) {
    ElMessage.error(`保存失败：${errorMessage(error)}`)
    return false
  } finally {
    saving.value = false
  }
}

async function saveConfig() {
  if (await persistConfig(true)) await refreshLive(true)
}

async function startTask() {
  const name = selectedName.value
  const version = selectionVersion
  if (!selectedOnline.value) {
    ElMessage.warning('角色当前不在线')
    return
  }
  if (enabledTotal.value === 0) {
    ElMessage.warning('请至少启用一种颜色')
    return
  }
  starting.value = true
  try {
    if (!await persistConfig(false, name, version)) return
    if (version !== selectionVersion || name !== selectedName.value) return
    const response = await fetch(`/api/enchant-stone/start/${encodeURIComponent(name)}`, { method: 'POST' })
    if (!response.ok) throw new Error(await responseError(response))
    const result = await response.json() as { state?: WasherState }
    if (version !== selectionVersion || name !== selectedName.value) return
    if (result.state) applyWasherState(result.state)
    ElMessage.success('自动洗石头已启动')
    await refreshLive()
  } catch (error: unknown) {
    ElMessage.error(`启动失败：${errorMessage(error)}`)
  } finally {
    starting.value = false
  }
}

async function stopTask() {
  const name = selectedName.value
  const version = selectionVersion
  if (!name) return
  stopping.value = true
  try {
    const response = await fetch(`/api/enchant-stone/stop/${encodeURIComponent(name)}`, { method: 'POST' })
    if (!response.ok) throw new Error(await responseError(response))
    const result = await response.json() as { state?: WasherState }
    if (version !== selectionVersion || name !== selectedName.value) return
    if (result.state) applyWasherState(result.state)
    ElMessage.success('任务已停止')
    await refreshLive()
  } catch (error: unknown) {
    ElMessage.error(`停止失败：${errorMessage(error)}`)
  } finally {
    stopping.value = false
  }
}

async function refreshLive(queryGame = false) {
  const name = selectedName.value
  if (!name || refreshing.value) return
  refreshing.value = true
  try {
    if (queryGame && selectedOnline.value) {
      const refreshResponse = await fetch(
        `/api/enchant-stone/refresh/${encodeURIComponent(name)}`,
        { method: 'POST' },
      )
      if (name !== selectedName.value) return
      if (refreshResponse.ok) {
        const refreshed = await refreshResponse.json() as { state?: WasherState }
        if (refreshed.state) applyWasherState(refreshed.state)
        readError.value = ''
      } else {
        readError.value = await responseError(refreshResponse)
      }
    }
    const [stateResponse, logResponse] = await Promise.all([
      fetch(`/api/enchant-stone/state/${encodeURIComponent(name)}`),
      fetch(`/api/enchant-stone/logs/${encodeURIComponent(name)}`),
    ])
    if (name !== selectedName.value) return
    if (stateResponse.ok) applyWasherState(await stateResponse.json() as WasherState)
    if (logResponse.ok) {
      const nextLogs = await logResponse.json() as string[]
      const changed = nextLogs.length !== logs.value.length ||
        nextLogs[nextLogs.length - 1] !== logs.value[logs.value.length - 1]
      logs.value = Array.isArray(nextLogs) ? nextLogs : []
      if (changed) {
        await nextTick()
        if (logRef.value) logRef.value.scrollTop = logRef.value.scrollHeight
      }
    }
  } catch (error: unknown) {
    // 轮询失败时保留最后一次状态；主动读取失败则明确显示原因。
    if (queryGame && name === selectedName.value) readError.value = errorMessage(error)
  } finally {
    refreshing.value = false
  }
}

function startPoll() {
  stopPoll()
  pollTimer = setInterval(() => void refreshLive(), 1500)
}

function stopPoll() {
  if (pollTimer !== null) clearInterval(pollTimer)
  pollTimer = null
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}

async function loadConfigNames() {
  try {
    const response = await fetch('/api/enchant-stone/configs')
    if (!response.ok) return
    const configs = await response.json() as WasherConfig[]
    savedNames.value = Array.isArray(configs)
      ? configs.map((entry) => String(entry.characterName || '')).filter(Boolean)
      : []
  } catch {
    // 在线实例仍可使用；配置列表会在下次进入页面时重试。
  }
}

watch(selectedName, () => void loadSelected())

watch(characterOptions, (options) => {
  if (selectedName.value || options.length === 0) return
  const preferred = selectedInstance.value?.characterName
  selectedName.value = options.find((option) => option.value === preferred)?.value ?? options[0].value
}, { immediate: true })

onMounted(() => void loadConfigNames())
onUnmounted(stopPoll)
</script>

<style scoped>
.enchant-stone-view {
  max-width: 1180px;
}

.config-card,
.status-card {
  margin-bottom: 16px;
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
}

.card-header.compact {
  min-height: 28px;
}

.card-title {
  color: var(--el-text-color-primary);
  font-size: 16px;
  font-weight: 600;
}

.card-subtitle {
  margin-top: 4px;
  color: var(--el-text-color-secondary);
  font-size: 12px;
}

.empty-hint {
  padding: 28px 0;
  color: var(--el-text-color-placeholder);
  text-align: center;
}

.settings-row {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.settings-label {
  font-weight: 500;
}

.section-gap {
  margin: 14px 0;
}

.hint,
.muted {
  color: var(--el-text-color-secondary);
  font-size: 12px;
}

.color-tabs {
  margin-top: 14px;
}

.color-session-row,
.color-session-switch,
.rule-editor {
  display: flex;
  align-items: center;
}

.color-session-row {
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 12px;
}

.color-session-switch,
.rule-editor {
  gap: 12px;
}

.rule-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
  margin-bottom: 12px;
}

.rule-card {
  padding: 12px 14px;
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 7px;
  background: var(--el-fill-color-lighter);
}

.special-rule-card {
  border-color: var(--el-color-warning-light-7);
}

.rule-title {
  color: var(--el-text-color-primary);
  font-weight: 600;
}

.rule-slots {
  margin: 3px 0 10px;
  color: var(--el-text-color-secondary);
  font-size: 12px;
}

.tab-label {
  display: inline-flex;
  align-items: center;
  gap: 7px;
}

.color-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  box-shadow: 0 0 0 2px rgb(255 255 255 / 75%);
}

.stone-summary {
  display: grid;
  grid-template-columns: repeat(4, minmax(130px, 1fr));
  gap: 10px;
  margin-bottom: 12px;
}

.stone-summary > div {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 12px;
  padding: 9px 12px;
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 6px;
  background: var(--el-fill-color-lighter);
}

.summary-label {
  color: var(--el-text-color-secondary);
  font-size: 12px;
}

.target-table :deep(.active-target-row > td.el-table__cell) {
  background: var(--el-color-warning-light-9) !important;
}

.actions {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 16px;
}

.status-actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.error-text {
  color: var(--el-color-danger);
}

.resource-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.resource-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 18px;
  min-width: 180px;
  padding: 9px 12px;
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 6px;
}

.requirement-line {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
  margin-top: 10px;
  color: var(--el-text-color-regular);
  font-size: 13px;
}

.log-output {
  min-height: 90px;
  max-height: 250px;
  margin: 0;
  padding: 12px;
  overflow: auto;
  border-radius: 6px;
  background: #18212b;
  color: #d7e1eb;
  font-family: Consolas, 'Courier New', monospace;
  font-size: 12px;
  line-height: 1.65;
  white-space: pre-wrap;
  word-break: break-all;
}

@media (max-width: 900px) {
  .rule-grid {
    grid-template-columns: 1fr;
  }

  .color-session-row {
    align-items: flex-start;
    flex-direction: column;
  }

  .stone-summary {
    grid-template-columns: repeat(2, minmax(130px, 1fr));
  }

  .card-header {
    align-items: flex-start;
    flex-direction: column;
  }

  .status-descriptions :deep(.el-descriptions__body) {
    overflow-x: auto;
  }
}
</style>

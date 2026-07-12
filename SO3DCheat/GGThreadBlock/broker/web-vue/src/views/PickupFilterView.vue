<template>
  <div class="pickupfilter-view">
    <el-card shadow="never" class="config-card">
      <template #header>
        <div class="card-header">
          <span>拾取设置 — 全局共享配置</span>
          <div class="header-tags">
            <el-tag :type="config.pickupExecutionMode === 'nativeGame' ? 'primary' : 'info'" size="small">
              {{ config.pickupExecutionMode === 'nativeGame' ? '游戏原生拾取' : 'DLL 自动拾取' }}
            </el-tag>
            <el-tag :type="config.mode === 'whitelist' ? 'warning' : 'info'" size="small">
              {{ config.mode === 'whitelist' ? `白名单 (${config.items.length})` : '全部拾取' }}
            </el-tag>
          </div>
        </div>
      </template>

      <p class="hint">
        执行方式与过滤规则会立即同步到所有在线实例，新上线实例也会自动补推。
        游戏原生拾取由副本工作流寻路靠近后触发游戏内掉落点击，DLL 的直接发包拾取会暂停。
      </p>

      <el-form label-width="110px" size="default" style="max-width: 820px">
        <el-form-item label="拾取执行方式">
          <el-radio-group v-model="config.pickupExecutionMode">
            <el-radio-button label="autoPickup">DLL 自动拾取</el-radio-button>
            <el-radio-button label="nativeGame">游戏原生拾取</el-radio-button>
          </el-radio-group>
        </el-form-item>

        <el-form-item label="过滤规则">
          <el-radio-group v-model="config.mode">
            <el-radio-button label="all">全部拾取</el-radio-button>
            <el-radio-button label="whitelist">过滤拾取(白名单)</el-radio-button>
          </el-radio-group>
          <span class="hint">
            全部拾取 = 周围可捡的都捡。过滤拾取 = 只捡下面白名单里的物品。
          </span>
        </el-form-item>

        <template v-if="config.mode === 'whitelist'">
          <el-form-item label="添加物品">
            <el-select
              v-model="picked"
              filterable
              remote
              reserve-keyword
              :remote-method="doSearch"
              :loading="searching"
              placeholder="输入物品名或 itemId 搜索"
              style="width: 420px"
              @change="onPick"
            >
              <el-option
                v-for="o in searchResults"
                :key="o.itemId"
                :label="`${o.name || '(无名)'} (#${o.itemId})`"
                :value="o.itemId"
              />
            </el-select>
            <el-input-number
              v-model="manualId"
              :min="1"
              :controls="false"
              placeholder="或直接填 itemId"
              style="width: 150px; margin-left: 8px"
            />
            <el-button style="margin-left: 8px" @click="addManual">加入</el-button>
            <div class="hint" v-if="searchStatus && !searchStatus.loaded" style="margin-top: 4px">
              未找到 item_names.json,名字搜索不可用 —— 仍可直接填 itemId 加入。
            </div>
          </el-form-item>

          <el-form-item label="白名单">
            <div style="width: 100%">
              <el-table :data="config.items" size="small" max-height="380" empty-text="白名单为空(此时不会捡任何东西)">
                <el-table-column label="itemId" width="120" prop="itemId" />
                <el-table-column label="物品名" min-width="220">
                  <template #default="{ row }">
                    {{ row.name || '(无名)' }}
                  </template>
                </el-table-column>
                <el-table-column label="操作" width="90">
                  <template #default="{ $index }">
                    <el-button size="small" text type="danger" @click="removeAt($index)">移除</el-button>
                  </template>
                </el-table-column>
              </el-table>
              <div class="hint" style="margin-top: 6px">共 {{ config.items.length }} 种。</div>
            </div>
          </el-form-item>

          <el-form-item label="模糊拾取">
            <div style="width: 100%">
              <div class="fuzzy-input">
                <el-input
                  v-model.trim="fuzzyInput"
                  placeholder="输入名称关键字，例如：宝石"
                  clearable
                  @keyup.enter="addFuzzyKeyword"
                />
                <el-button @click="addFuzzyKeyword">加入</el-button>
              </div>
              <div class="fuzzy-tags">
                <el-tag
                  v-for="(keyword, index) in config.fuzzyKeywords"
                  :key="keyword.toLowerCase()"
                  closable
                  @close="removeFuzzyKeyword(index)"
                >{{ keyword }}</el-tag>
                <span v-if="config.fuzzyKeywords.length === 0" class="hint">
                  未配置关键字；精确白名单仍照常生效。
                </span>
              </div>
              <div class="hint" style="margin-top: 6px">
                名称包含任一关键字的物品都会拾取；保存后与上方精确白名单合并下发。
              </div>
            </div>
          </el-form-item>
        </template>

        <el-form-item>
          <el-button type="primary" @click="save" :loading="saving">保存并下发</el-button>
          <el-button @click="reload">重新加载</el-button>
        </el-form-item>
      </el-form>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { ElMessage } from 'element-plus'

interface FilterItem {
  itemId: number
  name: string
}
interface PickupFilterConfig {
  pickupExecutionMode: 'autoPickup' | 'nativeGame'
  mode: 'all' | 'whitelist'
  items: FilterItem[]
  fuzzyKeywords: string[]
}
interface SearchStatus {
  loaded: boolean
  count: number
  source: string
}

const config = ref<PickupFilterConfig>(defaultConfig())
const saving = ref(false)

const picked = ref<number | undefined>(undefined)
const manualId = ref<number | undefined>(undefined)
const searchResults = ref<FilterItem[]>([])
const searching = ref(false)
const searchStatus = ref<SearchStatus | null>(null)
const fuzzyInput = ref('')

function defaultConfig(): PickupFilterConfig {
  return { pickupExecutionMode: 'autoPickup', mode: 'all', items: [], fuzzyKeywords: [] }
}

function normalize(cfg: Partial<PickupFilterConfig>): PickupFilterConfig {
  const seen = new Set<number>()
  const items: FilterItem[] = []
  for (const it of Array.isArray(cfg.items) ? cfg.items : []) {
    const itemId = Math.floor(Number(it?.itemId))
    if (!Number.isInteger(itemId) || itemId <= 0 || seen.has(itemId)) continue
    seen.add(itemId)
    items.push({ itemId, name: typeof it?.name === 'string' ? it.name : '' })
  }
  const seenKeywords = new Set<string>()
  const fuzzyKeywords: string[] = []
  for (const value of Array.isArray(cfg.fuzzyKeywords) ? cfg.fuzzyKeywords : []) {
    const keyword = String(value ?? '').trim()
    const key = keyword.toLowerCase()
    if (!keyword || seenKeywords.has(key)) continue
    seenKeywords.add(key)
    fuzzyKeywords.push(keyword)
  }
  return {
    pickupExecutionMode: cfg.pickupExecutionMode === 'nativeGame' ? 'nativeGame' : 'autoPickup',
    mode: cfg.mode === 'whitelist' ? 'whitelist' : 'all',
    items,
    fuzzyKeywords,
  }
}

function addFuzzyKeyword() {
  const keyword = fuzzyInput.value.trim()
  if (!keyword) return
  if (config.value.fuzzyKeywords.some((value) => value.toLowerCase() === keyword.toLowerCase())) {
    ElMessage.info(`关键字“${keyword}”已存在`)
    return
  }
  config.value.fuzzyKeywords.push(keyword)
  fuzzyInput.value = ''
}

function removeFuzzyKeyword(index: number) {
  config.value.fuzzyKeywords.splice(index, 1)
}

let searchSeq = 0
async function doSearch(query: string) {
  const q = (query || '').trim()
  if (!q) {
    searchResults.value = []
    return
  }
  const mySeq = ++searchSeq
  searching.value = true
  try {
    const res = await fetch(`/api/pickup-filter/item-search?q=${encodeURIComponent(q)}&limit=50`)
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const data = await res.json()
    if (mySeq !== searchSeq) return // 过期结果丢弃
    searchStatus.value = { loaded: !!data.loaded, count: Number(data.count) || 0, source: String(data.source || '') }
    searchResults.value = Array.isArray(data.items) ? data.items : []
  } catch (e: any) {
    if (mySeq === searchSeq) searchResults.value = []
  } finally {
    if (mySeq === searchSeq) searching.value = false
  }
}

function addItem(itemId: number, name: string) {
  if (!Number.isInteger(itemId) || itemId <= 0) return
  if (config.value.items.some((i) => i.itemId === itemId)) {
    ElMessage.info(`#${itemId} 已在白名单`)
    return
  }
  config.value.items.push({ itemId, name })
}

function onPick(itemId: number | undefined) {
  if (itemId === undefined) return
  const hit = searchResults.value.find((o) => o.itemId === itemId)
  addItem(itemId, hit?.name || '')
  picked.value = undefined
  searchResults.value = []
}

function addManual() {
  const id = Math.floor(Number(manualId.value))
  if (!Number.isInteger(id) || id <= 0) {
    ElMessage.warning('请输入有效的 itemId')
    return
  }
  addItem(id, '')
  manualId.value = undefined
}

function removeAt(index: number) {
  config.value.items.splice(index, 1)
}

async function reload() {
  try {
    const res = await fetch('/api/pickup-filter/config')
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    config.value = normalize((await res.json()) as Partial<PickupFilterConfig>)
  } catch (e: any) {
    ElMessage.error(`加载失败: ${e.message}`)
  }
}

async function save() {
  saving.value = true
  try {
    const res = await fetch('/api/pickup-filter/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    config.value = normalize((await res.json()) as Partial<PickupFilterConfig>)
    ElMessage.success('已保存并下发到所有在线实例')
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  } finally {
    saving.value = false
  }
}

onMounted(reload)
</script>

<style scoped>
.pickupfilter-view { padding: 8px; }
.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 8px;
}
.header-tags {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
}
.hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-left: 8px;
  line-height: 1.6;
}
.fuzzy-input {
  display: flex;
  gap: 8px;
  max-width: 600px;
}
.fuzzy-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 10px;
}
</style>

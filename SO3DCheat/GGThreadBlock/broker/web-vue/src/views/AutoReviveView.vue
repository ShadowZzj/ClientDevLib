<template>
  <div class="autorevive-view">
    <!-- 顶部操作栏(sticky):角色配置的「保存/重置」+「运行控制」始终可见,
         避免保存按钮埋在页面中间找不到。 -->
    <div class="action-bar">
      <div class="ab-left">
        <span class="ab-title">自动复活</span>
        <el-tag v-if="characterKey" effect="dark" size="small">{{ characterKey }}</el-tag>
        <el-tag v-else type="info" size="small">未选角色</el-tag>
        <el-tag :type="phaseTagType" size="small">broker: {{ state?.phase || 'idle' }}</el-tag>
        <el-tag v-if="state?.phase === 'running' && state.currentStepIdx >= 0" type="warning" size="small">
          step {{ state.currentStepIdx + 1 }}/{{ config.steps.length }}
        </el-tag>
        <el-tag v-if="pendingRemainingSec > 0" type="danger" size="small">
          排程 {{ Math.ceil(pendingRemainingSec) }}s
        </el-tag>
      </div>
      <div class="ab-right">
        <span class="ab-group-label">配置</span>
        <el-button type="success" :disabled="!characterKey" @click="saveConfig">保存配置</el-button>
        <el-button :disabled="!characterKey" @click="resetConfig">重置</el-button>
        <el-divider direction="vertical" />
        <span class="ab-group-label">运行</span>
        <el-button type="danger" :loading="running" :disabled="running || !characterKey"
                   title="不看死活,立刻按脚本跑一遍当前角色" @click="runNow">
          {{ running ? '执行中…' : '立即执行' }}
        </el-button>
        <el-button type="danger" plain :loading="revivingAll"
                   title="对所有 HP=0 的在线角色各跑一遍复活脚本" @click="runAllDeadNow">全部复活</el-button>
        <el-button :disabled="!running" @click="abortScript">中止</el-button>
      </div>
    </div>

    <p class="hint">
      无人值守:配置存在 broker(<code>auto_revive.json</code>),浏览器只是遥控器 —— 关掉/切 tab 都不影响。
      下面各区块改完,统一点右上角 <strong>保存配置</strong> 写入 broker。
      <strong>「模板库」是全局共享的,跟「保存配置」是两码事。</strong>
    </p>

    <el-divider content-position="left">① 角色配置</el-divider>

    <el-form label-width="100px" style="max-width: 980px">
      <el-form-item label="当前实例">
        <span v-if="selectedInstance">
          <strong>{{ selectedInstance.characterName || `pid=${selectedInstance.pid}` }}</strong>
          (pid {{ selectedInstance.pid }})
        </span>
        <span v-else class="muted">未选择实例</span>
      </el-form-item>

      <el-form-item label="状态">
        <el-tag v-if="liveStatus?.isDead" type="danger">死亡 HP=0</el-tag>
        <el-tag v-else-if="liveStatus" type="success">存活 HP={{ liveStatus.hp ?? '?' }}</el-tag>
        <el-tag v-else type="info">未刷新</el-tag>
        <el-tag v-if="liveStatus" type="info" style="margin-left: 8px">
          地图 {{ liveStatus.mapId ?? '?' }}
          <span v-if="mapName" class="muted">({{ mapName }})</span>
        </el-tag>
        <el-tag v-if="liveStatus" type="info" style="margin-left: 8px">userId {{ liveStatus.userId ?? '?' }}</el-tag>
        <el-tag v-if="liveStatus && hasLivePos" type="info" style="margin-left: 8px">
          坐标 ({{ Math.round(liveStatus!.posX as number) }}, {{ Math.round(liveStatus!.posY as number) }})
        </el-tag>
        <el-button :loading="loadingStatus" @click="refreshStatus" size="small" style="margin-left: 12px">
          刷新
        </el-button>
        <el-tag v-if="state?.pausedByGm" type="warning" style="margin-left: 6px">
          GM 暂停: {{ state.nearbyGmName || '?' }}
          <span v-if="state.nearbyGmDistance !== undefined">({{ Math.round(state.nearbyGmDistance) }})</span>
        </el-tag>
        <el-tag v-if="state?.pausedBySchedule" type="warning" style="margin-left: 6px">
          时间段外暂停
        </el-tag>
      </el-form-item>

      <el-form-item label="自动运行">
        <el-checkbox v-model="config.autoRun">
          broker 检测到 HP=0 时自动执行复活脚本
        </el-checkbox>
        <span class="sub-hint">改完点「保存配置」生效。关掉会清掉当前 pending 排程。</span>
      </el-form-item>

      <el-form-item label="GM 暂停">
        <el-checkbox v-model="config.pauseOnNearbyGm">
          检测到 GM 在旁边时暂停复活倒计时
        </el-checkbox>
        <span class="sub-hint">GM 名单读取“GM 自动回复”的 GM 角色名。</span>
      </el-form-item>

      <el-form-item label="位置卡住">
        <el-checkbox v-model="config.stuckReviveEnabled">
          存活时坐标
        </el-checkbox>
        <el-input-number
          v-model="config.stuckReviveMinutes"
          :min="1"
          :step="1"
          :disabled="!config.stuckReviveEnabled"
          style="width: 110px; margin: 0 6px"
        />
        <span>分钟没变化 → 自动执行复活流程</span>
        <span class="sub-hint">
          需先开“自动运行”。仅在存活时判定;死亡仍走上面的延迟排程。开了“GM 暂停”则 GM 在旁时不触发。
          <span v-if="config.stuckReviveEnabled && stuckIdleSec > 0" style="color: var(--el-color-warning);">
            当前已 {{ Math.floor(stuckIdleSec) }}s 未移动
          </span>
        </span>
      </el-form-item>

      <el-form-item label="默认延迟">
        <el-input-number v-model="config.delayMinMin" :min="0" :step="1" style="width: 110px" />
        <span style="margin: 0 8px">~</span>
        <el-input-number v-model="config.delayMinMax" :min="0" :step="1" style="width: 110px" />
        <span class="sub-hint">
          分钟,未配置时间段时使用。配置时间段后,只在时间段内启用,并使用对应时间段的延迟。
          <span v-if="pendingRemainingSec > 0" style="color: var(--el-color-warning);">
            <strong>broker 已排程,剩 {{ Math.ceil(pendingRemainingSec) }}s 执行</strong>
            <el-button link type="primary" size="small" @click="cancelPending">取消排程</el-button>
          </span>
        </span>
      </el-form-item>

      <el-form-item label="时间段">
        <div class="schedule-editor">
          <div class="schedule-hint">
            为空 = 全天按默认延迟运行。添加后,只有启用且命中的时间段会自动复活;跨午夜时间段也支持。
          </div>
          <el-table :data="config.scheduleWindows" size="small" style="width: 760px">
            <el-table-column label="开" width="58">
              <template #default="{ row }">
                <el-checkbox v-model="row.enabled" />
              </template>
            </el-table-column>
            <el-table-column label="开始" width="130">
              <template #default="{ row }">
                <el-time-picker v-model="row.start" format="HH:mm" value-format="HH:mm" :clearable="false" size="small" style="width: 110px" />
              </template>
            </el-table-column>
            <el-table-column label="结束" width="130">
              <template #default="{ row }">
                <el-time-picker v-model="row.end" format="HH:mm" value-format="HH:mm" :clearable="false" size="small" style="width: 110px" />
              </template>
            </el-table-column>
            <el-table-column label="延迟分钟" min-width="240">
              <template #default="{ row }">
                <el-input-number v-model="row.delayMinMin" :min="0" :step="1" size="small" style="width: 95px" />
                <span style="margin: 0 8px">~</span>
                <el-input-number v-model="row.delayMinMax" :min="0" :step="1" size="small" style="width: 95px" />
              </template>
            </el-table-column>
            <el-table-column label="操作" width="90">
              <template #default="{ $index }">
                <el-button size="small" type="danger" @click="removeScheduleWindow($index)">删</el-button>
              </template>
            </el-table-column>
          </el-table>
          <div class="schedule-actions">
            <el-button size="small" @click="addScheduleWindow">+ 加时间段</el-button>
            <el-button size="small" @click="sortScheduleWindows">按开始时间排序</el-button>
          </div>
        </div>
      </el-form-item>

    </el-form>

    <el-divider content-position="left">② 脚本步骤</el-divider>

    <p class="hint">
      复活 / 位置卡住 / 手动「立即执行」时,broker 按顺序跑这些步骤。
      共 <strong>{{ config.steps.length }}</strong> 步 · 估计 {{ totalDurationSec.toFixed(1) }}s。改完点右上角「保存配置」。
    </p>

    <el-table :data="config.steps" size="small" style="max-width: 1100px">
      <el-table-column type="index" label="#" width="50" />
      <el-table-column prop="type" label="动作" width="170">
        <template #default="{ row, $index }">
          <el-select v-model="row.type" size="small" @change="onStepTypeChange($index)">
            <el-option label="reviveToTown (回主城)" value="reviveToTown" />
            <el-option label="moveTo (走到 x,y)" value="moveTo" />
            <el-option label="waitInTown (等到达主城)" value="waitInTown" />
            <el-option label="waitOutOfTown (等离开主城)" value="waitOutOfTown" />
            <el-option label="sendDialogSelectRaw (411026 NPC 跳图)" value="sendDialogSelectRaw" />
            <el-option label="pressHookedKey (调用 123.dll HOOKPROC 按键)" value="pressHookedKey" />
            <el-option label="sleep (空等)" value="sleep" />
          </el-select>
        </template>
      </el-table-column>

      <el-table-column label="参数" min-width="320">
        <template #default="{ row }">
          <template v-if="row.type === 'moveTo'">
            x<el-input-number v-model="row.x" :precision="2" :step="50" size="small" style="width:110px; margin-left:4px" />
            y<el-input-number v-model="row.y" :precision="2" :step="50" size="small" style="width:110px; margin-left:4px" />
            <el-select v-model="row.action" size="small" style="width:120px; margin-left:6px">
              <el-option label="纯走路" :value="1" />
              <el-option label="走+攻击" :value="3" />
            </el-select>
          </template>
          <template v-else-if="row.type === 'sendDialogSelectRaw'">
            <el-select
              :model-value="dialogTemplateIdFor(row)"
              size="small"
              filterable
              placeholder="选模板套用"
              style="width:160px; margin-right:6px"
              @change="(id: string) => applyDialogTemplate(row, id)"
            >
              <el-option
                v-for="t in dialogTemplates"
                :key="t.id"
                :label="`${t.name} (${t.npcId}/${t.option})`"
                :value="t.id"
              />
            </el-select>
            <el-tag v-if="dialogTemplateNameFor(row)" type="success" size="small" style="margin-right:6px">
              模板: {{ dialogTemplateNameFor(row) }}
            </el-tag>
            <el-tag v-else type="info" size="small" style="margin-right:6px">自定义</el-tag>
            npcId<el-input-number v-model="row.npcId" :min="0" :step="1" size="small" style="width:140px; margin-left:4px" />
            option<el-input-number v-model="row.option" :min="0" :step="1" size="small" style="width:140px; margin-left:4px" />
            <el-button v-if="dialog?.open" size="small" link type="primary" @click="fillFromCurrentNpc(row)">
              从当前 NPC 填入
            </el-button>
            <el-button size="small" link type="success" @click="saveDialogTemplateFromRow(row)">
              存为模板
            </el-button>
          </template>
          <template v-else-if="row.type === 'pressHookedKey'">
            vkey<el-input-number v-model="row.vkey" :min="1" :max="255" :step="1" size="small" style="width:110px; margin-left:4px" />
            <el-checkbox v-model="row.alt" style="margin-left:8px">Alt</el-checkbox>
            <el-checkbox v-model="row.ctrl">Ctrl</el-checkbox>
            <el-checkbox v-model="row.shift">Shift</el-checkbox>
          </template>
          <template v-else-if="row.type === 'waitInTown' || row.type === 'waitOutOfTown'">
            目标mapId<el-input-number v-model="row.mapId" :min="0" :step="1" size="small" style="width:120px; margin-left:4px" />
            超时(ms)<el-input-number v-model="row.timeoutMs" :min="500" :step="500" size="small" style="width:120px; margin-left:4px" />
          </template>
          <template v-else-if="row.type === 'sleep'">
            <span class="muted">只等下方 delay 时长</span>
          </template>
          <template v-else>
            <span class="muted">(无参数)</span>
          </template>
        </template>
      </el-table-column>

      <el-table-column label="后置延迟ms" width="130">
        <template #default="{ row }">
          <el-input-number v-model="row.delayMs" :min="0" :step="100" size="small" style="width:110px" />
        </template>
      </el-table-column>

      <el-table-column label="循环次数" width="120">
        <template #default="{ row }">
          <el-input-number v-model="row.repeatCount" :min="1" :max="999" :step="1" size="small" style="width:96px" />
        </template>
      </el-table-column>

      <el-table-column label="循环延迟ms" width="130">
        <template #default="{ row }">
          <el-input-number v-model="row.repeatDelayMs" :min="0" :step="100" size="small" style="width:110px" />
        </template>
      </el-table-column>

      <el-table-column label="操作" width="170">
        <template #default="{ $index }">
          <el-button size="small" :disabled="$index === 0" @click="moveStep($index, -1)">↑</el-button>
          <el-button size="small" :disabled="$index === config.steps.length - 1" @click="moveStep($index, 1)">↓</el-button>
          <el-button size="small" type="danger" @click="removeStep($index)">删</el-button>
        </template>
      </el-table-column>
    </el-table>

    <div class="step-toolbar">
      <el-button @click="addStep">+ 加一步</el-button>
      <el-button @click="addPressKeyStep">+ 按键(Alt+W)</el-button>
      <el-button @click="loadDefaultScript">加载默认脚本</el-button>
      <el-button link type="primary" @click="showCapturedHooks">查看截获的 HOOKPROC</el-button>
      <span class="sub-hint">编辑步骤后别忘了点右上角「保存配置」。</span>
    </div>

    <el-divider content-position="left">③ 定点挂机 + 模板库</el-divider>

    <div class="template-bar">
      <div class="template-row">
        <span class="template-label">脚本步骤模板库(全局共享)</span>
        <el-select
          v-model="stepTemplatePick"
          size="small"
          filterable
          clearable
          placeholder="套用一个模板的步骤"
          style="width:240px"
        >
          <el-option
            v-for="t in stepTemplates"
            :key="t.id"
            :label="stepTemplateLabel(t)"
            :value="t.id"
          />
        </el-select>
        <el-button size="small" :disabled="!stepTemplatePick" @click="applyStepTemplate">套用步骤</el-button>
        <el-button size="small" type="success" @click="saveCurrentStepsAsTemplate">把当前步骤存为模板</el-button>
        <el-button size="small" :disabled="!stepTemplatePick" @click="overwriteStepTemplate">覆盖所选模板</el-button>
        <el-button size="small" type="danger" :disabled="!stepTemplatePick" @click="deleteStepTemplate">删除所选模板</el-button>
      </div>
      <div class="template-row">
        <span class="template-label">定点挂机坐标</span>
        x<el-input-number v-model="farmXInput" :step="50" :precision="0" size="small" style="width:120px; margin:0 4px" />
        y<el-input-number v-model="farmYInput" :step="50" :precision="0" size="small" style="width:120px; margin:0 4px" />
        <el-button size="small" :disabled="!hasLivePos" @click="fillFarmFromCurrent">读取当前坐标填入</el-button>
        <el-checkbox v-model="config.farmPushEnabled">每次跑脚本后推送定点挂机位置到游戏</el-checkbox>
      </div>
      <span class="sub-hint">
        模板含脚本步骤 + 定点挂机坐标(坐标跟模板走,套用模板会带入)。坐标 + 推送开关随角色配置保存(点「保存配置」)。
        勾上「推送」后,自动复活流程或「立即执行脚本」跑完才会把坐标推给游戏(只写坐标,不会替你勾选游戏内「定点挂机」)。
      </span>
    </div>

    <el-divider content-position="left">④ 批量应用到其他角色</el-divider>

    <div class="bulk-panel">
      <p class="hint" style="margin-top: 0">
        把<strong>当前角色</strong>勾选的部分配置复制到选中的其他在线角色。某个号要特殊配置,切过去单独改再保存。
      </p>
      <div class="bulk-row">
        <el-select
          v-model="bulkTargets"
          multiple
          filterable
          collapse-tags
          collapse-tags-tooltip
          placeholder="选择要套用配置的角色"
          style="width: 420px"
        >
          <el-option
            v-for="inst in onlineNamedInstances"
            :key="inst.characterName"
            :label="`${inst.characterName} (pid ${inst.pid})`"
            :value="inst.characterName"
          />
        </el-select>
        <el-button size="small" @click="selectAllBulkTargets">全选在线</el-button>
        <el-button size="small" @click="bulkTargets = []">清空</el-button>
      </div>
      <div class="bulk-row">
        <el-checkbox-group v-model="bulkSections">
          <el-checkbox label="runtime">自动运行 / GM 暂停 / 位置卡住</el-checkbox>
          <el-checkbox label="schedule">默认延迟 / 时间段</el-checkbox>
          <el-checkbox label="steps">脚本步骤 + 定点挂机</el-checkbox>
        </el-checkbox-group>
        <el-button type="primary" :loading="bulkApplying" @click="applyBulkConfig">
          应用到选中角色
        </el-button>
      </div>
    </div>

    <el-divider content-position="left">运行日志</el-divider>
    <pre class="run-log" ref="logRef">{{ logText }}</pre>

    <!-- 当前 NPC 对话 — 跟「寻路移动」那边一模一样,方便: 走到 NPC 后看引擎
         推下来的选项,直接知道 npcInteractId / option,再回去配 sendDialogSelectRaw。
         也支持点选项实测当前对话路径。 -->
    <el-divider content-position="left">当前 NPC 对话(配步骤辅助)</el-divider>
    <p class="hint">
      跟 NPC 说话后,游戏里的对话框选项会同步显示到这里。每点一个选项就发一次
      411026 给服务器,服务器再推下层菜单回来(自动刷新)。配脚本时用「从当前 NPC 填入」
      可以直接把 <code>npcInteractId</code> 灌到 sendDialogSelectRaw 步骤。
    </p>
    <div class="dialog-toolbar">
      <el-button @click="refreshDialog" :loading="loadingDialog">刷新</el-button>
      <el-checkbox v-model="autoRefreshDialog">每秒自动刷新</el-checkbox>
      <span v-if="dialog?.open" class="dialog-meta">
        NPC interactId={{ dialog.npcInteractId }} · monsterTblId={{ dialog.monsterTblId }} · mode={{ dialog.mode }}
      </span>
      <span v-else class="muted dialog-meta">无对话</span>
    </div>
    <div v-if="dialog?.open" class="dialog-box">
      <div v-if="dialog.body" class="dialog-body">{{ dialog.body }}</div>
      <div v-if="dialog.options?.length" class="dialog-options">
        <el-button
          v-for="opt in dialog.options"
          :key="opt.index"
          :loading="sendingOption === opt.index"
          @click="doSelectDialogOption(opt.index)"
          class="dialog-option-btn"
        >
          <span class="opt-num">{{ opt.index + 1 }}.</span>
          <span class="opt-text">{{ opt.text || '(空选项)' }}</span>
          <span class="opt-code" v-if="opt.opt">[opt {{ opt.opt }}]</span>
          <span class="opt-tag" v-if="opt.tag">[tag {{ opt.tag }}]</span>
        </el-button>
      </div>
      <div v-else class="muted">(没有可点选项)</div>
    </div>
    <div v-else class="muted dialog-empty">
      暂无对话 —— 先到「寻路移动」点附近 NPC 的「走过去 + 对话」,等 dialog 框
      出现后再回来看这里。
    </div>
  </div>
</template>

<script setup lang="ts">
// 自动复活 — 完全由 broker 跑,前端只是配置 UI + 状态显示。
//
// 数据流:
//   config 编辑 → 「保存配置」→ PUT /api/auto-revive/configs/<name>
//   broker 自己 5s 轮询 getStatus,推进 idle / pending / running 状态机。
//   WS 收 autoRevive.state + autoRevive.log,前端只读、显示。
//
// 迁移老的 localStorage(ggtb.autorevive.<name>):
//   组件 mount 时,若 broker 还没这个角色的配置,但本机 localStorage 里有 →
//   自动 PUT 上去,然后删掉本地 key,避免下次再迁。一次性完成。

import { computed, onMounted, onUnmounted, ref, watch, nextTick } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useInstances } from '@/composables/useInstances'
import { useWebSocket } from '@/composables/useWebSocket'
import { getMapName } from '@/utils/mapNames'
import type { Instance } from '@/types'

const { instances, selectedPid, selectedInstance } = useInstances()
const { onMessage } = useWebSocket()

interface StepBase { delayMs: number; repeatCount?: number; repeatDelayMs?: number }
interface StepReviveToTown extends StepBase { type: 'reviveToTown' }
interface StepMoveTo extends StepBase { type: 'moveTo'; x: number; y: number; action: 1 | 3 }
interface StepWaitInTown extends StepBase { type: 'waitInTown'; mapId: number; timeoutMs: number }
interface StepWaitOutOfTown extends StepBase { type: 'waitOutOfTown'; mapId: number; timeoutMs: number }
interface StepSendDialogSelectRaw extends StepBase { type: 'sendDialogSelectRaw'; npcId: number; option: number }
interface StepPressHookedKey extends StepBase { type: 'pressHookedKey'; vkey: number; alt: boolean; ctrl: boolean; shift: boolean }
interface StepSleep extends StepBase { type: 'sleep' }
type Step = StepReviveToTown | StepMoveTo | StepWaitInTown | StepWaitOutOfTown
          | StepSendDialogSelectRaw | StepPressHookedKey | StepSleep

interface ScheduleWindow {
  enabled: boolean
  start: string
  end: string
  delayMinMin: number
  delayMinMax: number
}

interface ReviveConfig {
  characterName: string
  autoRun: boolean
  delayMinMin: number
  delayMinMax: number
  pauseOnNearbyGm: boolean
  stuckReviveEnabled: boolean
  stuckReviveMinutes: number
  scheduleWindows: ScheduleWindow[]
  steps: Step[]
  // 定点挂机坐标:跟脚本模板走,套用模板时复制进来。
  farmX?: number
  farmY?: number
  // 勾上才会在每次跑脚本后把定点坐标推送给游戏。
  farmPushEnabled: boolean
}

// 全局共享脚本步骤模板库:命名的脚本步骤集合 + 定点挂机坐标 + 是否推送(都跟模板走)
interface StepTemplate { id: string; name: string; steps: Step[]; farmX?: number; farmY?: number; farmPushEnabled?: boolean }
// 全局共享对话模板库:给 sendDialogSelectRaw 的 (npcId, option) 命名
interface DialogTemplate { id: string; name: string; npcId: number; option: number }

interface BrokerState {
  characterName: string
  phase: 'idle' | 'pending' | 'running' | 'armed'
  deadAt: number
  scheduledAt: number
  currentStepIdx: number
  lastError?: string
  lastRunAt?: number
  pausedByGm?: boolean
  nearbyGmName?: string
  nearbyGmDistance?: number
  gmPauseLastTickAt?: number
  pausedBySchedule?: boolean
  schedulePauseLastTickAt?: number
  lastMoveAt?: number
}

interface LiveStatus {
  hp: number
  isDead: boolean
  mapId: number
  userId: number
  posX?: number
  posY?: number
}

const liveStatus = ref<LiveStatus | null>(null)
const loadingStatus = ref(false)
const logText = ref('')
const logRef = ref<HTMLPreElement | null>(null)

const defaultConfig = (name: string): ReviveConfig => ({
  characterName: name,
  autoRun: false,
  delayMinMin: 5,
  delayMinMax: 5,
  pauseOnNearbyGm: false,
  stuckReviveEnabled: false,
  stuckReviveMinutes: 10,
  scheduleWindows: [],
  steps: [],
  farmPushEnabled: false,
})
function withStepDefaults<T extends Step>(step: T): T {
  step.delayMs = Math.max(0, Math.round(Number(step.delayMs) || 0))
  step.repeatCount = Math.max(1, Math.round(Number(step.repeatCount) || 1))
  step.repeatDelayMs = Math.max(0, Math.round(Number(step.repeatDelayMs) || 0))
  return step
}
function normalizeSteps(steps: Step[]) {
  return steps.map((s) => withStepDefaults(s))
}
function normalizeClock(value: string | undefined, fallback: string): string {
  const m = String(value || '').trim().match(/^(\d{1,2}):(\d{2})$/)
  if (!m) return fallback
  const h = Number(m[1])
  const min = Number(m[2])
  if (!Number.isInteger(h) || !Number.isInteger(min) || h < 0 || h > 23 || min < 0 || min > 59) return fallback
  return `${String(h).padStart(2, '0')}:${String(min).padStart(2, '0')}`
}
function addHours(clock: string, hours: number): string {
  const [h, m] = normalizeClock(clock, '00:00').split(':').map(Number)
  const minutes = (h * 60 + m + hours * 60) % (24 * 60)
  const fixed = minutes < 0 ? minutes + 24 * 60 : minutes
  return `${String(Math.floor(fixed / 60)).padStart(2, '0')}:${String(fixed % 60).padStart(2, '0')}`
}
function normalizeScheduleWindows(windows: ScheduleWindow[] | undefined): ScheduleWindow[] {
  return (windows || []).map((w) => {
    let lo = Number(w.delayMinMin)
    let hi = Number(w.delayMinMax)
    if (!Number.isFinite(lo)) lo = config.value.delayMinMin
    if (!Number.isFinite(hi)) hi = lo
    if (hi < lo) [lo, hi] = [hi, lo]
    return {
      enabled: w.enabled !== false,
      start: normalizeClock(w.start, '00:00'),
      end: normalizeClock(w.end, '23:59'),
      delayMinMin: Math.max(0, lo),
      delayMinMax: Math.max(0, hi),
    }
  })
}
const defaultScript = (): Step[] => ([
  withStepDefaults({ type: 'reviveToTown', delayMs: 5000 }),
  withStepDefaults({ type: 'waitInTown', mapId: 11, timeoutMs: 5000, delayMs: 5000 }),
  withStepDefaults({ type: 'moveTo', x: 252, y: 286, action: 1, delayMs: 5000 }),
  withStepDefaults({ type: 'sendDialogSelectRaw', npcId: 19811, option: 10245, delayMs: 5000 }),
  withStepDefaults({ type: 'pressHookedKey', vkey: 0x57, alt: true, ctrl: false, shift: false, delayMs: 1000 }),
])

const config = ref<ReviveConfig>(defaultConfig(''))
const state = ref<BrokerState | null>(null)
const bulkTargets = ref<string[]>([])
const bulkSections = ref<Array<'runtime' | 'schedule' | 'steps'>>(['runtime', 'schedule'])
const bulkApplying = ref(false)
const revivingAll = ref(false)

// 定点挂机坐标:用本地 input 双向绑定,save 时写回 config.farmX/farmY。
// 推送由 broker 在脚本跑完后自动做(setStationaryFarm),前端不再手动推。
const farmXInput = ref<number>(0)
const farmYInput = ref<number>(0)

// 全局模板库
const stepTemplates = ref<StepTemplate[]>([])
const dialogTemplates = ref<DialogTemplate[]>([])
const stepTemplatePick = ref<string>('')

const characterKey = computed(() =>
  selectedInstance.value?.characterName || ''
)
const onlineNamedInstances = computed<Instance[]>(() =>
  [...instances.value]
    .filter((inst) => !!inst.characterName)
    .sort((a, b) => a.characterName.localeCompare(b.characterName) || a.pid - b.pid)
)

const running = computed(() => state.value?.phase === 'running')
const phaseTagType = computed(() => {
  switch (state.value?.phase) {
    case 'running': return 'warning'
    case 'pending': return 'danger'
    default:        return 'success'
  }
})

const mapName = computed(() =>
  liveStatus.value ? getMapName(liveStatus.value.mapId) : ''
)

const hasLivePos = computed(() =>
  liveStatus.value != null
  && typeof liveStatus.value.posX === 'number'
  && typeof liveStatus.value.posY === 'number'
)

const totalDurationSec = computed(() => {
  let ms = 0
  for (const s of config.value.steps) {
    const repeatCount = Math.max(1, Math.round(s.repeatCount ?? 1))
    ms += (s.delayMs ?? 0)
    ms += Math.max(0, repeatCount - 1) * (s.repeatDelayMs ?? 0)
    if (s.type === 'waitInTown' || s.type === 'waitOutOfTown') ms += repeatCount * ((s.timeoutMs ?? 0) / 4)
  }
  return ms / 1000
})

// pending 倒计时:每秒 tick 重新算
const tickNow = ref<number>(Date.now())
const pendingRemainingSec = computed(() => {
  if (!state.value || state.value.phase !== 'pending') return 0
  if (state.value.pausedByGm) return Math.max(0, (state.value.scheduledAt - (state.value.gmPauseLastTickAt ?? tickNow.value)) / 1000)
  if (state.value.pausedBySchedule) return Math.max(0, (state.value.scheduledAt - (state.value.schedulePauseLastTickAt ?? tickNow.value)) / 1000)
  const rem = state.value.scheduledAt - tickNow.value
  return rem > 0 ? rem / 1000 : 0
})

// 位置卡住:broker state.lastMoveAt 起到现在的「未移动」秒数(仅显示用)。
const stuckIdleSec = computed(() => {
  const t = state.value?.lastMoveAt
  if (!t || t <= 0) return 0
  const sec = (tickNow.value - t) / 1000
  return sec > 0 ? sec : 0
})

// ---------- broker config CRUD ----------
async function loadConfigFromBroker() {
  if (!characterKey.value) {
    config.value = defaultConfig('')
    state.value = null
    logText.value = ''
    return
  }
  try {
    const r = await fetch(`/api/auto-revive/configs/${encodeURIComponent(characterKey.value)}`)
    if (r.ok) {
      config.value = { ...defaultConfig(characterKey.value), ...(await r.json()) }
      config.value.steps = normalizeSteps(config.value.steps || [])
      config.value.scheduleWindows = normalizeScheduleWindows(config.value.scheduleWindows)
    } else if (r.status === 404) {
      // broker 没有 → 检查本地 localStorage 老格式做一次性迁移
      const migrated = await migrateFromLocalStorage(characterKey.value)
      if (!migrated) {
        const cfg = defaultConfig(characterKey.value)
        cfg.steps = defaultScript()
        config.value = cfg
      }
    }
  } catch (e: any) {
    ElMessage.error(`加载 broker 配置失败: ${e.message}`)
  }
  // 同步定点坐标到本地 input
  farmXInput.value = Number.isFinite(config.value.farmX as number) ? (config.value.farmX as number) : 0
  farmYInput.value = Number.isFinite(config.value.farmY as number) ? (config.value.farmY as number) : 0
  // state + logs
  try {
    const [sr, lr] = await Promise.all([
      fetch(`/api/auto-revive/states/${encodeURIComponent(characterKey.value)}`),
      fetch(`/api/auto-revive/logs/${encodeURIComponent(characterKey.value)}`),
    ])
    if (sr.ok) state.value = await sr.json()
    if (lr.ok) {
      const lines = (await lr.json()) as string[]
      logText.value = lines.join('\n') + (lines.length ? '\n' : '')
    }
  } catch { /* silent */ }
}

async function migrateFromLocalStorage(name: string): Promise<boolean> {
  const oldKey = `ggtb.autorevive.${name}`
  const raw = localStorage.getItem(oldKey)
  if (!raw) return false
  try {
    const parsed = JSON.parse(raw)
    // 老 schema: delayMin 单值 / 还有 townMapId / delegateNpcId / delegateOption。
    // 单值迁移成 [min,max] 两端相等;另外几个字段丢弃(不再有这些 UI)。
    const single =
      Number.isFinite(parsed.delayMin) ? parsed.delayMin :
      Number.isFinite(parsed.cooldownMin) ? parsed.cooldownMin : 5
    const cfg: ReviveConfig = {
      characterName: name,
      autoRun: !!parsed.autoRun,
      delayMinMin: Number.isFinite(parsed.delayMinMin) ? parsed.delayMinMin : single,
      delayMinMax: Number.isFinite(parsed.delayMinMax) ? parsed.delayMinMax : single,
      pauseOnNearbyGm: !!parsed.pauseOnNearbyGm,
      stuckReviveEnabled: !!parsed.stuckReviveEnabled,
      stuckReviveMinutes: Number.isFinite(parsed.stuckReviveMinutes) ? parsed.stuckReviveMinutes : 10,
      scheduleWindows: normalizeScheduleWindows(parsed.scheduleWindows),
      steps: Array.isArray(parsed.steps) && parsed.steps.length ? normalizeSteps(parsed.steps) : defaultScript(),
      farmPushEnabled: !!parsed.farmPushEnabled,
    }
    const r = await fetch(`/api/auto-revive/configs/${encodeURIComponent(name)}`, {
      method: 'PUT',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(cfg),
    })
    if (!r.ok) return false
    config.value = { ...defaultConfig(name), ...(await r.json()) }
    config.value.scheduleWindows = normalizeScheduleWindows(config.value.scheduleWindows)
    // 迁移成功 → 清掉本地 + 老的 deadAt key,以后只读 broker
    localStorage.removeItem(oldKey)
    localStorage.removeItem(`${oldKey}.deadAt`)
    ElMessage.success(`已从本地迁移 ${name} 的配置到 broker`)
    return true
  } catch {
    return false
  }
}

async function saveConfig() {
  if (!characterKey.value) {
    ElMessage.warning('当前未选角色,无法保存')
    return
  }
  try {
    config.value.steps = normalizeSteps(config.value.steps)
    config.value.scheduleWindows = normalizeScheduleWindows(config.value.scheduleWindows)
    config.value.farmX = Math.round(Number(farmXInput.value) || 0)
    config.value.farmY = Math.round(Number(farmYInput.value) || 0)
    const r = await fetch(`/api/auto-revive/configs/${encodeURIComponent(characterKey.value)}`, {
      method: 'PUT',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(config.value),
    })
    if (!r.ok) throw new Error(`HTTP ${r.status}`)
    config.value = { ...defaultConfig(characterKey.value), ...(await r.json()) }
    config.value.steps = normalizeSteps(config.value.steps || [])
    config.value.scheduleWindows = normalizeScheduleWindows(config.value.scheduleWindows)
    ElMessage.success(`已保存到 broker (${characterKey.value})`)
  } catch (e: any) {
    ElMessage.error(`保存失败: ${e.message}`)
  }
}

async function resetConfig() {
  if (!characterKey.value) return
  try {
    await fetch(`/api/auto-revive/configs/${encodeURIComponent(characterKey.value)}`, { method: 'DELETE' })
    const cfg = defaultConfig(characterKey.value)
    cfg.steps = defaultScript()
    config.value = cfg
    farmXInput.value = 0
    farmYInput.value = 0
    ElMessage.info('已重置当前角色配置(broker 端已删除)')
  } catch (e: any) {
    ElMessage.error(`重置失败: ${e.message}`)
  }
}

function loadDefaultScript() {
  config.value.steps = defaultScript()
}

// ---------- 定点挂机坐标 ----------
function fillFarmFromCurrent() {
  if (!hasLivePos.value || !liveStatus.value) return
  farmXInput.value = Math.round(liveStatus.value.posX as number)
  farmYInput.value = Math.round(liveStatus.value.posY as number)
}

// ---------- 全局模板库 ----------
async function loadTemplates() {
  try {
    const [sr, dr] = await Promise.all([
      fetch('/api/auto-revive/step-templates'),
      fetch('/api/auto-revive/dialog-templates'),
    ])
    if (sr.ok) stepTemplates.value = (await sr.json()) as StepTemplate[]
    if (dr.ok) dialogTemplates.value = (await dr.json()) as DialogTemplate[]
  } catch { /* silent */ }
}

function stepTemplateLabel(t: StepTemplate): string {
  const hasFarm = Number.isFinite(t.farmX as number) && Number.isFinite(t.farmY as number)
  const farm = hasFarm ? ` · 定点(${Math.round(t.farmX as number)}, ${Math.round(t.farmY as number)})` : ''
  const push = hasFarm ? (t.farmPushEnabled ? ' 推送' : ' 不推') : ''
  return `${t.name} (${t.steps.length} 步)${farm}${push}`
}

// 当前 input + 推送开关(给存模板用),坐标无值则只回推送开关。
function currentFarmCoords(): { farmX?: number; farmY?: number; farmPushEnabled: boolean } {
  const x = Number(farmXInput.value)
  const y = Number(farmYInput.value)
  const farmPushEnabled = !!config.value.farmPushEnabled
  if (!Number.isFinite(x) || !Number.isFinite(y)) return { farmPushEnabled }
  return { farmX: Math.round(x), farmY: Math.round(y), farmPushEnabled }
}

function applyStepTemplate() {
  const t = stepTemplates.value.find((x) => x.id === stepTemplatePick.value)
  if (!t) return
  config.value.steps = normalizeSteps(cloneConfig(t.steps))
  // 定点坐标 + 推送开关跟模板走:有就带入,没有就清零(切到不带坐标的模板时不残留上一个)。
  const hasFarm = Number.isFinite(t.farmX as number) && Number.isFinite(t.farmY as number)
  farmXInput.value = hasFarm ? Math.round(t.farmX as number) : 0
  farmYInput.value = hasFarm ? Math.round(t.farmY as number) : 0
  config.value.farmX = hasFarm ? Math.round(t.farmX as number) : undefined
  config.value.farmY = hasFarm ? Math.round(t.farmY as number) : undefined
  config.value.farmPushEnabled = !!t.farmPushEnabled
  const farmMsg = hasFarm
    ? `,定点 (${farmXInput.value}, ${farmYInput.value}) ${t.farmPushEnabled ? '推送' : '不推送'}`
    : ''
  ElMessage.success(`已套用模板「${t.name}」的 ${t.steps.length} 步${farmMsg}(记得点「保存配置」)`)
}

async function saveCurrentStepsAsTemplate() {
  let name = ''
  try {
    const r = await ElMessageBox.prompt('给这个步骤模板起个名字(会一并存定点挂机坐标)', '存为模板', {
      confirmButtonText: '保存', cancelButtonText: '取消',
    })
    name = String(r.value || '').trim()
  } catch { return }
  if (!name) { ElMessage.warning('名字不能为空'); return }
  await upsertStepTemplate({ name, steps: cloneConfig(normalizeSteps(config.value.steps)), ...currentFarmCoords() })
}

async function overwriteStepTemplate() {
  const t = stepTemplates.value.find((x) => x.id === stepTemplatePick.value)
  if (!t) return
  await upsertStepTemplate({ id: t.id, name: t.name, steps: cloneConfig(normalizeSteps(config.value.steps)), ...currentFarmCoords() })
}

async function upsertStepTemplate(payload: { id?: string; name: string; steps: Step[]; farmX?: number; farmY?: number; farmPushEnabled?: boolean }) {
  try {
    const r = await fetch('/api/auto-revive/step-templates', {
      method: 'PUT',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(payload),
    })
    if (!r.ok) throw new Error(`HTTP ${r.status}`)
    const saved = (await r.json()) as StepTemplate
    await loadTemplates()
    stepTemplatePick.value = saved.id
    ElMessage.success(`已保存模板「${saved.name}」`)
  } catch (e: any) {
    ElMessage.error(`保存模板失败: ${e.message}`)
  }
}

async function deleteStepTemplate() {
  const t = stepTemplates.value.find((x) => x.id === stepTemplatePick.value)
  if (!t) return
  try {
    await ElMessageBox.confirm(`删除模板「${t.name}」?`, '确认', { type: 'warning' })
  } catch { return }
  await fetch(`/api/auto-revive/step-templates/${encodeURIComponent(t.id)}`, { method: 'DELETE' })
  stepTemplatePick.value = ''
  await loadTemplates()
  ElMessage.info('已删除模板')
}

// 按行反查:这行当前的 npcId/option 对应哪个对话模板。下拉用它回显命中的模板,
// 旁边的标签也据此显示「模板: xxx」/「自定义」。不再用全局单一 pick(多行会串)。
function dialogTemplateMatch(row: any): DialogTemplate | undefined {
  const npcId = Number(row.npcId)
  const option = Number(row.option)
  if (!Number.isFinite(npcId) || !Number.isFinite(option)) return undefined
  return dialogTemplates.value.find((x) => x.npcId === npcId && x.option === option)
}
function dialogTemplateIdFor(row: any): string {
  return dialogTemplateMatch(row)?.id ?? ''
}
function dialogTemplateNameFor(row: any): string {
  return dialogTemplateMatch(row)?.name ?? ''
}

function applyDialogTemplate(row: any, id: string) {
  if (!id) return
  const t = dialogTemplates.value.find((x) => x.id === id)
  if (!t) return
  row.npcId = t.npcId
  row.option = t.option
}

async function saveDialogTemplateFromRow(row: any) {
  let name = ''
  try {
    const r = await ElMessageBox.prompt(`给 (npcId=${row.npcId}, option=${row.option}) 起个名字`, '存为对话模板', {
      confirmButtonText: '保存', cancelButtonText: '取消',
    })
    name = String(r.value || '').trim()
  } catch { return }
  if (!name) { ElMessage.warning('名字不能为空'); return }
  try {
    const r = await fetch('/api/auto-revive/dialog-templates', {
      method: 'PUT',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ name, npcId: Number(row.npcId), option: Number(row.option) }),
    })
    if (!r.ok) throw new Error(`HTTP ${r.status}`)
    await loadTemplates()
    ElMessage.success(`已保存对话模板「${name}」`)
  } catch (e: any) {
    ElMessage.error(`保存对话模板失败: ${e.message}`)
  }
}

async function cancelPending() {
  if (!characterKey.value) return
  await fetch(`/api/auto-revive/cancel/${encodeURIComponent(characterKey.value)}`, { method: 'POST' })
}

async function runNow() {
  if (!characterKey.value) return
  const r = await fetch(`/api/auto-revive/run-now/${encodeURIComponent(characterKey.value)}`, { method: 'POST' })
  const obj = await r.json()
  if (!obj.ok) ElMessage.error(`无法启动: ${obj.detail || '?'}`)
}

async function runAllDeadNow() {
  revivingAll.value = true
  try {
    const r = await fetch('/api/auto-revive/run-all-dead', { method: 'POST' })
    const obj = await r.json()
    const results = Array.isArray(obj.results) ? obj.results : []
    const ok = results.filter((x: any) => x.ok).length
    const skipped = results.length - ok
    if (results.length === 0) {
      ElMessage.info('当前没有 HP=0 的在线角色')
    } else if (skipped > 0) {
      ElMessage.warning(`已触发 ${ok} 个,跳过 ${skipped} 个`)
    } else {
      ElMessage.success(`已触发 ${ok} 个死亡角色的复活脚本`)
    }
  } catch (e: any) {
    ElMessage.error(`全部复活失败: ${e.message}`)
  } finally {
    revivingAll.value = false
  }
}

async function abortScript() {
  if (!characterKey.value) return
  await fetch(`/api/auto-revive/abort/${encodeURIComponent(characterKey.value)}`, { method: 'POST' })
}

function addScheduleWindow() {
  const last = config.value.scheduleWindows[config.value.scheduleWindows.length - 1]
  const start = last?.end || '00:00'
  config.value.scheduleWindows.push({
    enabled: true,
    start,
    end: addHours(start, 2),
    delayMinMin: config.value.delayMinMin,
    delayMinMax: config.value.delayMinMax,
  })
}
function removeScheduleWindow(i: number) {
  config.value.scheduleWindows.splice(i, 1)
}
function sortScheduleWindows() {
  config.value.scheduleWindows = normalizeScheduleWindows(config.value.scheduleWindows)
    .sort((a, b) => a.start.localeCompare(b.start) || a.end.localeCompare(b.end))
}
function selectAllBulkTargets() {
  bulkTargets.value = onlineNamedInstances.value.map((inst) => inst.characterName)
}
function cloneConfig<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T
}
async function fetchConfigForName(name: string): Promise<ReviveConfig> {
  const r = await fetch(`/api/auto-revive/configs/${encodeURIComponent(name)}`)
  if (r.ok) {
    const cfg = { ...defaultConfig(name), ...(await r.json()) }
    cfg.steps = normalizeSteps(cfg.steps || [])
    cfg.scheduleWindows = normalizeScheduleWindows(cfg.scheduleWindows)
    return cfg
  }
  const cfg = defaultConfig(name)
  cfg.steps = defaultScript()
  return cfg
}
async function putConfigForName(name: string, cfg: ReviveConfig): Promise<ReviveConfig> {
  const r = await fetch(`/api/auto-revive/configs/${encodeURIComponent(name)}`, {
    method: 'PUT',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ ...cfg, characterName: name }),
  })
  if (!r.ok) throw new Error(`${name}: HTTP ${r.status}`)
  const saved = { ...defaultConfig(name), ...(await r.json()) }
  saved.steps = normalizeSteps(saved.steps || [])
  saved.scheduleWindows = normalizeScheduleWindows(saved.scheduleWindows)
  return saved
}
async function applyBulkConfig() {
  const targets = Array.from(new Set(bulkTargets.value.filter(Boolean)))
  if (targets.length === 0) {
    ElMessage.warning('先选择要应用的角色')
    return
  }
  if (bulkSections.value.length === 0) {
    ElMessage.warning('至少勾选一个应用内容')
    return
  }

  const source = cloneConfig(config.value)
  source.steps = normalizeSteps(source.steps || [])
  source.scheduleWindows = normalizeScheduleWindows(source.scheduleWindows)
  // 定点坐标以当前 input 为准(可能还没点保存),同步进 source。
  const srcFarm = currentFarmCoords()
  source.farmX = srcFarm.farmX
  source.farmY = srcFarm.farmY
  bulkApplying.value = true
  try {
    let ok = 0
    for (const name of targets) {
      const target = await fetchConfigForName(name)
      if (bulkSections.value.includes('runtime')) {
        target.autoRun = source.autoRun
        target.pauseOnNearbyGm = source.pauseOnNearbyGm
        target.stuckReviveEnabled = source.stuckReviveEnabled
        target.stuckReviveMinutes = source.stuckReviveMinutes
      }
      if (bulkSections.value.includes('schedule')) {
        target.delayMinMin = source.delayMinMin
        target.delayMinMax = source.delayMinMax
        target.scheduleWindows = cloneConfig(source.scheduleWindows)
      }
      if (bulkSections.value.includes('steps')) {
        target.steps = cloneConfig(source.steps)
        // 定点坐标 + 推送开关跟脚本走 → 套步骤时一并复制(同地图同点)。
        target.farmX = source.farmX
        target.farmY = source.farmY
        target.farmPushEnabled = source.farmPushEnabled
      }
      const saved = await putConfigForName(name, target)
      if (name === characterKey.value) config.value = saved
      ok += 1
    }
    ElMessage.success(`已应用到 ${ok} 个角色`)
  } catch (e: any) {
    ElMessage.error(`批量应用失败: ${e.message}`)
  } finally {
    bulkApplying.value = false
  }
}

// ---------- 步骤编辑 ----------
function addStep() {
  config.value.steps.push(withStepDefaults({ type: 'sleep', delayMs: 1000 } as StepSleep))
}
function addPressKeyStep() {
  config.value.steps.push(withStepDefaults({
    type: 'pressHookedKey', vkey: 0x57, alt: true, ctrl: false, shift: false, delayMs: 200,
  } as StepPressHookedKey))
}
function removeStep(i: number) {
  config.value.steps.splice(i, 1)
}
function moveStep(i: number, dir: number) {
  const j = i + dir
  if (j < 0 || j >= config.value.steps.length) return
  const [s] = config.value.steps.splice(i, 1)
  config.value.steps.splice(j, 0, s)
}
function onStepTypeChange(idx: number) {
  const s = config.value.steps[idx]
  if (s.type === 'moveTo') {
    const ms = s as StepMoveTo
    if (ms.x == null) ms.x = 0
    if (ms.y == null) ms.y = 0
    if (ms.action == null) ms.action = 1
  } else if (s.type === 'sendDialogSelectRaw') {
    const ds = s as StepSendDialogSelectRaw
    if (ds.npcId == null) ds.npcId = 0
    if (ds.option == null) ds.option = 0
  } else if (s.type === 'pressHookedKey') {
    const ks = s as StepPressHookedKey
    if (ks.vkey == null) ks.vkey = 0x57
    if (ks.alt == null)  ks.alt  = true
    if (ks.ctrl == null) ks.ctrl = false
    if (ks.shift == null) ks.shift = false
  } else if (s.type === 'waitInTown' || s.type === 'waitOutOfTown') {
    const ws = s as StepWaitInTown
    // 默认 11 — 用户改 step 时如果想要别的可以直接改 row.mapId
    if (ws.mapId == null) ws.mapId = 11
    if (ws.timeoutMs == null) ws.timeoutMs = 15000
  }
  if (s.delayMs == null) s.delayMs = 500
  withStepDefaults(s)
}

// 把当前在对话的 NPC 的 interactId 灌进去 — 配 sendDialogSelectRaw 步骤的时候
// 不用回头去抓包,看到对话框打开了直接「填入」省事。option 字段保留用户原值。
function fillFromCurrentNpc(row: any) {
  if (!dialog.value?.open) return
  if (typeof dialog.value.npcInteractId === 'number')
    row.npcId = dialog.value.npcInteractId
}

// ---------- 实时 status 显示(只是 UI,不参与判定) ----------
async function postCommand(action: string, args: Record<string, any>): Promise<any> {
  if (!selectedPid.value) return null
  const res = await fetch(`/api/command/${selectedPid.value}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ action, args }),
  })
  return res.json()
}

async function refreshStatus() {
  if (!selectedPid.value) return
  loadingStatus.value = true
  try {
    const r = await postCommand('getStatus', {})
    if (r?.ok) {
      const obj = typeof r.detail === 'string' ? JSON.parse(r.detail) : r.detail
      liveStatus.value = obj as LiveStatus
    }
  } catch { /* silent */ }
  finally {
    loadingStatus.value = false
  }
}

async function showCapturedHooks() {
  const r = await postCommand('listCapturedHooks', {})
  if (!r?.ok) {
    ElMessage.error('listCapturedHooks 失败: ' + (r?.detail || r?.error))
    return
  }
  const list = typeof r.detail === 'string' ? JSON.parse(r.detail) : r.detail
  appendLocalLog('[captured hooks]')
  if (!list || list.length === 0) {
    appendLocalLog('  (空) — 第三方 DLL 还没装 hook')
    return
  }
  for (const h of list)
    appendLocalLog(`  idHook=${h.idHook}(${h.idHookName}) lpfn=${h.lpfn} hMod=${h.hMod} tid=${h.tid}`)
}

function appendLocalLog(line: string) {
  const ts = new Date().toLocaleTimeString()
  logText.value += `[${ts}] ${line}\n`
  nextTick(() => {
    if (logRef.value) logRef.value.scrollTop = logRef.value.scrollHeight
  })
}

// ---------- WS 监听 broker 推送 ----------
const offMsg = onMessage((msg) => {
  if (msg.type === 'autoRevive.state' && msg.characterName === characterKey.value) {
    state.value = msg.state
  } else if (msg.type === 'autoRevive.log' && msg.characterName === characterKey.value) {
    logText.value += msg.line + '\n'
    nextTick(() => {
      if (logRef.value) logRef.value.scrollTop = logRef.value.scrollHeight
    })
  } else if (msg.type === 'autoRevive.states') {
    // 首连 snapshot,如果当前角色在里头就直接刷新
    const s = (msg.states as BrokerState[] | undefined)?.find(
      (x) => x.characterName === characterKey.value
    )
    if (s) state.value = s
  }
})

// 切角色 → 重新拉 broker 配置;UI 上的实时 hp/map 也刷一下
watch(characterKey, () => {
  logText.value = ''
  loadConfigFromBroker()
  refreshStatus()
}, { immediate: true })

let statusTimer: number | null = null
let tickTimer: number | null = null
onMounted(() => {
  refreshStatus()
  loadTemplates()
  // 1Hz 刷 UI hp + pendingRemainingSec 重算。注:这只是 UI 显示,broker
  // 自己也在 5s 轮询。前端关掉这个不会影响 broker 自动模式工作。
  statusTimer = window.setInterval(refreshStatus, 2000)
  tickTimer   = window.setInterval(() => { tickNow.value = Date.now() }, 1000)
})
onUnmounted(() => {
  if (statusTimer !== null) { window.clearInterval(statusTimer); statusTimer = null }
  if (tickTimer   !== null) { window.clearInterval(tickTimer);   tickTimer   = null }
  offMsg && (offMsg as any)()
})

watch(selectedPid, () => { refreshStatus() })

// ---------- NPC dialog (跟 MoveToView 那份 1:1 复用) ----------
// 引擎本身的多级对话:每点一个选项就发 411026 (CG_NPC_DIALOG_SELECT)。server
// 推回 521603,把新选项码写进 g_NpcDialogState 链表,客户端 rebuild 菜单。
// 我们这一侧只是把 state 链表读出来当 JSON,前端按钮发回 selectDialogOption。
interface DialogOption {
  index: number
  text: string
  tag: number
  opt?: number
}
interface DialogState {
  open: boolean
  mode?: number
  npcInteractId?: number
  monsterTblId?: number
  body?: string
  options?: DialogOption[]
}

const dialog = ref<DialogState | null>(null)
const loadingDialog = ref(false)
const sendingOption = ref<number | null>(null)
const autoRefreshDialog = ref(true)
let dialogTimer: number | null = null

async function refreshDialog() {
  if (!selectedPid.value) return
  loadingDialog.value = true
  try {
    const r = await postCommand('getDialog', {})
    if (!r || !r.ok) return
    const obj = typeof r.detail === 'string' ? JSON.parse(r.detail) : r.detail
    dialog.value = obj as DialogState
  } catch {
    // 静默
  } finally {
    loadingDialog.value = false
  }
}

async function doSelectDialogOption(index: number) {
  if (!selectedPid.value) { ElMessage.warning('未选择实例'); return }
  sendingOption.value = index
  try {
    const r = await postCommand('selectDialogOption', { option: index })
    if (r?.ok) {
      ElMessage.success(`已选: ${dialog.value?.options?.[index]?.text || `选项 ${index + 1}`}`)
      setTimeout(() => { refreshDialog() }, 500)
    } else {
      ElMessage.error(`失败: ${r?.detail || r?.error || 'unknown'}`)
    }
  } catch (e: any) {
    ElMessage.error(`失败: ${e.message}`)
  } finally {
    sendingOption.value = null
  }
}

function startDialogPoll() {
  stopDialogPoll()
  if (!autoRefreshDialog.value) return
  dialogTimer = window.setInterval(() => { refreshDialog() }, 1000)
}
function stopDialogPoll() {
  if (dialogTimer !== null) {
    window.clearInterval(dialogTimer)
    dialogTimer = null
  }
}
onMounted(() => { startDialogPoll() })
onUnmounted(() => { stopDialogPoll() })
watch(autoRefreshDialog, () => { startDialogPoll() })
</script>

<style scoped>
.autorevive-view { padding: 8px; }
.hint {
  color: var(--el-text-color-secondary);
  font-size: 13px;
  margin: 4px 0 16px;
  line-height: 1.6;
}
.sub-hint { color: var(--el-text-color-secondary); font-size: 12px; margin-left: 8px; }
.muted { color: var(--el-text-color-secondary); font-style: italic; }
h4 { margin: 16px 0 8px; }
.action-bar {
  position: sticky;
  top: 0;
  z-index: 10;
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 10px;
  padding: 10px 12px;
  margin: -8px -8px 8px;
  background: var(--el-bg-color);
  border-bottom: 1px solid var(--el-border-color);
}
.action-bar .ab-left,
.action-bar .ab-right {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}
.action-bar .ab-title {
  font-size: 16px;
  font-weight: 600;
  margin-right: 4px;
}
.action-bar .ab-group-label {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}
.step-toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 10px 0;
}
.template-bar {
  display: flex;
  flex-direction: column;
  gap: 10px;
  max-width: 1100px;
  margin: 10px 0 16px;
  padding: 10px 12px;
  border: 1px solid var(--el-border-color);
  border-radius: 6px;
  background: var(--el-fill-color-lighter);
}
.template-row {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
}
.template-label {
  font-weight: 600;
  font-size: 13px;
}
.schedule-editor {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.schedule-hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
}
.schedule-actions {
  display: flex;
  gap: 8px;
}
.bulk-panel {
  max-width: 980px;
  margin: 12px 0 18px;
  padding: 12px 14px;
  border: 1px solid var(--el-border-color);
  border-radius: 6px;
  background: var(--el-fill-color-lighter);
}
.bulk-panel h4 {
  margin-top: 0;
}
.bulk-row {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  margin: 8px 0;
}
.run-log {
  max-height: 320px;
  overflow-y: auto;
  background: var(--el-fill-color-light);
  border: 1px solid var(--el-border-color);
  border-radius: 4px;
  padding: 10px 12px;
  font-size: 12px;
  line-height: 1.5;
  white-space: pre-wrap;
  margin: 4px 0;
}
code {
  background: var(--el-fill-color);
  padding: 1px 4px;
  border-radius: 3px;
  font-size: 12px;
}

.dialog-toolbar {
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 8px 0;
}
.dialog-meta {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-left: auto;
}
.dialog-box {
  border: 1px solid var(--el-border-color);
  border-radius: 6px;
  padding: 12px 14px;
  background: var(--el-fill-color-light);
  max-width: 720px;
}
.dialog-body {
  white-space: pre-wrap;
  font-size: 14px;
  line-height: 1.7;
  margin-bottom: 10px;
  color: var(--el-text-color-primary);
}
.dialog-options {
  display: flex;
  flex-direction: column;
  gap: 6px;
  align-items: stretch;
}
.dialog-option-btn {
  justify-content: flex-start;
  text-align: left;
  white-space: normal;
  height: auto;
  padding: 8px 12px;
}
.dialog-option-btn .opt-num {
  font-weight: 600;
  margin-right: 6px;
  color: var(--el-color-primary);
}
.dialog-option-btn .opt-text { flex: 1; }
.dialog-option-btn .opt-code {
  font-size: 11px;
  color: var(--el-color-success);
  margin-left: 8px;
  font-family: ui-monospace, 'SF Mono', Menlo, monospace;
}
.dialog-option-btn .opt-tag {
  font-size: 11px;
  color: var(--el-text-color-secondary);
  margin-left: 8px;
}
.dialog-empty {
  padding: 10px 0;
  font-size: 13px;
}
</style>

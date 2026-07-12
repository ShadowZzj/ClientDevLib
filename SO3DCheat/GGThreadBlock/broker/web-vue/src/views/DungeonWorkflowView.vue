<template>
  <div class="workflow-view">
    <div class="page-title">
      <div>
        <h2>副本编排</h2>
        <p>由 broker 常驻执行。网页关闭后流程仍会继续；每个角色同一时间只运行一个流程。</p>
      </div>
      <el-segmented v-model="activePanel" :options="panelOptions" />
    </div>

    <template v-if="activePanel === 'design'">
      <div class="toolbar">
        <el-select
          v-model="selectedWorkflowId"
          placeholder="选择工作流"
          filterable
          style="width: 260px"
          @change="selectWorkflow"
        >
          <el-option v-for="item in workflows" :key="item.id" :label="item.name" :value="item.id" />
        </el-select>
        <el-button @click="newWorkflow">新建</el-button>
        <el-button :disabled="!draft" @click="duplicateWorkflow">复制</el-button>
        <el-button type="primary" :loading="saving" :disabled="!draft" @click="saveDraft">保存</el-button>
        <el-button type="danger" plain :disabled="!draft" @click="deleteDraft">删除</el-button>
        <span v-if="validationMessage" class="validation-hint">{{ validationMessage }}</span>
      </div>

      <el-card v-if="draft" shadow="never" class="meta-card">
        <el-form inline label-width="86px">
          <el-form-item label="流程名称">
            <el-input v-model="draft.name" style="width: 220px" />
          </el-form-item>
          <el-form-item label="最大跳转">
            <el-input-number v-model="draft.maxTransitions" :min="1" :max="100000" :step="100" />
          </el-form-item>
          <el-form-item label="最长运行">
            <el-input-number v-model="runtimeMinutes" :min="1" :max="1440" :step="10" />
            <span class="unit">分钟</span>
          </el-form-item>
          <el-form-item label="BOSS最大HP">
            <el-input-number
              v-model="bossHpThreshold"
              :min="0"
              :max="9007199254740991"
              :step="10000000"
              :controls="false"
              :disabled="bossNodeCount === 0"
              style="width: 180px"
            />
            <span class="unit">统一最大血量下限</span>
          </el-form-item>
          <el-form-item label="说明" class="description-item">
            <el-input v-model="draft.description" placeholder="例如：狮子城 → 副本入口 → 分房清怪 → 领奖" />
          </el-form-item>
        </el-form>
      </el-card>

      <div v-if="draft" class="designer-grid">
        <el-card shadow="never" class="palette-card">
          <template #header>
            <div class="card-title">节点库</div>
          </template>
          <div v-for="group in nodeGroups" :key="group.name" class="palette-group">
            <div class="palette-title">{{ group.name }}</div>
            <el-button
              v-for="type in group.types"
              :key="type"
              class="palette-button"
              plain
              @click="addNode(type)"
            >
              <span>{{ nodeMeta(type).name }}</span>
              <small>{{ nodeMeta(type).short }}</small>
            </el-button>
          </div>
          <el-alert
            title="卡片顺序只影响显示"
            description="真正执行顺序由节点的成功/失败/真假连线决定，移动卡片不会暗改连线。"
            type="info"
            :closable="false"
            show-icon
          />
        </el-card>

        <el-card shadow="never" class="flow-card">
          <template #header>
            <div class="flow-header">
              <span class="card-title">流程节点（{{ draft.nodes.length }}）</span>
              <span class="muted">起点：{{ nodeLabel(draft.startNodeId) }}</span>
            </div>
          </template>

          <el-empty v-if="draft.nodes.length === 0" description="从左侧添加第一个节点" />
          <div v-else class="node-list">
            <div v-for="(node, index) in draft.nodes" :key="node.id" class="node-wrap">
              <div
                class="node-card"
                :class="{
                  selected: selectedNodeId === node.id,
                  running: activeNodeIds.has(node.id),
                  start: draft.startNodeId === node.id,
                }"
                @click="selectNode(node.id)"
              >
                <div class="node-card-head">
                  <div>
                    <el-tag size="small" :type="nodeTagType(node.type)">{{ nodeMeta(node.type).name }}</el-tag>
                    <strong>{{ node.name || node.id }}</strong>
                  </div>
                  <div class="node-badges">
                    <el-tag v-if="draft.startNodeId === node.id" size="small" type="success">起点</el-tag>
                    <el-tag v-if="activeNodeIds.has(node.id)" size="small" type="warning">运行中</el-tag>
                  </div>
                </div>
                <div class="node-summary">{{ nodeSummary(node) }}</div>
                <div class="edge-row">
                  <template v-if="node.type === 'condition' || node.type === 'loop'">
                    <span class="edge true">真 → {{ nodeLabel(node.onTrue || node.next) }}</span>
                    <span class="edge false">假 → {{ nodeLabel(node.onFalse || node.next) }}</span>
                  </template>
                  <template v-else-if="node.type !== 'end'">
                    <span class="edge">下一步 → {{ nodeLabel(node.next) }}</span>
                  </template>
                  <span v-if="node.onFailure" class="edge failure">失败 → {{ nodeLabel(node.onFailure) }}</span>
                </div>
                <div class="node-actions" @click.stop>
                  <el-button link size="small" type="success" @click="setStart(node.id)">设为起点</el-button>
                  <el-button link size="small" :disabled="index === 0" @click="moveNode(index, -1)">上移</el-button>
                  <el-button link size="small" :disabled="index === draft.nodes.length - 1" @click="moveNode(index, 1)">下移</el-button>
                  <el-button link size="small" @click="duplicateNode(node.id)">复制</el-button>
                  <el-button link size="small" type="danger" @click="removeNode(node.id)">删除</el-button>
                </div>
              </div>
              <div v-if="index < draft.nodes.length - 1" class="visual-connector">↓</div>
            </div>
          </div>
        </el-card>

        <el-card shadow="never" class="editor-card">
          <template #header>
            <div class="card-title">节点参数</div>
          </template>
          <el-empty v-if="!selectedNode" description="选择中间的节点" :image-size="70" />
          <template v-else>
            <el-form label-position="top" size="small">
              <div class="two-cols">
                <el-form-item label="节点名称">
                  <el-input v-model="selectedNode.name" />
                </el-form-item>
                <el-form-item label="节点类型">
                  <el-select :model-value="selectedNode.type" @change="changeNodeType">
                    <el-option
                      v-for="type in allNodeTypes"
                      :key="type"
                      :label="nodeMeta(type).name"
                      :value="type"
                    />
                  </el-select>
                </el-form-item>
              </div>

              <div class="node-description">{{ nodeMeta(selectedNode.type).description }}</div>

              <template v-if="selectedNode.type === 'condition' || selectedNode.type === 'waitUntil'">
                <el-form-item label="条件类型">
                  <el-select :model-value="conditionParams.kind" @change="changeConditionKind">
                    <el-option label="当前地图" value="map" />
                    <el-option label="当前地图名称" value="mapName" />
                    <el-option label="坐标范围" value="position" />
                    <el-option label="死亡状态" value="dead" />
                    <el-option label="附近 NPC / 怪物" value="entity" />
                    <el-option label="附近玩家" value="player" />
                    <el-option label="任意命令结果" value="command" />
                  </el-select>
                </el-form-item>

                <template v-if="conditionParams.kind === 'map'">
                  <el-form-item label="地图 ID"><el-input-number v-model="conditionParams.mapId" :min="1" :controls="false" /></el-form-item>
                  <el-form-item label="判断"><el-select v-model="conditionParams.operator"><el-option label="等于" value="eq" /><el-option label="不等于" value="ne" /></el-select></el-form-item>
                </template>

                <template v-else-if="conditionParams.kind === 'mapName'">
                  <el-form-item label="地图名称包含"><el-input v-model="conditionParams.value" placeholder="例如：賢者之塔" clearable /></el-form-item>
                </template>

                <template v-else-if="conditionParams.kind === 'position'">
                  <div class="two-cols">
                    <el-form-item label="中心 X"><el-input-number v-model="conditionParams.x" :controls="false" /></el-form-item>
                    <el-form-item label="中心 Y"><el-input-number v-model="conditionParams.y" :controls="false" /></el-form-item>
                  </div>
                  <el-form-item label="半径"><el-input-number v-model="conditionParams.distance" :min="0.1" :step="1" /></el-form-item>
                </template>

                <template v-else-if="conditionParams.kind === 'dead'">
                  <el-form-item label="期望状态"><el-switch v-model="conditionParams.value" active-text="已死亡" inactive-text="存活" /></el-form-item>
                </template>

                <template v-else-if="conditionParams.kind === 'entity'">
                  <el-form-item label="实体类型">
                    <el-select :model-value="entityKind" @change="changeEntityKind">
                      <el-option label="任意实体" value="any" />
                      <el-option label="NPC" value="npc" />
                      <el-option label="可攻击怪物" value="monster" />
                    </el-select>
                  </el-form-item>
                  <el-form-item label="搜索范围">
                    <el-switch v-model="conditionParams.allVisible" active-text="当前视野全部" inactive-text="按半径" />
                  </el-form-item>
                  <div class="two-cols">
                    <el-form-item label="搜索半径"><el-input-number v-model="conditionParams.maxDistance" :min="1" :controls="false" :disabled="conditionParams.allVisible" /></el-form-item>
                    <el-form-item label="模板 ID"><el-input-number v-model="conditionParams.monsterTblId" :min="0" :controls="false" /></el-form-item>
                  </div>
                  <el-form-item label="名称包含"><el-input v-model="conditionParams.nameContains" clearable /></el-form-item>
                  <el-form-item label="名称不包含"><el-input v-model="conditionParams.nameNotContains" clearable placeholder="例如 [BOSS]" /></el-form-item>
                  <div class="two-cols">
                    <el-form-item label="数量判断"><el-select v-model="conditionParams.operator"><el-option label="存在" value="exists" /><el-option label="不存在" value="notExists" /><el-option label="数量 ≥" value="gte" /></el-select></el-form-item>
                    <el-form-item v-if="conditionParams.operator === 'gte'" label="数量"><el-input-number v-model="conditionParams.value" :min="1" /></el-form-item>
                  </div>
                </template>

                <template v-else-if="conditionParams.kind === 'player'">
                  <el-form-item label="搜索半径"><el-input-number v-model="conditionParams.maxDistance" :min="1" :controls="false" /></el-form-item>
                  <el-form-item label="玩家名（留空=任意）"><el-input v-model="conditionParams.name" clearable /></el-form-item>
                  <el-form-item label="判断"><el-select v-model="conditionParams.operator"><el-option label="存在" value="exists" /><el-option label="不存在" value="notExists" /><el-option label="数量 ≥" value="gte" /></el-select></el-form-item>
                  <el-form-item v-if="conditionParams.operator === 'gte'" label="数量"><el-input-number v-model="conditionParams.value" :min="1" /></el-form-item>
                </template>

                <template v-else-if="conditionParams.kind === 'command'">
                  <el-form-item label="查询 action"><el-input v-model="conditionParams.action" placeholder="例如 getDialog" /></el-form-item>
                  <el-form-item label="结果 JSON 路径"><el-input v-model="conditionParams.path" placeholder="例如 options[0].text" /></el-form-item>
                  <div class="two-cols">
                    <el-form-item label="比较"><el-select v-model="conditionParams.operator"><el-option v-for="op in comparisonOperators" :key="op.value" :label="op.label" :value="op.value" /></el-select></el-form-item>
                    <el-form-item label="期望值"><el-input v-model="conditionParams.value" /></el-form-item>
                  </div>
                </template>

                <div v-if="selectedNode.type === 'waitUntil'" class="two-cols">
                  <el-form-item label="轮询间隔(ms)"><el-input-number v-model="selectedNode.params.pollMs" :min="100" :step="100" /></el-form-item>
                  <el-form-item label="持续成立(ms)"><el-input-number v-model="selectedNode.params.stableDurationMs" :min="0" :step="500" /></el-form-item>
                </div>
              </template>

              <template v-else-if="selectedNode.type === 'pathTo' || selectedNode.type === 'moveTo' || selectedNode.type === 'warpTo'">
                <div class="two-cols">
                  <el-form-item label="目标 X"><el-input-number v-model="selectedNode.params.x" :controls="false" /></el-form-item>
                  <el-form-item label="目标 Y"><el-input-number v-model="selectedNode.params.y" :controls="false" /></el-form-item>
                </div>
                <template v-if="selectedNode.type !== 'warpTo'">
                  <el-form-item label="动作">
                    <el-radio-group v-model="selectedNode.params.action"><el-radio-button :value="1">仅移动</el-radio-button><el-radio-button :value="3">追击目标</el-radio-button></el-radio-group>
                  </el-form-item>
                  <el-form-item v-if="selectedNode.params.action === 3" label="目标 creatureId"><el-input-number v-model="selectedNode.params.targetId" :min="0" :controls="false" /></el-form-item>
                </template>
                <template v-if="selectedNode.type !== 'pathTo'">
                  <el-form-item label="确认到达"><el-switch v-model="selectedNode.params.confirm" /></el-form-item>
                  <el-form-item v-if="selectedNode.params.confirm" label="到达半径"><el-input-number v-model="selectedNode.params.arriveDistance" :min="0.1" :step="0.5" /></el-form-item>
                </template>
                <el-form-item label="轮询间隔(ms)"><el-input-number v-model="selectedNode.params.pollMs" :min="100" :step="100" /></el-form-item>
              </template>

              <template v-else-if="selectedNode.type === 'teleport'">
                <div class="two-cols">
                  <el-form-item label="目的地 destId"><el-input-number v-model="selectedNode.params.destId" :min="0" :controls="false" /></el-form-item>
                  <el-form-item label="城市名"><el-input v-model="selectedNode.params.cityName" placeholder="destId 优先" /></el-form-item>
                </div>
                <el-form-item label="预期地图 ID"><el-input-number v-model="selectedNode.params.expectedMapId" :min="1" :controls="false" /></el-form-item>
                <el-form-item v-if="!selectedNode.params.expectedMapId" label="发包后等待(ms)"><el-input-number v-model="selectedNode.params.settleMs" :min="0" :step="500" /></el-form-item>
              </template>

              <template v-else-if="selectedNode.type === 'revive'">
                <el-form-item label="回城模式"><el-select v-model="selectedNode.params.mode"><el-option label="乐园镇 (1)" :value="1" /><el-option label="狮子城 (2)" :value="2" /></el-select></el-form-item>
                <el-form-item label="死亡安全检查"><el-switch v-model="selectedNode.params.safetyCheck" /></el-form-item>
                <el-form-item label="预期地图 ID"><el-input-number v-model="selectedNode.params.expectedMapId" :min="1" :controls="false" /></el-form-item>
              </template>

              <template v-else-if="selectedNode.type === 'approachEntity'">
                <el-alert title="只寻路靠近目标，不调用 talkOrAttack，也不会打开 NPC 对话。" type="info" :closable="false" />
                <el-form-item label="直接 creatureId"><el-input-number v-model="selectedNode.params.creatureId" :min="0" :controls="false" /></el-form-item>
                <el-form-item label="搜索范围">
                  <el-switch v-model="selectedNode.params.allVisible" active-text="当前视野全部" inactive-text="按半径" />
                </el-form-item>
                <div class="two-cols">
                  <el-form-item label="搜索半径"><el-input-number v-model="selectedNode.params.maxDistance" :min="1" :controls="false" :disabled="selectedNode.params.allVisible" /></el-form-item>
                  <el-form-item label="模板 ID（可选）"><el-input-number v-model="selectedNode.params.monsterTblId" :min="0" :controls="false" /></el-form-item>
                </div>
                <el-form-item label="名称精确匹配"><el-input v-model="selectedNode.params.name" clearable /></el-form-item>
                <el-form-item label="名称包含"><el-input v-model="selectedNode.params.nameContains" clearable /></el-form-item>
                <el-form-item label="名称不包含"><el-input v-model="selectedNode.params.nameNotContains" clearable /></el-form-item>
                <div class="two-cols">
                  <el-form-item label="NPC 筛选"><el-select v-model="selectedNode.params.isNpc" clearable><el-option label="仅 NPC" :value="true" /><el-option label="非 NPC" :value="false" /></el-select></el-form-item>
                  <el-form-item label="攻击筛选"><el-select v-model="selectedNode.params.attackable" clearable><el-option label="可攻击" :value="true" /><el-option label="不可攻击" :value="false" /></el-select></el-form-item>
                </div>
                <div class="two-cols">
                  <el-form-item label="靠近距离"><el-input-number v-model="selectedNode.params.approachDistance" :min="0.5" :step="0.5" /></el-form-item>
                  <el-form-item label="距离轮询(ms)"><el-input-number v-model="selectedNode.params.pollMs" :min="100" :step="100" /></el-form-item>
                </div>
              </template>

              <template v-else-if="selectedNode.type === 'interact'">
                <el-form-item label="直接 creatureId"><el-input-number v-model="selectedNode.params.creatureId" :min="0" :controls="false" /></el-form-item>
                <el-form-item label="搜索范围">
                  <el-switch v-model="selectedNode.params.allVisible" active-text="当前视野全部" inactive-text="按半径" />
                </el-form-item>
                <div class="two-cols">
                  <el-form-item label="搜索半径"><el-input-number v-model="selectedNode.params.maxDistance" :min="1" :controls="false" :disabled="selectedNode.params.allVisible" /></el-form-item>
                  <el-form-item label="模板 ID"><el-input-number v-model="selectedNode.params.monsterTblId" :min="0" :controls="false" /></el-form-item>
                </div>
                <el-form-item label="名称包含"><el-input v-model="selectedNode.params.nameContains" clearable /></el-form-item>
                <el-form-item label="名称不包含"><el-input v-model="selectedNode.params.nameNotContains" clearable /></el-form-item>
                <div class="two-cols">
                  <el-form-item label="NPC 筛选"><el-select v-model="selectedNode.params.isNpc" clearable><el-option label="仅 NPC" :value="true" /><el-option label="非 NPC" :value="false" /></el-select></el-form-item>
                  <el-form-item label="攻击筛选"><el-select v-model="selectedNode.params.attackable" clearable><el-option label="可攻击" :value="true" /><el-option label="不可攻击" :value="false" /></el-select></el-form-item>
                </div>
                <el-form-item label="等待对话打开"><el-switch v-model="selectedNode.params.waitDialog" /></el-form-item>
              </template>

              <template v-else-if="selectedNode.type === 'dialogSelect'">
                <el-form-item label="直接发包"><el-switch :model-value="selectedNode.params.raw" @change="changeDialogRaw" /></el-form-item>
                <el-alert
                  v-if="selectedNode.params.raw"
                  :title="selectedNode.params.nearestNpc ? '执行时查询附近 NPC，取最近实体的动态 id，再发送固定 opt；不会打开对话。' : '兼容模式：使用固定 npcId 与原始 opt。npcId 不是 monsterTblId。'"
                  type="info"
                  :closable="false"
                />
                <el-form-item v-if="selectedNode.params.raw" label="NPC ID 来源">
                  <el-switch
                    :model-value="selectedNode.params.nearestNpc"
                    active-text="运行时取最近 NPC"
                    inactive-text="固定 npcId（兼容）"
                    @change="changeDialogNpcSource"
                  />
                </el-form-item>
                <template v-if="selectedNode.params.raw && selectedNode.params.nearestNpc">
                  <el-form-item label="搜索范围">
                    <el-switch v-model="selectedNode.params.allVisible" active-text="当前视野全部" inactive-text="按半径" />
                  </el-form-item>
                  <el-form-item v-if="!selectedNode.params.allVisible" label="搜索半径">
                    <el-input-number v-model="selectedNode.params.maxDistance" :min="0.5" :step="0.5" />
                  </el-form-item>
                  <el-form-item label="NPC 名称精确匹配（可选）"><el-input v-model="selectedNode.params.name" clearable /></el-form-item>
                  <el-form-item label="NPC 名称包含（可选）"><el-input v-model="selectedNode.params.nameContains" clearable /></el-form-item>
                  <div class="two-cols">
                    <el-form-item label="等待 NPC 出现(ms)"><el-input-number v-model="selectedNode.params.waitForNpcMs" :min="0" :step="500" /></el-form-item>
                    <el-form-item label="查询间隔(ms)"><el-input-number v-model="selectedNode.params.pollMs" :min="100" :step="100" /></el-form-item>
                  </div>
                </template>
                <div class="two-cols">
                  <el-form-item v-if="selectedNode.params.raw && !selectedNode.params.nearestNpc" label="npcId"><el-input-number v-model="selectedNode.params.npcId" :min="1" :controls="false" /></el-form-item>
                  <el-form-item :label="selectedNode.params.raw ? '原始 opt' : '选项 index'"><el-input-number v-model="selectedNode.params.option" :min="0" :controls="false" /></el-form-item>
                </div>
                <el-form-item v-if="selectedNode.params.raw" label="sub"><el-input-number v-model="selectedNode.params.sub" :min="0" /></el-form-item>
                <el-form-item label="预期地图 ID"><el-input-number v-model="selectedNode.params.expectedMapId" :min="1" :controls="false" /></el-form-item>
              </template>

              <template v-else-if="selectedNode.type === 'dungeonEntry'">
                <el-alert
                  title="入口 opt 只发送一次；客户端自动续发不会由工作流重复发送。"
                  type="warning"
                  :closable="false"
                />
                <el-form-item label="NPC 搜索范围">
                  <el-switch v-model="selectedNode.params.allVisible" active-text="当前视野全部" inactive-text="按半径" />
                </el-form-item>
                <div class="two-cols">
                  <el-form-item label="搜索半径"><el-input-number v-model="selectedNode.params.maxDistance" :min="0.5" :step="0.5" :disabled="selectedNode.params.allVisible" /></el-form-item>
                  <el-form-item label="NPC 名称包含"><el-input v-model="selectedNode.params.nameContains" clearable /></el-form-item>
                </div>
                <div class="two-cols">
                  <el-form-item label="等待 NPC 出现(ms)"><el-input-number v-model="selectedNode.params.waitForNpcMs" :min="0" :step="500" /></el-form-item>
                  <el-form-item label="查询间隔(ms)"><el-input-number v-model="selectedNode.params.pollMs" :min="100" :step="100" /></el-form-item>
                </div>
                <div class="two-cols">
                  <el-form-item label="入口 opt"><el-input-number v-model="selectedNode.params.option" :min="9575" :max="9575" :controls="false" disabled /></el-form-item>
                  <el-form-item label="sub"><el-input-number v-model="selectedNode.params.sub" :min="1" :max="1" disabled /></el-form-item>
                </div>
                <el-form-item label="成功地图名称包含"><el-input v-model="selectedNode.params.expectedMapNameContains" clearable /></el-form-item>
                <el-form-item label="免费次数用尽时自动使用额外入场券">
                  <el-switch v-model="selectedNode.params.autoUseExtraTicket" />
                </el-form-item>
                <el-alert
                  title="固定票券：itemId=26419 · Another 額外入場券 · 单流程最多消耗 1 张"
                  type="info"
                  :closable="false"
                />
                <div class="two-cols">
                  <el-form-item label="原生对话超时(ms)"><el-input-number v-model="selectedNode.params.nativeDialogTimeoutMs" :min="1000" :step="500" /></el-form-item>
                  <el-form-item label="原生对话最大推进步数"><el-input-number v-model="selectedNode.params.nativeDialogMaxSteps" :min="1" :max="32" /></el-form-item>
                </div>
                <div class="ticket-count-panel">
                  <div class="ticket-count-head">
                    <strong>选中角色当前票数</strong>
                    <el-button size="small" :loading="ticketCountLoading" @click="refreshTicketCounts">刷新</el-button>
                  </div>
                  <div v-if="ticketCountRows.length === 0" class="muted">请先选择角色后刷新。</div>
                  <div v-for="row in ticketCountRows" :key="row.characterName" class="ticket-count-row">
                    <span>{{ row.characterName }}</span>
                    <el-tag v-if="row.status === 'ok'" type="success" size="small">{{ row.count }} 张</el-tag>
                    <el-tag v-else-if="row.status === 'loading'" type="info" size="small">查询中</el-tag>
                    <el-tag v-else-if="row.status === 'offline'" type="info" size="small">离线</el-tag>
                    <el-tooltip v-else :content="row.error || '不支持'" placement="top">
                      <el-tag type="danger" size="small">不支持/失败</el-tag>
                    </el-tooltip>
                  </div>
                </div>
              </template>

              <template v-else-if="selectedNode.type === 'clearMonsters'">
                <el-alert title="只处理 attackable 且 HP>0 的实体；先纯寻路靠近，再选中怪物进入战斗。" type="info" :closable="false" />
                <el-form-item label="清理范围">
                  <el-switch v-model="selectedNode.params.allVisible" active-text="当前视野全部" inactive-text="按半径" />
                </el-form-item>
                <div class="two-cols">
                  <el-form-item label="清怪半径"><el-input-number v-model="selectedNode.params.maxDistance" :min="1" :controls="false" :disabled="selectedNode.params.allVisible" /></el-form-item>
                  <el-form-item label="怪物模板 ID"><el-input-number v-model="selectedNode.params.monsterTblId" :min="0" :controls="false" /></el-form-item>
                </div>
                <el-form-item label="名称包含"><el-input v-model="selectedNode.params.nameContains" clearable /></el-form-item>
                <el-form-item label="名称不包含"><el-input v-model="selectedNode.params.nameNotContains" clearable placeholder="例如 [BOSS]" /></el-form-item>
                <div class="two-cols">
                  <el-form-item label="选中前靠近距离"><el-input-number v-model="selectedNode.params.approachDistance" :min="0.5" :step="0.5" /></el-form-item>
                  <el-form-item label="重查间隔(ms)"><el-input-number v-model="selectedNode.params.pollMs" :min="100" :step="100" /></el-form-item>
                </div>
                <el-form-item label="同目标重新选中(ms)"><el-input-number v-model="selectedNode.params.reissueMs" :min="200" :step="500" /></el-form-item>
                <div class="two-cols">
                  <el-form-item label="连续空场次数"><el-input-number v-model="selectedNode.params.emptyConfirmations" :min="1" :max="20" /></el-form-item>
                  <el-form-item label="空场持续(ms)"><el-input-number v-model="selectedNode.params.emptyDurationMs" :min="500" :step="500" /></el-form-item>
                </div>
              </template>

              <template v-else-if="selectedNode.type === 'killBoss'">
                <el-alert
                  title="按名称与最大 HP 锁定 BOSS；先纯寻路靠近并选中，再用当前 HP/稳定消失判断死亡。"
                  type="warning"
                  :closable="false"
                />
                <el-form-item label="初次搜索范围">
                  <el-switch v-model="selectedNode.params.allVisible" active-text="当前视野全部" inactive-text="按半径" />
                </el-form-item>
                <div class="two-cols">
                  <el-form-item label="搜索半径"><el-input-number v-model="selectedNode.params.maxDistance" :min="1" :controls="false" :disabled="selectedNode.params.allVisible" /></el-form-item>
                  <el-form-item label="怪物模板 ID"><el-input-number v-model="selectedNode.params.monsterTblId" :min="0" :controls="false" /></el-form-item>
                </div>
                <el-form-item label="名称精确匹配"><el-input v-model="selectedNode.params.name" clearable /></el-form-item>
                <el-form-item label="名称包含"><el-input v-model="selectedNode.params.nameContains" clearable placeholder="例如 [BOSS]" /></el-form-item>
                <el-form-item label="名称不包含"><el-input v-model="selectedNode.params.nameNotContains" clearable /></el-form-item>
                <el-form-item label="最大 HP 大于">
                  <el-input-number v-model="selectedNode.params.maxHpGt" :min="0" :max="9007199254740991" :step="10000000" :controls="false" />
                </el-form-item>
                <div class="two-cols">
                  <el-form-item label="等待出现(ms)"><el-input-number v-model="selectedNode.params.waitForAppearanceMs" :min="0" :step="1000" /></el-form-item>
                  <el-form-item label="未出现时跳过"><el-switch v-model="selectedNode.params.skipIfAbsent" /></el-form-item>
                </div>
                <div class="two-cols">
                  <el-form-item label="选中前靠近距离"><el-input-number v-model="selectedNode.params.approachDistance" :min="0.5" :step="0.5" /></el-form-item>
                  <el-form-item label="重查间隔(ms)"><el-input-number v-model="selectedNode.params.pollMs" :min="100" :step="100" /></el-form-item>
                </div>
                <el-form-item label="重新选中间隔(ms)"><el-input-number v-model="selectedNode.params.reissueMs" :min="200" :step="500" /></el-form-item>
                <div class="two-cols">
                  <el-form-item label="连续消失次数"><el-input-number v-model="selectedNode.params.missingConfirmations" :min="1" :max="20" /></el-form-item>
                  <el-form-item label="消失持续(ms)"><el-input-number v-model="selectedNode.params.missingDurationMs" :min="500" :step="500" /></el-form-item>
                </div>
              </template>

              <template v-else-if="selectedNode.type === 'collectFilteredDrops'">
                <el-alert
                  title="按全局拾取方式处理过滤掉落；游戏原生模式会寻路靠近、触发 action=4 并等待精确 dropId 消失。单项失败只跳过。"
                  type="info"
                  :closable="false"
                />
                <div class="two-cols">
                  <el-form-item label="搜索半径(0=视野)"><el-input-number v-model="selectedNode.params.maxDistance" :min="0" :controls="false" /></el-form-item>
                  <el-form-item label="靠近距离"><el-input-number v-model="selectedNode.params.approachDistance" :min="0.5" :step="0.5" /></el-form-item>
                </div>
                <el-form-item label="包含暂不可捡物品"><el-switch v-model="selectedNode.params.includeUnpickable" /></el-form-item>
                <div class="two-cols">
                  <el-form-item label="最长拾取(ms)"><el-input-number v-model="selectedNode.params.maxDurationMs" :min="1000" :step="1000" /></el-form-item>
                  <el-form-item label="连续空场(ms)"><el-input-number v-model="selectedNode.params.emptyDurationMs" :min="0" :step="500" /></el-form-item>
                </div>
                <div class="two-cols">
                  <el-form-item label="重查间隔(ms)"><el-input-number v-model="selectedNode.params.pollMs" :min="100" :step="100" /></el-form-item>
                  <el-form-item label="原生确认等待(ms)"><el-input-number v-model="selectedNode.params.nativePickupTimeoutMs" :min="1000" :step="500" /></el-form-item>
                </div>
              </template>

              <template v-else-if="selectedNode.type === 'wait'">
                <el-form-item label="等待时间(ms)"><el-input-number v-model="selectedNode.params.ms" :min="0" :step="500" /></el-form-item>
              </template>

              <template v-else-if="selectedNode.type === 'loop'">
                <el-form-item label="循环次数"><el-input-number v-model="selectedNode.params.count" :min="1" :max="100000" /></el-form-item>
                <div class="muted">每次到达本节点，未满次数走“真”分支；满次数走“假”分支并清零。</div>
              </template>

              <template v-else-if="selectedNode.type === 'command'">
                <el-alert title="高级入口。非幂等命令不要配置自动重试；pathTo / stopPath 请使用专用节点。" type="warning" :closable="false" />
                <el-form-item label="action"><el-input v-model="selectedNode.params.action" /></el-form-item>
                <el-form-item label="输出变量名"><el-input v-model="selectedNode.params.saveAs" placeholder="可选" /></el-form-item>
                <el-form-item label="解析 detail JSON"><el-switch v-model="selectedNode.params.parseJson" /></el-form-item>
              </template>

              <template v-else-if="selectedNode.type === 'end'">
                <el-form-item label="结束结果"><el-switch v-model="selectedNode.params.success" active-text="成功" inactive-text="失败" /></el-form-item>
                <el-form-item label="结束消息"><el-input v-model="selectedNode.params.message" /></el-form-item>
              </template>

              <el-divider content-position="left">连线与容错</el-divider>
              <template v-if="selectedNode.type === 'condition' || selectedNode.type === 'loop'">
                <div class="two-cols">
                  <el-form-item :label="selectedNode.type === 'loop' ? '继续循环（真）' : '条件为真'">
                    <node-select v-model="selectedNode.onTrue" :nodes="draft.nodes" />
                  </el-form-item>
                  <el-form-item :label="selectedNode.type === 'loop' ? '循环完成（假）' : '条件为假'">
                    <node-select v-model="selectedNode.onFalse" :nodes="draft.nodes" />
                  </el-form-item>
                </div>
              </template>
              <el-form-item v-else-if="selectedNode.type !== 'end'" label="成功后下一步">
                <node-select v-model="selectedNode.next" :nodes="draft.nodes" />
              </el-form-item>
              <el-form-item v-if="selectedNode.type !== 'end'" label="失败分支">
                <node-select v-model="selectedNode.onFailure" :nodes="draft.nodes" allow-empty />
              </el-form-item>

              <template v-if="selectedNode.type !== 'end'">
                <div class="three-cols">
                  <el-form-item label="超时(ms)"><el-input-number v-model="selectedNode.timeoutMs" :min="100" :step="1000" :controls="false" /></el-form-item>
                  <el-form-item label="重试次数"><el-input-number v-model="selectedNode.retries" :min="0" :max="20" :disabled="selectedNode.type === 'killBoss' || selectedNode.type === 'dungeonEntry'" /></el-form-item>
                  <el-form-item label="重试间隔"><el-input-number v-model="selectedNode.retryDelayMs" :min="0" :step="500" :controls="false" /></el-form-item>
                </div>
              </template>

              <el-collapse class="advanced-json">
                <el-collapse-item title="高级：完整参数 JSON" name="json">
                  <el-input v-model="paramsJsonText" type="textarea" :rows="10" spellcheck="false" />
                  <div class="json-actions">
                    <span v-if="paramsJsonError" class="json-error">{{ paramsJsonError }}</span>
                    <el-button size="small" @click="resetParamsJson">重置</el-button>
                    <el-button size="small" type="primary" @click="applyParamsJson">应用 JSON</el-button>
                  </div>
                </el-collapse-item>
              </el-collapse>
            </el-form>
          </template>
        </el-card>
      </div>

      <el-empty v-else description="新建或选择一个工作流" />
    </template>

    <template v-else>
      <el-card shadow="never" class="run-launch-card">
        <template #header>
          <div class="flow-header">
            <span class="card-title">启动流程</span>
            <el-button type="primary" :loading="starting" :disabled="!draft || selectedPids.length === 0" @click="startDraft">
              在 {{ selectedPids.length }} 个在线角色上启动
            </el-button>
          </div>
        </template>
        <div class="run-workflow-row">
          <span>当前流程</span>
          <el-select v-model="selectedWorkflowId" style="width: 260px" @change="selectWorkflow">
            <el-option v-for="item in workflows" :key="item.id" :label="item.name" :value="item.id" />
          </el-select>
          <el-tag v-if="draft" type="info">{{ draft.nodes.length }} 个节点</el-tag>
        </div>
        <AccountMultiSelect />
      </el-card>

      <div class="runtime-grid">
        <el-card shadow="never" class="runs-card">
          <template #header>
            <div class="flow-header"><span class="card-title">运行实例</span><el-button size="small" @click="refreshRuns">刷新</el-button></div>
          </template>
          <el-table :data="visibleRuns" size="small" highlight-current-row @current-change="selectRun">
            <el-table-column label="角色" min-width="130"><template #default="{ row }">{{ row.characterName || row.pid }}</template></el-table-column>
            <el-table-column label="流程" min-width="150" prop="workflowName" />
            <el-table-column label="状态" width="92"><template #default="{ row }"><el-tag size="small" :type="runTagType(row.status)">{{ runStatusText(row.status) }}</el-tag></template></el-table-column>
            <el-table-column label="当前节点" min-width="150"><template #default="{ row }">{{ row.currentNodeName || row.currentNodeId || '-' }}</template></el-table-column>
            <el-table-column label="跳转" width="70" prop="transitions" />
            <el-table-column label="运行时间" width="105"><template #default="{ row }">{{ runDuration(row) }}</template></el-table-column>
            <el-table-column label="操作" width="170" fixed="right">
              <template #default="{ row }">
                <el-button v-if="row.status === 'running'" link type="warning" @click.stop="control(row, 'pause')">暂停</el-button>
                <el-button v-if="row.status === 'paused'" link type="success" @click.stop="control(row, 'resume')">恢复</el-button>
                <el-button v-if="isRunActive(row.status)" link type="danger" @click.stop="control(row, 'stop')">停止</el-button>
              </template>
            </el-table-column>
          </el-table>
        </el-card>

        <el-card shadow="never" class="log-card">
          <template #header>
            <div class="flow-header"><span class="card-title">运行日志</span><span class="muted">{{ selectedRun?.characterName || '未选择实例' }}</span></div>
          </template>
          <div v-if="selectedRun" class="run-detail">
            <el-descriptions :column="2" size="small" border>
              <el-descriptions-item label="状态">{{ runStatusText(selectedRun.status) }}</el-descriptions-item>
              <el-descriptions-item label="PID">{{ selectedRun.pid }}</el-descriptions-item>
              <el-descriptions-item label="当前节点">{{ selectedRun.currentNodeName || selectedRun.currentNodeId || '-' }}</el-descriptions-item>
              <el-descriptions-item label="最近错误">{{ selectedRun.lastError || '-' }}</el-descriptions-item>
            </el-descriptions>
            <div class="log-list">
              <div v-for="line in selectedRun.logs" :key="line.seq ?? `${line.at}-${line.message}`" class="log-line" :class="line.level">
                <span class="log-time">{{ formatTime(line.at) }}</span>
                <span v-if="line.nodeId" class="log-node">[{{ line.nodeId }}]</span>
                <span>{{ line.message }}</span>
              </div>
              <el-empty v-if="selectedRun.logs.length === 0" description="暂无日志" :image-size="60" />
            </div>
          </div>
          <el-empty v-else description="从左侧选择一个运行实例" />
        </el-card>
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed, defineComponent, h, onMounted, onUnmounted, ref, watch } from 'vue'
import { ElMessage, ElMessageBox, ElOption, ElSelect } from 'element-plus'
import AccountMultiSelect from '@/components/AccountMultiSelect.vue'
import { useSelectedAccounts } from '@/composables/useSelectedAccounts'
import { useWorkflowApi } from '@/composables/useWorkflowApi'
import type { WorkflowDefinition, WorkflowNode, WorkflowNodeType, WorkflowRunSnapshot } from '@/types/workflow'

const api = useWorkflowApi()
const { selectedNames, selectedTargets } = useSelectedAccounts()

interface TicketCountRow {
  characterName: string
  pid?: number
  status: 'loading' | 'ok' | 'offline' | 'error'
  count?: number
  error?: string
}

const allNodeTypes: WorkflowNodeType[] = [
  'condition', 'waitUntil', 'pathTo', 'moveTo', 'warpTo', 'teleport', 'revive',
  'approachEntity', 'interact', 'dialogSelect', 'dungeonEntry', 'clearMonsters', 'killBoss', 'collectFilteredDrops', 'wait', 'loop', 'command', 'end',
]

const meta: Record<WorkflowNodeType, { name: string; short: string; description: string }> = {
  condition: { name: '条件判断', short: '真假分支', description: '读取当前状态并根据结果走真/假分支。' },
  waitUntil: { name: '等待条件', short: '轮询确认', description: '持续查询条件，成立后继续；超时走失败分支。' },
  pathTo: { name: '远程寻路', short: 'A* 绕障', description: '启动 DLL A* 寻路并以 pathStatus=arrived 作为完成条件。' },
  moveTo: { name: '短距移动', short: '直线一跳', description: '调用一次 moveTo，适合附近无遮挡目标。' },
  warpTo: { name: '坐标瞬移', short: '直接发包', description: '发送坐标瞬移包，并可轮询位置确认。' },
  teleport: { name: '城市传送', short: '城市/destId', description: '按城市名或 destId 传送，可等待目标 mapId。' },
  revive: { name: '复活回城', short: '乐园/狮子', description: '复活或回城，并确认角色脱离死亡状态。' },
  approachEntity: { name: '靠近实体', short: '只走近，不交互', description: '按名称或 ID 寻找实体并走到指定距离内，不调用 talkOrAttack。' },
  interact: { name: '交互实体', short: 'NPC/怪物', description: '按实例 ID、模板 ID 或名称选择最近实体并 talkOrAttack。' },
  dialogSelect: { name: '选择对话', short: '最近NPC/原始opt', description: '选择当前对话 index，或动态取最近 NPC 的 id 后发送固定 opt。' },
  dungeonEntry: { name: '副本入场', short: '免费优先/最多一票', description: '打开最近 NPC 的原生对话，只沿能到达目标 opt 的唯一分支推进；最终 opt 仅提交一次。' },
  clearMonsters: { name: '清理怪物', short: '半径/指定怪', description: '持续重查并攻击最近的可攻击活怪，稳定空场后完成。' },
  killBoss: { name: '锁定击杀 BOSS', short: '最大HP阈值', description: '按名称与最大 HP 选中并锁定一个 BOSS，用当前 HP/稳定消失确认死亡。' },
  collectFilteredDrops: { name: '按过滤拾取', short: '全局方式 · 失败跳过', description: '按全局执行方式处理当前过滤规则命中的掉落；单项不可达、不可捡或确认超时时记录并继续。' },
  wait: { name: '等待', short: '可中止延迟', description: '暂停友好、可停止的定时等待。' },
  loop: { name: '计数循环', short: '固定次数', description: '按固定次数走继续/完成分支。' },
  command: { name: '通用命令', short: '高级入口', description: '透传已有 DLL command；适合尚未做成专用节点的只读或幂等能力。' },
  end: { name: '结束', short: '成功/失败', description: '明确结束当前角色的流程。' },
}

const nodeGroups = [
  { name: '判断', types: ['condition', 'waitUntil'] as WorkflowNodeType[] },
  { name: '移动与传送', types: ['pathTo', 'moveTo', 'warpTo', 'teleport', 'revive', 'approachEntity'] as WorkflowNodeType[] },
  { name: '交互与战斗', types: ['interact', 'dialogSelect', 'dungeonEntry', 'clearMonsters', 'killBoss', 'collectFilteredDrops'] as WorkflowNodeType[] },
  { name: '流程控制', types: ['wait', 'loop', 'command', 'end'] as WorkflowNodeType[] },
]

const comparisonOperators = [
  { label: '等于', value: 'eq' }, { label: '不等于', value: 'ne' },
  { label: '大于', value: 'gt' }, { label: '大于等于', value: 'gte' },
  { label: '小于', value: 'lt' }, { label: '小于等于', value: 'lte' },
  { label: '包含', value: 'contains' }, { label: '存在', value: 'exists' },
  { label: '不存在', value: 'notExists' }, { label: '为真', value: 'truthy' },
]

const panelOptions = [{ label: '流程编排', value: 'design' }, { label: '运行监控', value: 'runtime' }]
const activePanel = ref('design')
const workflows = ref<WorkflowDefinition[]>([])
const selectedWorkflowId = ref('')
const draft = ref<WorkflowDefinition | null>(null)
const selectedNodeId = ref('')
const saving = ref(false)
const starting = ref(false)
const validationMessage = ref('')
const paramsJsonText = ref('{}')
const paramsJsonError = ref('')
const runs = ref<WorkflowRunSnapshot[]>([])
const selectedRunId = ref('')
const ticketCountRows = ref<TicketCountRow[]>([])
const ticketCountLoading = ref(false)
let ticketCountRequest = 0
let runTimer: number | undefined

const selectedNode = computed(() => draft.value?.nodes.find((node) => node.id === selectedNodeId.value) ?? null)
const conditionParams = computed<any>(() => {
  if (!selectedNode.value) return {}
  if (!selectedNode.value.params.condition || typeof selectedNode.value.params.condition !== 'object') {
    selectedNode.value.params.condition = conditionDefaults('map')
  }
  return selectedNode.value.params.condition
})
const entityKind = computed(() => conditionParams.value.attackable === true ? 'monster' : conditionParams.value.isNpc === true ? 'npc' : 'any')
const runtimeMinutes = computed({
  get: () => Math.max(1, Math.round((draft.value?.maxRuntimeMs || 60000) / 60000)),
  set: (value: number) => { if (draft.value) draft.value.maxRuntimeMs = Math.max(1, value || 1) * 60000 },
})
const bossNodes = computed(() => draft.value?.nodes.filter((node) => node.type === 'killBoss') ?? [])
const bossNodeCount = computed(() => bossNodes.value.length)
const bossHpThreshold = computed({
  get: () => Number(bossNodes.value[0]?.params.maxHpGt ?? bossNodes.value[0]?.params.initialHpGt ?? 800000000),
  set: (value: number) => {
    const threshold = Math.max(0, Math.trunc(Number(value) || 0))
    for (const node of bossNodes.value) {
      node.params.maxHpGt = threshold
      delete node.params.initialHpGt
    }
    validationMessage.value = '有未保存改动'
  },
})
const selectedPids = computed(() => selectedTargets.value.map((item) => item.pid))
const selectedRun = computed(() => runs.value.find((run) => run.id === selectedRunId.value) ?? null)
const visibleRuns = computed(() => {
  const rows = selectedWorkflowId.value
    ? runs.value.filter((run) => run.workflowId === selectedWorkflowId.value)
    : runs.value
  return rows.slice().sort((a, b) => Number(b.startedAt || 0) - Number(a.startedAt || 0))
})
const activeNodeIds = computed(() => new Set(
  runs.value
    .filter((run) => run.workflowId === draft.value?.id && isRunActive(run.status))
    .map((run) => run.currentNodeId)
    .filter(Boolean) as string[],
))

const NodeSelect = defineComponent({
  name: 'NodeSelect',
  props: {
    modelValue: { type: String, default: '' },
    nodes: { type: Array as () => WorkflowNode[], required: true },
    allowEmpty: { type: Boolean, default: true },
  },
  emits: ['update:modelValue'],
  setup(props, { emit }) {
    return () => h(ElSelect, {
      modelValue: props.modelValue || '',
      clearable: props.allowEmpty,
      placeholder: '流程结束',
      style: 'width:100%',
      'onUpdate:modelValue': (value: string) => emit('update:modelValue', value || undefined),
    }, () => props.nodes.map((node) => h(ElOption, {
      key: node.id,
      label: `${node.name || node.id} · ${node.id}`,
      value: node.id,
    })))
  },
})

watch([selectedNodeId, () => selectedNode.value?.type], resetParamsJson)
watch(() => selectedNode.value?.params, resetParamsJson, { deep: true })
watch(selectedNames, () => {
  ticketCountRows.value = []
  if (selectedNode.value?.type === 'dungeonEntry') void refreshTicketCounts()
}, { deep: true })
watch(() => selectedNode.value?.type, (type) => {
  if (type === 'dungeonEntry') void refreshTicketCounts()
})

function nodeMeta(type: WorkflowNodeType) { return meta[type] }

function clone<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T
}

async function fetchTicketCount(pid: number): Promise<number> {
  const res = await fetch(`/api/command/${pid}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ action: 'getCashBagItems', args: {} }),
  })
  const text = await res.text()
  let reply: any = {}
  try { reply = text ? JSON.parse(text) : {} } catch { reply = { error: text } }
  if (!res.ok || !reply.ok) {
    throw new Error(reply.detail || reply.error || `HTTP ${res.status}`)
  }
  let items: any[] = []
  try { items = JSON.parse(reply.detail || '[]') } catch { throw new Error('getCashBagItems 返回格式无效') }
  if (!Array.isArray(items)) throw new Error('getCashBagItems 返回格式无效')
  return items
    .filter((item) => Number(item?.itemId) === 26419)
    .reduce((sum, item) => sum + Math.max(0, Number(item?.count) || 0), 0)
}

async function refreshTicketCounts() {
  const requestId = ++ticketCountRequest
  const names = [...selectedNames.value]
  if (names.length === 0) {
    ticketCountRows.value = []
    ticketCountLoading.value = false
    return
  }
  const onlineByName = new Map(selectedTargets.value.map((target) => [target.characterName, target]))
  ticketCountRows.value = names.map((characterName) => {
    const target = onlineByName.get(characterName)
    return target
      ? { characterName, pid: target.pid, status: 'loading' as const }
      : { characterName, status: 'offline' as const }
  })
  ticketCountLoading.value = true
  try {
    const resolved = await Promise.all(ticketCountRows.value.map(async (row): Promise<TicketCountRow> => {
      if (!row.pid) return row
      try {
        return { ...row, status: 'ok', count: await fetchTicketCount(row.pid) }
      } catch (error: any) {
        return { ...row, status: 'error', error: error?.message || String(error) }
      }
    }))
    if (requestId === ticketCountRequest) ticketCountRows.value = resolved
  } finally {
    if (requestId === ticketCountRequest) ticketCountLoading.value = false
  }
}

function newId(prefix: string): string {
  const suffix = typeof crypto !== 'undefined' && crypto.randomUUID
    ? crypto.randomUUID().slice(0, 8)
    : `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`
  return `${prefix}-${suffix}`
}

function nodeDefaults(type: WorkflowNodeType): Record<string, any> {
  switch (type) {
    case 'condition': return { condition: conditionDefaults('map') }
    case 'waitUntil': return { condition: conditionDefaults('map'), pollMs: 500, stableDurationMs: 0 }
    case 'pathTo': return { x: 0, y: 0, action: 1, targetId: 0, pollMs: 300 }
    case 'moveTo': return { x: 0, y: 0, action: 1, targetId: 0, confirm: true, arriveDistance: 1, pollMs: 300 }
    case 'warpTo': return { x: 0, y: 0, confirm: true, arriveDistance: 2, pollMs: 300 }
    case 'teleport': return { destId: 0, cityName: '', expectedMapId: undefined, settleMs: 1500 }
    case 'revive': return { safetyCheck: true, mode: 1, expectedMapId: undefined, pollMs: 500 }
    case 'approachEntity': return { creatureId: 0, maxDistance: 100, allVisible: false, monsterTblId: undefined, name: '', nameContains: '', nameNotContains: '', isNpc: undefined, attackable: undefined, approachDistance: 3, pollMs: 300 }
    case 'interact': return { creatureId: 0, maxDistance: 100, allVisible: false, monsterTblId: undefined, nameContains: '', nameNotContains: '', waitDialog: true, pollMs: 300 }
    case 'dialogSelect': return { raw: true, nearestNpc: true, npcId: undefined, maxDistance: 5, allVisible: false, name: '', nameContains: '', waitForNpcMs: 5000, pollMs: 300, option: 0, sub: 1, expectedMapId: undefined }
    case 'dungeonEntry': return { maxDistance: 5, allVisible: false, name: '', nameContains: '', waitForNpcMs: 5000, nativeDialogTimeoutMs: 8000, nativeDialogMaxSteps: 10, pollMs: 300, option: 9575, sub: 1, autoUseExtraTicket: true, ticketItemId: 26419, confirmTextContains: 'Another 額外入場券', expectedMapNameContains: '賢者之塔', mapStableDurationMs: 1500 }
    case 'clearMonsters': return { maxDistance: 30, allVisible: false, monsterTblId: undefined, nameContains: '', nameNotContains: '', approachDistance: 3, pollMs: 500, reissueMs: 3000, emptyConfirmations: 3, emptyDurationMs: 5000 }
    case 'killBoss': return { maxDistance: 0, allVisible: true, monsterTblId: undefined, name: '', nameContains: '[BOSS]', nameNotContains: '', maxHpGt: 800000000, waitForAppearanceMs: 180000, skipIfAbsent: false, approachDistance: 3, pollMs: 500, reissueMs: 2000, missingConfirmations: 3, missingDurationMs: 3000 }
    case 'collectFilteredDrops': return { maxDistance: 0, includeUnpickable: true, approachDistance: 2.5, pollMs: 500, emptyDurationMs: 3000, nativePickupTimeoutMs: 5000, maxDurationMs: 100000 }
    case 'wait': return { ms: 1000 }
    case 'loop': return { count: 1 }
    case 'command': return { action: 'getStatus', args: {}, saveAs: '', parseJson: true }
    case 'end': return { success: true, message: '副本流程完成' }
  }
}

function conditionDefaults(kind: string): Record<string, any> {
  switch (kind) {
    case 'mapName': return { kind, value: '', operator: 'contains' }
    case 'position': return { kind, x: 0, y: 0, distance: 2, operator: 'lte' }
    case 'dead': return { kind, value: true, operator: 'eq' }
    case 'entity': return { kind, maxDistance: 30, allVisible: false, monsterTblId: undefined, nameContains: '', nameNotContains: '', operator: 'exists', value: 1 }
    case 'player': return { kind, maxDistance: 600, name: '', operator: 'exists', value: 1 }
    case 'command': return { kind, action: 'getDialog', args: {}, path: 'open', operator: 'eq', value: true }
    default: return { kind: 'map', mapId: 1, operator: 'eq' }
  }
}

function defaultTimeout(type: WorkflowNodeType): number | undefined {
  if (type === 'end') return undefined
  if (type === 'pathTo' || type === 'approachEntity') return 120000
  if (type === 'clearMonsters') return 600000
  if (type === 'killBoss') return 900000
  if (type === 'collectFilteredDrops') return 110000
  if (type === 'waitUntil') return 60000
  if (type === 'dungeonEntry') return 60000
  if (type === 'wait') return 10000
  return 30000
}

function makeNode(type: WorkflowNodeType): WorkflowNode {
  return {
    id: newId('node'), type, name: meta[type].name, params: nodeDefaults(type),
    timeoutMs: defaultTimeout(type), retries: 0, retryDelayMs: 500,
  }
}

function newWorkflow() {
  const end = makeNode('end')
  const id = newId('workflow')
  draft.value = {
    id, name: '新副本流程', description: '', startNodeId: end.id,
    maxTransitions: 1000, maxRuntimeMs: 30 * 60000, nodes: [end],
  }
  selectedWorkflowId.value = id
  selectedNodeId.value = end.id
  validationMessage.value = '尚未保存'
}

function selectWorkflow(id: string) {
  const source = workflows.value.find((item) => item.id === id)
  if (!source) return
  draft.value = clone(source)
  selectedWorkflowId.value = id
  selectedNodeId.value = draft.value.startNodeId || draft.value.nodes[0]?.id || ''
  validationMessage.value = ''
}

function duplicateWorkflow() {
  if (!draft.value) return
  const copy = clone(draft.value)
  copy.id = newId('workflow')
  copy.name = `${copy.name} - 副本`
  copy.updatedAt = undefined
  draft.value = copy
  selectedWorkflowId.value = copy.id
  validationMessage.value = '复制完成，尚未保存'
}

async function saveDraft() {
  if (!draft.value) return
  const error = validateDraft(draft.value)
  if (error) { validationMessage.value = error; ElMessage.error(error); return }
  saving.value = true
  try {
    const saved = await api.saveWorkflow(clone(draft.value))
    const index = workflows.value.findIndex((item) => item.id === saved.id)
    if (index >= 0) workflows.value[index] = saved
    else workflows.value.push(saved)
    workflows.value.sort((a, b) => a.name.localeCompare(b.name, 'zh-CN'))
    draft.value = clone(saved)
    selectedWorkflowId.value = saved.id
    validationMessage.value = '已保存'
    ElMessage.success('工作流已保存')
  } catch (error: any) {
    ElMessage.error(`保存失败：${error.message}`)
  } finally {
    saving.value = false
  }
}

async function deleteDraft() {
  if (!draft.value) return
  try {
    await ElMessageBox.confirm(`删除工作流“${draft.value.name}”？正在运行的实例不受影响。`, '确认删除', { type: 'warning' })
    await api.deleteWorkflow(draft.value.id)
    workflows.value = workflows.value.filter((item) => item.id !== draft.value?.id)
    draft.value = null
    selectedWorkflowId.value = ''
    selectedNodeId.value = ''
    if (workflows.value[0]) selectWorkflow(workflows.value[0].id)
    ElMessage.success('已删除')
  } catch (error: any) {
    if (error !== 'cancel') ElMessage.error(`删除失败：${error.message || error}`)
  }
}

function addNode(type: WorkflowNodeType) {
  if (!draft.value) newWorkflow()
  if (!draft.value) return
  const node = makeNode(type)
  if (type === 'killBoss' && bossNodes.value.length > 0) {
    node.params.maxHpGt = bossHpThreshold.value
  }
  const selectedIndex = draft.value.nodes.findIndex((item) => item.id === selectedNodeId.value)
  if (draft.value.nodes.length === 1 && draft.value.nodes[0].type === 'end') {
    node.next = draft.value.nodes[0].id
    draft.value.nodes.unshift(node)
    draft.value.startNodeId = node.id
  } else if (selectedIndex >= 0 && draft.value.nodes[selectedIndex].type !== 'condition' && draft.value.nodes[selectedIndex].type !== 'loop' && draft.value.nodes[selectedIndex].type !== 'end') {
    const selected = draft.value.nodes[selectedIndex]
    node.next = selected.next
    selected.next = node.id
    draft.value.nodes.splice(selectedIndex + 1, 0, node)
  } else {
    draft.value.nodes.push(node)
    if (!draft.value.startNodeId) draft.value.startNodeId = node.id
  }
  selectedNodeId.value = node.id
  validationMessage.value = '有未保存改动'
}

function selectNode(id: string) { selectedNodeId.value = id }
function setStart(id: string) { if (draft.value) { draft.value.startNodeId = id; validationMessage.value = '有未保存改动' } }

function moveNode(index: number, delta: number) {
  if (!draft.value) return
  const target = index + delta
  if (target < 0 || target >= draft.value.nodes.length) return
  const [node] = draft.value.nodes.splice(index, 1)
  draft.value.nodes.splice(target, 0, node)
  validationMessage.value = '仅调整了显示顺序；连线未改变'
}

function duplicateNode(id: string) {
  if (!draft.value) return
  const index = draft.value.nodes.findIndex((node) => node.id === id)
  if (index < 0) return
  const copy = clone(draft.value.nodes[index])
  copy.id = newId('node')
  copy.name = `${copy.name} - 副本`
  draft.value.nodes.splice(index + 1, 0, copy)
  selectedNodeId.value = copy.id
  validationMessage.value = '有未保存改动'
}

function removeNode(id: string) {
  if (!draft.value) return
  draft.value.nodes = draft.value.nodes.filter((node) => node.id !== id)
  for (const node of draft.value.nodes) {
    for (const edge of ['next', 'onTrue', 'onFalse', 'onFailure'] as const) {
      if (node[edge] === id) node[edge] = undefined
    }
  }
  if (draft.value.nodes.length === 0) {
    const end = makeNode('end')
    draft.value.nodes.push(end)
  }
  if (draft.value.startNodeId === id) draft.value.startNodeId = draft.value.nodes[0].id
  selectedNodeId.value = draft.value.nodes[0].id
  validationMessage.value = '有未保存改动'
}

function changeNodeType(rawType: WorkflowNodeType) {
  if (!selectedNode.value) return
  const oldName = selectedNode.value.name
  const oldMeta = meta[selectedNode.value.type]
  selectedNode.value.type = rawType
  selectedNode.value.params = nodeDefaults(rawType)
  selectedNode.value.timeoutMs = defaultTimeout(rawType)
  if (rawType === 'dungeonEntry') selectedNode.value.retries = 0
  if (!oldName || oldName === oldMeta.name) selectedNode.value.name = meta[rawType].name
  if (rawType === 'condition' || rawType === 'loop') selectedNode.value.next = undefined
  else { selectedNode.value.onTrue = undefined; selectedNode.value.onFalse = undefined }
  if (rawType === 'end') {
    selectedNode.value.next = undefined
    selectedNode.value.onFailure = undefined
  }
  resetParamsJson()
  validationMessage.value = '有未保存改动'
}

function changeConditionKind(kind: string) {
  if (!selectedNode.value) return
  selectedNode.value.params.condition = conditionDefaults(kind)
  resetParamsJson()
}

function changeEntityKind(kind: string) {
  delete conditionParams.value.isNpc
  delete conditionParams.value.attackable
  if (kind === 'npc') conditionParams.value.isNpc = true
  if (kind === 'monster') conditionParams.value.attackable = true
}

function changeDialogRaw(value: boolean) {
  if (!selectedNode.value) return
  selectedNode.value.params.raw = value
  if (value) {
    selectedNode.value.params.nearestNpc = true
    delete selectedNode.value.params.npcId
  } else {
    selectedNode.value.params.nearestNpc = false
    delete selectedNode.value.params.npcId
  }
}

function changeDialogNpcSource(value: boolean) {
  if (!selectedNode.value) return
  selectedNode.value.params.nearestNpc = value
  if (value) delete selectedNode.value.params.npcId
  else selectedNode.value.params.npcId = 0
}

function resetParamsJson() {
  paramsJsonError.value = ''
  paramsJsonText.value = JSON.stringify(selectedNode.value?.params || {}, null, 2)
}

function applyParamsJson() {
  if (!selectedNode.value) return
  try {
    const parsed = JSON.parse(paramsJsonText.value)
    if (!parsed || Array.isArray(parsed) || typeof parsed !== 'object') throw new Error('参数必须是 JSON 对象')
    selectedNode.value.params = parsed
    paramsJsonError.value = ''
    ElMessage.success('参数 JSON 已应用')
  } catch (error: any) {
    paramsJsonError.value = error.message
  }
}

function validateDraft(workflow: WorkflowDefinition): string {
  if (!workflow.name.trim()) return '流程名称不能为空'
  if (workflow.nodes.length === 0) return '至少需要一个节点'
  const ids = new Set<string>()
  for (const node of workflow.nodes) {
    if (!node.id) return '节点 ID 不能为空'
    if (ids.has(node.id)) return `节点 ID 重复：${node.id}`
    ids.add(node.id)
  }
  if (!ids.has(workflow.startNodeId)) return '起点不存在'
  for (const node of workflow.nodes) {
    for (const edge of ['next', 'onTrue', 'onFalse', 'onFailure'] as const) {
      if (node[edge] && !ids.has(node[edge] as string)) return `${node.name}.${edge} 指向不存在的节点`
    }
    if ((node.type === 'condition' || node.type === 'loop') && !node.onTrue && !node.onFalse && !node.next) {
      return `${node.name} 还没有真假分支`
    }
  }
  return ''
}

function nodeLabel(id?: string) {
  if (!id) return '结束'
  const node = draft.value?.nodes.find((item) => item.id === id)
  return node ? node.name || node.id : id
}

function nodeSummary(node: WorkflowNode): string {
  const p: any = node.params
  if (node.type === 'condition' || node.type === 'waitUntil') {
    const c = p.condition || p
    if (c.kind === 'map') return `mapId ${c.operator === 'ne' ? '≠' : '='} ${c.mapId}`
    if (c.kind === 'mapName') return `地图名称包含 ${c.value || '?'}`
    if (c.kind === 'position') return `距离 (${c.x}, ${c.y}) ≤ ${c.distance}`
    if (c.kind === 'dead') return c.value ? '角色已死亡' : '角色存活'
    if (c.kind === 'entity') return `${c.attackable ? '可攻击怪' : c.isNpc ? 'NPC' : '实体'} · ${c.allVisible ? '当前视野' : `半径 ${c.maxDistance}`}`
    if (c.kind === 'player') return `玩家 ${c.name || '任意'} · 半径 ${c.maxDistance}`
    if (c.kind === 'command') return `${c.action || '?'} → ${c.path || '$'}`
  }
  if (node.type === 'pathTo' || node.type === 'moveTo' || node.type === 'warpTo') return `(${p.x}, ${p.y})`
  if (node.type === 'teleport') return p.destId ? `destId=${p.destId}` : p.cityName || '未配置'
  if (node.type === 'revive') return `模式 ${p.mode}`
  if (node.type === 'approachEntity') return `${p.name || p.nameContains || p.creatureId || '任意实体'} · 距离 ≤ ${p.approachDistance}`
  if (node.type === 'interact') return `ID ${p.creatureId || p.monsterTblId || '-'} · ${p.nameContains || '任意名称'}`
  if (node.type === 'dialogSelect') return p.raw
    ? `${p.nearestNpc ? '最近NPC' : `npc=${p.npcId}`}, opt=${p.option}`
    : `当前对话 index=${p.option}`
  if (node.type === 'dungeonEntry') return `最近 NPC · opt=${p.option} · ${p.autoUseExtraTicket ? '免费用尽后最多使用1张票' : '不自动用票'}`
  if (node.type === 'clearMonsters') return `${p.allVisible ? '当前视野' : `半径 ${p.maxDistance}`} · ${p.monsterTblId || p.nameContains || '全部可攻击怪'}`
  if (node.type === 'killBoss') return `${p.name || p.nameContains || '[BOSS]'} · 最大 HP > ${Number(p.maxHpGt ?? p.initialHpGt ?? 0).toLocaleString()}`
  if (node.type === 'collectFilteredDrops') return `当前视野过滤掉落 · 最长 ${Math.round((p.maxDurationMs || 0) / 1000)} 秒`
  if (node.type === 'wait') return `${p.ms} ms`
  if (node.type === 'loop') return `${p.count} 次`
  if (node.type === 'command') return p.action || '未配置 action'
  if (node.type === 'end') return p.success === false ? '失败结束' : '成功结束'
  return ''
}

function nodeTagType(type: WorkflowNodeType): '' | 'success' | 'warning' | 'danger' | 'info' {
  if (type === 'condition' || type === 'waitUntil' || type === 'loop') return 'warning'
  if (type === 'clearMonsters' || type === 'killBoss' || type === 'interact' || type === 'dialogSelect' || type === 'dungeonEntry') return 'danger'
  if (type === 'collectFilteredDrops' || type === 'approachEntity') return 'info'
  if (type === 'end') return 'success'
  if (type === 'command') return 'info'
  return ''
}

async function loadWorkflows() {
  try {
    workflows.value = await api.listWorkflows()
    if (workflows.value.length > 0) selectWorkflow(workflows.value[0].id)
    else newWorkflow()
  } catch (error: any) {
    ElMessage.error(`加载工作流失败：${error.message}`)
  }
}

async function startDraft() {
  if (!draft.value || selectedPids.value.length === 0) return
  const saved = workflows.value.find((item) => item.id === draft.value?.id)
  if (!saved || JSON.stringify(saved) !== JSON.stringify(draft.value)) {
    ElMessage.warning('请先保存当前工作流，再启动')
    return
  }
  starting.value = true
  try {
    const result = await api.runWorkflow(draft.value.id, selectedPids.value)
    await new Promise((resolve) => setTimeout(resolve, 200))
    await refreshRuns()
    activePanel.value = 'runtime'
    if (result.errors.length > 0) {
      const detail = result.errors.map((item) => `${item.pid}: ${item.error}`).join('；')
      ElMessage.warning(`已启动 ${result.runs.length} 个，失败 ${result.errors.length} 个：${detail}`)
    } else {
      ElMessage.success(`已启动 ${result.runs.length} 个运行实例`)
    }
  } catch (error: any) {
    ElMessage.error(`启动失败：${error.message}`)
  } finally {
    starting.value = false
  }
}

async function refreshRuns() {
  try {
    runs.value = await api.listRuns()
    if (selectedRunId.value && !runs.value.some((run) => run.id === selectedRunId.value)) selectedRunId.value = ''
    if (!selectedRunId.value && visibleRuns.value[0]) selectedRunId.value = visibleRuns.value[0].id
  } catch { /* broker 重启窗口静默重试 */ }
}

function selectRun(row: WorkflowRunSnapshot | undefined) { if (row) selectedRunId.value = row.id }

async function control(run: WorkflowRunSnapshot, action: 'pause' | 'resume' | 'stop') {
  try {
    await api.controlRun(run.id, action)
    await refreshRuns()
  } catch (error: any) {
    ElMessage.error(`操作失败：${error.message}`)
  }
}

function isRunActive(status: string) { return ['running', 'paused', 'stopping'].includes(status) }
function runStatusText(status: string) { return ({ running: '运行中', paused: '已暂停', stopping: '停止中', stopped: '已停止', completed: '已完成', failed: '失败' } as Record<string, string>)[status] || status }
function runTagType(status: string): '' | 'success' | 'warning' | 'danger' | 'info' {
  if (status === 'completed') return 'success'
  if (status === 'failed') return 'danger'
  if (status === 'running') return 'warning'
  return 'info'
}

function runDuration(run: WorkflowRunSnapshot) {
  const end = run.endedAt || Date.now()
  const seconds = Math.max(0, Math.floor((end - Number(run.startedAt || end)) / 1000))
  if (seconds < 60) return `${seconds}s`
  return `${Math.floor(seconds / 60)}m ${seconds % 60}s`
}

function formatTime(value?: number) {
  return value ? new Date(value).toLocaleTimeString() : '--:--:--'
}

onMounted(async () => {
  await Promise.all([loadWorkflows(), refreshRuns()])
  runTimer = window.setInterval(refreshRuns, 1000)
})

onUnmounted(() => {
  if (runTimer !== undefined) window.clearInterval(runTimer)
})
</script>

<style scoped>
.workflow-view { min-width: 980px; }
.page-title, .toolbar, .flow-header, .node-card-head, .json-actions, .run-workflow-row { display: flex; align-items: center; }
.page-title { justify-content: space-between; margin-bottom: 14px; }
.page-title h2 { margin: 0 0 4px; font-size: 22px; }
.page-title p, .muted { margin: 0; color: var(--el-text-color-secondary); font-size: 12px; }
.toolbar { gap: 8px; margin-bottom: 12px; }
.validation-hint { margin-left: auto; color: var(--el-text-color-secondary); font-size: 12px; }
.meta-card { margin-bottom: 12px; }
.meta-card :deep(.el-card__body) { padding-bottom: 2px; }
.description-item { width: min(480px, 100%); }
.description-item :deep(.el-form-item__content), .description-item :deep(.el-input) { width: 100%; }
.unit { margin-left: 6px; color: var(--el-text-color-secondary); }
.designer-grid { display: grid; grid-template-columns: 210px minmax(390px, 1fr) 350px; gap: 12px; align-items: start; }
.designer-grid > .el-card { min-width: 0; }
.card-title { font-weight: 600; }
.palette-group { margin-bottom: 14px; }
.palette-title { margin-bottom: 6px; color: var(--el-text-color-secondary); font-size: 12px; }
.palette-button { width: 100%; height: auto; min-height: 42px; margin: 0 0 6px !important; padding: 7px 10px; justify-content: space-between; }
.palette-button small { color: var(--el-text-color-secondary); }
.flow-header, .node-card-head, .json-actions { justify-content: space-between; gap: 8px; }
.flow-card :deep(.el-card__body) { background: var(--el-fill-color-lighter); }
.node-list { display: flex; flex-direction: column; }
.node-card { padding: 11px; border: 1px solid var(--el-border-color); border-left: 4px solid var(--el-border-color); border-radius: 7px; background: var(--el-bg-color); cursor: pointer; transition: .15s; }
.node-card:hover, .node-card.selected { border-color: var(--el-color-primary); box-shadow: 0 0 0 1px var(--el-color-primary-light-7); }
.node-card.start { border-left-color: var(--el-color-success); }
.node-card.running { box-shadow: 0 0 0 2px var(--el-color-warning-light-5); }
.node-card-head > div:first-child { display: flex; align-items: center; gap: 7px; min-width: 0; }
.node-card-head strong { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.node-badges, .edge-row, .node-actions { display: flex; flex-wrap: wrap; gap: 6px; }
.node-summary { margin: 8px 0; color: var(--el-text-color-regular); font-size: 13px; }
.edge { padding: 2px 6px; border-radius: 4px; background: var(--el-fill-color); color: var(--el-text-color-secondary); font-size: 11px; }
.edge.true { color: var(--el-color-success-dark-2); }.edge.false, .edge.failure { color: var(--el-color-danger-dark-2); }
.node-actions { justify-content: flex-end; margin-top: 6px; }
.node-actions .el-button + .el-button { margin-left: 0; }
.visual-connector { height: 22px; color: var(--el-text-color-placeholder); text-align: center; line-height: 22px; }
.editor-card { position: sticky; top: 0; max-height: calc(100vh - 145px); overflow: auto; }
.node-description { margin: -2px 0 12px; padding: 8px; border-radius: 5px; background: var(--el-fill-color-light); color: var(--el-text-color-secondary); font-size: 12px; line-height: 1.5; }
.ticket-count-panel { margin: 10px 0; padding: 9px; border: 1px solid var(--el-border-color); border-radius: 6px; }
.ticket-count-head, .ticket-count-row { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.ticket-count-head { margin-bottom: 7px; }.ticket-count-row + .ticket-count-row { margin-top: 6px; }
.two-cols, .three-cols { display: grid; gap: 8px; }.two-cols { grid-template-columns: 1fr 1fr; }.three-cols { grid-template-columns: 1fr 1fr 1fr; }
.advanced-json { margin-top: 10px; }.advanced-json :deep(textarea) { font-family: Consolas, monospace; font-size: 12px; }
.json-actions { margin-top: 7px; justify-content: flex-end; }.json-error { margin-right: auto; color: var(--el-color-danger); font-size: 12px; }
.run-launch-card { margin-bottom: 12px; }.run-workflow-row { gap: 10px; margin-bottom: 12px; }
.runtime-grid { display: grid; grid-template-columns: minmax(560px, 1.25fr) minmax(420px, .75fr); gap: 12px; }
.log-card { min-width: 0; }.run-detail { display: flex; flex-direction: column; gap: 10px; }
.log-list { height: 480px; overflow: auto; padding: 8px; border-radius: 5px; background: #111827; color: #d1d5db; font-family: Consolas, monospace; font-size: 12px; }
.log-line { display: flex; gap: 7px; padding: 2px 0; white-space: pre-wrap; word-break: break-all; }.log-line.warn { color: #fbbf24; }.log-line.error { color: #f87171; }
.log-time { color: #6b7280; flex-shrink: 0; }.log-node { color: #60a5fa; flex-shrink: 0; }
@media (max-width: 1250px) {
  .designer-grid { grid-template-columns: 190px minmax(360px, 1fr); }
  .editor-card { grid-column: 1 / -1; position: static; max-height: none; }
  .runtime-grid { grid-template-columns: 1fr; }
}
</style>

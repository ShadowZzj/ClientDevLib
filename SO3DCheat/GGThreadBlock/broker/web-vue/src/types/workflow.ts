export const WORKFLOW_NODE_TYPES = [
  'condition',
  'waitUntil',
  'pathTo',
  'moveTo',
  'warpTo',
  'teleport',
  'revive',
  'approachEntity',
  'interact',
  'dialogSelect',
  'dungeonEntry',
  'clearMonsters',
  'killBoss',
  'collectFilteredDrops',
  'wait',
  'loop',
  'command',
  'end',
] as const

export type WorkflowNodeType = (typeof WORKFLOW_NODE_TYPES)[number]

export const WORKFLOW_CONDITION_KINDS = [
  'map',
  'mapName',
  'position',
  'dead',
  'entity',
  'player',
  'command',
] as const

export type WorkflowConditionKind = (typeof WORKFLOW_CONDITION_KINDS)[number]

export interface WorkflowCondition {
  kind: WorkflowConditionKind
  [key: string]: any
}

export interface WorkflowNode {
  id: string
  type: WorkflowNodeType
  name: string
  params: Record<string, any>
  next?: string
  onTrue?: string
  onFalse?: string
  onFailure?: string
  timeoutMs?: number
  retries?: number
  retryDelayMs?: number
}

export interface WorkflowDefinition {
  id: string
  name: string
  description?: string
  startNodeId: string
  maxTransitions: number
  maxRuntimeMs: number
  nodes: WorkflowNode[]
  updatedAt?: number
}

export interface WorkflowCatalogParam {
  key: string
  label: string
  type: string
  required?: boolean
  default?: unknown
  options?: Array<unknown | { label: string; value: unknown }>
}

export interface WorkflowCatalogType {
  type: WorkflowNodeType | string
  name: string
  description?: string
  params?: WorkflowCatalogParam[]
}

export interface WorkflowCatalog {
  types: WorkflowCatalogType[]
}

export type WorkflowRunPhase =
  | 'pending'
  | 'running'
  | 'paused'
  | 'completed'
  | 'stopped'
  | 'failed'
  | string

export interface WorkflowRunLog {
  seq?: number
  at?: number
  level?: string
  message: string
  nodeId?: string
}

export interface WorkflowRunSnapshot {
  id: string
  workflowId: string
  workflowName?: string
  pid: number
  characterName?: string
  status: WorkflowRunPhase
  currentNodeId?: string
  currentNodeName?: string
  currentNodeType?: string
  transitions?: number
  startedAt?: number
  updatedAt?: number
  endedAt?: number
  lastError?: string
  logs: WorkflowRunLog[]
}

export interface WorkflowStartResult {
  runs: WorkflowRunSnapshot[]
  errors: Array<{ pid: number; error: string }>
}

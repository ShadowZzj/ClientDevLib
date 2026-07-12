import type {
  WorkflowCatalog,
  WorkflowCatalogType,
  WorkflowDefinition,
  WorkflowRunLog,
  WorkflowRunSnapshot,
  WorkflowStartResult,
} from '@/types/workflow'

async function readResponse(res: Response): Promise<any> {
  const text = await res.text()
  if (!text) return null
  try {
    return JSON.parse(text)
  } catch {
    return text
  }
}

async function request<T>(url: string, init?: RequestInit): Promise<T> {
  const res = await fetch(url, init)
  const data = await readResponse(res)
  if (!res.ok) {
    const detail = data?.error || data?.detail || data?.message || (typeof data === 'string' ? data : '')
    throw new Error(detail || `HTTP ${res.status}`)
  }
  return data as T
}

function unwrapArray<T>(value: any, key: string): T[] {
  if (Array.isArray(value)) return value as T[]
  if (Array.isArray(value?.[key])) return value[key] as T[]
  return []
}

function normalizeCatalog(value: any): WorkflowCatalog {
  const rawTypes = Array.isArray(value) ? value : value?.types
  const types: WorkflowCatalogType[] = Array.isArray(rawTypes)
    ? rawTypes.map((entry: any) => typeof entry === 'string'
      ? { type: entry, name: entry, params: [] }
      : {
          type: String(entry?.type || ''),
          name: String(entry?.name || entry?.label || entry?.type || ''),
          description: entry?.description ? String(entry.description) : '',
          params: Array.isArray(entry?.params) ? entry.params : [],
        })
    : []
  return { types: types.filter((entry) => !!entry.type) }
}

function normalizeLogs(value: any): WorkflowRunLog[] {
  const logs = Array.isArray(value) ? value : typeof value === 'string' ? value.split('\n') : []
  return logs
    .filter((entry: any) => entry !== '')
    .map((entry: any, index: number) => typeof entry === 'string'
      ? { seq: index + 1, message: entry }
      : {
          seq: Number.isFinite(Number(entry?.seq)) ? Number(entry.seq) : index + 1,
          at: Number.isFinite(Number(entry?.at)) ? Number(entry.at) : undefined,
          level: entry?.level ? String(entry.level) : undefined,
          message: String(entry?.message ?? entry?.line ?? ''),
          nodeId: entry?.nodeId ? String(entry.nodeId) : undefined,
        })
}

function normalizeRun(value: any): WorkflowRunSnapshot {
  return {
    ...value,
    id: String(value?.id ?? value?.runId ?? ''),
    workflowId: String(value?.workflowId ?? value?.definitionId ?? ''),
    pid: Number(value?.pid ?? 0),
    status: String(value?.status ?? value?.phase ?? 'pending'),
    currentNodeId: value?.currentNodeId ? String(value.currentNodeId) : undefined,
    logs: normalizeLogs(value?.logs ?? value?.log),
  }
}

export function useWorkflowApi() {
  async function getCatalog(): Promise<WorkflowCatalog> {
    return normalizeCatalog(await request<any>('/api/workflows/catalog'))
  }

  async function listWorkflows(): Promise<WorkflowDefinition[]> {
    return unwrapArray<WorkflowDefinition>(await request<any>('/api/workflows'), 'workflows')
  }

  async function getWorkflow(id: string): Promise<WorkflowDefinition> {
    const data = await request<any>(`/api/workflows/${encodeURIComponent(id)}`)
    return (data?.workflow ?? data) as WorkflowDefinition
  }

  async function saveWorkflow(workflow: WorkflowDefinition): Promise<WorkflowDefinition> {
    const data = await request<any>(`/api/workflows/${encodeURIComponent(workflow.id)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(workflow),
    })
    return (data?.workflow ?? data) as WorkflowDefinition
  }

  async function deleteWorkflow(id: string): Promise<void> {
    await request(`/api/workflows/${encodeURIComponent(id)}`, { method: 'DELETE' })
  }

  async function runWorkflow(id: string, pids: number[]): Promise<WorkflowStartResult> {
    const data = await request<any>(`/api/workflows/${encodeURIComponent(id)}/run`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pids }),
    })
    const rawRuns = unwrapArray<any>(data, 'runs')
    return {
      runs: rawRuns.map(normalizeRun),
      errors: Array.isArray(data?.errors) ? data.errors : [],
    }
  }

  async function listRuns(): Promise<WorkflowRunSnapshot[]> {
    const rows = unwrapArray<any>(await request<any>('/api/workflow-runs'), 'runs')
    return rows.map(normalizeRun).filter((run) => !!run.id)
  }

  async function getRun(id: string): Promise<WorkflowRunSnapshot> {
    const data = await request<any>(`/api/workflow-runs/${encodeURIComponent(id)}`)
    return normalizeRun(data?.run ?? data)
  }

  async function controlRun(id: string, action: 'pause' | 'resume' | 'stop'): Promise<void> {
    await request(`/api/workflow-runs/${encodeURIComponent(id)}/${action}`, { method: 'POST' })
  }

  return {
    getCatalog,
    listWorkflows,
    getWorkflow,
    saveWorkflow,
    deleteWorkflow,
    runWorkflow,
    listRuns,
    getRun,
    controlRun,
  }
}

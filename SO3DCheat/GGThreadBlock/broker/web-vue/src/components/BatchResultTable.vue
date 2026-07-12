<template>
  <el-table :data="rows" size="small" style="max-width: 560px; margin-bottom: 16px">
    <el-table-column label="账号" min-width="140">
      <template #default="{ row }">{{ row.characterName || `pid ${row.pid}` }}</template>
    </el-table-column>
    <el-table-column label="结果" width="90">
      <template #default="{ row }">
        <el-tag :type="row.ok ? 'success' : 'danger'" size="small">
          {{ row.ok ? '成功' : '失败' }}
        </el-tag>
      </template>
    </el-table-column>
    <el-table-column label="说明" prop="error" min-width="180" show-overflow-tooltip />
  </el-table>
</template>

<script setup lang="ts">
export interface BatchRow {
  pid: number
  characterName: string
  ok: boolean
  error?: string
}

defineProps<{ rows: BatchRow[] }>()
</script>

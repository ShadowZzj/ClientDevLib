<template>
  <div class="account-multi-select">
    <div class="select-row">
      <el-select
        v-model="selectedNames"
        placeholder="选择要下发的账号(可多选)"
        multiple
        filterable
        collapse-tags
        collapse-tags-tooltip
        style="width: 100%; max-width: 560px"
        @change="persist"
      >
        <el-option
          v-for="opt in characterOptions"
          :key="opt.value"
          :label="opt.label"
          :value="opt.value"
        />
      </el-select>
      <el-tag :type="onlineCount > 0 ? 'success' : 'info'" size="small" class="online-tag">
        {{ onlineCount > 0 ? `${onlineCount} 个在线` : '无在线角色' }}
      </el-tag>
    </div>
    <div class="select-actions">
      <el-button size="small" :disabled="onlineCount === 0" @click="selectAllOnline">
        全选在线 ({{ onlineCount }})
      </el-button>
      <el-button size="small" :disabled="selectedNames.length === 0" @click="clearSelection">
        清空
      </el-button>
      <span class="hint">已选 {{ selectedNames.length }} 个 · 两个标签页共用这份名单。离线账号会被自动跳过。</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { useSelectedAccounts } from '@/composables/useSelectedAccounts'

const { selectedNames, characterOptions, onlineCount, selectAllOnline, clearSelection, persist } =
  useSelectedAccounts()
</script>

<style scoped>
.account-multi-select {
  margin-bottom: 8px;
}
.select-row {
  display: flex;
  align-items: center;
  gap: 10px;
}
.online-tag {
  flex-shrink: 0;
}
.select-actions {
  margin-top: 6px;
  display: flex;
  align-items: center;
  gap: 8px;
}
.hint {
  color: var(--el-text-color-secondary);
  font-size: 12px;
  margin-left: 4px;
}
</style>

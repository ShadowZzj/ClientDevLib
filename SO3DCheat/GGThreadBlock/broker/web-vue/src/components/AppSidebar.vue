<template>
  <el-aside width="168px" class="app-sidebar">
    <el-menu
      :default-active="modelValue"
      :default-openeds="defaultOpeneds"
      unique-opened
      @select="(idx: string) => $emit('update:modelValue', idx)"
    >
      <el-menu-item index="overview">
        <el-icon><Monitor /></el-icon>
        <span>总览</span>
      </el-menu-item>

      <el-menu-item index="dungeonworkflow">
        <el-icon><Connection /></el-icon>
        <span>副本编排</span>
      </el-menu-item>

      <el-sub-menu index="grp-portable">
        <template #title>
          <el-icon><Operation /></el-icon>
          <span>便携功能</span>
        </template>
        <el-menu-item index="control">
          <el-icon><Operation /></el-icon>
          <span>控制</span>
        </el-menu-item>
        <el-menu-item index="moveto">
          <el-icon><Location /></el-icon>
          <span>寻路移动</span>
        </el-menu-item>
        <el-menu-item index="reviveteleport">
          <el-icon><Position /></el-icon>
          <span>复活传送</span>
        </el-menu-item>
        <el-menu-item index="mail">
          <el-icon><Message /></el-icon>
          <span>邮寄</span>
        </el-menu-item>
        <el-menu-item index="cashbag">
          <el-icon><Goods /></el-icon>
          <span>商城背包</span>
        </el-menu-item>
        <el-menu-item index="vendorpurchase">
          <el-icon><Goods /></el-icon>
          <span>摊贩购买</span>
        </el-menu-item>
      </el-sub-menu>

      <el-sub-menu index="grp-daily">
        <template #title>
          <el-icon><Loading /></el-icon>
          <span>日常自动</span>
        </template>
        <el-menu-item index="autorevive">
          <el-icon><RefreshLeft /></el-icon>
          <span>自动复活</span>
        </el-menu-item>
        <el-menu-item index="autodelegation">
          <el-icon><Loading /></el-icon>
          <span>自动委托</span>
        </el-menu-item>
        <el-menu-item index="autotrade">
          <el-icon><Switch /></el-icon>
          <span>自动交易</span>
        </el-menu-item>
        <el-menu-item index="buffkeeper">
          <el-icon><Sunny /></el-icon>
          <span>Buff 守护</span>
        </el-menu-item>
        <el-menu-item index="rewardclaim">
          <el-icon><Present /></el-icon>
          <span>奖励领取</span>
        </el-menu-item>
        <el-menu-item index="dailytask">
          <el-icon><Present /></el-icon>
          <span>每日猎杀任务</span>
        </el-menu-item>
        <el-menu-item index="clockwork">
          <el-icon><Watch /></el-icon>
          <span>自动洗发条</span>
        </el-menu-item>
        <el-menu-item index="enchantstone">
          <el-icon><MagicStick /></el-icon>
          <span>自动洗石头</span>
        </el-menu-item>
        <el-menu-item index="autocompose">
          <el-icon><MagicStick /></el-icon>
          <span>自动合成宝石</span>
        </el-menu-item>
        <el-menu-item index="pickupfilter">
          <el-icon><Filter /></el-icon>
          <span>拾取过滤</span>
        </el-menu-item>
        <el-menu-item index="npg">
          <el-icon><View /></el-icon>
          <span>附近玩家停手</span>
        </el-menu-item>
        <el-menu-item index="paodian">
          <el-icon><Coin /></el-icon>
          <span>泡点</span>
        </el-menu-item>
      </el-sub-menu>

      <el-sub-menu index="grp-multi">
        <template #title>
          <el-icon><Connection /></el-icon>
          <span>多开协同</span>
        </template>
        <el-menu-item index="sync">
          <el-icon><Connection /></el-icon>
          <span>同步</span>
        </el-menu-item>
        <el-menu-item index="autoteam">
          <el-icon><UserFilled /></el-icon>
          <span>自动组队</span>
        </el-menu-item>
        <el-menu-item index="onlinewhitelist">
          <el-icon><UserFilled /></el-icon>
          <span>在线白名单</span>
        </el-menu-item>
      </el-sub-menu>

      <el-sub-menu index="grp-comm">
        <template #title>
          <el-icon><ChatDotRound /></el-icon>
          <span>通讯监控</span>
        </template>
        <el-menu-item index="deathnotify">
          <el-icon><Bell /></el-icon>
          <span>死亡通知</span>
        </el-menu-item>
        <el-menu-item index="chat">
          <el-icon><ChatDotRound /></el-icon>
          <span>公屏聊天</span>
        </el-menu-item>
        <el-menu-item index="gmreply">
          <el-icon><MagicStick /></el-icon>
          <span>GM 回复</span>
        </el-menu-item>
      </el-sub-menu>
    </el-menu>
  </el-aside>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { Monitor, Operation, Location, Position, Message, Goods, Bell, RefreshLeft, Loading, UserFilled, Coin, ChatDotRound, MagicStick, Sunny, Connection, Switch, Watch, Present, Filter, View } from '@element-plus/icons-vue'

const props = defineProps<{ modelValue: string }>()
defineEmits<{ 'update:modelValue': [value: string] }>()

// 各分组包含哪些菜单 index,用于「初始展开当前所在的那一组」。
const groupItems: Record<string, string[]> = {
  'grp-portable': ['control', 'moveto', 'reviveteleport', 'mail', 'cashbag', 'vendorpurchase'],
  'grp-daily': ['autorevive', 'autodelegation', 'autotrade', 'buffkeeper', 'rewardclaim', 'dailytask', 'clockwork', 'enchantstone', 'autocompose', 'pickupfilter', 'npg', 'paodian'],
  'grp-multi': ['sync', 'autoteam', 'onlinewhitelist'],
  'grp-comm': ['deathnotify', 'chat', 'gmreply'],
}

// 默认展开当前激活项所在的分组(配合 unique-opened,其余收起)。
const defaultOpeneds = computed(() => {
  for (const [grp, items] of Object.entries(groupItems)) {
    if (items.includes(props.modelValue)) return [grp]
  }
  return ['grp-portable']
})
</script>

<style scoped>
.app-sidebar {
  border-right: 1px solid var(--el-border-color);
  overflow-y: auto;
  overflow-x: hidden;
}
.el-menu {
  border-right: none;
  min-height: 100%;
}
</style>

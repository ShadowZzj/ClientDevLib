<template>
  <el-aside width="140px" class="app-sidebar">
    <el-menu
      :default-active="modelValue"
      @select="(idx: string) => $emit('update:modelValue', idx)"
    >
      <el-menu-item index="overview">
        <el-icon><Monitor /></el-icon>
        <span>总览</span>
      </el-menu-item>
      <el-menu-item index="control">
        <el-icon><Operation /></el-icon>
        <span>控制</span>
      </el-menu-item>
      <el-menu-item index="moveto">
        <el-icon><Location /></el-icon>
        <span>寻路移动</span>
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
      <el-menu-item index="deathnotify">
        <el-icon><Bell /></el-icon>
        <span>死亡通知</span>
      </el-menu-item>
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
      <el-menu-item index="clockwork">
        <el-icon><Watch /></el-icon>
        <span>自动洗发条</span>
      </el-menu-item>
      <el-menu-item index="sync">
        <el-icon><Connection /></el-icon>
        <span>同步</span>
      </el-menu-item>
      <el-menu-item index="onlinewhitelist">
        <el-icon><UserFilled /></el-icon>
        <span>在线白名单</span>
      </el-menu-item>
      <el-menu-item index="paodian">
        <el-icon><Coin /></el-icon>
        <span>泡点</span>
      </el-menu-item>
      <el-menu-item index="chat">
        <el-icon><ChatDotRound /></el-icon>
        <span>公屏聊天</span>
      </el-menu-item>
      <el-menu-item index="gmreply">
        <el-icon><MagicStick /></el-icon>
        <span>GM 回复</span>
      </el-menu-item>
    </el-menu>
  </el-aside>
</template>

<script setup lang="ts">
import { Monitor, Operation, Location, Message, Goods, Bell, RefreshLeft, Loading, UserFilled, Coin, ChatDotRound, MagicStick, Sunny, Connection, Switch, Watch } from '@element-plus/icons-vue'

defineProps<{ modelValue: string }>()
defineEmits<{ 'update:modelValue': [value: string] }>()
</script>

<style scoped>
.app-sidebar {
  border-right: 1px solid var(--el-border-color);
  overflow: hidden;
}
.el-menu {
  border-right: none;
  height: 100%;
}
</style>

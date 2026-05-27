<template>
  <el-container class="app-root">
    <AppHeader />
    <el-container>
      <AppSidebar v-model="activeTab" />
      <el-main>
        <OverviewView v-if="activeTab === 'overview'" />
        <ControlView v-if="activeTab === 'control'" />
        <MoveToView v-if="activeTab === 'moveto'" />
        <MailView v-if="activeTab === 'mail'" />
        <CashBagView v-if="activeTab === 'cashbag'" />
        <DeathNotifyView v-if="activeTab === 'deathnotify'" />
        <AutoReviveView v-if="activeTab === 'autorevive'" />
        <AutoDelegationView v-if="activeTab === 'autodelegation'" />
        <ChatView v-if="activeTab === 'chat'" />
        <GmReplyView v-if="activeTab === 'gmreply'" />
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import AppHeader from '@/components/AppHeader.vue'
import AppSidebar from '@/components/AppSidebar.vue'
import OverviewView from '@/views/OverviewView.vue'
import ControlView from '@/views/ControlView.vue'
import MoveToView from '@/views/MoveToView.vue'
import MailView from '@/views/MailView.vue'
import CashBagView from '@/views/CashBagView.vue'
import DeathNotifyView from '@/views/DeathNotifyView.vue'
import AutoReviveView from '@/views/AutoReviveView.vue'
import AutoDelegationView from '@/views/AutoDelegationView.vue'
import ChatView from '@/views/ChatView.vue'
import GmReplyView from '@/views/GmReplyView.vue'

const activeTab = ref('overview')
</script>

<style scoped>
.app-root {
  height: 100vh;
  flex-direction: column;
}
</style>

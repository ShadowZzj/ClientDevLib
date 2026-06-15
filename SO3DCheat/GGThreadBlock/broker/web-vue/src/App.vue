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
        <VendorPurchaseView v-if="activeTab === 'vendorpurchase'" />
        <DeathNotifyView v-if="activeTab === 'deathnotify'" />
        <AutoReviveView v-if="activeTab === 'autorevive'" />
        <AutoDelegationView v-if="activeTab === 'autodelegation'" />
        <AutoTradeView v-if="activeTab === 'autotrade'" />
        <BuffKeeperView v-if="activeTab === 'buffkeeper'" />
        <ClockworkWasherView v-if="activeTab === 'clockwork'" />
        <SyncView v-if="activeTab === 'sync'" />
        <OnlineWhitelistView v-if="activeTab === 'onlinewhitelist'" />
        <PaodianView v-if="activeTab === 'paodian'" />
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
import VendorPurchaseView from '@/views/VendorPurchaseView.vue'
import DeathNotifyView from '@/views/DeathNotifyView.vue'
import AutoReviveView from '@/views/AutoReviveView.vue'
import AutoDelegationView from '@/views/AutoDelegationView.vue'
import AutoTradeView from '@/views/AutoTradeView.vue'
import BuffKeeperView from '@/views/BuffKeeperView.vue'
import ClockworkWasherView from '@/views/ClockworkWasherView.vue'
import SyncView from '@/views/SyncView.vue'
import OnlineWhitelistView from '@/views/OnlineWhitelistView.vue'
import PaodianView from '@/views/PaodianView.vue'
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

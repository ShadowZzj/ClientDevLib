<template>
  <div class="death-notify">
    <h3>死亡通知</h3>
    <el-form label-width="100px" style="max-width: 640px">
      <el-form-item label="短信通道">
        <el-switch v-model="smsEnabled" @change="saveConfig" />
        <span class="hint" v-if="smsEnabled">勾选后角色 HP=0 会下发短信</span>
      </el-form-item>
      <el-form-item label="手机号" v-if="smsEnabled">
        <el-input v-model="phone" placeholder="接收短信的手机号" @blur="saveConfig" />
      </el-form-item>

      <el-divider />

      <el-form-item label="Discord">
        <el-switch v-model="discordEnabled" @change="saveConfig" />
        <span class="hint" v-if="discordEnabled">勾选后角色 HP=0 会推 Discord webhook</span>
      </el-form-item>
      <el-form-item label="Webhook URL" v-if="discordEnabled">
        <el-input
          v-model="discordWebhook"
          placeholder="留空使用默认 webhook"
          clearable
          @blur="saveConfig"
        />
        <div class="hint">
          消息格式：<code>您的角色XXX已经死亡！</code>
        </div>
      </el-form-item>
      <el-form-item label="代理" v-if="discordEnabled">
        <el-input
          v-model="discordProxy"
          placeholder="http://127.0.0.1:7890  留空则读环境变量 HTTPS_PROXY"
          clearable
          @blur="saveConfig"
        />
        <div class="hint">
          国内访问 discord.com 需要代理；Node 不会自动用 Windows 系统代理。
        </div>
      </el-form-item>
    </el-form>

    <el-divider />

    <h4>在线角色</h4>
    <el-table :data="instances" size="small" stripe max-height="400">
      <el-table-column prop="characterName" label="角色" />
      <el-table-column label="HP" width="100">
        <template #default="{ row }">
          <el-tag :type="row.hp === 0 ? 'danger' : 'success'" size="small">
            {{ row.hp ?? '—' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="通知" width="80">
        <template #default="{ row }">
          <el-switch
            :model-value="enabledSet.has(row.characterName)"
            @change="(val: boolean) => toggleChar(row.characterName, val)"
            size="small"
          />
        </template>
      </el-table-column>
      <el-table-column prop="pid" label="PID" width="80" />
    </el-table>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from "vue";

interface InstanceInfo {
  pid: number;
  characterName?: string;
  hp?: number;
}

const phone = ref("");
const smsEnabled = ref(true);
const discordEnabled = ref(false);
const discordWebhook = ref("");
const discordProxy = ref("");
const enabledCharacters = ref<string[]>([]);
const instances = ref<InstanceInfo[]>([]);
let pollTimer: number | null = null;

const enabledSet = computed(() => new Set(enabledCharacters.value));

async function loadConfig() {
  const res = await fetch("/api/death-notify");
  const cfg = await res.json();
  phone.value = cfg.phone || "";
  enabledCharacters.value = cfg.enabledCharacters || [];
  smsEnabled.value = cfg.smsEnabled !== undefined ? !!cfg.smsEnabled : true;
  discordEnabled.value = !!cfg.discordEnabled;
  discordWebhook.value = cfg.discordWebhook || "";
  discordProxy.value = cfg.discordProxy || "";
}

async function saveConfig() {
  await fetch("/api/death-notify", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      phone: phone.value,
      enabledCharacters: enabledCharacters.value,
      smsEnabled: smsEnabled.value,
      discordEnabled: discordEnabled.value,
      discordWebhook: discordWebhook.value,
      discordProxy: discordProxy.value,
    }),
  });
}

function toggleChar(name: string, enabled: boolean) {
  if (enabled) {
    if (!enabledCharacters.value.includes(name)) {
      enabledCharacters.value.push(name);
    }
  } else {
    enabledCharacters.value = enabledCharacters.value.filter((c) => c !== name);
  }
  saveConfig();
}

async function loadInstances() {
  const res = await fetch("/api/instances");
  const list = await res.json();
  instances.value = list.filter((i: any) => i.characterName);
}

onMounted(async () => {
  await loadConfig();
  await loadInstances();
  pollTimer = window.setInterval(loadInstances, 3000);
});

onUnmounted(() => {
  if (pollTimer) clearInterval(pollTimer);
});
</script>

<style scoped>
.hint {
  margin-left: 8px;
  font-size: 12px;
  color: var(--el-text-color-secondary);
}
code {
  background: var(--el-fill-color-light);
  padding: 1px 6px;
  border-radius: 3px;
}
</style>

<template>
  <div class="page">
    <!-- 顶部状态条 -->
    <div class="hdr">
      <text class="hdr-title">NapCat 管理器</text>
      <div class="hdr-right">
        <div :class="['dot', status.running ? 'dot-on' : 'dot-off']"></div>
        <text class="hdr-state">{{ statusText }}</text>
      </div>
    </div>

    <!-- 主体：左侧状态 + 右侧操作 -->
    <div class="body">
      <!-- 左：关键信息 -->
      <div class="card info">
        <div class="row">
          <text class="k">账号</text>
          <text class="v">{{ status.account || '未登录' }}</text>
        </div>
        <div class="row">
          <text class="k">PID</text>
          <text class="v">{{ status.pid > 0 ? status.pid : '—' }}</text>
        </div>
        <div class="row">
          <text class="k">设备 IP</text>
          <text class="v">{{ status.ip || '—' }}</text>
        </div>
        <div class="row">
          <text class="k">WebUI</text>
          <text class="v">{{ webuiUrl }}</text>
        </div>
      </div>

      <!-- 中：操作按钮 -->
      <div class="card ops">
        <div
          :class="['btn', 'btn-start', busy ? 'btn-disabled' : (status.running ? 'btn-dim' : '')]"
          @click="onStart"
        >
          <text class="btn-t">启动</text>
        </div>
        <div
          :class="['btn', 'btn-stop', busy ? 'btn-disabled' : (status.running ? '' : 'btn-dim')]"
          @click="onStop"
        >
          <text class="btn-t">停止</text>
        </div>
        <div :class="['btn', 'btn-mini', busy ? 'btn-disabled' : '']" @click="onPing">
          <text class="btn-t">测 OneBot</text>
        </div>
      </div>

      <!-- 右：运行信息 -->
      <div class="card info">
        <div class="row">
          <text class="k">内存</text>
          <text class="v">{{ memText }}</text>
        </div>
        <div class="row">
          <text class="k">剩余空间</text>
          <text class="v">{{ diskText }}</text>
        </div>
        <div class="row">
          <text class="k">OneBot</text>
          <text class="v">{{ onebotText }}</text>
        </div>
        <div class="row">
          <text class="k">native</text>
          <text class="v">{{ nativeText }}</text>
        </div>
      </div>
    </div>

    <!-- 底部：日志 -->
    <div class="logbox">
      <div class="logbar">
        <text class="log-t">日志</text>
        <text class="log-hint" @click="showLogs">查看</text>
        <text class="log-hint" @click="onClearLogs">清空</text>
        <text class="log-hint" @click="onRefresh">刷新</text>
      </div>
      <scroller class="logscroll" scroll-y="true">
        <text class="logtext">{{ logText }}</text>
      </scroller>
    </div>

    <!-- 提示条 -->
    <div v-if="toast" class="toast">
      <text class="toast-t">{{ toast }}</text>
    </div>

    <!-- 日志弹层 -->
    <div v-if="logModal" class="modal" @click="logModal = false">
      <div class="modal-body" @click.stop>
        <text class="modal-title">运行日志</text>
        <scroller class="modal-scroll" scroll-y="true">
          <text class="logtext">{{ logText }}</text>
        </scroller>
        <div class="modal-btn" @click="logModal = false">
          <text class="btn-t">关闭</text>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { BasePage } from '../../base-page.js'
import * as napcat from '../../services/napcat.js'

// 轮询间隔（毫秒）。NapCat 启停是秒级操作，2.5s 足够且不浪费 CPU。
const POLL_MS = 2500

export default {
  data() {
    return {
      status: {
        running: false,
        pid: -1,
        webuiPort: 6099,
        onebotPort: 3000,
        ip: '',
        account: ''
      },
      device: {},
      busy: false,
      toast: '',
      logText: '（尚未加载）',
      logModal: false,
      onebotResult: null,
      nativeAvailable: napcat.isNativeAvailable()
    }
  },

  computed: {
    statusText() {
      if (this.busy) return '处理中…'
      return this.status.running ? '运行中' : '已停止'
    },
    webuiUrl() {
      const ip = this.status.ip || '—'
      if (ip === '—') return '—'
      return ip + ':' + this.status.webuiPort
    },
    memText() {
      const d = this.device
      if (!d || !d.memTotalKb) return '—'
      const total = Math.round(d.memTotalKb / 1024)
      const avail = Math.round((d.memAvailableKb || 0) / 1024)
      return avail + 'M 可用 / ' + total + 'M'
    },
    diskText() {
      const d = this.device
      if (!d || !d.diskFreeKb) return '—'
      return (d.diskFreeKb / 1048576).toFixed(1) + 'G'
    },
    onebotText() {
      if (!this.onebotResult) return this.status.onebotPort + ' (未测)'
      if (!this.onebotResult.ok) return this.status.onebotPort + ' 不可达'
      return this.status.onebotPort + ' 通 (' + this.onebotResult.latencyMs + 'ms)'
    },
    nativeText() {
      return this.nativeAvailable ? 'ok' : '缺失'
    }
  },

  onLoad() {
    this.options = {}
    if (!this.nativeAvailable) {
      this.showToast('native 模块缺失，功能受限')
    }
  },

  onShow() {
    // 进入页面即开始轮询；每次 onShow 都重置，避免重复定时器
    this.startPoll()
  },

  onHide() {
    this.stopPoll()
  },

  onUnload() {
    // BasePage.onUnload 会 release timer，这里只需停轮询逻辑
    this.stopPoll()
  },

  methods: {
    // ---------- 轮询 ----------
    startPoll() {
      this.stopPoll()
      this.refresh()
      this.pollToken = this.setInterval(() => {
        // 弹层打开或忙碌时不打扰
        if (this.busy || this.logModal) return
        this.refresh()
      }, POLL_MS)
    },

    stopPoll() {
      if (this.pollToken != null) {
        this.clearInterval(this.pollToken)
        this.pollToken = null
      }
    },

    async refresh() {
      const st = await napcat.getStatus()
      this.status = st
      const dev = await napcat.getDeviceInfo()
      if (dev && !dev.error) this.device = dev
    },

    // ---------- 操作 ----------
    async onStart() {
      if (this.busy) return
      if (this.status.running) {
        this.showToast('已经在运行')
        return
      }
      this.busy = true
      this.showToast('正在启动…')
      const r = await napcat.start()
      this.busy = false
      if (!r.ok) {
        this.showToast('启动失败：' + r.error)
        return
      }
      this.showToast('启动命令已下发')
      // 启动需要时间，稍后复查
      this.setTimeout(() => this.refresh(), 3000)
    },

    async onStop() {
      if (this.busy) return
      if (!this.status.running) {
        this.showToast('已经停止')
        return
      }
      this.busy = true
      this.showToast('正在停止…')
      const r = await napcat.stop()
      this.busy = false
      if (!r.ok) {
        this.showToast('停止失败：' + r.error)
        return
      }
      this.showToast('已停止')
      this.refresh()
    },

    async onPing() {
      if (this.busy) return
      this.busy = true
      const r = await napcat.pingOneBot()
      this.busy = false
      this.onebotResult = r
      if (!r.ok) {
        this.showToast('OneBot 不可达（' + (r.message || r.error || '') + '）')
      } else {
        this.showToast('OneBot 正常，' + r.latencyMs + 'ms')
      }
    },

    async showLogs() {
      await this.loadLogs(300)
      this.logModal = true
    },

    async loadLogs(tail) {
      const r = await napcat.getLogs(tail)
      this.logText = r.ok ? (r.text || '（空）') : ('读取失败：' + r.error)
    },

    async onClearLogs() {
      const r = await napcat.clearLogs()
      if (r.ok) {
        this.logText = '（已清空）'
        this.showToast('日志已清空')
      } else {
        this.showToast('清空失败：' + r.error)
      }
    },

    async onRefresh() {
      await this.loadLogs(120)
      this.showToast('已刷新')
    },

    showToast(msg) {
      this.toast = msg
      if (this.toastToken) this.clearTimeout(this.toastToken)
      this.toastToken = this.setTimeout(() => {
        this.toast = ''
        this.toastToken = null
      }, 2200)
    }
  }
}
</script>

<style lang="less">
@import '../../styles/var.less';

// 设计基准 936 x 280（设备实测逻辑分辨率）
.page {
  width: 100%;
  height: 100%;
  background-color: @bg;
  display: flex;
  flex-direction: column;
  padding: 6px;
  box-sizing: border-box;
}

.hdr {
  height: 30px;
  display: flex;
  flex-direction: row;
  align-items: center;
  justify-content: space-between;
  padding: 0 6px;
}
.hdr-title {
  font-size: 17px;
  color: @fg;
  font-weight: bold;
}
.hdr-right {
  display: flex;
  flex-direction: row;
  align-items: center;
}
.dot {
  width: 9px;
  height: 9px;
  border-radius: 5px;
  margin-right: 6px;
}
.dot-on {
  background-color: @ok;
}
.dot-off {
  background-color: @off;
}
.hdr-state {
  font-size: 13px;
  color: @fg-dim;
}

.body {
  flex: 1;
  display: flex;
  flex-direction: row;
  margin-top: 4px;
}

.card {
  background-color: @card;
  border-radius: 8px;
  padding: 8px 10px;
  box-sizing: border-box;
}
.info {
  flex: 1;
  display: flex;
  flex-direction: column;
  justify-content: center;
}
.ops {
  width: 210px;
  margin: 0 8px;
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
  align-items: center;
  justify-content: center;
}

.row {
  display: flex;
  flex-direction: row;
  align-items: center;
  height: 24px;
}
.k {
  width: 76px;
  font-size: 12px;
  color: @fg-dim;
}
.v {
  flex: 1;
  font-size: 12px;
  color: @fg;
}

.btn {
  height: 40px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.btn-t {
  font-size: 14px;
  color: #ffffff;
}
.btn-start {
  width: 95px;
  background-color: @ok;
  margin-right: 8px;
}
.btn-stop {
  width: 95px;
  background-color: @danger;
}
.btn-mini {
  width: 198px;
  height: 30px;
  margin-top: 8px;
  background-color: @accent;
}
.btn-dim {
  opacity: 0.4;
}
.btn-disabled {
  opacity: 0.5;
}

.logbox {
  height: 96px;
  margin-top: 6px;
  background-color: @card;
  border-radius: 8px;
  padding: 6px 8px;
  box-sizing: border-box;
  display: flex;
  flex-direction: column;
}
.logbar {
  height: 20px;
  display: flex;
  flex-direction: row;
  align-items: center;
}
.log-t {
  flex: 1;
  font-size: 12px;
  color: @fg-dim;
}
.log-hint {
  font-size: 12px;
  color: @accent;
  margin-left: 12px;
}
.logscroll {
  flex: 1;
  width: 100%;
}
.logtext {
  font-size: 10px;
  color: @fg-dim;
  line-height: 13px;
}

.toast {
  position: absolute;
  left: 50%;
  bottom: 14px;
  margin-left: -140px;
  width: 280px;
  background-color: rgba(0, 0, 0, 0.82);
  border-radius: 6px;
  padding: 6px 10px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.toast-t {
  font-size: 12px;
  color: #ffffff;
}

.modal {
  position: absolute;
  left: 0;
  top: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.72);
  display: flex;
  align-items: center;
  justify-content: center;
}
.modal-body {
  width: 86%;
  height: 86%;
  background-color: @card;
  border-radius: 10px;
  padding: 10px;
  box-sizing: border-box;
  display: flex;
  flex-direction: column;
}
.modal-title {
  font-size: 14px;
  color: @fg;
  margin-bottom: 6px;
}
.modal-scroll {
  flex: 1;
  width: 100%;
}
.modal-btn {
  height: 34px;
  margin-top: 8px;
  border-radius: 8px;
  background-color: @accent;
  display: flex;
  align-items: center;
  justify-content: center;
}
</style>

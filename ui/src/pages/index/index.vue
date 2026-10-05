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

    <!-- 主体 -->
    <div class="body">
      <!-- 左：账号与地址 -->
      <div class="card info">
        <div class="row">
          <text class="k">账号</text>
          <text class="v">{{ loginText }}</text>
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

      <!-- 中：操作 -->
      <div class="card ops">
        <div :class="['btn', 'btn-half', busy ? 'btn-disabled' : (status.running ? 'btn-dim' : 'btn-start')]" @click="onStart">
          <text class="btn-t">启动</text>
        </div>
        <div :class="['btn', 'btn-half', busy ? 'btn-disabled' : (status.running ? 'btn-stop' : 'btn-dim')]" @click="onStop">
          <text class="btn-t">停止</text>
        </div>
        <div :class="['btn', 'btn-half', 'btn-accent', busy ? 'btn-disabled' : '']" @click="onShowQr">
          <text class="btn-t">二维码</text>
        </div>
        <div :class="['btn', 'btn-half', 'btn-accent', busy ? 'btn-disabled' : '']" @click="onPing">
          <text class="btn-t">测 OneBot</text>
        </div>
        <div :class="['btn', 'btn-full', busy ? 'btn-disabled' : (status.onebotEnabled ? 'btn-warn' : 'btn-ok')]" @click="onToggleOnebot">
          <text class="btn-t">{{ status.onebotEnabled ? '关闭 OneBot' : '开启 OneBot' }}</text>
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

    <!-- 底部日志 -->
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

    <!-- 二维码弹层：扫码登录 -->
    <div v-if="qrModal" class="modal" @click="closeQr">
      <div class="qr-body" @click.stop>
        <text class="modal-title">扫码登录 QQ</text>
        <div class="qr-wrap">
          <image v-if="qrSrc" class="qr-img" :src="qrSrc" width="176" height="176" />
          <text v-else class="qr-empty">{{ qrHint }}</text>
        </div>
        <text class="qr-tip">{{ qrSub }}</text>
        <div class="modal-btn" @click="closeQr">
          <text class="btn-t">关闭</text>
        </div>
      </div>
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
import {
  getStatus,
  getDeviceInfo,
  start as napcatStart,
  stop as napcatStop,
  pingOneBot,
  getLogs,
  clearLogs,
  getQrcode,
  setOnebot,
  isNativeAvailable,
  probeSync
} from '../../services/napcat.js'

// 轮询间隔（毫秒）。NapCat 启停是秒级操作，2.5s 足够且不浪费 CPU。
const POLL_MS = 2500
// 二维码刷新间隔（NapCat 的码约 2 分钟过期，这里 10s 拉一次）
const QR_MS = 10000
// OneBot 默认端口
const DEFAULT_ONEBOT_PORT = 3000

export default {
  data() {
    return {
      status: {
        running: false,
        pid: -1,
        webuiPort: 6099,
        onebotPort: 0,
        onebotEnabled: false,
        ip: '',
        account: '',
        loggedIn: false,
        needQrcode: false
      },
      device: {},
      busy: false,
      toast: '',
      logText: '（尚未加载）',
      logModal: false,
      qrModal: false,
      qrSrc: '',
      qrHint: '正在获取二维码…',
      qrSub: '用手机 QQ 扫码授权登录',
      onebotResult: null,
      nativeAvailable: isNativeAvailable(),
      probeText: ''
    }
  },

  computed: {
    statusText() {
      if (this.busy) return '处理中…'
      return this.status.running ? '运行中' : '已停止'
    },
    loginText() {
      if (!this.status.running) return '未运行'
      if (this.status.loggedIn) return this.status.account || '已登录'
      return '未登录'
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
      return avail + 'M / ' + total + 'M'
    },
    diskText() {
      const d = this.device
      if (!d || !d.diskFreeKb) return '—'
      return (d.diskFreeKb / 1048576).toFixed(1) + 'G'
    },
    onebotText() {
      const p = this.status.onebotPort
      if (!p) return '未启用'
      if (this.onebotResult) {
        if (this.onebotResult.ok) return p + ' 通 (' + this.onebotResult.latencyMs + 'ms)'
        return p + ' 不可达'
      }
      return p + (this.status.onebotEnabled ? '' : ' (已关闭)')
    },
    nativeText() {
      return this.nativeAvailable ? 'ok' : '缺失'
    }
  },

  methods: {
    // ================================================================
    // 生命周期钩子必须放在 methods 里 —— BasePage 通过 $root.onXxx() 调用
    // ================================================================
    onLoad() {
      this.options = {}
      if (!this.nativeAvailable) this.showToast('native 模块缺失，功能受限')
    },

    onShow() {
      this.startPoll()
    },

    onHide() {
      this.stopPoll()
      this.stopQrPoll()
    },

    onUnload() {
      this.stopPoll()
      this.stopQrPoll()
      this._clearAllTimers()
    },

    // ---------- 定时器 ----------
    _addTimer(token) {
      if (!this._timers) this._timers = []
      this._timers.push(token)
      return token
    },
    _clearAllTimers() {
      if (!this._timers) return
      for (const t of this._timers) {
        clearTimeout(t)
        clearInterval(t)
      }
      this._timers = []
    },

    // ---------- 状态轮询 ----------
    startPoll() {
      this.stopPoll()
      this.refresh()
      this.pollToken = this._addTimer(
        setInterval(() => {
          if (this.busy || this.logModal) return
          this.refresh()
        }, POLL_MS)
      )
    },
    stopPoll() {
      if (this.pollToken != null) {
        clearInterval(this.pollToken)
        this.pollToken = null
      }
    },

    async refresh() {
      const st = await getStatus()
      this.status = st
      const dev = await getDeviceInfo()
      if (dev && !dev.error) this.device = dev
    },

    // ---------- 启停 ----------
    async onStart() {
      if (this.busy) return
      if (this.status.running) return this.showToast('已经在运行')
      this.busy = true
      this.showToast('正在启动…')
      const r = await napcatStart()
      this.busy = false
      if (!r.ok) return this.showToast('启动失败：' + r.error)
      this.showToast('已下发启动命令')
      this._addTimer(setTimeout(() => this.refresh(), 3000))
    },

    async onStop() {
      if (this.busy) return
      if (!this.status.running) return this.showToast('已经停止')
      this.busy = true
      this.showToast('正在停止…')
      const r = await napcatStop()
      this.busy = false
      if (!r.ok) return this.showToast('停止失败：' + r.error)
      this.showToast('已停止')
      this.refresh()
    },

    // ---------- OneBot ----------
    async onPing() {
      if (this.busy) return
      this.busy = true
      const r = await pingOneBot()
      this.busy = false
      this.onebotResult = r
      if (!r.ok) {
        const why = this.status.onebotEnabled ? (r.message || r.error || '') : '尚未开启 OneBot'
        this.showToast('不可达：' + why)
      } else {
        this.showToast('OneBot 正常，' + r.latencyMs + 'ms')
      }
    },

    async onToggleOnebot() {
      if (this.busy) return
      const enable = !this.status.onebotEnabled
      const port = this.status.onebotPort || DEFAULT_ONEBOT_PORT
      this.busy = true
      this.showToast(enable ? '正在开启 OneBot…' : '正在关闭 OneBot…')
      const r = await setOnebot(enable, port)
      this.busy = false
      if (!r.ok) return this.showToast('操作失败：' + r.error)
      this.showToast(enable ? '已开启，重启后生效' : '已关闭，重启后生效')
      this._addTimer(setTimeout(() => this.refresh(), 1500))
    },

    // ---------- 二维码 ----------
    async onShowQr() {
      if (this.busy) return
      this.qrModal = true
      await this.loadQr()
      this.startQrPoll()
    },
    closeQr() {
      this.qrModal = false
      this.stopQrPoll()
    },
    startQrPoll() {
      this.stopQrPoll()
      this.qrToken = this._addTimer(setInterval(() => this.loadQr(), QR_MS))
    },
    stopQrPoll() {
      if (this.qrToken != null) {
        clearInterval(this.qrToken)
        this.qrToken = null
      }
    },
    async loadQr() {
      const r = await getQrcode()
      if (!r.ok) {
        this.qrSrc = ''
        this.qrHint = '读取失败：' + r.error
        return
      }
      if (!r.base64) {
        this.qrSrc = ''
        if (!this.status.running) {
          this.qrHint = 'NapCat 未运行，请先启动'
        } else if (this.status.loggedIn) {
          this.qrHint = '已登录，无需扫码'
        } else {
          this.qrHint = '暂无二维码，请稍候…'
        }
        this.qrSub = this.status.loggedIn ? '' : '二维码约每 2 分钟刷新一次'
        return
      }
      this.qrSrc = 'data:image/png;base64,' + r.base64
      this.qrSub = '用手机 QQ 扫码授权（约 2 分钟刷新）'
    },

    // ---------- 日志 ----------
    async showLogs() {
      await this.loadLogs(300)
      this.logModal = true
    },
    async loadLogs(tail) {
      const r = await getLogs(tail)
      this.logText = r.ok ? (r.text || '（空）') : ('读取失败：' + r.error)
    },
    async onClearLogs() {
      const r = await clearLogs()
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
      if (this.toastToken) clearTimeout(this.toastToken)
      this.toastToken = this._addTimer(
        setTimeout(() => {
          this.toast = ''
          this.toastToken = null
        }, 2200)
      )
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
  height: 28px;
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
.dot-on { background-color: @ok; }
.dot-off { background-color: @off; }
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
  width: 216px;
  margin: 0 8px;
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
  align-content: center;
  justify-content: space-between;
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
  height: 36px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.btn-t {
  font-size: 13px;
  color: #ffffff;
}
// 卡片内容宽 = 216 - 2*10(padding) = 196px，两列各 92px 才能放下
.btn-half {
  width: 92px;
  margin-bottom: 6px;
}
.btn-full {
  width: 196px;
  height: 30px;
}
.btn-start { background-color: @ok; }
.btn-stop { background-color: @danger; }
.btn-accent { background-color: @accent; }
.btn-ok { background-color: @ok; }
.btn-warn { background-color: @warn; }
.btn-dim { opacity: 0.4; }
.btn-disabled { opacity: 0.5; }

.logbox {
  height: 92px;
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
  margin-left: -150px;
  width: 300px;
  background-color: rgba(0, 0, 0, 0.85);
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
  background-color: rgba(0, 0, 0, 0.78);
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
  height: 32px;
  margin-top: 8px;
  border-radius: 8px;
  background-color: @accent;
  display: flex;
  align-items: center;
  justify-content: center;
}

// 二维码弹层：横向布局，适配 936x280 的矮屏
.qr-body {
  width: 62%;
  height: 92%;
  background-color: @card;
  border-radius: 10px;
  padding: 8px 12px;
  box-sizing: border-box;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
}
.qr-wrap {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
}
.qr-img {
  width: 176px;
  height: 176px;
}
.qr-empty {
  font-size: 12px;
  color: @fg-dim;
}
.qr-tip {
  font-size: 11px;
  color: @fg-dim;
  margin-top: 2px;
}
</style>

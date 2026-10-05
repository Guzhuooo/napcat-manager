// NapCat 服务适配层
//
// 职责：把 native 模块的能力包成稳定接口，统一错误语义。
// 页面只消费这里的方法，不直接碰 native 模块，也不做能力探测。
//
// 关于模块加载：
//   Falcon 运行时用 ES module，native 模块通过 import 引入。
//   这里用静态 import 是构建期行为 —— 打包器会把 'napcat' 当外部模块
//   保留为运行时 import，因此 .so 缺失时会在运行时报错而非构建失败。
//   我们捕获首次调用错误，降级为「native 缺失」而不是崩溃。

import { NapcatModule } from 'napcat'

const mod = NapcatModule
let modError = null

if (!mod) {
  modError = 'native 模块未导出 NapcatModule'
  console.log('[napcat-service] ' + modError)
}

export const isNativeAvailable = () => !!mod

/**
 * 统一把 native 的错误包装成 { ok, error } 形态，
 * 避免页面里到处 try/catch。
 */
async function call(method, ...args) {
  if (!mod || typeof mod[method] !== 'function') {
    return {
      ok: false,
      error: modError || ('native 方法不存在: ' + method),
      nativeMissing: true
    }
  }
  try {
    const r = await mod[method](...args)
    return { ok: true, data: r }
  } catch (err) {
    return { ok: false, error: String(err && err.message ? err.message : err) }
  }
}

// ---------------- 状态 ----------------

export async function getStatus() {
  const r = await call('getStatus')
  if (!r.ok) {
    return {
      running: false,
      pid: -1,
      webuiPort: 6099,
      onebotPort: 0,
      onebotEnabled: false,
      ip: '',
      account: '',
      loggedIn: false,
      needQrcode: false,
      error: r.error,
      nativeMissing: !!r.nativeMissing
    }
  }
  const d = r.data || {}
  return {
    running: !!d.running,
    pid: typeof d.pid === 'number' ? d.pid : -1,
    webuiPort: typeof d.webuiPort === 'number' ? d.webuiPort : 6099,
    onebotPort: typeof d.onebotPort === 'number' ? d.onebotPort : 0,
    onebotEnabled: !!d.onebotEnabled,
    ip: d.ip || '',
    account: d.account || '',
    loggedIn: !!d.loggedIn,
    needQrcode: !!d.needQrcode
  }
}

// 配置 OneBot 网络服务（写入 NapCat 配置，需重启生效）
export async function setOnebot(enable, port) {
  const r = await call('setOnebot', !!enable, port || 3000)
  if (!r.ok) return { ok: false, error: r.error }
  return { ok: true, result: r.data }
}

// 同步方法自检：不涉及 Promise，用于区分「模块不可用」与「异步挂起」
export function probeSync() {
  if (!mod) return 'mod=null'
  const keys = Object.keys(mod).join(',')
  let ver = '-'
  try {
    ver = typeof mod.getVersion === 'function' ? mod.getVersion() : 'no-getVersion'
  } catch (e) {
    ver = 'throw:' + (e && e.message ? e.message : e)
  }
  return 'keys=[' + keys + '] ver=' + ver
}

export async function getDeviceInfo() {
  const r = await call('getDeviceInfo')
  if (!r.ok) return { error: r.error }
  return r.data || {}
}

// ---------------- 控制 ----------------

export async function start() {
  const r = await call('start')
  if (!r.ok) return { ok: false, error: r.error }
  return { ok: true, status: r.data }
}

export async function stop() {
  const r = await call('stop')
  if (!r.ok) return { ok: false, error: r.error }
  return { ok: true, status: r.data }
}

export async function quickLogin(qq) {
  const r = await call('quickLogin', qq || '')
  if (!r.ok) return { ok: false, error: r.error }
  return { ok: true, result: r.data }
}

// ---------------- 日志 ----------------

export async function getLogs(tail) {
  const r = await call('getLogs', tail || 200)
  if (!r.ok) return { ok: false, error: r.error, text: '' }
  return { ok: true, text: (r.data && r.data.text) || '' }
}

export async function clearLogs() {
  const r = await call('clearLogs')
  return { ok: r.ok, error: r.error }
}

// ---------------- 配置 ----------------

export async function readConfig() {
  const r = await call('readConfig')
  if (!r.ok) return { ok: false, error: r.error }
  return { ok: true, config: r.data || {} }
}

export async function writeConfig(patch) {
  const r = await call('writeConfig', patch)
  if (!r.ok) return { ok: false, error: r.error }
  return { ok: true }
}

// ---------------- 二维码 ----------------

export async function getQrcode() {
  const r = await call('getQrcode')
  if (!r.ok) return { ok: false, error: r.error, base64: '' }
  return { ok: true, base64: (r.data && r.data.base64) || '' }
}

// ---------------- OneBot 探测 ----------------

export async function pingOneBot() {
  const r = await call('pingOneBot')
  if (!r.ok) return { ok: false, error: r.error }
  return r.data || { ok: false }
}

export function getVersion() {
  if (!mod || typeof mod.getVersion !== 'function') return 'n/a'
  try {
    return mod.getVersion()
  } catch (e) {
    return 'n/a'
  }
}
// NapCat 服务适配层
//
// 职责：把 native 模块的能力包成稳定接口，统一错误语义。
// 页面只消费这里的方法，不直接碰 native 模块，也不做能力探测。
//
// native 模块不可用时（例如还没装带 .so 的包），提供 mock 降级，
// 让 UI 仍可渲染，并在 status.mock 里标注，避免"假装成功"。

let mod = null
let modError = null

try {
  // eslint-disable-next-line
  mod = require('napcat').NapcatModule
} catch (err) {
  modError = String(err && err.message ? err.message : err)
  console.log('[napcat-service] native 模块加载失败: ' + modError)
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
      onebotPort: 3000,
      ip: '',
      account: '',
      error: r.error,
      nativeMissing: !!r.nativeMissing
    }
  }
  const d = r.data || {}
  return {
    running: !!d.running,
    pid: typeof d.pid === 'number' ? d.pid : -1,
    webuiPort: typeof d.webuiPort === 'number' ? d.webuiPort : 6099,
    onebotPort: typeof d.onebotPort === 'number' ? d.onebotPort : 3000,
    ip: d.ip || '',
    account: d.account || ''
  }
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

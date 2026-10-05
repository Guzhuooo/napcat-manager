import { BasePage } from './base-page.js'

// 设备 profile：真机实测（Rockchip RK3562 / cfg.json）
//   logical  : 936 x 280
//   physical : 280 x 936, direction 270
//   touch    : sitronix_ts_spi, tp_direction 270
const DESIGN_WIDTH = 936
const LOGICAL_HEIGHT = 280

class App extends $falcon.App {
  constructor() {
    super()
  }

  onLaunch(options) {
    super.onLaunch(options)

    // 以真机实测的逻辑宽度做设计基准；高度由 flex 自适应
    this.setViewPort(DESIGN_WIDTH)
    this.screenInfo = {
      designWidth: DESIGN_WIDTH,
      logicalHeight: LOGICAL_HEIGHT
    }

    $falcon.useDefaultBasePageClass(BasePage)
    console.log('[napcat-manager] launch, designWidth=' + DESIGN_WIDTH)
  }

  onShow() {
    super.onShow()
  }

  onHide() {
    super.onHide()
  }

  onDestroy() {
    super.onDestroy()
  }
}

try {
  globalThis['window'] = {
    requestAnimationFrame,
    cancelAnimationFrame
  }
} catch (err) {
  console.log(err)
}

try {
  globalThis['process'] = {
    env: {
      NODE_ENV: 'production'
    }
  }
} catch (err) {
  console.log(err)
}

export default App

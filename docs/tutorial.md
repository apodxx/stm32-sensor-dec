# STM32 SensorDeck 教程：从零搭建多传感器监测台

> 这份教程带你从"一块 Blue Pill + 一块 OLED"开始，一步步加上心率血氧、温湿度、光照、六轴、转速、舵机、蜂鸣器和旋钮，最后调成一个可以旋钮切页的小仪表台。
>
> 每一节都包含：**要接哪几根线 → 代码在哪 → 怎么验证 → 踩过什么坑**。
> 引脚和 API 的详细表格在 [README](../README.md)，这份文档偏"操作流程"。

---

## 目录

1. [成品是什么样](#1-成品是什么样)
2. [准备清单](#2-准备清单)
3. [获取代码并编译](#3-获取代码并编译)
4. [烧录与第一次上电](#4-烧录与第一次上电)
5. [接线总表](#5-接线总表)
6. [逐个模块搭起来](#6-逐个模块搭起来)
7. [程序是怎么组织的](#7-程序是怎么组织的)
8. [加一个新传感器](#8-加一个新传感器)
9. [故障排查手册](#9-故障排查手册)
10. [源码地址与二次开发](#10-源码地址与二次开发)
11. [还能做什么](#11-还能做什么)

---

## 1. 成品是什么样

一块 128×64 的 OLED 上，5 个页面用旋钮切换：

| 页面 | 内容 |
|---|---|
| 心率血氧 | 心率、血氧、PPG 波形、手指状态 |
| 温湿度 | 温度、湿度（大号数字） |
| 亮度 | 光照百分比、亮度条、分压电压 |
| 加速度 | 三轴加速度（g）、芯片温度 |
| 转速 | 电机转速（RPM） |

另外还有两个"不带页面"的执行器：**SG90 舵机**（上电自动扫动）和**有源蜂鸣器**（开机两声 + 心跳打拍子）。

整机主循环长这样：

```
每 10ms   读 MAX30102 FIFO      → 心率/血氧/波形
每 100ms  读 MPU6050 六轴
每 100ms  测速窗口统计（内部 500ms）
每 200ms  读光敏电阻 ADC
每 1s     读 DHT11（内部限速 2s）
实时      旋钮编码器 → 切页
每 40ms   刷屏（25fps）
```

---

## 2. 准备清单

### 2.1 硬件

| 器件 | 型号/规格 | 大约价格 |
|---|---|---|
| 主控板 | STM32F103C8T6 最小系统板（Blue Pill） | 10 元 |
| 屏幕 | 0.96" SSD1306 OLED，I2C 4 针 | 8 元 |
| 心率血氧 | MAX30102 模块 | 10 元 |
| 温湿度 | DHT11（3 针模块或 4 针裸件） | 5 元 |
| 光照 | GL5516 光敏电阻 + 10k 电阻 | 1 元 |
| 六轴 | MPU6050（GY-521） | 8 元 |
| 测速 | 槽型光耦模块 + 码盘 | 8 元 |
| 舵机 | SG90 | 8 元 |
| 蜂鸣器 | 有源蜂鸣器（3 针模块） | 2 元 |
| 旋钮 | 360° 旋转编码器模块（A/B/C 三脚） | 4 元 |
| 下载器 | ST-Link V2 | 10 元 |
| 其他 | 杜邦线、面包板、5V 电源（舵机用） | — |

> 不要求一次买齐。每一节都是独立的，缺哪个模块就先跳过那一页。

### 2.2 软件

**只需要两个软件**，都是 ST 官方免费工具：

| 软件 | 版本 | 用途 | 下载地址 |
|---|---|---|---|
| **STM32CubeMX** | 6.17.0 | 图形化配置引脚/时钟，生成初始化代码 | [stm32cubemx-win-v6-17-0.zip](https://pan.baud-dance.com/d/r2/cube/stm32cubemx-win-v6-17-0.zip) |
| **STM32CubeIDE** | 2.1.1 | 代码编辑、编译、下载、调试（自带 GCC / CMake / Ninja） | [st-stm32cubeide_2.1.1_28236_20260312_0043_x86_64.exe.zip](https://pan.baud-dance.com/d/r2/cube/st-stm32cubeide_2.1.1_28236_20260312_0043_x86_64.exe.zip) |

**安装顺序**：先装 CubeMX，再装 CubeIDE。CubeIDE 安装时会关联 `.ioc` 文件，之后双击工程里的
`stm32_hal_ssd1306.ioc` 就能直接打开图形化配置。

装完这两个，**不需要再单独装 GCC、CMake、Ninja、VS Code**——CubeIDE 全套自带。

> 安装路径建议不带中文和空格，例如 `D:\ST\STM32CubeIDE_2.1.1`；
> 如果打开工程后提示缺源文件，右键工程 → `Refresh`（F5）即可。

**（可选）VS Code + CMake**

工程也带了 CMake 配置，习惯命令行的可以用：

- VS Code + CMake Tools 扩展；
- 直接用 CubeIDE 自带的 GCC/Ninja（把 `...\plugins\...\tools\bin` 加进 PATH），
  或单独装 [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)。

---

## 3. 获取代码并编译

**源码地址：<https://github.com/apodxx/stm32-sensor-dec>**

```bash
git clone https://github.com/apodxx/stm32-sensor-dec.git
cd stm32-sensor-dec
```

不想装 Git 的话，网页上点 `Code → Download ZIP`，解压即可。

两种编译方式任选：

- **CubeIDE 图形界面**（推荐新手）→ 见 [3.2](#32-cubeide-编译)
- **命令行 CMake**（脚本化、适合 CI）→ 见 [3.1](#31-命令行编译)

### 3.1 命令行编译

```bash
cmake --preset debug
cmake --build build/debug/build
```

第一次会提示找不到工具链。如果装了 CubeIDE，它通常已经在 PATH 里；否则：

```bash
# Windows PowerShell 示例，路径按实际安装位置改
$env:PATH = "C:\ST\STM32CubeIDE_1.x.x\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*\tools\bin;" + $env:PATH
```

编译成功后产物在 `build/debug/build/`：

| 文件 | 用途 |
|---|---|
| `stm32_sensor_deck.hex` | 烧录用 |
| `stm32_sensor_deck.bin` | 其他工具用 |
| `stm32_sensor_deck.elf` | 调试用 |

正常占用约 **44KB Flash / 5KB RAM**（64KB / 20KB 上限）。

> **报错 `CMake was unable to find a build program corresponding to "Ninja"`**：
> 说明 PATH 里没有 ninja。CubeIDE 自带的那份在
> `...\plugins\com.st.stm32cube.ide.mcu.externaltools.ninja.*\tools\bin`，把它加进 PATH 即可；
> 或者在配置时显式指定：
> ```bash
> cmake --preset debug -DCMAKE_MAKE_PROGRAM="<ninja 的完整路径>"
> ```

### 3.2 CubeIDE 编译

1. 打开 STM32CubeIDE → `File → Import...`
2. 选 `General → Existing Projects into Workspace` → Next
3. `Select root directory` 选工程根目录（`stm32-sensor-deck`）→ Finish
4. 左侧工程名上右键 → `Refresh`（F5），确保新加的 `.c` 文件都进来了
5. 点工具栏的小锤子 `Build`（或 `Project → Build All`）

产物在工程的 `Debug/` 目录：`stm32_hal_ssd1306.elf / .hex / .bin`。

> ⚠️ 如果报 `HAL_TIM_MODULE_ENABLED` 未定义，确认 `Core/Inc/stm32f1xx_hal_conf.h` 里这一行是打开的
> （舵机 PWM 需要 `HAL_TIM`）。

---

## 4. 烧录与第一次上电

### 4.1 接线（ST-Link）

| ST-Link | 开发板 |
|---|---|
| SWDIO | PA13 |
| SWCLK | PA14 |
| GND | GND |
| 3.3V | 3.3V（板子已 USB 供电时可不接） |

BOOT0 跳线保持 0。

### 4.2 烧录

ST-Link Utility / CubeProgrammer 打开 `stm32_sensor_deck.hex` → Connect → Program → 复位。

### 4.3 第一次上电应该看到

1. **蜂鸣器"嘀嘀"两声**（接线正确的话）；
2. 屏幕显示 bilibili 开机图；
3. 进入心率页：`心率--- 血氧--%`，中间一条虚线，底部"请放手指"；
4. 转动旋钮，页面依次切换。

> 如果屏幕全黑：先查 OLED 的 SCL/SDA 有没有接反（PB10/PB11），再确认模块地址是 0x3C 还是 0x3D。

---

## 5. 接线总表

先把这张表存下来，后面每一节都会用到：

| 外设 | 接口 | 引脚 | 备注 |
|---|---|---|---|
| OLED SSD1306 | I2C2 | PB10=SCL、PB11=SDA | 地址 0x3C |
| MAX30102 | I2C1 | PB6=SCL、PB7=SDA | 地址 0x57，3.3V |
| MPU6050 | I2C1 | PB6/PB7（与上一行共用） | 地址 0x68，3.3V |
| DHT11 | 单总线 | PB0 | 需 4.7k~10k 上拉 |
| GL5516 | ADC | PA0（ADC1_IN0） | 10k 分压 |
| SG90 舵机 | PWM | PA1（TIM2_CH2） | 5V 供电，共地 |
| 蜂鸣器 | GPIO | PA2 | 有源，给电平就响 |
| 测速模块 | EXTI | PB1 | 下降沿计数 |
| 旋转编码器 | EXTI | PA5=A、PA6=B | C 接 GND |
| ESP32-S3（小智 AI） | UART1 | PA9=TX、PA10=RX | 语音控制，见 [esp32-mcp.md](esp32-mcp.md) |
| ST-Link | SWD | PA13/PA14 | |

**电源注意**：

- 所有模块 VCC 统一 **3.3V**（舵机除外，必须 5V）；
- 舵机、蜂鸣器耗电大，**不要从 3.3V 取电**，否则会掉压把单片机复位；
- 所有 GND 必须连在一起。

---

## 6. 逐个模块搭起来

> 建议顺序就是这个顺序：先屏 → 再一个传感器 → 逐个加。每加一个就烧一次、验证一次，出问题容易定位。

### 6.1 OLED：先把"眼睛"点亮

**接线**：VCC→3.3V、GND→GND、SCL→PB10、SDA→PB11。

**代码**：[`Core/Src/oled.c`](../Core/Src/oled.c)（现成驱动，不用改）。

**验证**：上电能看到开机图和界面。

**常见坑**：

| 现象 | 原因 |
|---|---|
| 全黑 | SCL/SDA 接反；或模块是 0x3D（改 `OLED_ADDRESS`） |
| 花屏/闪烁 | 杜邦线太长，I2C 400kHz 下信号质量差，可降到 100kHz（`i2c.c` 的 `ClockSpeed`） |

---

### 6.2 MAX30102：心率血氧

**接线**：VIN→3.3V、GND、SCL→PB6、SDA→PB7；NT/RD/IRD 全部**悬空**。

> RD/IRD 是 LED 阴极测试点，模块手册明确写"一般不接"。接了反而可能短路 LED 驱动。

**原理**：红光 + 红外两个 LED 交替照射手指，光电二极管接收反射光。血氧靠两个波长的吸收差异算比值。

**代码**：[`max30102.c`](../Core/Src/max30102.c)
- `MAX30102_Init()` 配置 100sps、4 次平均（FIFO 25Hz）、LED 7.2mA
- `MAX30102_Process()` 每 10ms 读 FIFO
- 心率：去直流 → 3 点滑动平均 → 找局部极大（阈值 45% × 包络）→ 300ms 不应期 → 4 拍平均
- 血氧：2.5s 窗口取 min/max 算 AC/DC → R 比值 → Maxim 经验公式

**验证**：手指轻放（别用力压），1~3 秒出心率，波形跟着心跳起伏。

**踩过的坑**（很典型）：

1. **波形是直线、心率测不到**：多半是手指压太紧（阻断血流）或没盖住窗口；
2. **红外通道完全没信号**（IR 恒为 0~40）：说明红外 LED 不亮，常见于克隆芯片。固件会**自动改用红光通道测心率**（页面上 `CH:R`），此时血氧测不了、显示 `--%`；
3. **屏幕显示"传感器未连接"**：先确认 SDA/SCL 在 **PB6/PB7**，不是 PB10/PB11（那是 OLED）。

---

### 6.3 DHT11：温湿度

**接线**：VCC→3.3V、DATA→PB0、GND；DATA 需要 4.7k~10k 上拉到 3.3V（3 针模块一般自带）。

**原理**：单总线，主机拉低 18ms 以上发起始信号，传感器回 40 位数据，每位用高电平长短区分 0/1。

**代码**：[`dht11.c`](../Core/Src/dht11.c)
- 用 DWT 周期计数器做微秒延时（不受中断影响）
- 读位期间关中断约 4ms，保证时序
- 连续失败 3 次才判无效，避免偶发一次就闪

**验证**：温湿度页显示 `温度 26℃ / 湿度 58%`。

**常见坑**：

| 现象 | 原因 |
|---|---|
| 一直 `--` | 没接上拉；DATA 不是 PB0；两次读取间隔 <1s |
| 偶尔 `ERR` | 单总线受干扰，正常，连续 3 次才判无效 |
| 数值明显偏高 | 传感器被水滴/结露打湿，晾干 1~2 天 |

---

### 6.4 GL5516：光照

**接线**（分压）：

```
3.3V ──[GL5516]──┬── PA0
                 │
              [10kΩ]
                 │
                GND
```

**原理**：光敏电阻阻值随照度指数变化，分压后送 ADC。

**代码**：[`light.c`](../Core/Src/light.c) + [`adc1.c`](../Core/Src/adc1.c)
- ADC1 直写寄存器（12 位、239.5 周期采样），不依赖 HAL 的 ADC 模块
- 每 200ms 采 8 次平均
- 百分比不是线性电压，用标定点做了分段映射（10%≈1lux、40%≈10lux、70%≈100lux、100%≈1000lux）

**验证**：手遮住光敏电阻，百分比和电压应该明显下降。

**常见坑**：分压电阻用 1k 会一直 100%，用 10k。

---

### 6.5 MPU6050：六轴

**接线**：VCC→**3.3V**、GND、SCL→PB6、SDA→PB7（和 MAX30102 共用 I2C1）、AD0 悬空。

> ⚠️ 别用 5V：GY-521 板载上拉会跟着 VCC 走，5V 会把整条 I2C 总线拉到 5V。

**原理**：加速度计 + 陀螺仪，I2C 读 0x3B 开始的 14 字节。

**代码**：[`mpu6050.c`](../Core/Src/mpu6050.c)
- 自动探测地址 0x68 / 0x69（AD0 接高就是 0x69）
- 兼容 WHO_AM_I = 0x68/0x70/0x71/0x72/0x73/0x74/0x98
- 温度公式按型号切换（MPU6050 是 raw/340+36.53，6500/6515 是 raw/333.87+21）

**验证**：平放时 `Z ≈ +1.00`，翻过来 `Z ≈ -1.00`，立起来 X 或 Y 变 ±1。

**踩过的坑**：

- **显示"传感器未连接"但扫描能看到 0x68**：说明芯片不是 MPU6050。诊断页会显示 `ID xx`，比如 `74` 就是 MPU6515（寄存器兼容，已支持）；
- **温度偏高十几度**：型号判断错了，用错公式。

> 页面上 `I2C 57 68` 是扫描到的地址，`ID 74` 是 WHO_AM_I，这两个值能把问题定位到"接线 / 地址 / 型号"中的哪一种。

---

### 6.6 测速模块：槽型光耦

**接线**：VCC→3.3V 或 5V、GND、DO→PB1；AO 悬空。

**原理**：码盘每挡住一次光，DO 翻转一次。500ms 窗口内数脉冲数 P，转速 = P × 120 / 槽数。

**代码**：[`speed.c`](../Core/Src/speed.c)
- PB1 下降沿中断，ISR 里只做 `pulseCnt++`
- 窗口结束时关中断读一次再清零，不会漏脉冲

**验证**：转速页第三行 `DO x  P xxx`：
- `DO` 是引脚当前电平；
- `P` 是上一窗口的脉冲数。

用纸片插一下槽口，`DO` 应该翻转、`P` 加 1。

**踩过的坑**：

- **DO 恒为 0**：这类无比较器的模块是"透光=0、挡光=1"，槽口空着就是 0，正常；
- **转速一直是 0**：码盘没转、或齿没挡住光路；
- **数值差一个固定倍数**：`speed.h` 的 `SPEED_SLOTS` 和实际码盘槽数不一致。

---

### 6.7 SG90 舵机

**接线**：棕/黑→GND、红→**5V**、橙/黄→PA1。

**代码**：[`servo.c`](../Core/Src/servo.c)
- TIM2 预分频 72 → 1MHz 计数，ARR=20000 → 20ms 周期
- CCR 直接就是脉宽微秒数：500=0°、1500=90°、2500=180°

**验证**：上电后舵机来回扫动（`main.c` 里 `SERVO_DEMO_SWEEP` 控制）。

**常见坑**：

- **舵机乱抖/单片机复位**：供电问题。必须 5V、共地、并 100µF 以上电容；
- **转到头还在响**：角度超出行程，把 0~180 收窄到 5~175。

---

### 6.8 有源蜂鸣器

**接线**：VCC→5V、GND、I/O→PA2。

**代码**：[`buzzer.c`](../Core/Src/buzzer.c)
- 非阻塞：`Buzzer_Beep(30)` 只是打开 + 记时间，`Buzzer_Task()` 到点关
- 开机响两声，之后每次心跳响一下

**常见坑**：

- **不响**：模块是低电平触发，把 `BUZZER_ACTIVE_LOW` 改成 1；
- **一直响**：同上，极性反了；
- **2 脚裸蜂鸣器**：不能直连 GPIO，要加 NPN 三极管（见 `buzzer.c` 顶部注释）。

---

### 6.9 旋转编码器：切页

**接线**：GND、VCC→3.3V、A→PA5、B→PA6、C→GND。

**原理**：A/B 两相相差 90°，按相位先后判断正转/反转。一个机械档位 = 4 个计数。

**代码**：[`encoder.c`](../Core/Src/encoder.c)
- 用四状态表 `table[(prev<<2)|cur]` 解码，抖动和非法跳变都计 0
- `Encoder_GetDetents()` 返回档位（计数/4）

**验证**：转动旋钮，页面依次切换，到头自动回绕。

**常见坑**：

| 现象 | 处理 |
|---|---|
| 转一下跳两页 | 每档只有 2 个计数，把 `/4` 改成 `/2` |
| 方向反了 | A、B 两根线对调 |
| 没反应 | 量 PA5/PA6 静态电压应为 3.3V（内部上拉） |

> **如果你手上的其实是电位器**（转到两端会卡住）：改为 3.3V/GND 接两端、滑片接 PA4，用工程里的 `knob.c` 替代 `encoder.c`。

---

### 6.10 关于中文字库

界面文字来自 `Core/Src/font.c` 里的 `zh16x16`，格式是：

```c
const uint8_t zh16x16[][36] = {
    {0xE5, 0xBF, 0x83,          /* 4 字节 UTF-8 */
     0x00, 0x00, ... },          /* 32 字节 16×16 点阵 */
    ...
};
```

- 字模是**列行式、页优先**（`data[page*16 + col]`），bit0 在最上面；
- 数字和符号不需要生成，`font16x16.ascii = &afont16x8` 会自动兜底；
- 想加字：用取模工具（波特律动 LED 取模助手等）按上面的格式生成，替换数组即可，代码不用改。

---

## 7. 程序是怎么组织的

### 7.1 三层结构

```
main.c          ← 调度层：只按周期调用，不含业务逻辑
  ├── ui.c      ← 界面层：只画，不碰硬件
  │     ├── max30102.h
  │     ├── dht11.h
  │     ├── light.h
  │     ├── mpu6050.h
  │     └── speed.h
  ├── max30102.c ← 驱动层：只采数据，不碰屏幕
  ├── dht11.c
  ├── light.c  → adc1.c
  ├── mpu6050.c
  ├── speed.c
  ├── servo.c
  ├── buzzer.c
  └── encoder.c
```

三条规矩：

1. **驱动层不画屏**：只提供 `XXX_Init()` / `XXX_Task()` / `XXX_GetData()`；
2. **界面层不碰硬件**：只调用驱动暴露的数据结构；
3. **调度层不做业务**：只写 `if (时间到了) 调用XXX()`。

这样换屏幕、换传感器、加页面都互不影响。

### 7.2 主循环

```c
while (1)
{
    now = HAL_GetTick();

    if (now - lastPollTick  >= 10)   { MAX30102_Process(); }
    if (now - lastEnvTick   >= 1000) { DHT11_Task(); }
    if (now - lastLightTick >= 200)  { Light_Task(); }
    if (now - lastMpuTick   >= 100)  { MPU6050_Task(); }
    if (now - lastSpeedTick >= 100)  { Speed_Task(); }

    /* 旋钮切页 */
    d = Encoder_GetDetents();
    if (d != lastDetent) { ... UI_SetPage(...); }

    Buzzer_Task();
    if (MAX30102_TakeBeatEvent()) Buzzer_Beep(30);

    UI_Task();   /* 内部 40ms 节流 */
}
```

要点：**没有任何 `HAL_Delay` 出现在主循环里**，所有模块都是"到点执行、立即返回"，所以 25fps 刷屏和 100Hz 采样能共存。

---

## 8. 加一个新传感器

以加一个"土壤湿度传感器"（模拟量，接 PA4）为例：

**第 1 步：写驱动** `Core/Inc/soil.h` + `Core/Src/soil.c`

```c
/* soil.h */
#ifndef __SOIL_H__
#define __SOIL_H__
#include "main.h"

typedef struct
{
    uint16_t raw;      /* 0~4095 */
    uint8_t  percent;  /* 0~100 */
} Soil_Data;

void Soil_Init(void);
void Soil_Task(void);
const Soil_Data *Soil_GetData(void);
#endif
```

```c
/* soil.c */
#include "soil.h"
#include "adc1.h"

#define SOIL_ADC_CH 4   /* PA4 */

static Soil_Data soil;

void Soil_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin = GPIO_PIN_4;
    g.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &g);
    ADC1_Init();
}

void Soil_Task(void)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 8; i++) sum += ADC1_Read(SOIL_ADC_CH);
    soil.raw = sum / 8;
    soil.percent = (uint8_t)((4095 - soil.raw) * 100 / 4095);  /* 越湿电压越低 */
}

const Soil_Data *Soil_GetData(void) { return &soil; }
```

**第 2 步：加进构建**

`CMakeLists.txt` 的 `target_sources` 里加一行 `Core/Src/soil.c`。

**第 3 步：加进调度**

`main.c` 里 `#include "soil.h"`，初始化时 `Soil_Init();`，主循环里按周期调 `Soil_Task();`。

**第 4 步：加页面**

1. `ui.h` 的 `UI_Page` 里加 `UI_PAGE_SOIL`（放在 `UI_PAGE_COUNT` 之前）；
2. `ui.c` 里写 `UI_DrawSoil()`，画法参照 `UI_DrawLight()`；
3. `UI_Task()` 的 `switch` 里加一个 `case`；
4. 如果页面数变了，同步改 `knob.h` 的 `KNOB_PAGES`（用编码器则不涉及）。

**第 5 步：编译验证**

```bash
cmake --build build/debug/build
```

---

## 9. 故障排查手册

### 9.1 通用排查顺序

1. **供电**：万用表量模块 VCC 对 GND，是不是 3.3V/5V；
2. **共地**：所有 GND 是否连在一起（尤其外接电源时）；
3. **接错脚**：对照[第 5 节](#5-接线总表)的引脚表；
4. **接反线**：I2C 的 SCL/SDA、串口的 TX/RX 最容易反。

### 9.2 常见现象对照表

| 现象 | 最可能的原因 |
|---|---|
| OLED 全黑 | SCL/SDA 反了；模块地址 0x3D |
| `传感器未连接`（血氧页） | 接到 PB10/PB11 了，应该是 PB6/PB7 |
| 有波形但心率 `---` | 手指压太紧 / 没盖住窗口 |
| 波形是直线 | 红外 LED 不亮；固件会自动切红光通道 |
| 温湿度 `--` | 没上拉 / 不是 PB0 / 读取间隔太短 |
| 光照恒 0% 或 100% | 分压电阻没接 / 阻值不对 |
| 加速度页 `传感器未连接` | 看诊断页 `I2C` 和 `ID`：只有 57 → 没应答；有 68 但 ID 不认识 → 型号问题 |
| 转速恒 0 | 码盘没转；或 DO 恒低（模块"透光=0"的正常状态） |
| 旋钮没反应 | PA5/PA6 接错；或编码器每档 2 计数（改 /2） |
| 舵机乱抖 | 3.3V 供电 / 没共地 |
| 蜂鸣器不响 | 低电平触发模块，改 `BUZZER_ACTIVE_LOW` |

### 9.3 调试小技巧

- **屏幕是最好的调试器**：每个页面都留了原始值（R/IR、ADC、DO 电平、WHO_AM_I 等），出问题先看这些数；
- **拔线法**：怀疑某个模块干扰总线时，拔掉它看其他设备是否恢复；
- **先慢后快**：I2C 不稳就把 `ClockSpeed` 从 400000 降到 100000。

---

## 10. 源码地址与二次开发

**本工程开源地址：<https://github.com/apodxx/stm32-sensor-dec>**

- 遇到问题可以提 Issue，改好了欢迎 PR；
- 想基于它做自己的版本：`Fork` 之后改，或者克隆一份另开仓库（MIT 协议，注明出处即可）；
- `docs/` 下的教程和接线表可以随工程一起带走。

### 10.1 发布你自己的改造版

```bash
git init
git add .
git commit -m "feat: 基于 STM32 SensorDeck 的改造"
git branch -M main
git remote add origin https://github.com/<你的用户名>/<仓库名>.git
git push -u origin main
```

> 首次推送会要求登录。GitHub 已不支持账号密码，请用 **Personal Access Token**
> （Settings → Developer settings → Personal access tokens → Fine-grained tokens，
> 勾选 `Contents: Read and write`），推送时用户名填 GitHub 用户名、密码填 token。
>
> 或者装 GitHub CLI：`gh auth login` 之后用 `gh repo create --source=. --push`。

### 10.2 提交前检查

- [ ] `build/`、`Debug/`、`.vs/`、`.settings/` 没被提交（`.gitignore` 已处理）；
- [ ] 编译产物（.hex/.elf/.bin）没提交；
- [ ] 没有把个人路径写进代码；
- [ ] README 里的接线表和代码一致。

---

## 11. 还能做什么

- **加按键**：用 `UI_NextPage()` 做手动切页，或者长按进入设置页；
- **数据记录**：把心率/温湿度写进 W25Q64 或 SD 卡；
- **无线上报**：加 ESP-01S / NRF24L01，把数据发到手机或云端；
- **闭环控制**：用测速模块 + 舵机/电机做 PID 调速，这套代码里的 `speed` 已经是现成的反馈源；
- **姿态解算**：MPU6050 加上互补滤波或 Mahony，出 pitch/roll/yaw。

---

## 附：文件速查

| 文件 | 作用 |
|---|---|
| `Core/Src/main.c` | 初始化 + 主循环调度 |
| `Core/Src/ui.c` | 五个页面的绘制与切换 |
| `Core/Src/max30102.c` | 心率血氧驱动 + PPG 算法 |
| `Core/Src/dht11.c` | 单总线温湿度 |
| `Core/Src/light.c` | 光敏电阻采样 + 亮度换算 |
| `Core/Src/adc1.c` | ADC1 公共驱动 |
| `Core/Src/mpu6050.c` | 六轴驱动 |
| `Core/Src/speed.c` | 测速中断 + RPM 换算 |
| `Core/Src/servo.c` | 舵机 PWM |
| `Core/Src/buzzer.c` | 蜂鸣器 |
| `Core/Src/encoder.c` | 旋钮正交解码 |
| `Core/Src/knob.c` | 电位器版旋钮（备选） |
| `Core/Src/font.c` | ASCII + 中文 16×16 字库 |

祝玩得开心 🎉

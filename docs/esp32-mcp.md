# 接入小智 AI：用 ESP32-S3 语音控制 STM32

> 目标：对着 ESP32-S3 说「读一下血氧」「现在多少度」，小智把工具调用转发给 STM32，
> 拿到数据再用语音回答你；也能说「切到转速页」「响一声」。

---

## 1. 架构

```
你说话
  │
  ▼
ESP32-S3（小智固件）──WiFi──> 云端大模型
  │                              │
  │  MCP tool call（JSON-RPC）    │
  │<─────────────────────────────┘
  │
  │ UART 115200（文本命令）
  ▼
STM32 SensorDeck ──> 读传感器 / 切页 / 蜂鸣器
  │
  └── 回一行文本 ──> 小智 ──> TTS 播报
```

要点：**MCP 工具注册在设备端**（小智固件的 `McpServer::AddTool`），
工具的执行体就是"发一条串口命令、读回一行结果"。不需要自己写云端服务。

---

## 2. 接线

### 2.1 小智 AI 扩展板 GT-42P-V1.1（接屏幕 + 不用 4G）

这块板子上：

- CN1/CN2/CN3 是同一路 I2C，`SDA=GPIO41`、`SCL=GPIO42`，而这两个脚**同时接了 TFT 的 CS / BL**
  —— 接了屏就不能当串口；
- 42P 排针可能被其它模块占用；
- **4G 模块座 H3 空着时，GPIO11/12 是现成的空闲串口**，GND 也在同一排。

| H3 针脚 | 丝印 | 网络 | 接到 STM32 | 说明 |
|---|---|---|---|---|
| 1 | BAT | — | 不接 | 未连接 |
| 2 | EN | — | 不接 | 未连接 |
| 3 | RXD | **GPIO12** | **PA10（RX）** | ESP32 发 |
| 4 | TXD | **GPIO11** | **PA9（TX）** | ESP32 收 |
| 5 | GND | GND | **GND** | 必须共地 |
| 6 | 5V | 5V_IN | **不接** | 是 5V 电源脚，不是 3.3V |

> ⚠️ 前提：H3 上**没有焊 4G 模块（ML307R）**，并且编译的板型里**没有启用 4G**。
> 否则 GPIO11/12 会被 modem 占用。

这块板上**已被占用、要避开**的脚：

| 用途 | GPIO |
|---|---|
| I2S 功放 MAX98357 | 7、15、16 |
| I2S 麦克风 INMP441 | 4、5、6 |
| TFT 屏 | 21、40、41、42、45、47 |
| 按键 SW1/SW3/SW4 | 40、39、0 |
| 4G（如果焊了模块） | 11、12 |

### 2.2 通用接法（任意 ESP32-S3 板）

| ESP32-S3 | STM32F103 | 说明 |
|---|---|---|
| 任意空闲 GPIO（TX） | **PA10**（RX） | 交叉接 |
| 任意空闲 GPIO（RX） | **PA9**（TX） | 交叉接 |
| GND | GND | **必须共地** |
| 5V / 3.3V | 3.3V | 两边各自供电时不用接 |

- 两边都是 3.3V 电平，**不需要电平转换**；
- 串口选 **UART1 或 UART2**，别用 UART0（那是日志口）；
- 避开音频 I2S（GPIO4/5/6/7/15/16）、屏幕（GPIO21/40/41/42/45/47）、按键（GPIO0/39/40）。

---

## 3. STM32 侧协议（已实现）

`Core/Src/link.c`，USART1，115200 8N1，ASCII 文本，一行一条。

| 命令 | 回复示例 |
|---|---|
| `PING` | `PONG` |
| `GET ALL` | `HR=72 SpO2=0 Finger=1 Temp=26 Humi=58 Light=62 RPM=1234 Ax=0.12 Ay=-0.03 Az=1.00` |
| `GET HR` | `HR=72 SpO2=0 Finger=1` |
| `GET ENV` | `Temp=26 Humi=58` |
| `GET LIGHT` | `Light=62` |
| `GET IMU` | `Ax=0.12 Ay=-0.03 Az=1.00` |
| `GET RPM` | `RPM=1234` |
| `PAGE 3` | `OK Page=3` |
| `BEEP 200` | `OK` |
| 其它 | `ERR` |

**先用串口助手验一遍**：115200、发 `PING`，回 `PONG` 就说明 STM32 侧通了。

> 注意：`SpO2` 需要模块红外通道正常；红外坏了会一直是 0（心率仍可用，走红光通道）。

---

## 4. 小智固件侧

### 4.1 准备

1. 拉源码：`git clone https://github.com/78/xiaozhi-esp32.git`
2. 按官方文档装 ESP-IDF（v6.0.1+），`idf.py set-target esp32s3`
3. 选一个接近的板型（`idf.py menuconfig` → Xiaozhi Assistant → Board Type），先保证能正常对话

### 4.2 加一路 UART（示例）

在板子的初始化里（`main/boards/<your-board>/`）：

```cpp
#include "driver/uart.h"

static void Stm32UartInit()
{
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_1, 512, 0, 0, nullptr, 0);
    uart_param_config(UART_NUM_1, &cfg);
    uart_set_pin(UART_NUM_1, GPIO_NUM_17, GPIO_NUM_18,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}
```

### 4.3 封装"发命令 + 收一行"

```cpp
#include "esp_timer.h"

static std::string Stm32Cmd(const std::string& cmd, int timeout_ms = 300)
{
    uart_write_bytes(UART_NUM_1, cmd.c_str(), cmd.size());
    uart_write_bytes(UART_NUM_1, "\n", 1);

    std::string resp;
    uint8_t ch;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    while (esp_timer_get_time() < deadline) {
        if (uart_read_bytes(UART_NUM_1, &ch, 1, 10) == 1) {
            if (ch == '\n') break;
            if (ch != '\r') resp.push_back((char)ch);
        }
    }
    return resp;
}
```

### 4.4 注册 MCP 工具

```cpp
// 在 Board 初始化、mcp_server_ 可用之后
auto& mcp = *mcp_server_;

mcp.AddTool("stm32.get_vitals", "读取当前心率和血氧（需要手指放在传感器上）",
    PropertyList(), [](const PropertyList&) -> ReturnValue {
        return Stm32Cmd("GET HR");
    });

mcp.AddTool("stm32.get_env", "读取环境温度和湿度",
    PropertyList(), [](const PropertyList&) -> ReturnValue {
        return Stm32Cmd("GET ENV");
    });

mcp.AddTool("stm32.get_light", "读取当前光照强度百分比",
    PropertyList(), [](const PropertyList&) -> ReturnValue {
        return Stm32Cmd("GET LIGHT");
    });

mcp.AddTool("stm32.get_imu", "读取三轴加速度（单位 g）",
    PropertyList(), [](const PropertyList&) -> ReturnValue {
        return Stm32Cmd("GET IMU");
    });

mcp.AddTool("stm32.get_rpm", "读取电机转速（RPM）",
    PropertyList(), [](const PropertyList&) -> ReturnValue {
        return Stm32Cmd("GET RPM");
    });

mcp.AddTool("stm32.get_all", "一次读取所有传感器数据",
    PropertyList(), [](const PropertyList&) -> ReturnValue {
        return Stm32Cmd("GET ALL");
    });

mcp.AddTool("stm32.set_page", "切换屏幕页面",
    PropertyList({
        Property("page", kPropertyTypeInteger, 0, 4),
    }),
    [](const PropertyList& p) -> ReturnValue {
        return Stm32Cmd("PAGE " + std::to_string(p["page"].value<int>()));
    });

mcp.AddTool("stm32.beep", "让设备响一声",
    PropertyList({
        Property("ms", kPropertyTypeInteger, 50, 1000),
    }),
    [](const PropertyList& p) -> ReturnValue {
        return Stm32Cmd("BEEP " + std::to_string(p["ms"].value<int>()));
    });
```

> ⚠️ `AddTool` 的参数类型和 `PropertyList` 的写法随固件版本略有差异，
> 以你拉下来的 `main/mcp_server.h` 为准；名字和描述要与上面的保持一致，
> 大模型是靠**描述**来决定调哪个工具的。

### 4.5 编译烧录

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

---

## 5. 另一种方案：不改固件

小智支持把**外部 MCP 服务端**挂在云端（控制台里配置接入点），这样：

- ESP32-S3 保持官方固件不动；
- 在一台常开的电脑/树莓派上跑一个 MCP 服务（Python），通过 USB 串口连 STM32；
- 云端把工具调用转到你的服务上。

优点：不用碰 ESP-IDF；缺点：多一台常开的设备。
适合"先验证想法"，验证完再固件化。

---

## 6. 调试顺序（别跳步）

1. **串口助手**：115200 发 `PING`，回 `PONG` → STM32 侧 OK；
2. **ESP32 单独测试**：写个临时任务，`Stm32Cmd("GET ALL")` 打印到日志 → 串口链路 OK；
3. **只注册 1 个工具**（`stm32.get_env`），语音问"现在多少度" → MCP 链路 OK；
4. 再一次性把其它工具加上。

---

## 7. 常见坑

| 现象 | 原因 |
|---|---|
| 回复乱码 | 波特率不一致；或没共地 |
| 完全没回复 | TX/RX 没交叉；或 STM32 没上电 |
| 能收但发不出 | ESP32 的 TX 接到了 STM32 的 TX（都接成输出了） |
| 工具不被调用 | 工具描述太模糊，或名字太长；改成人能看懂的短描述 |
| 调用超时 | `Stm32Cmd` 的 timeout 太短；STM32 侧别在命令里做长操作 |
| 语音回答不对 | 工具返回的是"原始文本"，可以再包一层自然语言，例如 `"当前温度 26 度，湿度 58%"` |

---

## 8. 可以做的语音指令

- "现在心率多少？" → `stm32.get_vitals`
- "房间里多少度？" → `stm32.get_env`
- "灯够不够亮？" → `stm32.get_light`
- "板子放平了吗？" → `stm32.get_imu`
- "电机转多快？" → `stm32.get_rpm`
- "切到转速页" → `stm32.set_page`
- "响一声" → `stm32.beep`
- "把所有数据念一遍" → `stm32.get_all`

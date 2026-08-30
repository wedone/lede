# Breed 使用说明文档

> 本文档整理自恩山无线论坛 hackpascal 的 Breed 发布帖（第 4–6 楼），并包含 `breed-ar7161-blank.bin` 的专项说明。

---

## 一、AR7161 Blank 版本专项说明

### 1.1 文件信息

| 文件名 | 说明 |
| :--- | :--- |
| `breed-ar7161-blank.bin` | AR7161 专用 Blank 版，支持 **AR8035 / IP1001 / MV88E1116 / BCM5481** 千兆 PHY |

### 1.2 版本特性

- **无预置 GPIO 配置**：该版本不包含任何 GPIO（复位键等）配置，首次刷入时**无法通过按复位键中断启动**进入 Web 刷机模式。
- **自定义复位键**：刷入后需通过 **BreedEnter 工具** 或 **TTL 串口** 中断启动，进入 Breed 后开启环境变量支持，再通过环境变量 `gpio.customized.reset` 自行设定有效的复位键。
- **不支持超频**：此类 Blank 版**不支持自定义超频**功能。
- **维护状态**：属于“不再维护的 CPU”才会推出的 Blank 版；正常维护的其它 CPU 依然提供专用版和固定复位键的版本。

### 1.3 刷入后初始化流程

1. 通过 **BreedEnter**（需电脑安装 Npcap）或 **TTL 串口** 在启动时中断进入 Breed。
2. 在 Breed Web 恢复控制台中**开启环境变量支持**（若可用）。
3. 设置环境变量 `gpio.customized.reset`（例如 `11L` 表示 GPIO#11、低电平有效），并保存。
4. 此后即可使用设定的按钮进入 Web 刷机模式。

---

## 二、Breed 命令控制台说明（4楼）

Breed 内置了命令解释器，支持通过 **TTL 串口** 或 **Telnet** 进入命令控制台进行操作。

### 2.1 进入命令控制台的方法

| 方式 | 操作步骤 |
| :--- | :--- |
| **TTL 串口** | 在启动提示 `Press any key to interrupt autoboot ...` 时按任意键中断启动 |
| **Telnet** | 通过按复位键或 BreedEnter 中断启动后，在 CMD 中执行 `telnet 192.168.1.1`（IP 以实际为准） |
| **客户端建议** | 请使用 Windows 自带 Telnet 客户端或 PuTTY；Linux 下部分 Telnet 客户端兼容性不佳 |

### 2.2 控制台功能特性

- VT100 控制台兼容
- 支持 **10 条历史命令**（上下键调出）
- 每条命令最长支持 **1024 个字符**
- 支持左右键、Home/End 键移动光标，可插入和删除字符

### 2.3 内置命令详解

#### 系统与网络

| 命令 | 语法 | 说明 |
| :--- | :--- | :--- |
| `abstatus` | `abstatus` | 显示自动启动被中断的原因 |
| `arp` | `arp` | 显示当前 ARP 表 |
| `dhcp` | `dhcp [enable\|disable]` | 显示当前 DHCP 租约；启用/禁用 DHCP 服务（**重启后恢复默认**） |
| `net` | `net [start\|stop]` | 启动或停止网络服务，并关闭所有网络连接 |
| `netstat` | `netstat` | 显示当前活动的网络连接 |
| `reset` | `reset` | 重启路由 |
| `setbrg` | `setbrg <val>` | 设置 Breed 串口输出的波特率，**重启后依然生效** |
| `sysinfo` | `sysinfo` | 显示系统信息 |
| `thread` | `thread` | 列出所有线程信息 |
| `wget` | `wget [addr] <url>` | 通过 HTTP 协议加载文件到内存（URL 里只能使用 IP 地址） |

#### 启动相关

| 命令 | 语法 | 说明 |
| :--- | :--- | :--- |
| `boot` | `boot mem <addr>` | 从内存地址启动固件 |
| | `boot flash [bank <n>] <addr>` | 从 Flash 启动固件（`bank` 可选，默认 0） |
| | `boot linux <addr>` | 将地址视作 Linux 内核入口地址并启动 |
| | `boot raw <addr>` | 禁用中断，并从地址处直接执行 |
| | `boot breed <addr>` | 将地址视作上传的 Breed bin 文件地址并直接启动 |

#### 环境变量

| 命令 | 语法 | 说明 |
| :--- | :--- | :--- |
| `env` | `env list` | 列出所有环境变量，显示总空间和空闲空间 |
| | `env get <key>` | 获取指定环境变量的值 |
| | `env set <key> <value>` | 设置/新建环境变量 |
| | `env unset <key>` | 删除指定环境变量 |
| | `env clear` | 清除所有环境变量 |
| | `env save` | **保存**环境变量（修改后必须执行） |
| `envconf` | `envconf disable` | 禁用环境变量功能 |
| | `envconf <addr> <size>` | 启用环境变量，指定 Flash 起始地址和大小（不得小于 `0x100`） |
| | | **注意**：部分型号（如 NAND 启动版、WDR6500v2）使用固定环境变量设置，此命令不可用 |

#### Flash 操作

| 命令 | 语法 | 说明 |
| :--- | :--- | :--- |
| `flash` | `flash list` | 列出所有 Flash |
| | `flash [bank <n>] info` | 显示 Flash 详细信息 |
| | `flash [bank <n>] dump <addr> <size>` | 显示 Flash 内的数据 |
| | `flash [bank <n>] read <addr> <dst> <size>` | 将 Flash 数据读取到内存 |
| | `flash [bank <n>] erase <addr> <size>` | 擦除 Flash |
| | `flash [bank <n>] write <addr> <src> <size>` | 将内存数据写入 Flash |

#### GPIO 操作

| 命令 | 语法 | 说明 |
| :--- | :--- | :--- |
| `gpio` | `gpio [status\|list]` | 列出所有 GPIO 及其状态 |
| | `gpio button` | 显示当前路由上按钮的状态 |
| | `gpio led` | 显示当前路由上部分 LED 的状态 |
| | `gpio get <n>` | 获取指定 GPIO 的电平状态 |
| | `gpio set <n> <hi\|lo>` | 设置指定 GPIO 的电平 |
| | `gpio dir set <n> <in\|out>` | 设置 GPIO 方向 |
| | `gpio led set <name> <on\|off>` | 设置 LED 亮灭 |

#### 内存与调试

| 命令 | 语法 | 说明 |
| :--- | :--- | :--- |
| `mem` | `mem dump [keep] <start_addr> [size]` | 显示内存数据；`keep` 保持原始字节序 |
| | `mem crc32 <addr> <size>` | 计算 CRC32 校验 |
| | `mem read [byte\|short\|long] <addr>` | 读取内存数值（1/2/4 字节） |
| | `mem write [byte\|short\|long] <addr> <value>` | 写入内存数值 |
| | `mem write str <addr> <str>` | 将字符串写入内存（支持 C 语言编码） |
| | `mem copy <dst> <src> <size>` | 复制内存块 |
| | `mem fill <dst> <val> <size>` | 填充内存块 |
| | `mem compare <addr1> <addr2> <size>` | 比较内存块 |
| `mdio` | `mdio list` | 列出网络接口设备 |
| | `mdio <dev> dump <phy>` | 转储 MII 寄存器（0~31） |
| | `mdio <dev> read <phy> <reg>` | 读取 MII 寄存器 |
| | `mdio <dev> write <phy> <reg> <val>` | 修改 MII 寄存器 |
| `spi` | `spi list` | 列出所有 SPI 设备 |
| | `spi [dev <slave>] <op> [<op> ...]` | 执行 SPI 操作序列（`start`/`stop`/`read`/`write`/`speed`） |

#### 其它

| 命令 | 语法 | 说明 |
| :--- | :--- | :--- |
| `btntst` | `btntst` | 测试 GPIO 按钮状态 |
| | `btntst enable <n>` | 启用对 GPIO#n 的状态检测 |
| | `btntst disable <n>` | 禁用对 GPIO#n 的状态检测 |
| | `btntst restore` | 恢复默认检测设置 |
| `exit` | `exit` | 退出 Telnet 模式 |
| `help` | `help` | 列出所有可用的命令 |

---

## 三、TTL 刷机教程（4楼）

> 适用于通过 Breed 命令控制台刷写固件或更新 Bootloader。

### 3.1 准备工作

参考《U-Boot 刷机方法大全》，在电脑上使用 **HFS** 搭建本地 HTTP 文件服务器。

### 3.2 刷机步骤

#### 第 1 步：传输文件到内存

在 Breed 控制台执行：

```text
wget http://<电脑IP地址>/<文件名>
```

记录输出中的以下两项：
- **Saving to address**：内存地址（如 `0x80000000`）
- **Length**：数据大小（如 `0x800000`）

#### 第 2 步：擦除 Flash

根据实际情况确定擦除起始地址和大小，执行：

```text
flash erase <起始地址> <擦除大小>
```

#### 第 3 步：写入数据

将内存中的数据写入 Flash：

```text
flash write <Flash地址> <内存地址> <数据大小>
```

- `<内存地址>`：第 1 步记录的 `Saving to address`
- `<数据大小>`：第 1 步记录的 `Length`

---

## 四、复位键测试说明（5楼）

若不确定路由器的复位键对应哪个 GPIO，可通过以下步骤测试。

### 4.1 测试方法

1. 通过上述方法进入 **Breed 命令控制台**。
2. 执行命令：

```text
btntst
```

3. 按下或松开路由器上的按钮，控制台会实时输出 GPIO 状态变化：

```text
GPIO#<n> <dts定义> <状态>
```

- **第一列**：`GPIO#` 后面的数字即为当前按钮的 GPIO 号。
- **第二列**：用于 OpenWrt dts 文件中的 GPIO 定义。
- **第三列**：GPIO 状态。

### 4.2 判断电平类型

| 按下按钮 | 松开按钮 | 类型 |
| :--- | :--- | :--- |
| 状态为 `0` | 状态为 `1` | **低电平有效**（active-low） |
| 状态为 `1` | 状态为 `0` | **高电平有效**（active-high） |

### 4.3 处理持续误触发

若执行 `btntst` 后某 GPIO 持续输出电平变化，可先禁用该 GPIO 的检测：

```text
btntst disable <n>
```

测试完成后再恢复：

```text
btntst enable <n>
```

或一次性恢复所有默认设置：

```text
btntst restore
```

---

## 五、环境变量与自定义复位键说明（6楼）

### 5.1 环境变量支持判断

- **部分型号固定设置**：NAND 启动的 Breed、部分专用版 Breed 使用**固定环境变量**，没有设置页面和 `envconf` 命令。
- **WDR6500v2**：因文件体积限制，不支持环境变量。
- **判断方法**：观察 Breed Web 恢复控制台中是否存在 **“环境变量编辑”** 页面；或在命令行执行 `env` 命令，若存在即支持。

### 5.2 环境变量设置方式

1. **Web 页面**：在 Breed Web 恢复控制台中进入“环境变量编辑”页面。
2. **命令行**：使用 `env set <key> <value>` 设置后，必须执行 `env save` 保存。

### 5.3 可用环境变量列表

| 变量名 | 说明 | 示例 |
| :--- | :--- | :--- |
| `network.ipaddr` | Breed 的 IP 地址 | `env set network.ipaddr 192.168.1.1` |
| `network.netmask` | 子网掩码 | `env set network.netmask 255.255.255.0` |
| `network.dhcpd.disabled` | 禁用 DHCP 服务器（`1` 禁用，其它启用） | `env set network.dhcpd.disabled 1` |
| `network.autoneg.timeout` | 以太网 PHY 自动协商等待时间（`0`–`10`，`0` 不等待） | `env set network.autoneg.timeout 5` |
| `sys.led_blink.disabled` | 禁用 SYS LED Heartbeat 闪烁（`1` 禁用） | `env set sys.led_blink.disabled 1` |
| `autoboot.disabled` | 禁用自动启动（`1` 禁用，直接进入 Breed） | `env set autoboot.disabled 1` |
| `autoboot.delay` | 自动启动等待时间（必须大于 0） | `env set autoboot.delay 5` |
| `autoboot.command` | 自动启动命令（多个命令用半角分号 `;` 分隔） | `env set autoboot.command "boot mem 0x9f020000"` |
| `linux.cmdline` | Linux 内核命令行 | `env set linux.cmdline "console=ttyS0,115200 ..."` |
| `linux.initrd.start` | initrd 起始地址 | `env set linux.initrd.start 0x80400000` |
| `linux.initrd.size` | initrd 大小 | `env set linux.initrd.size 0x500000` |
| `gpio.customized.reset` | **自定义复位键**（仅对 Blank 版有效） | `env set gpio.customized.reset 11L` |

> **格式说明**：`gpio.customized.reset` 的数据格式为 `数字+活动状态`。数字为 GPIO 编号；活动状态为 `L`（低电平有效，active-low）或 `H`（高电平有效，active-high），不区分大小写。

### 5.4 恢复默认设置

若要恢复某项为默认值，直接删除该变量即可：

```text
env unset <key>
env save
```

### 5.5 自定义复位键完整流程（适用于 Blank 版，含 AR7161 Blank）

> 仅对文件名以 `-blank` 结尾的版本有效（如 `breed-ar7161-blank.bin`）。

**限制说明**：
- 首次刷入时**不能通过按复位键中断启动**。
- **不支持自定义超频**。

**设置步骤**：

1. 通过 **BreedEnter** 中断启动，或通过 **TTL 串口** 中断启动。
2. 在 Breed Web 控制台中**开启环境变量支持**（若需手动设置 `envconf`）。
3. 设置环境变量 `gpio.customized.reset`，例如：

```text
env set gpio.customized.reset 11L
env save
```

4. 重启后，即可使用设定的按钮进入 Web 刷机模式。


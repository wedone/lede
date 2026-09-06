# AP2600IFM — 瘦AP固件方向性需求清单（供 ar71xx 分支实施）

> 应用对象是 **ar71xx(4.14) 分支**（`ap2600-ar71xx`）。本文档为**方向性**描述：给出优化目标与决策原则，具体包名/实现由实施 agent 依据其所处分支的实际包体系（与 ap2600/ath79 6.6 时代不同）自行决定。
> 仅「B1 中文支持」「B2 主题 design」为例外——这两项为**准确需求**，需按文档精确落地。

***

## A. 瘦AP优化方向（目标 + 决策原则）

**总体目标**：AP2600IFM 是纯瘦 AP（单千兆口、无 WAN/NAT/USB 硬件/无交换机），固件应只保留无线 AP 所需组件，剔除路由/上网/存储类负担，减小体积、内存与攻击面。

### A1. 从固件中剔除的设备无关组件（示例，非穷尽）

方向：按「该功能在瘦 AP 场景是否使用」逐项判定。参考排除类别（本设备均不需要）：

- **USB 相关**（kmod-usb\* 等）——板子无 USB 硬件

- **WAN/拨号**：ppp/pppoe/proto-ppp、pptp 等拨号类

- **NAT/防火墙**：防火墙主程序、IPv6 NAT、UPnP/miniupnpd、NAT helper、raw 表等 —— 瘦 AP 桥接不转发路由，无外网/NAT

- **下载/服务器类**：vsftpd、vlmcsd、ddns、ssr-plus、wol、etherwake 等

- **监控/统计类**：nlbwmon、accesscontrol、autoreboot、filetransfer、arpbind、turboacc 等

- **基准测试**：curl、coremark 等

决策原则：

1. 不盲抄 ap2600 的包表——**目标分支没有的包跳过**；**目标分支特有的同类包按同一方向原则处理**（例：19.07 的某些 luci-app-\* 路由组件与 6.6 命名不同）。
2. 保留底线（AP 必需）：dnsmasq(LAN DHCP/DNS)、无线主机(hostapd/wpad full)、IPv6 基础设施(odhcpd/odhcp6c)、web 服务(uhttpd+LuCI)、基础工具。**IPv6 与 LAN DHCP 必须保留**。
3. 剔除在设备级做（不影响同分支其他设备）。

### A2. 网络形态：无 WAN

- 物理上只有 eth0，netifd 配置**不得存在 wan/wan6 接口**。

- 方向：在**源头**（板级网络定义）只声明 lan，而不是启动后删除——避免升级/恢复固件时残留空 WAN 接口。

- 实现方式按目标分支机制：4.14/ar71xx 用 mach 文件 + 板级 network 配置（无 board.d/DTS），具体落在哪由实施 agent 决定。

- LAN 默认 IP：**192.168.3.1/24**（避开光猫 192.168.1.1）；LAN MAC 需稳定固定（ar71xx 从 nvram/board 读，勿随机）。

### A3. 首次启动默认配置（firstboot 生效，不覆盖用户已存配置）

- 默认主机名：**AP2600IFM**

- 默认时区/语言：见 B1

- 默认开启无线漫游 **802.11k / 802.11v(bss\_transition) / 802.11r(FT)**：在板级 firstboot 脚本对全部 radio 写 `ieee80211k / bss_transition / ieee80211r`（11r 用 PSK 本地生成时含 ft\_psk\_generate\_local 类参数）。

- 前提：无线后端必须是**支持 11k/11v/11r 的完整 hostapd**（按目标分支的 wpad/hostapd 变体选择，4.14 时代可能无 mbedtls 后缀，选功能最全且含 11k/11v 的即可）；hostapd 编译需含 `CONFIG_IEEE80211K=y`、`CONFIG_WNM=y`。

### A4. 引导与稳定性（ar71xx 分支已部分完成，回归验证）

- 引导：Breed（ATH-SDK-16MB 布局意识），标准 uImage，kernel 入口 0x80060000。

- 回归项：MAC 固定、救援/急救模式、WIFI 灯行为等——这些在该分支已有实现（见该分支 git log），方向是**对齐且不回归**，无需在本文档展开。

***

## B. 准确需求

### B1. 增加中文支持（必做，精确要求）

- LuCI 界面**默认语言为 zh\_cn**，用户登录即中文。

- 实施方式：

  1. 按目标分支的 LuCI feed，加入**中文翻译包**并把其装入固件（6.6 时代为 `luci-i18n-zh-cn` 或各模块 `-zh-cn`；4.14 老 LuCI 的翻译包命名以该分支 feed 为准，选覆盖所有已装 LuCI 模块的翻译组合）。
  2. firstboot 设置：`uci set luci.main.lang='zh_cn'`（或等价机制）并 commit。

- 验收：首次登录即中文界面，无英文混合残留。

### B2. 主题仅保留 luci-theme-design（必做，精确要求）

- 主题来源：**<https://github.com/0x676e67/luci-theme-design>**

- 要求：固件安装**仅** design 一个用户主题（现有其他 luci-theme-\*\*\* 如 argon 等一律从固件剔除，不再安装）。

- 实施方式：

  1. 集成该主题源码到目标分支可构建的位置（6.6 时代放 `package/luci-theme-design/`），产出 `luci-theme-design` 包。
  2. 固件包列表只含 design（剔除所有其他 luci-theme-\*）。
  3. firstboot 设置默认主题：`uci set luci.main.mediaurlbase='/luci-static/design'`（或该主题在 4.14 老 LuCI 下的等价设置）并 commit。
  4. **bootstrap 特判**：bootstrap 是 LuCI 系统兜底主题。若目标分支可以安全移除（不影响无主题时的界面渲染），则移除；否则保留 bootstrap 仅作系统兜底，但**用户可见默认与唯一可选主题必须是 design**。实施 agent 据实际行为决定并注明。

- **兼容性必核**：0x676e67 的 design 主题对 LuCI 版本有要求；4.14 老版 LuCI（19.07）若渲染异常，需选用该主题仓库中兼容老 LuCI 的分支/版本，不得降级为"换主题名但用别的主题"。

- 验收：`opkg list-installed` 中 luci-theme-\* 仅 design（允许 bootstrap 兜底）；登录界面为 design 风格；argon 及其他主题不存在。

***

## C. 实施顺序与验收

1. 先落地 B1、B2（影响面小、需求明确）。
2. 再按 A 部分逐维度做瘦AP精简（A1 清单判定 → A2 无 WAN → A3 默认配置），每次只依赖该分支实际存在的包/机制。
3. 最终回归：LEDE 常见回归点（启动、DHCP、无线 UP、漫游参数生效、IP 192.168.3.1、无 wan 残留、hostname=AP2600IFM、中文、design 主题）。
4. 交付：可刷固件 + 本需求逐项完成情况说明（含因分支缺失而跳过的包项清单，便于审计）。


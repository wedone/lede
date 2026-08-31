# MASELinK AP2600IFM → OpenWrt ath79 迁移开发文档 v9

> 本文档面向具备 OpenWrt 内核开发经验的专业开发者/AI，梳理需求、硬件事实、参考资源及迁移路径。代码以伪代码/关键片段呈现，完整实现需开发者补充。
>
> **仓库选择**：本文档基于当前 **`d:\vc\lede`（coolsnowwolf/lede）** 仓库撰写，已通过实测/在线资料核实重要事实，并在各修订版中标注。
>
> **修订要点（v10，2026-08-31）**：
>
> 1. **确认官方 8 分区可用** **`fis write`** **/ 编程器整写回官方固件做硬件验证**：解析 `ap2600/FIS_directorymtd5.bin`（RedBoot FIS directory）得到官方 8 分区精确 flash 基址，与论坛教程（`openwrtap2600split` 目录）的 `fis write` 命令逐一核对**完全一致**（mtd1\@0xbf040000/0x120000、mtd2\@0xbf160000/0xe00000、mtd3\@0xbff60000/0x80000、mtd4\@0xbffe0000/0x10000、mtd56\@0xbfff0000/0x10000）；
> 2. **验证** **`fisconfigmtd7.bin`** **= FIS 配置区整块备份（0x10000）**，内容 = `FIS_directorymtd5`(0xf000) + `redbootconfigmtd6`(0x1000)（逐字节相等），与论坛 `mtd56.bin` 同源；证实 `ap2600/` 与 `openwrtap2600split/` 两个目录同源（仅 mtd5/mtd6 拆开 vs mtd56 合并）；
> 3. **已拼装官方 16MB 编程器镜像**：`build_official_image.py` 按 FIS 偏移拼装 7 片（RedBoot\@0x000000 + vmlinux\@0x040000 + rootfs\@0x160000 + config\@0xf60000 + board-config\@0xfe0000 + FIS目录@0xff0000 + RedBootConfig\@0xfff000），成品 `AP2600IFM_官方8分区_编程器固件.bin`（md5=c8eb2aa4dce43a3149411be27c44ca2b），一步回官方固件验证 5G 硬件；
> 4. 新增 **§十二“用官方 8 分区写回验证 5G 硬件”**：给两条落地路径（CH341A 编程器整写 / 先回 RedBoot 再 `fis write` 分片），以及“原厂下 5G 是否正常”的判定逻辑与回退保护。
>
> **修订要点（v9，2026-08-31）**：
>
> 1. **新增 §十一“5G 卡供电/复位排查清单”**，作为作者回家后对 5G 卡（AR9220，`028c:0029`）芯片唤醒失败（`Couldn't reset chip`）的**实测操作手册**：覆盖卡座供电/RESET# 对比测量、板级 LDO 使能脚定位、串口软件先行验证、结果对照表及三种修复路径（补 GPIO 上电 / 补 RESET 时序 / 换卡），全部以**实际设备可观测点**为依据，不臆测 GPIO 号；
> 2. 明确当前**已确认事实边界**：5G 卡 PCI 能被枚举、配置空间可读、`pci_enable_device` 成功（`enabling device`）→ **3.3V 主供电与 PCIe 链路大概率正常**，真正的怀疑点收敛到 **RESET# 时序 / 辅助偏压或 LDO 使能**，据此把测量优先级做了调整；
> 3. 记录三条候选修复路径，供实测数据出来后直接落地（DTS `<&gpio 复位>` / ga 提前拉高 / 软件翻转），避免回家后临时拍脑袋改代码。

> **修订要点（v4，2026-08-29）**：
>
> 1. 以 `mach-maselink-ap2600ifm.c` 为硬件权威，核对 PHY/MAC/LED/按键/PLL，全部吻合；
> 2. 核实官方 `ar7161_aruba_ap-175.dts` 与 `generic.mk` 真实定义，修正分区、镜像大小、loader 机制；
> 3. **MAC 地址布局由所选的 bootloader 决定**（保留 RedBoot / 更换 U-Boot 两条路线）；原"board-config\@0x00"方案废弃，改为对齐官方 AP-175 的 `hwinfo@0x1c`；
> 4. 核实 AR9220 / AR9223 的 PCI Device ID 均为 `0x0029`，`pci168c,0029` 通吃；
> 5. 修正第 4 节失效链接（openwrt.org/toh/aruba/ap-175 已 404）；
> 6. 评审确认当前 lede 具备 OKLI loader 编译能力，可承载该移植。
>
> **修订要点（v5，2026-08-30）**：
>
> 1. 实机 sysfs 确认无线卡身份：2.4G 卡（00:11.0）= **AR9223**，`168c:0029` / 子系统 `168c:2091`（同 Unex DNMA-91，单 2.4G）；5G 卡（00:12.0）= **AR9220**，`028c:0029` / 子系统 `028c:2096`（同 WLM200NX/DNMA-92，双频卡工作于 5G）；**修正 v4 中"2.4G=AR9220 / 5G=AR9223"的假定（原为反向）**；
> 2. 5G 卡 **vendor ID 被 OEM 改写为未注册的** **`0x028c`**（pci.ids 无此厂商），导致 ath9k 默认不识别（未绑定驱动、IRQ 0）——已确认根因；最终修复：在补丁 `513-ath9k_add_pci_ids.patch` 的 `ath_pci_id_table` 表尾新增 `{ PCI_DEVICE_SUB(0x028c, 0x0029, 0x028c, 0x2096), .driver_data = ATH9K_PCI_LED_ACT_HI }`（仿官方 PCOEM 下 WLM200NX 条目 `168c:0029/168c:2096`，仅 vendor 换为实机 `0x028c`），已同步到 `patches-6.18`；仅改 `pci.c`，**不动** `hw.c/hw.h`（经与 backports 6.6.15 实际源码核对：`hw.h` 已定义 `AR9280_DEVID_PCI=0x0029`，`hw.c` 的 `ath9k_hw_init` switch 已覆盖该值，无需新增 case）；**早期曾误按"devid=0xabcd"假设添加** **`AR9300_DEVID_INVALID`** **条目，评审确认该假设错误、纯属臆测，已删除**；两个补丁均已用 `git apply --check` 对 backports 6.6.15 / 6.18.7 实际源码验证可干净应用；
> 3. OF 把 PCI 设备按 **devfn（reg 属性）** 匹配到 DTS 节点、不比对 compatible，故两节点保持 `pci168c,0029` 即可，不受 vendor 改写影响。
>
> **修订要点（v6，2026-08-30）**：
>
> 1. **PHY 地址实测推翻 mach 的 @20**：Breed 下 mdio 实测确认 **IP1001 实际挂在 MDIO 地址 1**（`read 1 2 -> 0243`、`read 1 3 -> 0d91`，PHY ID `0x02430d91`），地址 20 无响应；与官方 AP-175 板值 @1 一致（见 DTS `ar7161_maselink_ap2600ifm.dts` 注释，实测日期 2026-08-30，详见 §2.4）；
> 2. **分区改为 Breed ATH-SDK-16MB 布局**：`u-boot` 0x0-0x50000 / `firmware` 0x50000-0xf90000 / `hwinfo@fe0000` / `u-boot-env@ff0000`，已落地 DTS，generic.mk `IMAGE_SIZE=15872k`（§3.3/§5.1/§5.2）；
> 3. **弃用 OKLI loader 路线**：改由 Breed 直接引导标准 uImage（magic `0x27051956`，load `0x80060000`），不再需要 `LOADER_FLASH_OFFS`/`CONFIG_FLASH_OFFS`（§5.2/§6）；早前 0x42000/0x52000 的 OKLI 偏移调试图纸废弃；
> 4. **Breed 已实际烧录并启动**：`breed-ar7161-blank.bin` 写入 mtd0（原 RedBoot 分区，flash 0x0），md5sum 校验通过；Breed 初始化识别 64MB DRAM / AR7161 / S25FL128P，网络 192.168.1.1（§4.3/§7）；
> 5. 参考设计更正为 **Atheros PB42**（RedBoot 曾误报 pb44，kernel 以 `board=pb42` 为准）；
> 6. 待确认事项收敛：PHY 地址、无线卡型号、bootloader 路线均已实测确认；当前唯一未决为 **5G 卡（AR9220，vendor** **`0x028c`）在补丁** **`513-ath9k_add_pci_ids.patch`** **编译后的驱动绑定验证**（§9）。
>
> **修订要点（v8，2026-08-31）**：
>
> 1. **更正 v7"京信官方 OpenWrt 就是原厂"的误判**：用户确认 **Comba 官方从未发布过 OpenWrt**。E 盘 `京信2600IFM-OPENWRT-2015.06.19\` 及 `ap2600\` 各分区备份、`openwrtap2600split\` 均为**社区固件**（mach `mach-maselink-ap2600ifm.c` 出自 hackpascal/LEDE 社区，与根文件系统 `etc/hotplug.d/net/10-ar922x-led-fix`、`01_leds` 的构建时间戳 2015-06-19 吻合），**非 Comba 原厂系统**；
> 2. **当前状态澄清**：用户记忆"官方下 5G 正常"指 **Comba 原厂商用系统**，其完整镜像尚未找到备份；现有留存（社区 OpenWrt 全分区 + 社区 OpenWrt 内核）在任何变体下 **5G 卡均未正常驱动**（官方分区日志仅 phy0；我们 6.6 固件 12.0 可枚举但 RTC 唤醒失败）；
> 3. **修复方向**：此前的"补 5G 卡上电 GPIO"仍是最可能路径，但依据从"反汇编社区内核"转向"**联网检索 Comba 原厂固件/驱动/GPIO 依据** + **实物万用表测量**。作者不在设备旁，下一步等实物测量与官方资料检索。

***

> **修订要点（v7，2026-08-31）**：
>
> 1. **推翻"5G 卡芯片无法唤醒 = 硬件问题"的早期定论**：该结论基于当时"所有 OpenWrt 变体（含旧固件）下 5G 从未工作"的观测，但**遗漏了"Comba 原厂系统下 5G 正常"这一用户实据**。现将早期"判定硬件问题"修正为"**软硬件原因未定，缺供电/复位初始化，倾向原厂固件持有一套 OpenWrt 缺失的 5G 卡 (AR9220) 上电 GPIO 时序**"（§7 Phase1-3、§8、§9-1）；
> 2. **关键事实（用户补充，2026-08-31）**：用户称"官方固件下 5G 正常"指的是 **Comba 原厂商用系统**（非 RedBoot+OpenWrt）；但**硬盘中留存的 16MB 编程器固件备份均为 RedBoot+OpenWrt**，**Comba 原厂系统完整镜像未备份**，无法离线反汇编提取其 5G 卡供电/复位 GPIO；
> 3. **证据链修正 §9-1**：唯一一份官方 2015 固件串口日志（`E:\...\lede-ap2600\串口启动日志2.txt`）PCI 仅枚举 `0000:00:11.0`（AR9280=2.4G），**未见 12.0/5G**；我们 6.6 固件 12.0 可被 PCI 枚举（配置空间可读）但 RTC 域唤醒失败——故**无任何留存镜像中 5G 被真正驱动过**，无法据现有备份判定硬件好坏；
> 4. **官方权威移植 mach 源码核对（已抓取，`bin\ap2600ifm\mach-maselink-ap2600ifm.c`）**：`ap2600ifm_setup()` 仅调 `ath79_register_pci()` 挂两张卡，**对 5G 卡无任何 GPIO 供电/复位处理**——印证 OpenWrt 侧缺少 5G 上电初始化；
> 5. **结论与后续**：当下无"回到官方验证 5G"的可行路径（无原厂镜像）；修复方向锁定为**实测定测 5G 卡供电/复位的板级 GPIO** 并在 DTS/启动代码补齐。作者不在设备旁，下一步等实物测量。

***

## 一、需求概述

| 项目       | 说明                                                                                                       |
| -------- | -------------------------------------------------------------------------------------------------------- |
| **目标设备** | Comba（京信）MASELinK AP2600-IFM                                                                             |
| **目标平台** | OpenWrt `ath79/generic`，落地仓库为当前 `d:\vc\lede`                                                             |
| **现状**   | 仅有 ar71xx 时代社区支持（LEDE-r2481，2016 年停止维护），官方 ath79 从未收录本机                                                  |
| **核心诉求** | 彻底迁移到 ath79，弃用/替换 RedBoot，实现标准 sysupgrade 流程                                                             |
| **关键约束** | 底板与 Aruba AP-175 近似，但**内存（64/128MB）、外挂扩展模块不同**，不能直接照搬 AP-175 DTS；PHY 地址实测与 AP-175 同为 @1（mach 曾标 @20，已作废） |
| **硬件权威** | `mach-maselink-ap2600ifm.c`（ar71xx 时代，由 **Weijie Gao (hackpascal)** 编写——同一位开发者亦维护 Breed 不死 U-Boot）       |

***

## 二、硬件规格(已核对,标注来源)

> 标注：`[mach]`=源自 `mach-maselink-ap2600ifm.c` 实测；`[板]`=板级/丝印；`[待测]`=需拆机/上电实测；`[sysfs]`=运行固件 `/sys/bus/pci/devices/` 实测。

### 2.1 核心组件

| 组件      | 芯片/规格                                               | 来源        | 备注                                                                                             |
| ------- | --------------------------------------------------- | --------- | ---------------------------------------------------------------------------------------------- |
| SoC     | Atheros AR7161 rev 2 @ 680 MHz (MIPS 24Kc)          | \[mach/板] | 与官方 AP-175/AP-105 同为 AR7161                                                                    |
| 内存      | ESMT M13S2561616A × 2 = **64MB DDR**                | \[板]      | **官方 AP-175 为 128MB**，此为关键差异                                                                   |
| Flash   | MX25L12805D 或 S25SL12801 = **16MB SPI-NOR**         | \[板]      | 两种芯片可互换                                                                                        |
| 有线 PHY  | IC+ IP1001，**MDIO 地址 @1（2026-08-30 实测）**            | \[实测]     | `mdio read 1 2/1 3` → PHY ID `0x02430d91`；**mach 曾标 @20 已作废**；与官方 AP-175 板值 @1 一致（AP-105 为 @0） |
| 2.4G 无线 | AR9223 (PCIe 00:11.0)，`168c:0029` / 子系统 `168c:2091` | \[sysfs]  | 单 2.4GHz 卡，板型同 Unex DNMA-91；已正常绑定 ath9k                                                        |
| 5G 无线   | AR9220 (PCIe 00:12.0)，`028c:0029` / 子系统 `028c:2096` | \[sysfs]  | 双频卡（本机工作于 5G），板型同 WLM200NX/DNMA-92；**vendor 被改写为** **`0x028c`**，ath9k 需补 PCI ID（见 §2.5）        |
| 串口      | 16550A @ MMIO 0x18020000, IRQ 11                    | \[mach/板] | JP1 排针：3=RX, 5=TX, 6=GND，波特率 115200                                                            |
| USB     | 支持                                                  | —         | 旧固件已加载，供电 GPIO 待确认                                                                             |

### 2.2 底板信息

| 项目                | 内容                                                                                                                                             |
| ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| 底板型号              | `48RPAA05.0GB` (2009/07/30)                                                                                                                    |
| 参考设计              | Atheros **PB42**（RedBoot 曾将本机误报为 pb44，须以 kernel 参数 `board=pb42` 为准）                                                                            |
| 与 Aruba AP-175 关系 | **非完全相同**。芯片平台/无线架构一致，但：内存 64/128MB 不同、外挂扩展模块不同（AP-175 焊接了 TCA6416/LM75/DS1374/24C256/CP210X，本机均未焊接）；**PHY 地址实测同为 1**（mach 曾标 @20，实测推翻，见 §2.4） |
| 与 AP-105 关系       | 同样仅作架构参考，PHY/LED/扩展件均不同                                                                                                                        |

### 2.3 GPIO 定义（源自 mach，权威）

```
LED:  GPIO 0  = green:d24      (active_low)   ← mach: AP2600IFM_GPIO_LED_D24
LED:  GPIO 2  = green:rf1      (active_low)   ← mach: AP2600IFM_GPIO_LED_RF1
LED:  GPIO 3  = green:d24top   (active_low)   ← mach: AP2600IFM_GPIO_LED_D24_TOP
LED:  GPIO 4  = green:rf2      (active_low)   ← mach: AP2600IFM_GPIO_LED_RF2
LED:  GPIO 5  = green:rf2top   (active_low)   ← mach: AP2600IFM_GPIO_LED_RF2_TOP
LED:  GPIO 7  = green:rf1top   (active_low)   ← mach: AP2600IFM_GPIO_LED_RF1_TOP
KEY:  GPIO 8  = reset  (active_low, debounce = 3×poll(20ms) = 60ms)
```

> 注意：官方 AP-175 复位键为 GPIO 6，LED 走 GPIO 扩展（TCA6416）；**本机全部直连 SoC GPIO 且为 6 个独立 LED + 复位 @ GPIO8**，故不能复用 AP-175 的 LED/按键定义，须按本表重写。

### 2.4 网口参数

```
PHY 地址: 1 (0x01)     ← ✅ 已实测（Breed 下 mdio read 1 2/1 3 得到 ID 0x02430d91=IP1001）
          vs 20 (0x14) ← mach 曾写 @20（PHYAD=20 的板级接法），但实机 @20 无响应，作废
phy_mask: BIT(1)        ← 实测有效；PHY 扫描到 @1
接口模式: RGMII (PHY_INTERFACE_MODE_RGMII)
速率:     1000 Mbps / 全双工 [mach]
PLL 1000: 0x00110000
PLL 100:  0x00001099
PLL 10:   0x00991099
```

> **PHY 地址确认过程（2026-08-30）**：Breed 命令行 `mdio read` 实测：`read 1 2 -> 0243`、`read 1 3 -> 0d91`，PHY ID `0x02430d91` = **IC+ IP1001**，**地址 1 有效**；地址 20 无响应。结果与官方 AP-175 板值 @1 一致，DTS 已按 @1 落地（`ethernet-phy@1`）。mach 的 `phy_mask=BIT(20)` 在实机上不成立，仅在文档 §9 保留历史记录。

### 2.5 无线 PCI ID 核实

| 卡            | 芯片     | PCI ID (vendor:device) | Subsystem（实机 sysfs） | 板型对照               | 结论                            |
| ------------ | ------ | ---------------------- | ------------------- | ------------------ | ----------------------------- |
| 2.4G 00:11.0 | AR9223 | `168c:0029`            | `168c:2091`         | Unex DNMA-91       | 标准 Atheros ID，ath9k 直接识别      |
| 5G 00:12.0   | AR9220 | **`028c:0029`**        | `028c:2096`         | WLM200NX / DNMA-92 | vendor 被 OEM 改写，**需补 PCI ID** |

> 两卡主 Device ID 都是 `0x0029`（AR9280 硅片族），差异在 subsystem（0x2091/0x2096）。**但 5G 卡的 vendor 被 OEM 改写为未注册的** **`0x028c`**（pci.ids 无记录），ath9k 默认只匹配 `0x168c`，故该卡不识别、不绑定。已在补丁 `513-ath9k_add_pci_ids.patch` 的 ID 表表尾（`#endif` 之后、`{ 0 }` 之前，不受 `CPTCFG_ATH9K_PCOEM` 保护）新增：
>
> ```c
> 	/* AP2600IFM 5GHz card: AR9220 MB92 (Compex WLM200NX / Wistron DNMA-92),
> 	 * OEM-rewritten vendor ID 0x028c (subsys 0x028c:0x2096).
> 	 * Mirrors the official PCOEM entry (168c:0029/168c:2096) with rewritten vendor. */
> 	{ PCI_DEVICE_SUB(0x028c, 0x0029, 0x028c, 0x2096),
> 	  .driver_data = ATH9K_PCI_LED_ACT_HI },
> ```
>
> 匹配后走 `ath9k_pci_probe → ath9k_hw_init`：devid=`0x0029`=`AR9280_DEVID_PCI`，该值已在 `hw.c` 的 switch 内（backports 6.6.15 hw\.c L674），故无需改 `hw.c/hw.h`；卡上 EEPROM 决定频段能力（AR9220 双频，ipfire 实测 WLM200NX 在 `168c:0029` 下 2.4G/5G 均可用）。DTS 两节点仍写 `compatible = "pci168c,0029"`：ath79 的 OF 按 **devfn（reg）** 匹配节点、不比对 compatible 字符串，故不改 DTS。

***

## 三、Flash 分区与 MAC 布局（已选定 Breed）

> **核心决策（2026-08-29 定，2026-08-30 落地）**：**已用 Breed 替换 RedBoot 并实际引导**。分区采用 Breed 的 **ATH-SDK-16MB** 布局（固件区 `0x50000` 起），MAC 使用官方 AP-175 同款 `hwinfo@0x1c` + `mac-base` 布局，均已写入 DTS。**原 RedBoot 路线 A 已作废**，以下保留其事实仅作历史参考。

### 3.1 路线 A（保留 RedBoot）——已作废

* 曾计划不替换 bootloader、沿用 RedBoot 分区与 NVRAM MAC 读取（mach 通过 `ath79_nvram_parse_mac_addr(KSEG1ADDR(0x1f040004), ..., "macaddr=")` 解析，即 firmware 分区内偏移 0x4 的 NVRAM 变量区）。

* **2026-08-29 已实际将 Breed 写入 mtd0（原 RedBoot 分区所在，flash 0x0），RedBoot 不复存在**，该路线作废。

### 3.2 Breed + ATH-SDK-16MB 布局（已采用，落地于 DTS）

| 分区           | 起始           | 结束           | 大小        | 说明                                      |
| ------------ | ------------ | ------------ | --------- | --------------------------------------- |
| `u-boot`     | `0x00000000` | `0x00050000` | 320 KB    | Breed bootloader（原 RedBoot 分区被覆盖）       |
| `firmware`   | `0x00050000` | `0x00FE0000` | \~15.6 MB | OpenWrt 内核 + squashfs，标准 uImage（无 OKLI） |
| `hwinfo`     | `0x00FE0000` | `0x00FF0000` | 64 KB     | **MAC，偏移** **`0x1c`，6 字节**              |
| `u-boot-env` | `0x00FF0000` | `0x01000000` | 64 KB     | 环境变量                                    |

> **MAC 布局（与官方 AP-175 DTS 完全一致）**：`hwinfo` 分区偏移 `0x1c` 为基址 MAC，采用 `mac-base` + `#nvmem-cell-cells = <1>`，eth0 用偏移 0、2.4G 用偏移 1、5G 用偏移 2。
>
> **固件大小一致性**：firmware `0x50000→0xfe0000 = 0xF90000 = 15872k`，`generic.mk` 的 `IMAGE_SIZE` 必须为 `15872k`（v4 文档的 `16000k` 是按 0x40000 起算的旧值，随布局一并修正）。
>
> 无线 ART 校准数据**不在 Flash**——两张 mini-PCIe 卡自带 EEPROM，`ath9k` 自动读取，天然免疫 ART 丢失风险。

***

## 四、参考资源清单（已核实/修正链接）

### 4.1 核心参考文件（官方 AP-175 骨架）

| 资源            | 地址                                                                                                   | 状态   | 用途                                                                       |
| ------------- | ---------------------------------------------------------------------------------------------------- | ---- | ------------------------------------------------------------------------ |
| 官方 AP-175 DTS | <https://github.com/openwrt/openwrt/blob/master/target/linux/ath79/dts/ar7161_aruba_ap-175.dts>      | ✅ 存在 | **拉取为骨架**，删除 TCA6416/LM75/24C256/DS1374 扩展件，改内存/PHY/LED/按键               |
| AP-175 支持 PR  | <https://github.com/openwrt/openwrt/pull/10794>                                                      | ✅    | 硬件清单与初次移植说明（脚本细节）                                                        |
| APBoot 兼容镜像引入 | <https://git.openwrt.org/?p=openwrt/openwrt.git;a=commit;h=90ad13c76360e5c0ff45db2fd88ffb2595afe451> | ✅    | OKLI loader + `go 0x84000040` 加载机制（**最终未采用**，仅作思路参考，本机走 Breed 直导 uImage） |
| 官方 APBoot 文档  | 🔗 **已失效**（原 openwrt.org/toh/aruba/ap-175 返回 404）                                                    | ❌    | 不再引用，改以 PR/DTS 为准                                                        |

> **仓库实现差异（重要）**：当前 `d:\vc\lede` 的 `aruba_ap-105` 定义为旧版（无 LOADER/OKLI）；官方 AP-175 定义含 `LOADER_TYPE`, `LOADER_FLASH_OFFS`, `COMPILE=loader-okli`, `KERNEL`(magic `0x4f4b4c49`) 等。**本机不需要这套 OKLI/LOADER 机制**——Breed 直导标准 uImage（§5.2），仅 APBoot 场景才需要；早期 v4/v5 建议"照搬官方 AP-175 完整 Device 定义"已随 Breed 路线作废。

### 4.2 历史参考文件（ar71xx 参数来源）

| 资源                          | 地址                                                                                                                       | 用途                                                           |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------ |
| `mach-maselink-ap2600ifm.c` | <https://github.com/Macuilxochitl/lede/blob/d4cf545/target/linux/ar71xx/files/arch/mips/ath79/mach-maselink-ap2600ifm.c> | **硬件权威**：GPIO/PHY/PLL/MAC 全部来自此文件，作者为 Weijie Gao(hackpascal) |
| `mach-maselink-ap2600i.c`   | <https://github.com/Macuilxochitl/lede/blob/d4cf545/target/linux/ar71xx/files/arch/mips/ath79/mach-maselink-ap2600i.c>   | ⚠️ **不同设备（AR9344），仅作对比，勿混淆**                                 |
| 添加 commit                   | <https://github.com/coolsnowwolf/lede/commit/0759aaa96d5adb9f68bd730e9e02a7485500547f>                                   | 确认文件添加时间（2017-11-25）                                         |

### 4.3 U-Boot 与加载器资源

| 资源                          | 说明                                                                                                                                                                                                    |
| --------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Breed（已烧录并启动）**           | `breed-ar7161-blank.bin` 已写入 mtd0（flash 0x0，原 RedBoot 分区），md5sum 校验通过，Breed 识别 64MB DRAM / AR7161 / S25FL128P，网络 192.168.1.1；本机 mach 由 Breed 作者 hackpascal 编写（参考设计 **Atheros PB42**，RedBoot 曾误报 pb44） |
| **OKLI loader（OpenWrt 自带）** | 官方 AP-175 通过 `loader-okli` 生成 APBoot 兼容镜像；**本机已弃用**——Breed 自带 LZMA 解压，直接引导标准 uImage（magic `0x27051956`），不需要 OKLI / `CONFIG_FLASH_OFFS`（§5.2）                                                          |
| APB.boot AP-175 专用 U-Boot   | <https://github.com/Hurricos/u-boot-ap105> （分支 `ap175`）——官方 PR #10794 使用的替换 bootloader（本机未采用，仅供参考）                                                                                                    |
| OpenWrt 19.07 uboot-ar71xx  | `package/boot/uboot-ar71xx/` 中 PB44 官方 U-Boot 源码，需修改 DDR 为 64MB（本机未采用）                                                                                                                                |
| U-Boot 官方历史版                | `board/ar71xx/pb44/`（v2014.10 或 v2015.10），需修改 DDR 初始化（本机未采用）                                                                                                                                          |

### 4.4 辅助工具

| 资源                     | 用途                                                  |
| ---------------------- | --------------------------------------------------- |
| `pepe2k/ar9300_eeprom` | ART 分区结构分析工具（GitHub）                                |
| `art-collection`       | 社区收集的 Atheros ART 转储（GitHub），本机 ART 在卡 EEPROM，仅应急参考 |

***

## 五、DTS 适配要点（基于官方 AP-175 骨架改造）

### 5.1 DTS 适配要点（已落地 `target/linux/ath79/dts/ar7161_maselink_ap2600ifm.dts`）

> 现状：DTS 已按下方清单落地（对应实际文件），以官方 `ar7161_aruba_ap-175.dts` 为骨架、注入 mach 参数，并叠加三处实机修正（mdio\_syscon / PHY\@1 / Breed 分区）。

```dts
// 1) 顶层
/ {
    compatible = "maselink,ap2600ifm", "qca,ar7161";
    model = "MASELinK AP2600IFM";

    chosen {
        bootargs = "console=ttyS0,115200 mem=64M";   // 与 mach 一致 115200；AP-175 亦 115200
    };

    memory@0 {
        device_type = "memory";
        reg = <0x0 0x04000000>;                       // 64MB（AP-175 为 128MB，必改）
    };
};

// 2) 独立 MDIO syscon（实机修正，原 AP-175 无此节点）
//    ag71xx 主驱动与 mdio 驱动会用 of_syscon_register(eth0) 抢 reset#9 导致 -EBUSY，
//    故新建 mdio-syscon@19000000（仅 MDIO/MII 寄存器区、不带 resets），
//    &mdio0 用 regmap = <&mdio_syscon> 指向它，绕开独占冲突。
mdio_syscon: mdio-syscon@19000000 {
    compatible = "syscon";
    reg = <0x19000000 0x200>;
};

// 3) LED：删除全部 &gpio_ext（TCA6416）LED，改用直接 GPIO（mach 权威）
//    GPIO 0/2/3/4/5/7，全部 active_low：
//    green:d24(0) green:rf1(2) green:d24top(3)
//    green:rf2(4) green:rf2top(5) green:rf1top(7)

// 4) 按键：复位键从 AP-175 的 GPIO 6 改为 GPIO 8，active_low

// 5) 删除扩展件节点：gpio_ext(TCA6416@21)、temp-sensor@4a(LM75)、
//    eeprom@50(24C256)、rtc@68(ds1374)，以及 &i2c0 内对应 &gpio_ext 引用

// 6) PHY：直接沿用 AP-175 的 @1 —— 实测推翻 mach 的 @20
//    2026-08-30 Breed 下 mdio read 1 2/1 3 得到 PHY ID 0x02430d91 = IC+ IP1001，
//    @20(0x14) 无响应；与官方 AP-175 板值 @1 一致。
//    无需 phy-mask（@1 为 0 号总线的有效地址，不加 mask 亦可扫出）。
&mdio0 {
    status = "okay";
    regmap = <&mdio_syscon>;                          // 见 2)
    phy1: ethernet-phy@1 { reg = <1>; };
};

&eth0 {
    status = "okay";
    phy-handle = <&phy1>;
    phy-mode = "rgmii";
    pll-data = <0x00110000 0x00001099 0x00991099>;    // mach: 0x00110000/0x00001099/0x00991099
};

// 7) 无线：两卡 compatible 均用 "pci168c,0029"（AR9220/AR9223 主 ID 同为 0x0029）
//    ath79 的 OF 按 devfn（reg 属性）匹配 PCI 节点、不比对 compatible 字符串，
//    故 5G 卡 vendor 被改写为 0x028c 也无需改这里的 compatible。
//    MAC：hwinfo@0x1c 基址 + eth0 偏移0 / 2.4G 偏移1 / 5G 偏移2 的方案暂以注释保留，
//    待 5G 驱动绑定验证后再启用 nvmem-cells（避免过早引入 MAC 相关变数）。
&pcie0 {
    status = "okay";
    wifi@11,0 {   // 2.4G AR9223（168c:2091）
        compatible = "pci168c,0029";
        reg = <0x8800 0 0 0 0>;
        #gpio-cells = <2>;
        gpio-controller;
    };
    wifi@12,0 {   // 5G AR9220（028c:2096，vendor 被 OEM 改写）
        compatible = "pci168c,0029";
        reg = <0x9000 0 0 0 0>;
        #gpio-cells = <2>;
        gpio-controller;
    };
};

// 8) 分区 + hwinfo nvmem（Breed ATH-SDK-16MB 布局，已落地）
//    u-boot(0x0,320k) | firmware(0x50000,0xf90000,uimage) | hwinfo(0xfe0000,64k) | u-boot-env(0xff0000,64k)
&spi {
    flash@0 {
        partitions {
            compatible = "fixed-partitions";
            #address-cells = <1>;
            #size-cells = <1>;

            partition@0 {                 // Breed bootloader（原 RedBoot 分区被覆盖）
                label = "u-boot";
                reg = <0x000000 0x50000>;
                read-only;
            };
            partition@50000 {             // OpenWrt 固件，标准 uImage（无 OKLI）
                label = "firmware";
                reg = <0x50000 0xf90000>;
                compatible = "denx,uimage";
            };
            hwinfo: partition@fe0000 {    // MAC 基址 @0x1c
                label = "hwinfo";
                reg = <0xfe0000 0x10000>;
                read-only;
                nvmem-layout {
                    compatible = "fixed-layout";
                    #address-cells = <1>;
                    #size-cells = <1>;
                    macaddr_hwinfo_1c: macaddr@1c {
                        compatible = "mac-base";
                        reg = <0x1c 0x6>;
                        #nvmem-cell-cells = <1>;
                    };
                };
            };
            partition@ff0000 {
                label = "u-boot-env";
                reg = <0xff0000 0x10000>;
                read-only;
            };
        };
    };
};
```

> **原 RedBoot 路线（已作废，仅作历史参考）**：分区用原 RedBoot 布局、MAC 从 firmware 分区 NVRAM 偏移 0x04 读取（对应 mach 的 `0x1f040004`），不使用 hwinfo\@0x1c。RedBoot 已被 Breed 覆盖，此路线不再考虑。

### 5.2 image/generic.mk 定义（已落地，Breed 直导标准 uImage）

> **实际定义**（`target/linux/ath79/image/generic.mk` L1519-1531）。官方 AP-175 的 OKLI/LOADER 机制被移除：Breed 自带 LZMA 解压，直接识别标准 uImage（magic `0x27051956`，load `0x80060000`），不需要 `LOADER_FLASH_OFFS`/`CONFIG_FLASH_OFFS`。

```makefile
define Device/maselink_ap2600ifm
  SOC := ar7161
  DEVICE_VENDOR := MASELinK
  DEVICE_MODEL := AP2600IFM
  IMAGE_SIZE := 15872k       # firmware 0x50000→0xfe0000 = 0xF90000 = 15872k（Breed ATH-SDK-16MB 布局）
  DEVICE_PACKAGES := kmod-usb2
  # 引导：Breed (ATH-SDK-16MB 布局, 固件 0x50000 起)。
  # 回落 ath79 默认 KERNEL (kernel-bin | append-dtb | lzma | uImage lzma),
  # 即标准 uImage (magic 0x27051956, load 0x80060000)。Breed 自带 LZMA 解压,
  # 直接识别标准 uImage 引导, 无需 OKLI / CONFIG_FLASH_OFFS。
endef
TARGET_DEVICES += maselink_ap2600ifm
```

> **修正对照**：v4/v5 文档沿用官方 AP-175 的 `IMAGE_SIZE := 16000k` + OKLI loader（`LOADER_FLASH_OFFS := 0x42000`、KERNEL magic `0x4f4b4c49`）——那是为 Aruba 原厂 APBoot 准备的，与 Breed 无关，反而引入 flash 偏移适配易错点，**v6 起作废**。固件实际大小由分区决定：`0x50000→0xfe0000 = 0xF90000 = 15872k`。`DEVICE_PACKAGES` 去掉官方 AP-175 的扩展件（kmod-gpio-pca953x/kmod-hwmon-lm75/kmod-i2c-gpio/kmod-rtc-ds1374），仅保留 `kmod-usb2`。

***

## 六、仓库选择与构建可行性（已评审当前 lede）

| 检查项                                        | 当前 `d:\vc\lede`                           | 结论                    | <br /> |
| ------------------------------------------ | ----------------------------------------- | --------------------- | :----- |
| ath79 AR7161 DTS 系列                        | ✅ 存在（ap-105/buffalo/netgear/ubnt 等）       | 可直接承载                 | <br /> |
| 本机 DTS `ar7161_maselink_ap2600ifm.dts`     | ✅ 已按 §5.1 落地于 `target/linux/ath79/dts/`   | 骨架 + 实机修正已完成          | <br /> |
| generic.mk 设备定义 `maselink_ap2600ifm`       | ✅ 已加入（§5.2，IMAGE\_SIZE=15872k）            | 已编译可用                 | <br /> |
| OKLI loader（`loader-okli`/`loader-kernel`） | ✅ image 层已支持（**本机不需要**，Breed 直导标准 uImage） | 仅 APBoot 场景需要         | <br /> |
| Aruba AP-175 DTS                           | ❌ 官方 `ar7161_aruba_ap-175.dts` 未入库        | 本机 DTS 以其为骨架手写（非直接引入） | <br /> |

**决策**：在**当前** **`d:\vc\lede`** **仓库**开发（已执行）。实际路径：① 以官方 AP-175 DTS 为骨架手写本机 DTS（§5.1 已落地，叠加 mdio\_syscon / PHY\@1 / Breed 分区三处实机修正）；② 在 `target/linux/ath79/image/generic.mk` 添加 §5.2 的 Device 定义（Breed 布局，勿照搬官方 AP-175 的 OKLI/LOADER 机制）。

***

## 七、实施路线图（已推进至 Phase 1 编译验证）

### Phase 0：实测与落地（已完成，2026-08-29\~30）

1. **PHY 地址实测**：Breed 下 `mdio read 1 2 / 1 3` 确认 IP1001 挂在 **@1**（原 mach 假设 @20 作废），同步修正 DTS
2. **Breed 烧录**：`breed-ar7161-blank.bin` 经 OpenWrt `mtd write` 写入 mtd0（flash 0x0），md5sum 校验通过并成功启动
3. **DTS 落地**：`ar7161_maselink_ap2600ifm.dts`（§5.1，含 mdio\_syscon / PHY\@1 / Breed 分区）
4. **generic.mk 落地**：`maselink_ap2600ifm` 定义（§5.2，IMAGE\_SIZE=15872k、标准 uImage）
5. **无线卡身份实测**：2.4G=AR9223（`168c:0029/2091`）、5G=AR9220（`028c:0029/2096`，vendor 被 OEM 改写）
6. **ath9k 补丁**：`package/kernel/mac80211/patches*/ath9k/513-ath9k_add_pci_ids.patch` 表尾新增 `PCI_DEVICE_SUB(0x028c, 0x0029, 0x028c, 0x2096)`（§2.5）

### Phase 1：编译验证 5G 无线（已完成，2026-08-31 实测定论）

1. **编译**：GitHub Action 触发成功（Run #33336285638，52m7s），产物含 `513-ath9k_add_pci_ids.patch`，刷入 `firmware` 分区（ATH-SDK-16MB 布局）后串口验证启动正常

2. **验证结果**（dl6 与 dl7 两次独立编译产物烧录测试一致）：

   * [x] 串口(115200)正常，进入 shell

   * [x] `dmesg` 显示 **0000:00:12.0（5G，0x028c:0029）已被 ath9k 识别并** **`enabling device`**（补丁生效）；但随即 `Couldn't reset chip` → `Unable to initialize hardware; initialization status: -5`（EIO），**probe failed**，`/sys/class/ieee80211` 仅 `phy0`

   * [x] 5G 卡 **无法创建 AP**（芯片唤醒失败）；2.4G（AR9280/AR9223）回归正常（`phy0: Atheros AR9280 Rev:2`）

   * [x] 千兆网口正常（PHY IP1001 @1，`eth0: link up (1000Mbps/Full duplex)`）

   * [ ] 6 个 LED 可控 / 复位键反应（本次烧录未专项验证）

3. **结论**：补丁已解决"驱动识别"问题，但 **5G 卡芯片仍无法唤醒**。早期据此判定"硬件问题、软件无修复"，**已被 v7 推翻**：用户明确 Comba 原厂系统下 5G 正常，故硬件故障非唯一解释。当前定为"**软硬件原因未定，缺 5G 卡供电/复位初始化**"——倾向原厂引导完成了一套 OpenWrt 缺失的 5G 卡上电 GPIO 时序（如给 5G LDO/偏压供电），需实测定测板级 GPIO 后补齐。当前设备以 2.4G 单频（phy0）可用。

### Phase 2：收尾（5G 验证通过后）

1. 启用无线 MAC 读取：DTS 两个 `wifi@` 节点接入 `nvmem-cells = <&macaddr_hwinfo_1c N>`（eth0 偏移0 / 2.4G 偏移1 / 5G 偏移2），确认 `hwinfo@0x1c` 派生正确（§5.1 注释方案，当前暂以注释保留）
2. 验证 sysupgrade 在 Breed ATH-SDK-16MB 布局下正常（含 `hwinfo`/`u-boot-env` 只读保护）
3. （可选）提交到 OpenWrt 官方

***

## 八、已知风险与保护

| 风险                | 说明                                   | 保护方案                                                                                                                                                                                 |
| ----------------- | ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **ART 丢失**        | 无线校准数据丢失后无法恢复                        | 本机 ART 在 mini-PCIe 卡 EEPROM，不在 Flash，**天然免疫**                                                                                                                                        |
| **MAC 丢失**        | hwinfo/NVRAM 被覆盖                     | 刷机前备份 hwinfo 64KB；新布局中 hwinfo 为只读分区                                                                                                                                                  |
| **Bootloader 误刷** | 刷 Breed 覆盖错误区域                       | Breed 写入前 md5sum 校验；完整备份旧 Flash（RedBoot）                                                                                                                                             |
| **DDR 初始化失败**     | Bootloader 中 64MB 配错（AP-175 是 128MB） | Breed 初始化已识别 64MB DRAM（实测通过）                                                                                                                                                         |
| **PHY 不通**        | PHY 地址/模式错                           | 已实测确认 PHY\@1+RGMII（IP1001），写入 DTS（§2.4）                                                                                                                                              |
| **5G 无线不进系统**     | 5G 卡 vendor 被改写（0x028c）导致 ath9k 不识别  | 补丁 `513-ath9k_add_pci_ids.patch` 已解决驱动识别；但芯片唤醒失败（`Couldn't reset chip`/error -5）——早期判定硬件问题，**v7 修正**：用户确认 Comba 原厂下 5G 正常，更可能是 **OpenWrt 缺 5G 卡上电/复位 GPIO 初始化**，需实测板级 GPIO 后补齐（§九-1） |

***

## 九、待确认事项（已收敛，留给实测定论）

> PHY 地址（@1）、无线卡型号（2.4G=AR9223/5G=AR9220）、bootloader 路线（Breed）均已实测确认。**5G 卡驱动绑定已实测**：补丁生效、驱动成功识别 `028c:0029` 并 `enabling device`，但芯片初始化失败（`Couldn't reset chip` → `error -5`）。**v7（2026-08-31）修正**：早期"判定硬件问题"为**过早定论**——用户确认 Comba 原厂系统下 5G 正常，而 Comba 原厂系统镜像未备份，故**目前无法判定硬件好坏**；倾向 **OpenWrt 侧缺 5G 卡 (AR9220) 上电/复位 GPIO 初始化**，需实测定测板级 GPIO 后补齐。

| 序号 | 事项                                                                                                                                                                                                                                                                                                                            | 验证时机/结论                                                                                                                         |
| -- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| 1  | **5G 卡（AR9220，vendor 0x028c）芯片无法唤醒**。证据链：① dl6/dl7 两次独立编译产物烧录测试一致复现 `Couldn't reset chip`（`Failed to wakeup in 500us`，RTC 域无响应）；② 唯一官方 2015 OpenWrt 日志 `E:\...\lede-ap2600\串口启动日志2.txt` 仅枚举 11.0（AR9280），**未见 12.0/5G**；③ 我们 6.6 固件 12.0 配置空间可读（链路通）但 RTC 域唤醒失败；④ mach 源码无无线卡电源/复位 GPIO；⑤ **用户确认 Comba 原厂系统下 5G 正常，但原厂镜像未备份** | **判定（v7 修正）：软硬件原因未定**。PCI 枚举说明链路/插槽大概率正常；最可能 **OpenWrt 缺 5G 卡上电/复位 GPIO**。**后续**：实测定测板级 5G 供电/复位 GPIO 并补齐初始化；如测后确认供电无缺失，再回到硬件检查 |
| 2  | USB 供电是否需要某个 GPIO 拉高                                                                                                                                                                                                                                                                                                          | 网口/无线验证通过后测试 USB                                                                                                                |
| 3  | 无线 MAC 是否按 `hwinfo@0x1c` 布局正确派生（eth0 偏移0 / 2.4G 偏移1 / 5G 偏移2）                                                                                                                                                                                                                                                                 | Phase 2 启用 nvmem-cells 后 `iw dev` 查看                                                                                            |
| 4  | sysupgrade 在 Breed ATH-SDK-16MB 布局下是否正常工作                                                                                                                                                                                                                                                                                     | Phase 2                                                                                                                         |

***

## 十、历史时间线

| 时间              | 事件                                                                                                                                                                    |
| --------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2009-07         | 底板 `48RPAA05.0GB` 设计完成                                                                                                                                                |
| 2010-03         | RedBoot 编译（启动日志显示 built Mar 27 2010）                                                                                                                                  |
| 2014            | anywlan 论坛首次发布 ar71xx 自编译固件                                                                                                                                           |
| 2015-2016       | 恩山论坛持续更新，修复千兆/USB/MAC                                                                                                                                                 |
| 2016-12         | 最后一次更新 LEDE-r2481，作者声明停止维护                                                                                                                                            |
| 2017-11         | `mach-maselink-ap2600ifm.c` 入库（作者 Weijie Gao），见 commit 0759aaa                                                                                                        |
| 2019-06         | OpenWrt 官方废弃 ar71xx，转向 ath79                                                                                                                                          |
| 2020-08         | OpenWrt 19.07 成为 ar71xx 最后一个版本                                                                                                                                        |
| 2021-06         | coolsnowwolf/lede 删除 ar71xx 目标，文件随之消失                                                                                                                                 |
| 2022-09→2023-04 | 官方合并 Aruba AP-175 ath79 支持（PR #10794，APBoot 兼容 commit 90ad13c）                                                                                                        |
| 2026-08-29      | Breed（`breed-ar7161-blank.bin`）经 `mtd write` 写入 mtd0 替换 RedBoot，md5sum 校验通过                                                                                           |
| 2026-08-30      | Breed mdio 实测 PHY\@1（IP1001）；sysfs 实测无线卡身份（2.4G=AR9223 / 5G=AR9220，vendor 0x028c）；DTS + generic.mk 落地 Breed ATH-SDK-16MB 布局；ath9k 补丁 `513-ath9k_add_pci_ids.patch` 就绪 |
| 2026-08-31      | GitHub Action 编译（#33336285638）并烧录验证：**513 补丁生效**（5G 卡 `028c:0029` 被 ath9k 识别绑定），但芯片初始化失败 `Couldn't reset chip`（error -5）稳定复现，判定 **5G 卡为硬件问题**；当前以 2.4G 单频运行           |
| 现在              | **本文档 v9 修订：明确丢弃“5G 硬件问题”定论，新增 §十一 5G 卡供电/复位排查清单，待作者回家实测**                                                                                                            |

***

## 十一、5G 卡供电/复位排查清单（待实测 2026-08-31）

> 本节是给作者"回家后对着实机"执行的**操作手册**，用于定位 5G 卡（AR9220，`028c:0029/2096`）芯片为何无法唤醒（`Couldn't reset chip`、`Failed to wakeup in 500us`，RTC 域无响应）。所有动作、测量点均以**设备上可见/可测的点**为准，**不预设具体 GPIO 号**，避免凭空改代码。

### 11.1 目标

对实机用万用表/示波器采集 5G 卡与 2.4G 卡的供电、复位差异数据，收敛到三类修复路径之一（见 11.9），并落成 DTS / 内核改动，让 5G 卡（AR9220）能创建 AP。

### 11.2 已确认事实（用于缩小怀疑范围）

| 事实                                                | 证据（本文档实测）                          | 推断                                    |
| ------------------------------------------------- | ---------------------------------- | ------------------------------------- |
| 5G 卡能被 PCI 枚举，链路通                                 | `0000:00:12.0` 出现；配置空间可读           | **不是卡槽/金手指断链**                        |
| 5G 卡 `pci_enable_device` 成功（打印 `enabling device`） | dl6/dl7 两次编译产物一致                   | **3.3V 主供电、PCIe 时钟大概率正常**             |
| 2.4G 卡正常                                          | `phy0: Atheros AR9280 Rev:2`，可建 AP | 作测量基准                                 |
| 5G 卡 RTC（always-on）域唤醒无响应                         | `Couldn't reset chip`（error -5）    | 芯片处于**深度复位/掉电**状态，或**复位脚（RESET#）被拉死** |

**结论收敛**：主供电大概率够，优先怀疑 **① RESET# 复位脚电平/上电时序**；**② 卡片辅助偏压（1.8V LDO / PA 前级）使能缺失**。这也是 mach 源码 `ap2600ifm_setup()` 仅调 `ath79_register_pci()`、对无线卡无任何 GPIO 上电/复位处理的关键嫌疑点。

### 11.3 测量优先级

1. **P0**：两卡的 **RESET#（PERST#）** 复位脚电平（万用表稳态）+ 上电瞬间波形（示波器抓时序）。
2. **P1**：两卡各自 **3.3V 主供电**（卡上大去耦电容两端）电压。
3. **P2**：**辅助供电/偏压**：卡座附近 3.3V→1.0V/1.8V 的板上 LDO/开关稳压，测 VIN/VOUT/EN 使能脚，2.4G 与 5G 对照。

### 11.4 工具与准备

* 数字万用表（DCV 最小档即可）

* 示波器（建议 100 MHz 以上，用于抓 RESET# 上电时序；若无则退回万用表稳态）

* 本设备底板实物 + 已刷固件（现 2.4G 单频可用）

* mini-PCIe 卡座/金手指引脚图（对照 RESET#、3.3V、GND 位置）

* **断电操作**：拆装、量电阻类测量前先完全断电并放电；测电压时再上电

### 11.5 测量步骤 A：卡座供电 / RESET# 对比（以 2.4G 卡为基准）

> 两块卡物理特性相同（均为 Atheros mini-PCIe），把 2.4G 卡作为"已知良好"基准，与 5G 卡逐项对比最能定位差异。

1. 设备上电进入 OpenWrt shell（或直接观察指示灯稳定）。
2. **A1｜主供电**：用万用表 DCV 分别测 2.4G 卡、5G 卡金手指附近的 **3.3V 主供电测试点**（卡上大滤波电容两端，或卡座 3.3V 排），记录电压。

   * 两卡都 ≈3.3V → 主供电正常，跳过供电分路问题。

   * 5G 卡明显偏低/为 0 → 供电分路/保险/稳压问题，回到硬件层面检查。
3. **A2｜RESET#（PERST#）**：分别测两卡 **复位脚** 电平，记录。

   * 正常 mini-PCIe 复位由板上 RC 上电自动释放（空闲为高约 3.3V）。

   * 若 **5G 卡复位脚被持续拉低**（0V）→ 复位被 SoC/逻辑拉死，是 OpenWrt（或 Breed）缺失复位释放的关键证据。

   * 若两卡复位电平一致且为高 → 复位脚本身正常，转 A3/P2。
4. **A3｜（可选，需示波器）上电瞬间时序**：抓两卡 RESET# 与 3.3V 的先后/脉宽，比对差异；若无示波器跳过。

### 11.6 测量步骤 B：定位板级 LDO / 使能脚

* 目测 5G 卡座附近是否有独立小稳压 IC（常见 Marking 如 1117/RT 系列等），与 2.4G 卡对应位置比对。

* 测该 IC 的 **VIN / VOUT / EN(使能)** 三点电位，逐一记录。

* **若该 EN 受 SoC GPIO 控制**：记录当前 EN 电位 → 以此反查对应 GPIO（在 mach/`ar7100.dtsi` GPIO 编号中对照，最终以图纸或滴测为准）→ 落到 11.9 路径一。

### 11.7 软件先行验证（无需万用表，串口即可做）

回家有串口 shell 时，即使暂时没有表笔也可先执行：

```sh
# 1) 挂 debugfs 查看 GPIO 状态（确认是否有空闲 GPIO 被驱动/方向）
mount -t debugfs none /sys/kernel/debug
cat /sys/kernel/debug/gpio

# 2) 查看 5G 卡是否仍在总线、驱动绑定情况
ls -l /sys/bus/pci/devices/0000:00:12.0/driver 2>/dev/null
cat /sys/bus/pci/devices/0000:00:12.0/vendor /sys/bus/pci/devices/0000:00:12.0/device

# 3) 重新触发 5G 卡 probe，复现/观察（已有 retry_5g_rescan.py 记录基线）
echo 1 > /sys/bus/pci/devices/0000:00:12.0/remove
echo 1 > /sys/bus/pci/rescan
dmesg | tail -30

# 4) 抓与复位/唤醒相关日志
dmesg | grep -iE 'ath9k|rtc|reset|pci 0000:00:12'
```

> 说明：**不要臆测去翻转某个 GPIO**。只有先确认"5G 卡某一供电/复位链路确实依赖某 GPIO 而当前未拉"（通过 A/B 或图纸），才去改代码，避免引入新的时序/总线冲突。

### 11.8 记录表（回家填写）

| 项                       | 2.4G 卡实测值 | 5G 卡实测值 | 判定              |
| ----------------------- | --------- | ------- | --------------- |
| 3.3V 主供电                | <br />    | <br />  | ≈3.3V 正常 / 偏低异常 |
| RESET#(PERST#) 稳态       | <br />    | <br />  | 高·正常 / 低·被拉死    |
| RESET# 上电时序（可选）         | <br />    | <br />  | 一致 / 5G 异常      |
| 板载 LDO VIN/VOUT/EN      | <br />    | <br />  | EN 是否受控         |
| 卡在 `debug/gpio` 关联 GPIO | <br />    | <br />  | 逐条记录            |
| 重新 probe 结果             | —         | <br />  | 复现 / 改善         |

### 11.9 三种修复路径（实测数据出来后落地）

> 以下为**候选**，具体以 11.5/11.6 数据为准，不预设。
>
> **路径一：缺 GPIO 上电/复位 → 在 DTS 补复位 GPIO**
> 若确认 5G 卡 RESET#/LDO-EN 受某 SoC GPIO 控制且当前未拉，在 `ar7161_maselink_ap2600ifm.dts` 的 `wifi@12,0` 节点按 ath79 惯例补复位：
>
> ```dts
> wifi@12,0 {
>     compatible = "pci168c,0029";   /* 保持，无碍 vendor 改写 */
>     reg = <0x9000 0 0 0 0>;
>     reset-gpios = <&gpio N GPIO_ACTIVE_LOW>; /* N 由实测确定 */
> };
> ```
>
> 若经实测确认复位机制非 SoC GPIO 控制，则该节点 `reset-gpios` 无效，应改走路径二。
>
> **路径二：RESET# 时序不对 → 早期拉高/加脉宽**
> 若复位脚为高但芯片仍不自举，考虑在启动早期（`prom.c`/pci fixup）或 ath9k 探测前对复位脚做一次低→高脉宽释放；具体放哪个 GPIO/寄存器需实测。
>
> **路径三：供电/复位均正常仍无法唤醒 → 回到硬件**
> 仅当 A/B 数据排除一切软件供电、复位因素后，才考虑卡槽接触、金手指氧化或换卡（AR9220/AR9280）验证——避免一开始就归因硬件。

### 11.10 安全与注意事项

* 全程断电做拆装与通断测量；上电后才测电压。

* 勿短接卡座相邻引脚；示波器探头接地良好。

* 改动 DTS/内核后需重新编译烧录（GitHub Action `build-ap2600ifm.yml`），每次烧录保留版本号与现象记录，便于回退对照。

### 11.11 关于“官方下 5G 是否正常”的判定语义

> 本节已由 v10 §十二替代 —— 作者已确认持有**完整官方 8 分区备份**（`ap2600/`），直接写回即可，无需再从社区固件推断。判定逻辑见 §12.4。

***

## 十二、用官方 8 分区写回验证 5G 硬件（v10 新增）

### 12.1 背景与思路

当前板子 = **Breed + OpenWrt**（Breed 覆写 flash 0x0，OpenWrt 从 0x50000）。5G 卡（AR9220 `028c:0029`）在 OpenWrt 下 `Couldn't reset chip` 无法唤醒，软硬件原因未定。作者确认 `ap2600/` 目录 8 分区为**官方（Comba 原厂 Atheros SDK）系统**——`FIS_directorymtd5.bin` 的 boot\_script 为`fis load -d vmlinux.bin.gz`、boot\_script\_data 为    ` exec -c "...root=31:02 rootfstype=jffs2 init=/sbin/init mem=64M"\`，是原生 SDK 布局，非社区 OpenWrt。

把官方 8 分区写回并运行官方系统，即可做**决定性硬件验证**：

* 原厂 RedBoot 用 `ath_pci`（非 hostapd/ath9k）加载无线卡，初始化 PCI/reset/eeprom 的路径与 OpenWrt 不同，若硬件正常，原厂通常能带起设备；

* **官方系统下 5G 正常 → 硬件没坏，问题在 OpenWrt 端（驱动/GPIO/PB42 board 解析）**，回到 §十一/补丁路线继续；

* **官方系统下 5G 仍** **`Couldn't reset chip`** **/ 无 phy1 → 基本坐实卡供电或复位硬件问题**，执行 §11.5\~11.6 万用表实测。

### 12.2 官方分区精确布局（FIS 解析，2026-08-31）

| 分区           | 文件（`ap2600/`）            | flash 基址 | 大小       | 说明                    |
| ------------ | ------------------------ | -------- | -------- | --------------------- |
| RedBoot      | `redbootmtd0.bin`        | 0x000000 | 0x040000 | 原厂引导器                 |
| vmlinux      | `vmlinux.bin.gzmtd1.bin` | 0x040000 | 0x120000 | 原厂内核，entry 0x80245000 |
| rootfs       | `pb44-jffs2mtd2.bin`     | 0x160000 | 0xe00000 | jffs2 根文件系统           |
| config       | `configmtd3.bin`         | 0xf60000 | 0x080000 | 系统配置                  |
| board-config | `board-configmtd4.bin`   | 0xfe0000 | 0x010000 | 板级信息(@fe0000)         |
| FIS 目录       | `FIS_directorymtd5.bin`  | 0xff0000 | 0x00f000 | RedBoot FIS           |
| RedBoot 配置   | `redbootconfigmtd6.bin`  | 0xfff000 | 0x001000 | RedBoot 环境            |

> `fisconfigmtd7.bin`（0x10000）= 上述 FIS目录+RedBoot配置的**整块**备份（逐字节相等），等价论坛 `openwrtap2600split/mtd56.bin`。
>
> 以上偏移与论坛教程 `fis write` 命令（mtd1\@0xbf040000/0x120000、mtd2\@0xbf160000/0xe00000、mtd3\@0xbff60000/0x80000、mtd4\@0xbffe0000/0x10000、mtd56\@0xbfff0000/0x10000）**完全一致**，已双向验证。

### 12.3 落地路径

已拼装好 **16MB 编程器整片镜像**：`build_official_image.py` 输出 `…\IP1001+ar7161+AR9220+AR9223\AP2600IFM_官方8分区_编程器固件.bin`，**md5 =** **`c8eb2aa4dce43a3149411be27c44ca2b`**，烧录后核对。

**路径 A（推荐，一步回官方）**：CH341A 编程器整写 16MB 镜像到 S25F128P。

1. **最先备份**：编程器读当前 16MB（Breed+OpenWrt）存盘，`md5sum` 记录，用于随时回退；
2. 擦除 → 整写 `AP2600IFM_官方8分区_编程器固件.bin` → 回读校验 md5 = `c8eb…ca2b`；
3. 上电 → 应进官方 RedBoot，自动 `fis load vmlinux.bin.gz` 启动官方系统；
4. 观察 5G：官方 dmesg / `ath_pci` 是否加载第二卡、`iwconfig` 是否出 ra0/ra1。

**路径 B（不用拆线，走 TFTP +** **`fis write`）**：需先有 RedBoot 环境。

1. 编程器先把已有 **RedBoot+OpenWrt 编程器固件**（`AP2600-IFM_大板_OpenWRT编程器固件.BIN`，含原厂式 RedBoot）写入 → 进 RedBoot 交互提示符；
2. 参照论坛命令，把官方文件经 TFTP 写回（文件换成 `ap2600/` 官方件，地址不变）：

```
# 以 TFTP 服务器 192.168.1.100 为例；-b 0x80100000 为 RAM 暂存，与 flash 目标无关
load vmlinux.bin ..  -b 0x80100000 -r -m tftp -h 192.168.1.100
fis write -b 0x80100000 -l 0x120000 -f 0xbf040000   # 内核
load pb44-jffs2 ..  -b 0x80100000 -r -m tftp -h 192.168.1.100
fis write -b 0x80100000 -l 0xe00000 -f 0xbf160000    # rootfs
load config ..      -b 0x80100000 -r -m tftp -h 192.168.1.100
fis write -b 0x80100000 -l 0x80000  -f 0xbff60000     # config
load board-config ..-b 0x80100000 -r -m tftp -h 192.168.1.100
fis write -b 0x80100000 -l 0x10000  -f 0xbffe0000     # board-config
load fisconfig ..   -b 0x80100000 -r -m tftp -h 192.168.1.100
fis write -b 0x80100000 -l 0x10000  -f 0xbfff0000     # FIS 配置区(0xff0000 起 64KB)
```

> **为什么不能直接** **`mtd write`** **写 8 分区**：Breed 布局与官方 FIS 布局不对齐——官方 `vmlinux`(0x040000-0x160000) 跨 Breed `u-boot`(至0x50000)+`firmware` 边界，官方 `config`(0xf60000-0xfe0000) 压在 `firmware`(至0xf90000)+之后的空洞上；且 **0xf90000-0xfe0000 在 Breed DTS 中无 mtd 设备**，OpenWrt 无法写该段。

**路径 C（免拆机在线，仅用于进入 RedBoot）**：OpenWrt shell 只负责把 bootloader 换回 RedBoot，之后全程走 TFTP `fis write`。适合不想拆机、已能进 OpenWrt 的场景。

1. 先在 OpenWrt 里**备份当前 16MB bootloader 区**（写回 RedBoot 前留 Breed 以便回退）：

```
dd if=/dev/mtd0 of=/tmp/breed_u-boot.bin        # 或 mtdinfo 找到 u-boot 设备
```

1. 用 OpenWrt `mtd write` 把官方 RedBoot 写到 flash 0x0（`u-boot` 分区）：

```
mtd write /tmp/redbootmtd0.bin u-boot           # 官方 RedBoot 落在 0x0-0x40000，覆盖 Breed
```

1. `reboot` → 当前 flash 无有效官方 FIS，RedBoot 检测失败后**停在** **`RedBoot>`** **提示符**（安全，不会乱跑）；
2. 在 RedBoot 提示符走路径 B 的命令（TFTP + `fis write` 写官方 8 分区）→ 重启进官方系统。

> 若 TFTP / 写失败或中途断电，通常仍停在 `RedBoot>` 可重试；只有 RedBoot 自身也无法启动才需编程器兜底，因此**务必在换 bootloader 前备份当前 16MB**。

### 12.4 结果判定

| 写回官方后 5G 表现                        | 结论                     | 后续动作                                                                 |
| ---------------------------------- | ---------------------- | -------------------------------------------------------------------- |
| 官方出现第二无线（ra1/ath\_pci 成功，可建 5G AP） | **硬件正常**，问题在 OpenWrt 端 | 回 OpenWrt 排查 board/PB42/GPIO·驱动，补丁 `513-ath9k_add_pci_ids.patch` 已就绪 |
| 官方仍无第二卡 / `reset chip` 失败          | **硬件/板级概率大**           | 按 §11.5\~11.6 万用表测 5G 卡供电与 RESET#；确认后修 DTS/GPIO 或换卡                  |

### 12.5 回退保护

* 写回前**必读当前 16MB** 备份（Breed+OpenWrt），回退＝编程器整写回该备份；

* 每次烧录记录 md5 与现象（沿用工程约定）；

* 8 分区写回属高风险操作，务必确认好镜像与偏移再落笔，避免顺坏 FIS/board-config 导致变砖。

***

## 十三、官方固件实测信息（v11 新增，2026-08-31 串口/实测）

> 本节记录将官方 8 分区写回后运行 **Comba 原厂 Atheros SDK 系统**的实测信息，便于后续继续排查 5G；包含必要登录凭据与系统真相。

### 13.1 官方系统已可运行（已写回成功）

| 项 | 值 | 说明 |
| -- | - | - |
| 系统 | `root@comba:/` | 串口 shell（COM4,115200），官方 SDK 系统 |
| 设备 IP | `192.168.100.100/24` | 官方默认静态 IP（`default` 网卡）；**≠** 光猫 192.168.1.1 |
| Web 登录 | 用户 **admin** / 密码 **admin** | `mini_httpd -p 80`；web 功能精简（瘦AP基础配置） |
| telnet（文档记录） | 用户 **comba** / 密码 **password**，命令 `set system apmode fat ap` | 源自 E 盘 `瘦ap变胖ap.txt`；**未**在本会话验证 |
| SSH | `dropbear` 已运行 | - |
| 版本 | `config.xml` `<version>1.3.2001</version>`、`<type>2010</type>` | - |

> ⚠️ **注意**：`set` 在 ash 是 shell builtin，`set system apmode fat ap` 需进入官方专用 CLI（telnet 登录后）执行，不能在根 shell 直接敲。**用户已决定暂不切换胖AP**。

### 13.2 运行进程（ps 截取）

```
1  init / 3145 /usr/sbin/telnetd / 3262 /sbin/mini_httpd -d /www -u root -p 80
3423 crond / 3761 udhcpc -i default -t 3 -b / 3802-03 linkcheck eth0/eth1
3804 ap_monitor / 3824 dropbear / 3825 factoryreset / 3826 arpnotice / ash --login
```

### 13.3 无线现状（决定性）

官方系统（`ath_pci` 0.9.4.5 SDK 驱动）**同样只识别 2.4G 单卡**：

* 接口：仅 **`ath0`** = `IEEE 802.11ng`（2.462GHz，2.4G，AR9280）
* `/proc/bus/pci/devices`：
  * `0000  168c0029` → 2.4G AR9280，绑定 `ath_pci`（即 `wifi0`）
  * `0008  028c0029` → **5G AR9220，未绑定任何驱动**
* `dmesg`：仅 `wifi0: Atheros 9280: mem=0x10000000, irq=48`；无 5G 卡 attach/reset 报错
* **结论**：官方 SDK 的 `ath_pci` 命中 168c ID 表，ATMOS 因 5G 卡 vendor 被改写为 **0x028c** 而不匹配、不初始化——**与 OpenWrt 的 ath9k 同一根因**。官方固件下未触发 `Couldn't reset chip`（是不初始化而非初始化失败），故**无法借官方系统判定 5G 卡硬件好坏**。

### 13.4 config.xml 关键字段（官方配置）

* `<workmode>1</workmode>`（第 14 行）＝瘦AP模式（依赖 AC/CAPWAP）
* fat/thin 切换走 CLI `set system apmode fat ap`，无独立脚本文本（相关逻辑编译在二进制里）
* `/etc/config/config.ap83`、`config.wtp`（WTP/AC 配置，`<AC_ADDRESSES>255.255.255.255</AC_ADDRESSES>` 广播发现）
* `setapworkmode.sh` 仅写 `ip_forward`，**并非**胖AP切换（已核实）

### 13.5 结论与本阶段定位

* **5G 未工作的根因在驱动侧的 vendor ID 匹配**（官方 ath_pci 与 OpenWrt ath9k 一致），非胖/瘦AP模式或运行模式问题；
* 官方系统能正常跑且 2.4G 可用，可作后续实验底座；**但官方系统下无法验证 5G 硬件好坏**（它压根不初始化 5G 卡）；
* 结合 §九/§十二，5G 是否硬件问题的判定仍须：① 回 OpenWrt（已有 `513-ath9k_add_pci_ids.patch`）看是否能唤醒；② 或实测定测卡供电/RESET#（§11.5-11.6）。

***

*文档修订：v11，2026-08-31（新增 §十三 官方固件实测信息：登录凭据/IP/workmode/无线现状；确认官方 ath_pci 亦因 vendor 0x028c 不初始化 5G 卡）*
*上一版本：v10，2026-08-31（新增 §十二 用官方 8 分区写回验证 5G 硬件；确认官方 FIS 偏移与论坛 `fis write` 一致；产出官方 16MB 编程器镜像 md5=c8eb2aa4dce43a3149411be27c44ca2b）*
*目标仓库：`d:\vc\lede`（coolsnowwolf/lede），平台* *`ath79/generic`*
*核心策略：以官方 Aruba AP-175 的 ath79 DTS 为骨架，注入 mach-maselink-ap2600ifm.c 的硬件参数（64MB、GPIO），经实测修正（PHY\@1、Breed ATH-SDK-16MB 分区、5G 卡 vendor 0x028c 补丁）后落地；MAC 布局采用 hwinfo\@0x1c（暂以注释保留）*

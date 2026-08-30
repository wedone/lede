# MASELinK AP2600IFM → OpenWrt ath79 迁移开发文档 v6

> 本文档面向具备 OpenWrt 内核开发经验的专业开发者/AI，梳理需求、硬件事实、参考资源及迁移路径。代码以伪代码/关键片段呈现，完整实现需开发者补充。
>
> **仓库选择**：本文档基于当前 **`d:\vc\lede`（coolsnowwolf/lede）** 仓库撰写，已通过实测/在线资料核实重要事实，并在各修订版中标注。
>
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

3. **结论**：补丁已解决"驱动识别"问题，但 **5G 卡芯片无法唤醒属硬件问题**，软件层无可行修复（证据见 §九-1）。当前设备以 2.4G 单频（phy0）可用。

### Phase 2：收尾（5G 验证通过后）

1. 启用无线 MAC 读取：DTS 两个 `wifi@` 节点接入 `nvmem-cells = <&macaddr_hwinfo_1c N>`（eth0 偏移0 / 2.4G 偏移1 / 5G 偏移2），确认 `hwinfo@0x1c` 派生正确（§5.1 注释方案，当前暂以注释保留）
2. 验证 sysupgrade 在 Breed ATH-SDK-16MB 布局下正常（含 `hwinfo`/`u-boot-env` 只读保护）
3. （可选）提交到 OpenWrt 官方

***

## 八、已知风险与保护

| 风险                | 说明                                   | 保护方案                                                                                                     |
| ----------------- | ------------------------------------ | -------------------------------------------------------------------------------------------------------- |
| **ART 丢失**        | 无线校准数据丢失后无法恢复                        | 本机 ART 在 mini-PCIe 卡 EEPROM，不在 Flash，**天然免疫**                                                            |
| **MAC 丢失**        | hwinfo/NVRAM 被覆盖                     | 刷机前备份 hwinfo 64KB；新布局中 hwinfo 为只读分区                                                                      |
| **Bootloader 误刷** | 刷 Breed 覆盖错误区域                       | Breed 写入前 md5sum 校验；完整备份旧 Flash（RedBoot）                                                                 |
| **DDR 初始化失败**     | Bootloader 中 64MB 配错（AP-175 是 128MB） | Breed 初始化已识别 64MB DRAM（实测通过）                                                                             |
| **PHY 不通**        | PHY 地址/模式错                           | 已实测确认 PHY\@1+RGMII（IP1001），写入 DTS（§2.4）                                                                  |
| **5G 无线不进系统**     | 5G 卡 vendor 被改写（0x028c）导致 ath9k 不识别  | 补丁 `513-ath9k_add_pci_ids.patch` 已解决驱动识别；但芯片唤醒失败（`Couldn't reset chip`/error -5）判定为**硬件问题**（§九-1），软件层无修复 |

***

## 九、待确认事项（已收敛，留给实测定论）

> PHY 地址（@1）、无线卡型号（2.4G=AR9223/5G=AR9220）、bootloader 路线（Breed）均已实测确认。**5G 卡驱动绑定已实测**：补丁生效、驱动成功识别 `028c:0029` 并 `enabling device`，但芯片初始化失败（`Couldn't reset chip` → `error -5`），判定为硬件问题。

| 序号 | 事项                                                                                                                                                                                                                | 验证时机/结论                                               |
| -- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------- |
| 1  | **5G 卡（AR9220，vendor 0x028c）芯片无法唤醒**。证据链：① dl6/dl7 两次独立编译产物烧录测试一致复现 `Couldn't reset chip`（`Failed to wakeup in 500us`，RTC 域无响应）；② 旧固件启动日志同样仅 phy0（5G 从未工作）；③ PCI 配置空间可读（链路通）但芯片内部寄存器域无法唤醒；④ mach 源码无无线卡电源/复位 GPIO | **判定硬件问题**：5G 卡本身故障/供电/接触不良，软件层无可行修复；需拆机检查或更换 5G 卡后复测 |
| 2  | USB 供电是否需要某个 GPIO 拉高                                                                                                                                                                                              | 网口/无线验证通过后测试 USB                                      |
| 3  | 无线 MAC 是否按 `hwinfo@0x1c` 布局正确派生（eth0 偏移0 / 2.4G 偏移1 / 5G 偏移2）                                                                                                                                                     | Phase 2 启用 nvmem-cells 后 `iw dev` 查看                  |
| 4  | sysupgrade 在 Breed ATH-SDK-16MB 布局下是否正常工作                                                                                                                                                                         | Phase 2                                               |

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
| 现在              | **本文档 v7 修订，记录 Phase 1 实测定论（5G 硬件问题）**                                                                                                                                |

***

*文档修订：v7，2026-08-31（Phase 1 实测定论：5G 卡硬件问题）*
*目标仓库：`d:\vc\lede`（coolsnowwolf/lede），平台* *`ath79/generic`*
*核心策略：以官方 Aruba AP-175 的 ath79 DTS 为骨架，注入 mach-maselink-ap2600ifm.c 的硬件参数（64MB、GPIO），经实测修正（PHY\@1、Breed ATH-SDK-16MB 分区、5G 卡 vendor 0x028c 补丁）后落地；MAC 布局采用 hwinfo\@0x1c（暂以注释保留）*

# MASELinK AP2600IFM → OpenWrt ath79 迁移开发文档 v4

> 本文档面向具备 OpenWrt 内核开发经验的专业开发者/AI，梳理需求、硬件事实、参考资源及迁移路径。代码以伪代码/关键片段呈现，完整实现需开发者补充。
>
> **仓库选择**：本文档基于当前 **`d:\vc\lede`（coolsnowwolf/lede）** 仓库撰写，已通过实测/在线资料核实重要事实，并在本修订版（v4）中标注。
>
> **修订要点（v4，2026-08-29）**：
> 1. 以 `mach-maselink-ap2600ifm.c` 为硬件权威，核对 PHY/MAC/LED/按键/PLL，全部吻合；
> 2. 核实官方 `ar7161_aruba_ap-175.dts` 与 `generic.mk` 真实定义，修正分区、镜像大小、loader 机制；
> 3. **MAC 地址布局由所选的 bootloader 决定**（保留 RedBoot / 更换 U-Boot 两条路线）；原"board-config@0x00"方案废弃，改为对齐官方 AP-175 的 `hwinfo@0x1c`；
> 4. 核实 AR9220 / AR9223 的 PCI Device ID 均为 `0x0029`，`pci168c,0029` 通吃；
> 5. 修正第 4 节失效链接（openwrt.org/toh/aruba/ap-175 已 404）；
> 6. 评审确认当前 lede 具备 OKLI loader 编译能力，可承载该移植。

---

## 一、需求概述

| 项目 | 说明 |
|------|------|
| **目标设备** | Comba（京信）MASELinK AP2600-IFM |
| **目标平台** | OpenWrt `ath79/generic`，落地仓库为当前 `d:\vc\lede` |
| **现状** | 仅有 ar71xx 时代社区支持（LEDE-r2481，2016 年停止维护），官方 ath79 从未收录本机 |
| **核心诉求** | 彻底迁移到 ath79，弃用/替换 RedBoot，实现标准 sysupgrade 流程 |
| **关键约束** | 底板与 Aruba AP-175 近似，但**内存、PHY 地址、外挂扩展模块不同**，不能直接照搬 AP-175 DTS |
| **硬件权威** | `mach-maselink-ap2600ifm.c`（ar71xx 时代，由 **Weijie Gao (hackpascal)** 编写——同一位开发者亦维护 Breed 不死 U-Boot） |

---

## 二、硬件规格(已核对,标注来源)

> 标注：`[mach]`=源自 `mach-maselink-ap2600ifm.c` 实测；`[板]`=板级/丝印；`[待测]`=需拆机/上电实测。

### 2.1 核心组件

| 组件 | 芯片/规格 | 来源 | 备注 |
|------|----------|------|------|
| SoC | Atheros AR7161 rev 2 @ 680 MHz (MIPS 24Kc) | [mach/板] | 与官方 AP-175/AP-105 同为 AR7161 |
| 内存 | ESMT M13S2561616A × 2 = **64MB DDR** | [板] | **官方 AP-175 为 128MB**，此为关键差异 |
| Flash | MX25L12805D 或 S25SL12801 = **16MB SPI-NOR** | [板] | 两种芯片可互换 |
| 有线 PHY | IC+ IP1001，**MDIO 地址 @20 (0x14)** | [mach] | `phy_mask = BIT(20)`；**官方 AP-175 为 @1，AP-105 为 @0 —— 三者不同** |
| 2.4G 无线 | AR9220 类卡 (PCIe 00:11.0) | [板/待测] | **实卡型号待拆机确认** |
| 5G 无线 | AR9223 类卡 (PCIe 00:12.0) | [板/待测] | **实卡型号待拆机确认**；两卡 PCI ID 均为 `0x0029`，见 §2.5 |
| 串口 | 16550A @ MMIO 0x18020000, IRQ 11 | [mach/板] | JP1 排针：3=RX, 5=TX, 6=GND，波特率 115200 |
| USB | 支持 | — | 旧固件已加载，供电 GPIO 待确认 |

### 2.2 底板信息

| 项目 | 内容 |
|------|------|
| 底板型号 | `48RPAA05.0GB` (2009/07/30) |
| 参考设计 | Atheros PB44 |
| 与 Aruba AP-175 关系 | **非完全相同**。芯片平台/无线架构一致，但：内存 64/128MB 不同、PHY 地址(20/1)不同、且 AP-175 焊接了 TCA6416/LM75/DS1374/24C256/CP210X 等外挂扩展模块，**本机均未焊接** |
| 与 AP-105 关系 | 同样仅作架构参考，PHY/LED/扩展件均不同 |

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

### 2.4 网口参数（源自 mach，权威）

```
PHY 地址: 20 (0x14)   ← 首选（mach 真机配置，phy_mask=BIT(20)）
         vs 1 (0x01)   ← 备用（官方 AP-175 板值，仅当@20跑不通再试）
         vs 0 (0x00)   ← 备用（官方 AP-105 板值，最后尝试）
phy_mask: BIT(20)
接口模式: RGMII (PHY_INTERFACE_MODE_RGMII)
速率:     1000 Mbps / 全双工 (强制)
PLL 1000: 0x00110000
PLL 100:  0x0001099
PLL 10:   0x00991099
```

> **PHY 地址选择策略**：MDIO 物理地址由 IP1001 的 PHYAD 引脚电平决定，不随 ar71xx/ath79 内核机制改变。本机 mach 以 `phy_mask=BIT(20)`+强制千兆写入，可判定 @20 为真值。**优先 PHY@20；若 initramfs 下千兆不通，则改用 AP-175 的 @1，再不行试 AP-105 的 @0**（详见 §9 排查项，勿因"同为 PB44 参考"直接照搬 AP-175 的 @1）。

### 2.5 无线 PCI ID 核实

| 芯片 | PCI Device ID | Subsystem | 结论 |
|------|---------------|-----------|------|
| AR9220 | `0x0029` | `0xa094` | compatible 用 `pci168c,0029` |
| AR9223 | `0x0029` | `0xa095` | compatible **同样用** `pci168c,0029` |

> 两卡主 Device ID 都是 `0x0029`（差异仅在 subsystem），因此 DTS 中 2.4G/5G 两个无线节点均可写 `compatible = "pci168c,0029"`，无需按型号区分。5G（AR9223）是否在新版 ath9k 完全正常仍需实测。

---

## 三、Flash 分区与 MAC 布局（取决于 bootloader）

> **核心决策**：MAC 地址的存放偏移由所选 bootloader 决定。分两条路线，实施前必须先选定。

### 3.1 旧固件（RedBoot / mach 时代）的 MAC 事实

- mach 通过 `ath79_nvram_parse_mac_addr(KSEG1ADDR(0x1f040004), ..., "macaddr=")` 解析 MAC。
- `0x1f040004` = flash 物理 `0x040004`，即 **firmware 分区内偏移 0x4 的 NVRAM 变量区**；`macaddr=` 环境变量存放 6 字节 MAC。
- 结论：**原厂 MAC 不在独立"board-config"偏移，而在 firmware 分区开头的 NVRAM**。文档 v3 称"NVRAM 区域偏移 0x04"属实。

### 3.2 路线 A：保留 RedBoot + 原始分区（仅验证/过渡）

- 不替换 bootloader，沿用 RedBoot 分区与 NVRAM MAC 读取。
- Phase 1 验证最省事，flash 布局与 MAC 读取方式均不用改。

### 3.3 路线 B：更换 U-Boot + 现代化分区（最终目标，推荐）

- 采用与官方 AP-175 一致的布局（本机无独立 board-config）：

| 分区 | 起始 | 结束 | 大小 | 说明 |
|------|------|------|------|------|
| `u-boot` | `0x00000000` | `0x00040000` | 256 KB | 现代 U-Boot（需编译/获取） |
| `firmware` | `0x00040000` | `0x00FE0000` | ~15.6 MB | OpenWrt 内核 + squashfs（含 OKLI loader） |
| `hwinfo` | `0x00FE0000` | `0x00FF0000` | 64 KB | **MAC，偏移 `0x1c`，6 字节** |
| `u-boot-env` | `0x00FF0000` | `0x01000000` | 64 KB | 环境变量 |

> **MAC 布局（与官方 AP-175 DTS 完全一致）**：`hwinfo` 分区偏移 `0x1c` 为基址 MAC，采用 `mac-base` + `#nvmem-cell-cells = <1>`，eth0 用偏移 0、2.4G 用偏移 1、5G 用偏移 2。
>
> **废弃说明**：v3 文档建议"MAC 写入 board-config 偏移 0x00"，与官方 AP-175 骨架（`hwinfo@0x1c`）不一致。v4 改为对齐官方骨架，便于直接复用 sysupgrade 与 nvmem 解析逻辑。若坚持独立分区名，可命名 `hwinfo` 而非 `board-config`，偏移仍用 `0x1c`。
>
> 无线 ART 校准数据**不在 Flash**——两张 mini-PCIe 卡自带 EEPROM，`ath9k` 自动读取，天然免疫 ART 丢失风险。

---

## 四、参考资源清单（已核实/修正链接）

### 4.1 核心参考文件（官方 AP-175 骨架）

| 资源 | 地址 | 状态 | 用途 |
|------|------|------|------|
| 官方 AP-175 DTS | https://github.com/openwrt/openwrt/blob/master/target/linux/ath79/dts/ar7161_aruba_ap-175.dts | ✅ 存在 | **拉取为骨架**，删除 TCA6416/LM75/24C256/DS1374 扩展件，改内存/PHY/LED/按键 |
| AP-175 支持 PR | https://github.com/openwrt/openwrt/pull/10794 | ✅ | 硬件清单与初次移植说明（脚本细节） |
| APBoot 兼容镜像引入 | https://git.openwrt.org/?p=openwrt/openwrt.git;a=commit;h=90ad13c76360e5c0ff45db2fd88ffb2595afe451 | ✅ | OKLI loader + `go 0x84000040` 加载机制，可直接迁移思路 |
| 官方 APBoot 文档 | 🔗 **已失效**（原 openwrt.org/toh/aruba/ap-175 返回 404） | ❌ | 不再引用，改以 PR/DTS 为准 |

> **仓库实现差异（重要）**：当前 `d:\vc\lede` 的 `aruba_ap-105` 定义为旧版（无 LOADER/OKLI）；官方 AP-175 定义含 `LOADER_TYPE`, `LOADER_FLASH_OFFS`, `COMPILE=loader-okli`, `KERNEL`(magic `0x4f4b4c49`) 等，移植时必须把官方 AP-175 的**完整 Device 定义**带过来，不能只抄 ap-105。

### 4.2 历史参考文件（ar71xx 参数来源）

| 资源 | 地址 | 用途 |
|------|------|------|
| `mach-maselink-ap2600ifm.c` | https://github.com/Macuilxochitl/lede/blob/d4cf545/target/linux/ar71xx/files/arch/mips/ath79/mach-maselink-ap2600ifm.c | **硬件权威**：GPIO/PHY/PLL/MAC 全部来自此文件，作者为 Weijie Gao(hackpascal) |
| `mach-maselink-ap2600i.c` | https://github.com/Macuilxochitl/lede/blob/d4cf545/target/linux/ar71xx/files/arch/mips/ath79/mach-maselink-ap2600i.c | ⚠️ **不同设备（AR9344），仅作对比，勿混淆** |
| 添加 commit | https://github.com/coolsnowwolf/lede/commit/0759aaa96d5adb9f68bd730e9e02a7485500547f | 确认文件添加时间（2017-11-25） |

### 4.3 U-Boot 与加载器资源

| 资源 | 说明 |
|------|------|
| **OKLI loader（OpenWrt 自带）** | 官方 AP-175 通过 `loader-okli` 生成 APBoot 兼容镜像；当前 lede 已具备此能力，见 §6 |
| APB.boot AP-175 专用 U-Boot | https://github.com/Hurricos/u-boot-ap105 （分支 `ap175`）——官方 PR #10794 使用的替换 bootloader |
| **Breed（推荐）** | 本机 mach 由 Breed 作者 hackpascal 编写，Breed 对 AR7161/PB44 有板支持，优先在恩山/Right.com.cn 搜 "Breed PB44" / "Breed AR7161" |
| OpenWrt 19.07 uboot-ar71xx | `package/boot/uboot-ar71xx/` 中 PB44 官方 U-Boot 源码，需修改 DDR 为 64MB |
| U-Boot 官方历史版 | `board/ar71xx/pb44/`（v2014.10 或 v2015.10），需修改 DDR 初始化 |

### 4.4 辅助工具

| 资源 | 用途 |
|------|------|
| `pepe2k/ar9300_eeprom` | ART 分区结构分析工具（GitHub） |
| `art-collection` | 社区收集的 Atheros ART 转储（GitHub），本机 ART 在卡 EEPROM，仅应急参考 |

---

## 五、DTS 适配要点（基于官方 AP-175 骨架改造）

### 5.1 基于 AP-175 的修改清单

```dts
// 1) 复制官方 ar7161_aruba_ap-175.dts 到本仓库 dts/ 目录
// 2) 顶层
/ {
    compatible = "maselink,ap2600ifm", "qca,ar7161";
    model = "MASELinK AP2600IFM";

    chosen {
        bootargs = "console=ttyS0,115200 mem=64M";
        // 与 mach 一致用 115200；AP-175 同 115200（AP-105 才用 9600）
    };

    // 内存 64MB（AP-175 为 128MB，必须改）
    memory@0 {
        device_type = "memory";
        reg = <0x0 0x04000000>;
    };
};

// 3) LED：删除全部 &gpio_ext（TCA6416）LED，改用直接 GPIO（mach 权威）
//    GPIO 0/2/3/4/5/7，全部 active_low：
//    green:d24(0) green:rf1(2) green:d24top(3)
//    green:rf2(4) green:rf2top(5) green:rf1top(7)

// 4) 按键：复位键从 GPIO 6 改为 GPIO 8，active_low

// 5) 删除扩展件节点：gpio_ext(TCA6416@21)、temp-sensor@4a(LM75)、
//    eeprom@50(24C256)、rtc@68(ds1374)，以及 &i2c0 内对应 &gpio_ext 引用

// 6) PHY：地址从 AP-175 的 @1 改为 20（0x14），并加 phy-mask = <0x100000>（即 BIT(20)）
//    主选 @20。若 initramfs 下千兆不通，改 phy@1（AP-175）；仍不通再试 phy@0（AP-105）。
//    改动点：node reg、&eth0 的 phy-handle、phy-mask 三者要同步。
&mdio0 {
    status = "okay";
    phy-mask = <0x100000>;   // BIT(20)
    phy14: ethernet-phy@14 { reg = <0x14>; };  // 备用：phy1@1 | phy0@0（三处同步改）
};

&eth0 {
    status = "okay";
    phy-handle = <&phy14>;
    phy-mode = "rgmii";
    pll-data = <0x00110000 0x00001099 0x00991099>;  // mach: 0x00110000/0x0001099/0x00991099
    nvmem-cells = <&macaddr_hwinfo_1c 0>;           // 路线 B：hwinfo@0x1c, base offset 0
    nvmem-cell-names = "mac-address";
};

// 7) 无线：两卡 compatible 均用 "pci168c,0029"（AR9220/AR9223 同 ID）
//    eth0 偏移0 / 2.4G 偏移1 / 5G 偏移2（AP-175 骨架一致）
&pcie0 {
    status = "okay";
    wifi_2g: wifi@11,0 {
        compatible = "pci168c,0029";
        nvmem-cells = <&macaddr_hwinfo_1c 1>;
        nvmem-cell-names = "mac-address";
        reg = <0x8800 0 0 0 0>;
    };
    wifi_5g: wifi@12,0 {
        compatible = "pci168c,0029";
        nvmem-cells = <&macaddr_hwinfo_1c 2>;
        nvmem-cell-names = "mac-address";
        reg = <0x9000 0 0 0 0>;
    };
};

// 8) 分区 + hwinfo nvmem（对齐官方 AP-175）
//    u-boot(0x0,256k) | firmware(0x40000, 0xfa0000) | hwinfo(0xfe0000,64k) | u-boot-env(0xff0000,64k)
&spi {
    flash@0 {
        partitions {
            /*
            hwinfo: partition@fe0000 {
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
            */
        };
    };
};
```

> **路线 A（保留 RedBoot/过渡）**：分区与 nvmem 用原 RedBoot 布局，MAC 从 firmware 分区 NVRAM 偏移 0x04 读取（对应 mach 的 `0x1f040004`）。此时不用 hwinfo@0x1c。

### 5.2 image/generic.mk 定义（对齐官方 AP-175）

```makefile
define Device/maselink_ap2600ifm
  SOC := ar7161
  DEVICE_VENDOR := MASELinK
  DEVICE_MODEL := AP2600IFM
  IMAGE_SIZE := 16000k
  DEVICE_PACKAGES := kmod-usb2                  # 无外挂扩展件，仅 USB
  LOADER_TYPE := bin
  LOADER_FLASH_OFFS := 0x42000
  COMPILE := loader-$(1).bin
  COMPILE/loader-$(1).bin := loader-okli-compile
  KERNEL := kernel-bin | append-dtb | lzma | uImage lzma -M 0x4f4b4c49 | loader-okli $(1) 8128 | uImage none
  KERNEL_INITRAMFS := kernel-bin | append-dtb | lzma | loader-kernel | uImage none
endef
TARGET_DEVICES += maselink_ap2600ifm
```

> **修正对照**：v3 文档的 `IMAGE_SIZE := 16064k` 及 `0xF90000` 有误，官方 AP-175 为 `16000k`（firmware 0x40000→0xfe0000 = 0xFA0000 = 16000k）。`DEVICE_PACKAGES` 去掉官方 AP-175 的扩展件（kmod-gpio-pca953x/kmod-hwmon-lm75/kmod-i2c-gpio/kmod-rtc-ds1374）。

---

## 六、仓库选择与构建可行性（已评审当前 lede）

| 检查项 | 当前 `d:\vc\lede` | 结论 |
|--------|------------------|------|
| ath79 AR7161 DTS 系列 | ✅ 存在（ap-105/buffalo/netgear/ubnt 等） | 可直接承载 |
| OKLI loader（`loader-okli`/`loader-kernel`） | ✅ image 层已支持 | 官方 AP-175 镜像可用 |
| Aruba AP-175 DTS | ❌ 缺 `ar7161_aruba_ap-175.dts` | 需从官方拉入 |
| Aruba AP-175 generic.mk | ❌ 仅有旧版 `aruba_ap-105` | 需把官方 AP-175 定义带入 |
| en 用系统 | ✅ | — |

**决策**：在**当前 `d:\vc\lede` 仓库**开发。步骤：① 拉官方 AP-175 DTS 入 `target/linux/ath79/dts/`；② 按 §5.1 改造；③ 在 `target/linux/ath79/image/generic.mk` 增加 §5.2 的完整 Device 定义（勿照抄仓库里旧版 ap-105 的短定义）。

---

## 七、实施路线图

### Phase 1：initramfs 验证（风险最低）

1. 拉取官方 AP-175 DTS 并按 §5 改造（**路线 A：先不改 bootloader/MAC**）
2. 在 `image/generic.mk` 增加 Device 定义，编译 initramfs 镜像
3. **在现有 RedBoot 下**通过 TFTP 加载测试：
   ```
   RedBoot> load -r -b %{FREEMEMLO} openwrt-initramfs.bin
   RedBoot> go
   ```
4. 验证清单：
   - [ ] 串口(115200)正常，进入 shell
   - [ ] `dmesg` 确认 PHY 扫描到哪个地址：`ls /sys/class/mdio_bus/` + `dmesg | grep -i phy`
       - **首选 @20(0x14)**；若千兆不通，改 @1（AP-175），仍不通试 @0（AP-105），并同步改 mdio0/phy-handle/phy-mask 三处
   - [ ] `iw list` 显示 phy0 + phy1 两块无线
   - [ ] 两无线 MAC 是否为 `hwinfo@0x1c` + 偏移 0/1/2（路线 A 下为 NVRAM 派生）
   - [ ] 6 个 LED 通过 `/sys/class/leds/` 可控（GPIO 0/2/3/4/5/7）
   - [ ] 复位键（GPIO 8）有反应

### Phase 2：U-Boot 刷入（路线 B）

1. 获取/编译支持 AR7161 + **64MB DDR** 的 U-Boot（推荐 Breed blank 版或 Hurricos/u-boot-ap105@ap175 改 DDR）
2. 编程器准备 16MB Flash 镜像：
   - 前 256KB：U-Boot
   - firmware（0x40000 起）：可保留原 NVRAM 或留空
   - **hwinfo（0xfe0000）：把 MAC 写入偏移 0x1c**（如 `00:03:7f:..`）
   - 末尾 64KB：u-boot-env（留空，首次 `saveenv` 生成）
3. 编程器烧录，串口验证 U-Boot 启动
4. U-Boot 下配置环境变量，或走官方 APBoot 方式（`go 0x84000040` 加载 OKLI 镜像）
5. TFTP 加载 Phase 1 的 initramfs 再次验证

### Phase 3：正式固件

1. 编译 squashfs-sysupgrade 固件
2. U-Boot 下 TFTP 或 Breed Web 刷入 `firmware` 分区
3. 验证 sysupgrade 升级流程正常
4. （可选）提交到 OpenWrt 官方

---

## 八、已知风险与保护

| 风险 | 说明 | 保护方案 |
|------|------|---------|
| **ART 丢失** | 无线校准数据丢失后无法恢复 | 本机 ART 在 mini-PCIe 卡 EEPROM，不在 Flash，**天然免疫** |
| **MAC 丢失** | hwinfo/NVRAM 被覆盖 | 编程器单独备份 hwinfo 64KB；新布局中 hwinfo 为只读分区 |
| **RedBoot 误刷** | 刷 U-Boot 覆盖错误区域 | 用编程器精控写入范围；完整备份旧 Flash |
| **DDR 初始化失败** | U-Boot 中 64MB 配错（AP-175 是 128MB） | 参考 PB44 DDR 初始化，改 64MB 时序 |
| **PHY 不通** | PHY 地址/模式错 | 已确认 PHY@20(0x14)+RGMII+强制千兆，写入 DTS |
| **无线不进系统** | 新内核 ath9k 与 AR9223 兼容性 | `pci168c,0029` 通吃，创建 AP 实测 5G |

---

## 九、待确认事项（留给开发者实测）

| 序号 | 事项 | 验证时机 |
|------|------|---------|
| 1 | **PHY 实际地址**：主选 @20；不通退 @1(AP-175)，再退 @0(AP-105) | Phase 1 initramfs：`ls /sys/class/mdio_bus/` / `dmesg | grep phy` |
| 2 | USB 供电是否需要某个 GPIO 拉高 | Phase 1 initramfs 测试时 |
| 3 | **无线实卡型号**（文档假定 2.4G=AR9220 / 5G=AR9223） | 拆机确认 |
| 4 | 无线 MAC 是否按所选布局正确派生 | Phase 1 `iw dev` 查看 |
| 5 | 5G（AR9223）在新版 ath9k 下是否完全正常 | Phase 1 创建 AP 测试 |
| 6 | 选择**路线 A（保留 RedBoot）** 还是 **路线 B（换 U-Boot）**，据此定 MAC 布局 | Phase 1 后 |
| 7 | sysupgrade 在 U-Boot 布局下是否正常工作 | Phase 3 |

---

## 十、历史时间线

| 时间 | 事件 |
|------|------|
| 2009-07 | 底板 `48RPAA05.0GB` 设计完成 |
| 2010-03 | RedBoot 编译（启动日志显示 built Mar 27 2010） |
| 2014 | anywlan 论坛首次发布 ar71xx 自编译固件 |
| 2015-2016 | 恩山论坛持续更新，修复千兆/USB/MAC |
| 2016-12 | 最后一次更新 LEDE-r2481，作者声明停止维护 |
| 2017-11 | `mach-maselink-ap2600ifm.c` 入库（作者 Weijie Gao），见 commit 0759aaa |
| 2019-06 | OpenWrt 官方废弃 ar71xx，转向 ath79 |
| 2020-08 | OpenWrt 19.07 成为 ar71xx 最后一个版本 |
| 2021-06 | coolsnowwolf/lede 删除 ar71xx 目标，文件随之消失 |
| 2022-09→2023-04 | 官方合并 Aruba AP-175 ath79 支持（PR #10794，APBoot 兼容 commit 90ad13c） |
| 现在 | **本文档 v4 修订，落地于当前 `d:\vc\lede`，需完成 AP2600IFM 的 ath79 适配** |

---

*文档修订：v4，2026-08-29*
*目标仓库：`d:\vc\lede`（coolsnowwolf/lede），平台 `ath79/generic`*
*核心策略：以官方 Aruba AP-175 的 ath79 DTS 为骨架，注入 mach-maselink-ap2600ifm.c 的硬件参数（PHY@20、64MB、GPIO），MAC 布局由所选 bootloader 决定*
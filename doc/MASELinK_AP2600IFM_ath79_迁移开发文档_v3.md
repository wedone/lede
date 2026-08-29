# MASELinK AP2600IFM → OpenWrt ath79 迁移开发文档

> 本文档面向具备 OpenWrt 内核开发经验的专业开发者/AI，梳理需求、硬件事实、参考资源及迁移路径。代码以伪代码/关键片段呈现，完整实现需开发者补充。
> 
> **仓库选择**：开发者应根据实际仓库状态（官方 OpenWrt 或第三方分支）判断可行性，本文档不强制指定具体仓库。

---

## 一、需求概述

| 项目 | 说明 |
|------|------|
| **目标设备** | Comba（京信）MASELinK AP2600-IFM |
| **目标平台** | OpenWrt `ath79/generic`（最新稳定版或主分支） |
| **现状** | 仅有 ar71xx 时代的社区支持（LEDE-r2481，2016 年停止维护），官方从未收录 ath79 |
| **核心诉求** | 彻底迁移到 ath79，弃用 RedBoot，改用现代 U-Boot，实现标准 sysupgrade 流程 |
| **关键约束** | 底板与 Aruba AP-175 完全相同，仅内存和扩展模块不同；可利用 AP-175 的 ath79 实现作为骨架 |

---

## 二、硬件规格（已完全确认）

### 2.1 核心组件

| 组件 | 芯片/规格 | 备注 |
|------|----------|------|
| SoC | Atheros AR7161 rev 2 @ 680 MHz (MIPS 24Kc) | — |
| 内存 | ESMT M13S2561616A × 2 = **64MB DDR** | AP-175 为 128MB，这是核心差异 |
| Flash | MX25L12805D 或 S25SL12801 = **16MB SPI-NOR** | 两种芯片可互换 |
| 有线 PHY | IC+ IP1001，**MDIO 地址 @20** | 单口千兆，无交换芯片 |
| 2.4G 无线 | Atheros AR9220 (PCIe 00:11.0, IRQ 72) | mini-PCIe，ath9k 驱动 |
| 5G 无线 | Atheros AR9223 (PCIe 00:12.0, IRQ 73) | mini-PCIe，ath9k 驱动 |
| 串口 | 16550A @ MMIO 0x18020000, IRQ 11 | JP1 排针：3=RX, 5=TX, 6=GND, 波特率 115200 |
| USB | 支持 | 旧固件已加载，供电 GPIO 待确认 |

### 2.2 底板信息

| 项目 | 内容 |
|------|------|
| 底板型号 | `48RPAA05.0GB` (2009/07/30) |
| 参考设计 | Atheros PB44 |
| 与 Aruba AP-175 关系 | **底板完全相同**，AP-175 的所有扩展模块（TCA6416 GPIO 扩展、LM75 温度传感器、DS1374C RTC、24C256 EEPROM、CP210X USB 串口）均为外挂模块，本设备未焊接 |

### 2.3 GPIO 定义（来自 ar71xx mach 文件）

```
LED:  GPIO 0  = green:d24      (active_low)
LED:  GPIO 2  = green:rf1      (active_low)
LED:  GPIO 3  = green:d24top   (active_low)
LED:  GPIO 4  = green:rf2      (active_low)
LED:  GPIO 5  = green:rf2top   (active_low)
LED:  GPIO 7  = green:rf1top   (active_low)
KEY:  GPIO 8  = reset          (active_low, 去抖 60ms)
```

### 2.4 网口参数（来自 ar71xx mach 文件）

```
PHY 地址: 20 (0x14)
接口模式: RGMII
速率:     1000 Mbps 全双工
PLL 1000: 0x00110000
PLL 100:  0x0001099
PLL 10:   0x00991099
```

---

## 三、Flash 分区规划（现代化 U-Boot 布局）

> 目标：弃用 RedBoot，改用标准 U-Boot 分区表，使 sysupgrade 直接可用。

| 分区 | 起始 | 结束 | 大小 | 说明 |
|------|------|------|------|------|
| `u-boot` | `0x00000000` | `0x00040000` | 256 KB | 现代 U-Boot（需自行编译/获取） |
| `u-boot-env` | `0x00040000` | `0x00050000` | 64 KB | 环境变量（可读写） |
| `firmware` | `0x00050000` | `0x00FE0000` | ~15.3 MB | OpenWrt 内核 + squashfs rootfs |
| `board-config` | `0x00FE0000` | `0x00FF0000` | 64 KB | **MAC 地址（偏移 0x00，6 字节）** |

### 关于 board-config 的关键事实

- 旧固件（RedBoot 时代）中 `board-config` 仅存放设备序列号和型号字符串，MAC 是从 `firmware` 分区内的 NVRAM 区域（偏移 `0x4`）动态解析的
- **现代化改造**：将 MAC 地址（如 `00:03:7f:ff:ff:fe`）写入 `board-config` 偏移 `0x00`，DTS 中通过 `nvmem-cells` 固定偏移读取
- 无线 ART 校准数据**不存在于 Flash**，两张 mini-PCIe 卡（AR9220/AR9223）自带 EEPROM，`ath9k` 驱动自动读取

---

## 四、参考资源清单

### 4.1 核心参考文件（ath79 骨架）

| 资源 | 地址 | 用途 |
|------|------|------|
| Aruba AP-175 DTS | `target/linux/ath79/dts/ar7161_aruba_ap-175.dts` | **直接复制为骨架**，底板相同，仅需修改内存、分区表、GPIO、删扩展模块 |
| Aruba AP-175 硬件信息 | https://openwrt.org/toh/aruba/ap-175 | 确认底板型号、芯片组合、扩展模块清单 |
| Aruba AP-175 固件下载 | https://firmware-selector.openwrt.org/?version=24.10.0&target=ath79%2Fgeneric&id=aruba_ap-175 | 验证 ath79 对该芯片组合的支持状态 |

> **注意**：部分第三方分支（如某些国内 fork）可能已移除 Aruba AP-175 的支持。若目标仓库中不存在此文件，开发者应从 OpenWrt 官方仓库获取该骨架文件，或基于 `ar7100.dtsi` 从头构建。

### 4.2 历史参考文件（ar71xx 参数来源）

| 资源 | 地址 | 用途 |
|------|------|------|
| `mach-maselink-ap2600ifm.c` | https://github.com/Macuilxochitl/lede/blob/d4cf545/target/linux/ar71xx/files/arch/mips/ath79/mach-maselink-ap2600ifm.c | **提取 GPIO、PHY 地址、PLL 参数** |
| `mach-maselink-ap2600i.c` | https://github.com/Macuilxochitl/lede/blob/d4cf545/target/linux/ar71xx/files/arch/mips/ath79/mach-maselink-ap2600i.c | ⚠️ **不同设备（AR9344），仅作对比，勿混淆** |
| 添加 commit | https://github.com/coolsnowwolf/lede/commit/0759aaa96d5adb9f68bd730e9e02a7485500547f | 确认文件添加时间（2017-11-25） |

### 4.3 U-Boot 资源

| 资源 | 说明 |
|------|------|
| **Breed（推荐）** | 国内 hackpascal 开发的不死 U-Boot，支持 AR7161/PB44，Web 界面刷机。需在恩山/Right.com.cn 搜索 "Breed PB44" 或 "Breed AR7161" |
| OpenWrt 19.07 uboot-ar71xx | `package/boot/uboot-ar71xx/` 中有 PB44 官方 U-Boot 源码，需修改 DDR 为 64MB |
| U-Boot 官方历史版 | `git checkout v2014.10` 或 `v2015.10`，`board/ar71xx/pb44/`，需修改 DDR 初始化 |

> **Breed 版本说明**：`breed-ar7161-blank.bin` 为通用空白模板，不绑定任何特定设备，GPIO/PHY/MAC 均为默认值，需手动配置环境变量。对于冷门设备是唯一可用版本。

### 4.4 辅助工具

| 资源 | 用途 |
|------|------|
| `pepe2k/ar9300_eeprom` | ART 分区结构分析工具（GitHub） |
| `art-collection` | 社区收集的 Atheros ART 转储（GitHub），应急恢复用 |

---

## 五、DTS 适配要点（伪代码/关键片段）

### 5.1 基于 AP-175 的修改清单

```dts
// 若目标仓库存在 ar7161_aruba_ap-175.dts，复制为骨架
// 若不存在，基于 ar7100.dtsi 从头构建

/ {
    compatible = "maselink,ap2600ifm", "qca,ar7161";
    model = "MASELinK AP2600IFM";

    chosen {
        bootargs = "console=ttyS0,115200 mem=64M";
    };

    // 内存改为 64MB
    memory@0 {
        device_type = "memory";
        reg = <0x0 0x04000000>;
    };

    // LED: GPIO 0/2/3/4/5/7，全部 active_low
    // 复位键: GPIO 8，active_low
};

// 删除: i2c0 节点、gpio_ext (TCA6416)、所有 &gpio_ext LED、
//       temp-sensor@4a (LM75)、eeprom@50 (24C256)、rtc@68 (DS1374C)

&mdio0 {
    // PHY 地址改为 20 (0x14)
    phy20: ethernet-phy@14 { reg = <0x14>; };
};

&eth0 {
    phy-handle = <&phy20>;
    pll-data = <0x00110000 0x0001099 0x00991099>;
    // MAC 从 board-config 偏移 0x00 读取
};

&pcie0 {
    // 无线不指定 nvmem-cells 提供 ART
    // ath9k 从 mini-PCIe 卡 EEPROM 自动读取 MAC 和 ART
    // 但可指定 nvmem-cells 从 board-config 提供 MAC（如需要）
};

&spi {
    // 分区表改为标准 U-Boot 布局
    // u-boot (256K) | u-boot-env (64K) | firmware (~15.3M) | board-config (64K)
    // board-config 中定义 nvmem-layout，macaddr@0，reg=<0x0 0x6>
};
```

### 5.2 image/generic.mk 定义

```makefile
define Device/maselink_ap2600ifm
  SOC := ar7161
  DEVICE_VENDOR := MASELinK
  DEVICE_MODEL := AP2600IFM
  DEVICE_PACKAGES := kmod-usb-ohci kmod-usb2
  IMAGE_SIZE := 16064k  # 0xF90000
endef
TARGET_DEVICES += maselink_ap2600ifm
```

---

## 六、仓库选择策略

> 本文档不强制指定具体仓库。开发者应根据以下因素综合判断：

| 因素 | 官方 OpenWrt | 第三方分支（如 coolsnowwolf/lede） |
|------|-------------|-----------------------------------|
| Aruba AP-175 支持 | ✅ 完整存在 | ⚠️ 部分分支已移除，需从官方借文件或从头构建 |
| 编译环境 | 标准，国际友好 | 国内优化，软件包更全 |
| 代码干净度 | 最干净，无额外 patch | 含大量国内特有 patch，可能干扰 |
| 提交官方 PR | 直接可行 | 需额外整理 |
| 维护活跃度 | 最高 | 取决于具体分支 |

**建议**：
- 若目标仓库已包含 `ar7161_aruba_ap-175.dts`，直接在该仓库上开发
- 若目标仓库缺少此文件但包含其他 ath79 AR7161 设备，可从官方仓库下载该文件放入对应位置
- 若目标仓库 ath79 支持不完整，考虑切换到官方仓库

---

## 七、实施路线图

### Phase 1：initramfs 验证（风险最低）

1. 准备骨架文件（复制 AP-175 DTS 或基于 `ar7100.dtsi` 构建）
2. 按第 5 节修改 DTS
3. 添加 `image/generic.mk` 定义
4. 编译 initramfs 镜像
5. **在现有 RedBoot 下**，通过 TFTP 加载测试：
   ```
   RedBoot> load -r -b %{FREEMEMLO} openwrt-initramfs.bin
   RedBoot> go
   ```
6. 验证清单：
   - [ ] 串口正常，进入 shell
   - [ ] `ifconfig eth0` 千兆协商正常
   - [ ] `iw list` 显示 phy0(2.4G) + phy1(5G)
   - [ ] `iw dev wlan0 scan` 能扫描周围 WiFi
   - [ ] 6 个 LED 可通过 `/sys/class/leds/` 控制
   - [ ] 复位键（GPIO 8）有反应

### Phase 2：U-Boot 刷入

1. 获取/编译支持 AR7161 + 64MB DDR 的 U-Boot（推荐 Breed blank 版）
2. 用编程器准备 16MB Flash 镜像：
   - 前 256KB：U-Boot
   - 256K~320K：u-boot-env（留空，首次启动后 `saveenv` 生成）
   - 最后 64KB（0xFE0000）：写入 MAC 地址到偏移 0x00 的 board-config
3. 编程器烧录，串口验证 U-Boot 启动
4. U-Boot 下配置环境变量：
   ```
   setenv ethaddr 00:03:7f:ff:ff:fe
   setenv bootcmd 'sf probe; sf read 0x80060000 0x50000 0x300000; bootm 0x80060000'
   saveenv
   ```
5. TFTP 加载 Phase 1 的 initramfs 再次验证

### Phase 3：正式固件

1. 编译 squashfs-sysupgrade 固件
2. U-Boot 下通过 TFTP 或 Breed Web 刷入 `firmware` 分区
3. 验证 sysupgrade 升级流程正常
4. 提交到 OpenWrt 官方（可选）

---

## 八、已知风险与保护

| 风险 | 说明 | 保护方案 |
|------|------|---------|
| **ART 丢失** | 无线校准数据丢失后无法自行恢复 | 本设备 ART 在 mini-PCIe 卡 EEPROM 中，不在 Flash，**天然免疫此风险** |
| **MAC 丢失** | board-config 被覆盖 | 编程器单独备份 board-config 64KB；新布局中 board-config 为只读分区 |
| **RedBoot 误刷** | 刷 U-Boot 时覆盖错误区域 | 用编程器操作，精确控制写入范围；完整备份旧 Flash |
| **DDR 初始化失败** | U-Boot 中 64MB 配置错误导致无法启动 | 参考 PB44 的 DDR 初始化代码，确认内存时序参数 |
| **网口不通** | PHY 地址或 PLL 配置错误 | 已确认 PHY @20 和 PLL 参数，直接写入 DTS |

---

## 九、待确认事项（留给开发者实测）

| 序号 | 事项 | 验证时机 |
|------|------|---------|
| 1 | USB 供电是否需要某个 GPIO 拉高 | Phase 1 initramfs 测试时 |
| 2 | 无线 MAC 是否自动从卡 EEPROM 正确读取 | Phase 1 中 `iw dev` 查看 MAC |
| 3 | 5G 无线（AR9223）在新版 ath9k 下是否完全正常 | Phase 1 中创建 AP 测试 |
| 4 | sysupgrade 在 U-Boot 布局下是否正常工作 | Phase 3 |
| 5 | 目标仓库是否包含 Aruba AP-175 的 ath79 支持 | 开发前确认 |

---

## 十、历史时间线

| 时间 | 事件 |
|------|------|
| 2009-07 | 底板 `48RPAA05.0GB` 设计完成 |
| 2010-03 | RedBoot 编译（设备启动日志显示 built Mar 27 2010） |
| 2014 | anywlan 论坛首次发布 ar71xx 自编译固件 |
| 2015-2016 | 恩山论坛持续更新，修复千兆/USB/MAC |
| 2016-12 | 最后一次更新 LEDE-r2481，作者声明停止维护 |
| 2017-11-25 | coolsnowwolf/lede 添加 `mach-maselink-ap2600ifm.c`（commit 0759aaa） |
| 2019-06 | OpenWrt 官方废弃 ar71xx，转向 ath79 |
| 2020-08 | OpenWrt 19.07 成为 ar71xx 最后一个版本 |
| 2021-06 | coolsnowwolf/lede 删除 ar71xx 目标，文件随之消失 |
| 现在 | **需要社区自行完成 ath79 适配** |

---

*文档整理时间：2026-08-29*
*目标平台：OpenWrt ath79/generic*
*核心策略：以 Aruba AP-175 的 ath79 DTS 为骨架，注入 ar71xx 时期的硬件参数，现代化分区表和 Bootloader*

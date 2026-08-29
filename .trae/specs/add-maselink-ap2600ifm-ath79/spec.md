# MASELinK AP2600IFM ath79 移植 Spec

## Why
Comba MASELinK AP2600IFM（AR7161）仅有 2016 年停更的 ar71xx 支持，需在当前 `d:\vc\lede` 仓库完成 ath79 移植，使设备可运行现代 OpenWrt 并产出可刷写镜像。

## What Changes
- 新增 `target/linux/ath79/dts/ar7161_maselink_ap2600ifm.dts`：以**官方 openwrt master 的 `ar7161_aruba_ap-175.dts`** 为唯一骨架，注入 `mach-maselink-ap2600ifm.c` 的硬件参数
- 修改 `target/linux/ath79/image/generic.mk`：新增 `Device/maselink_ap2600ifm` 定义（对齐官方 AP-175 的 OKLI loader 完整定义）
- 编译验证：产出 ath79/generic 下含 maselink_ap2600ifm 的 initramfs 与 squashfs-sysupgrade 镜像

### 明确排除
- **完全不参考/不引用本仓库的 `aruba_ap-105` 任何内容**——与目标硬件毫无关联（PHY/LED/串口速率均不同）
- 不刷 bootloader（U-Boot/Breed 刷写由开发者后续自行操作，不在本 spec 代码范围内）
- 不修改仓库任何既有设备定义

### 关键技术决策（记录理由）
- **保留官方 AP-175 的 OKLI loader 定义（照抄不改动）**：OKLI 是官方为 APBoot 加载限制做的已验证方案；照抄完整定义比自创标准 uImage 更稳，且对将来 Breed/APBoot 均兼容。RedBoot 下 initramfs 验证走 `KERNEL_INITRAMFS`，不受影响。
- **ath79 无 `phy-mask` DTS 属性**（那是 ar71xx 机制）：PHY 地址仅通过 `ethernet-phy@14 { reg = <0x14>; }` 声明，勿照搬 v4 文档伪代码中的 phy-mask。
- **hwinfo 分区保留 nvmem-layout 定义但 eth0/无线暂不引用**：刻意为之。当前 flash 0xfe0000 处是旧板信息（非 MAC），MAC 派生待 bootloader 路线确定后再启用 nvmem-cells 引用。

## Impact
- Affected specs: 无（新设备移植）
- Affected code:
  - `target/linux/ath79/dts/ar7161_maselink_ap2600ifm.dts`（新增）
  - `target/linux/ath79/image/generic.mk`（追加一个 Device 块）

## ADDED Requirements

### Requirement: 设备树 ar7161_maselink_ap2600ifm.dts
系统 SHALL 提供基于官方 AP-175 DTS 骨架改造的设备树，参数以 `mach-maselink-ap2600ifm.c` 为唯一权威：

- `compatible = "maselink,ap2600ifm", "qca,ar7161"`，`model = "MASELinK AP2600IFM"`
- `bootargs = "console=ttyS0,115200 mem=64M"`；`memory@0` reg 为 `<0x0 0x04000000>`（64MB）
- LED：6 个，GPIO 0/2/3/4/5/7，全部 `GPIO_ACTIVE_LOW`，label 为 green:d24 / green:rf1 / green:d24top / green:rf2 / green:rf2top / green:rf1top；`led-boot/led-failsafe/led-upgrade` 指向 d24
- 按键：reset @ GPIO 8，`GPIO_ACTIVE_LOW`，`KEY_RESTART`
- 删除骨架中的：i2c0 节点、gpio_ext(TCA6416)、LM75、24C256 EEPROM、DS1374 RTC 及所有 `&gpio_ext` 引用
- MDIO：`phy@14 { reg = <0x14>; }`（首选 @20；注释标注备用 @1=官方 AP-175 板值，切换时 reg/phy-handle 同步改）
- eth0：`phy-handle` 指向 phy@14，`phy-mode = "rgmii"`，`pll-data = <0x00110000 0x00001099 0x00991099>`
- PCIe 无线：`wifi@11,0` 与 `wifi@12,0`，compatible 均为 `"pci168c,0029"`，reg 分别 `<0x8800 0 0 0 0>` / `<0x9000 0 0 0 0>`；MAC 派生暂不接 nvmem（注释保留 hwinfo@1c 方案，待 bootloader 路线确定后启用）
- SPI Flash 分区（对齐官方 AP-175 布局）：`u-boot`(0x0,0x40000,只读) / `firmware`(0x40000,0xfa0000,denx,uimage) / `hwinfo`(0xfe0000,0x10000,只读,含 macaddr@1c mac-base nvmem-layout) / `u-boot-env`(0xff0000,0x10000,只读)

#### Scenario: DTS 编译通过
- WHEN 构建 ath79/generic 且包含 maselink_ap2600ifm
- THEN dtc 无 error，`build_dir` 中生成对应 dtb

### Requirement: Device 定义 generic.mk
`target/linux/ath79/image/generic.mk` SHALL 追加（对齐官方 AP-175 定义，去掉扩展件包）：

```makefile
define Device/maselink_ap2600ifm
  SOC := ar7161
  DEVICE_VENDOR := MASELinK
  DEVICE_MODEL := AP2600IFM
  IMAGE_SIZE := 16000k
  DEVICE_PACKAGES := kmod-usb2
  LOADER_TYPE := bin
  LOADER_FLASH_OFFS := 0x42000
  COMPILE := loader-$(1).bin
  COMPILE/loader-$(1).bin := loader-okli-compile
  KERNEL := kernel-bin | append-dtb | lzma | uImage lzma -M 0x4f4b4c49 | loader-okli $(1) 8128 | uImage none
  KERNEL_INITRAMFS := kernel-bin | append-dtb | lzma | loader-kernel | uImage none
endef
TARGET_DEVICES += maselink_ap2600ifm
```

#### Scenario: 镜像生成
- WHEN 编译 ath79/generic（含 maselink_ap2600ifm）
- THEN `bin/targets/ath79/generic/` 产出含 `maselink_ap2600ifm` 的 initramfs 镜像与 squashfs-sysupgrade 镜像

## MODIFIED Requirements
无（纯新增设备，不修改既有设备）。

## REMOVED Requirements
无。

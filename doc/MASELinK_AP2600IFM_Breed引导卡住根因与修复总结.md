# MASELinK AP2600IFM — Breed 引导卡住根因与修复总结

> 时间：2026-08-30
> 状态：代码已修复（commit `66f57dd72`），正在 GitHub Actions 重建验证
> 关联：《迁移开发文档\_v4》、《交接文档》

***

## 一、现象

设备已刷入 **Breed**（`breed-ar7161-blank.bin`，软件见 `doc/breed.md`），固件按 Breed **ATH-SDK-16MB** 布局（firmware 从 `0x50000` 起）刷入后，**引导一直卡住跑不到内核**。

此前固件编译链沿用了官方 Aruba AP-175 骨架的 **OKLI loader** 方案（`KERNEL := ... uImage lzma -M 0x4f4b4c49 | loader-okli ... | uImage none` + `LOADER_FLASH_OFFS` + 硬编码 `CONFIG_FLASH_OFFS=0x52000`）。

## 二、根因（第一性原理分析）

### 2.1 OKLI 为什么存在

OKLI（OpenWrt Kernel Loader，`0x4f4b4c49`）是**为了兼容 Aruba 原厂 APBoot 引导器**而设计的：

* Aruba APBoot 对固件头有 **16 字节**的格式限制，装不下完整标准 uImage 头。

* 因此用 OKLI 这个微型 loader 去绕开 APBoot 的限制，由 OKLI 从 flash 读出并解压内核。

文档《迁移开发文档\_v4》以"复刻 AP-175"为出发点，把 OKLI 方案一并照搬到了本设备。

### 2.2 为什么 Breed 场景下 OKLI 会卡死

1. **Breed 不需要 OKLI**：Breed 自带 LZMA 解压器，原生识别并引导**标准 uImage**（magic `0x27051956`），加载地址从 uImage header 读取（`KERNEL_LOADADDR = 0x80060000`）。绝大多数刷 Breed 的 ath79 设备都是标准 uImage 直接启动，零额外配置。
2. **OKLI 的固有脆弱点**：OKLI loader 运行后，要根据**编译时硬编码的 flash 绝对偏移** **`CONFIG_FLASH_OFFS`** 去 flash 里读出压缩内核。该偏移受"固件写入位置 + 内核在固件内偏移"双重影响，一旦布局或编译产物变化就错位，随即**解压失败 → 引导卡死**。

> 结论：**卡住的根源不是硬件、不是 DTS，而是"在 Breed 上错误地沿用 OKLI 引导链"。** OKLI 只为 Aruba APBoot 存在，与 Breed 无关，且其"绝对 flash 偏移读内核"正是最脆弱的一环。

## 三、修复方案

既然已上 Breed，就**彻底去掉 OKLI，回落标准 uImage**：

```makefile
define Device/maselink_ap2600ifm
  SOC := ar7161
  DEVICE_VENDOR := MASELinK
  DEVICE_MODEL := AP2600IFM
  IMAGE_SIZE := 15872k
  DEVICE_PACKAGES := kmod-usb2
  # 回落 ath79 默认 KERNEL：
  # kernel-bin | append-dtb | lzma | uImage lzma
endef
TARGET_DEVICES += maselink_ap2600ifm
```

回落默认后实际生效（来自 `target/linux/ath79/image/Makefile` 的 `Device/Default`）：

* `KERNEL := kernel-bin | append-dtb | lzma | uImage lzma`（**标准 uImage**）

* `KERNEL_INITRAMFS := kernel-bin | append-dtb | lzma | uImage lzma`

* `KERNEL_LOADADDR = 0x80060000`

删除的关键项：`LOADER_TYPE`、`LOADER_FLASH_OFFS := 0x52000`、`COMPILE`、`COMPILE/loader-$(1).bin`、自定义 `KERNEL`、自定义 `KERNEL_INITRAMFS`。

## 四、落地事实核对

* DTS 分区（Breed ATH-SDK-16MB）**保持正确，无需改动**：

  * `u-boot` `0x0–0x50000`（320K，放 Breed）

  * `firmware` `0x50000–0xfe0000`（`0xf90000` = **15872k** = `IMAGE_SIZE`，一致）

  * `hwinfo` `0xfe0000`、`u-boot-env` `0xff0000`

* 真机实测项（远程已修复并保留）：PHY = **@1**（IP1001，PHY ID `0x02430d91`）、独立 `mdio_syscon@19000000` 消除 eth0 -EBUSY。

## 五、验证方式（Breed 直刷）

1. sysupgrade 固件头应为 **uImage magic** **`0x27051956`**（不再是 OKLI 的 `0x4f4b4c49`）。
2. Breed WebUI 选 **ATH-SDK-16MB** 布局，上传 `openwrt-ath79-generic-maselink_ap2600ifm-squashfs-sysupgrade.bin` 直刷。
3. autoboot 从 `0x50000` 引导，串口 `115200` 应能看到内核输出直到进入 shell。
4. 应急验证用 initramfs：`openwrt-...-initramfs-kernel.bin`（也是标准 uImage）。

## 五·五、真机验证结果（2026-08-30，✅ 全部通过）

> 固件：GitHub Actions run `33303561565`（commit `66f57dd72`，标准 uImage 去 OKLI）
> 烧录：Breed shell `wget` + `flash erase 0x50000 0x9a0000` + `flash write 0x50000 0x80000000 0x9a0000`
> 产物目录：`bin/ap2600ifm/dl5/`（`openwrt-ath79-generic-maselink_ap2600ifm-squashfs-sysupgrade.bin`，10,027,811 B）

启动日志关键行：

```
MIPS: machine is MASELinK AP2600IFM
Creating 4 MTD partitions on "spi0.0":   (u-boot/firmware/hwinfo/u-boot-env 与 DTS 全一致)
ag71xx-legacy 19000000.eth: connected to PHY at mdio.0:01 [uid=02430d91, driver=Generic PHY]
eth0: Atheros AG71xx at 0xb9000000, irq 4, mode: rgmii
eth0: link up (1000Mbps/Full duplex)      ← PHY 完全正常，千兆协商成功
ieee80211 phy0: Atheros AR9280 Rev:2      ← 无线模块识别成功
Please press Enter to activate this console.  ← 进入 OpenWrt shell
```

连通性实测：

| 项目               | 结果                        |
| ---------------- | ------------------------- |
| 系统 shell         | ✅ `root@LEDE` 可交互         |
| /proc/mtd        | ✅ 6 分区与 DTS 一致            |
| eth0 状态          | ✅ UP, br-lan=192.168.1.1  |
| 电脑→设备 ping       | ✅ 192.168.1.1 4/4 全通 <1ms |
| SSH(22)/HTTP(80) | ✅ 端口均开放，SSH 握手成功（ED25519） |
| 无线               | ✅ wlan0 存在（AR9280）        |

**结论**：OKLI 链路移除后，Breed 直刷 + 标准 uImage 方案在真机完全成功；eth0 PHY（mdio.0:01 = IP1001）与无线均工作正常。之前百年卡住的 OKLI 引导问题、mdio -EBUSY、PHY 地址错误三个阻塞点全部解决。

## 六、经验教训（供后续/复用）

* **"沿用骨架"不等于"沿用引导方案"**：AP-175 的 OKLI 是为其 Aruba APBoot 服务的；换 bootloader 时必须重新审视引导链，而非照搬。

* **Breed 设备 = 标准 uImage**：Breed 原生解压 LZMA，没必要为它再包一层 loader；多了 loader 就多一个"绝对 flash 偏移"的适配点。

* **先釜底抽薪，再查细节**：卡住时先质疑"这个引导链真的必要吗"，而不是反复调 loader 偏移（`0x52000` 的"fallback"正是这条弯路）。

* 文档（v4/交接）里 OKLI/RedBoot 相关表述已在新版交接文档中标注为 **Breed 阶段过时**，以交接文档顶部"★当前状态"表为准。


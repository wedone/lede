# MASELinK AP2600IFM ath79 移植 — 交接文档（给下一个 AI 会话）

> **如何恢复上下文**：新会话中让 AI 依次读取以下文件，即可完整接续本任务——
> 1. `d:\vc\lede\doc\MASELinK_AP2600IFM_ath79_迁移开发文档_v4.md`（需求/硬件/架构权威）
> 2. `d:\vc\lede\.trae\specs\add-maselink-ap2600ifm-ath79\spec.md` + `tasks.md` + `checklist.md`（已批准规格，Tasks/Checklist 已全部勾选）
> 3. 本文件（当前最新状态 + 下一步真机验证步骤）

---

## ★ 当前状态（2026-08-30，Breed 阶段）— 以下为准，旧 OKLI 思路已过时

**设备实物进展**：已刷入 **Breed**（`breed-ar7161-blank.bin`，见 `doc/breed.md`）。

**关键决策（第一性原理重新确认，含代码 commit `66f57dd72`）**：

| 主题 | 结论 |
|------|------|
| **引导格式** | 用 **标准 uImage**（`kernel-bin \| append-dtb \| lzma \| uImage lzma`）。Breed 自带 LZMA 解压、原生认 uImage magic `0x27051956`、按 header 的 `0x80060000` 加载。**彻底去掉 OKLI**（`0x4f4b4c49` / `loader-okli` / `CONFIG_FLASH_OFFS`）——OKLI 只为 Aruba 原厂 APBoot 存在，与 Breed 无关，且其"从 flash 绝对偏移读内核"正是引导卡死的根源。 |
| **flash 布局** | 对齐 **Breed ATH-SDK-16MB**：u-boot `0x0–0x50000`、firmware `0x50000` 起、`IMAGE_SIZE=15872k`。Breed WebUI 选 ATH-SDK-16MB 布局可直接直刷 sysupgrade，autoboot 从 `0x50000` 引导。 |
| **PHY 地址** | Breed mdio 实测 = **@1**（IP1001，PHY ID `0x02430d91`），地址 20 无响应。已改 DTS（与官方 AP-175 的 @1 一致）。 |
| **mdio/eth0 冲突** | 独立 `mdio_syscon@19000000`（不带 resets），消除 -EBUSY。 |
| **MAC** | 仍**未启用**（DTS 注释保留 hwinfo@0x1c 方案），等引导跑通后再定。 |

> **旧文档警示**：`迁移开发文档_v4.md` 的"OKLI loader / RedBoot TFTP 验证"路线仅适用于换回 Aruba APBoot 的场景。**当前已上 Breed，一律以本表为准。**

**GitHub Actions**：当前分支 `ap2600`。最新 run 见 `gh run list --repo wedone/lede`。

---

## 一、任务状态总览（截至 2026-08-29，OKLI 时代的归档记录，仅供参考）

| 阶段 | 状态 |
|------|------|
| 硬件参数核实（mach-maselink-ap2600ifm.c 权威） | ✅ 完成 |
| DTS 编写 `ar7161_maselink_ap2600ifm.dts` | ✅ 完成 |
| generic.mk Device 定义 | ✅ 完成 |
| GitHub Actions 单设备构建 | ✅ 成功（run 33236200734，52m02s） |
| **真机验证（RedBoot 加载 initramfs）** | ⏳ **待设备在家实测** |

**已提交**：commit `d94936e8a`，分支 `ap2600`，远端 `origin`（https://github.com/wedone/lede.git）。

## 二、仓库当前改动（全部已 commit）

| 文件 | 说明 |
|------|------|
| `target/linux/ath79/dts/ar7161_maselink_ap2600ifm.dts` | 设备树，175 行，新建 |
| `target/linux/ath79/image/generic.mk`（1519-1532 行） | `Device/maselink_ap2600ifm` 定义，插入 librerouter 与 meraki 之间 |
| `.github/workflows/build-ap2600ifm.yml` | 专用 workflow：push ap2600 / workflow_dispatch 触发，产物上传 |

> 未提交（untracked）：`.trae/`、`doc/*.md`、下载的产物 `bin/ap2600ifm/`

## 三、构建产物（已下载至 `d:\vc\lede\bin\ap2600ifm\`）

- **initramfs（RedBoot 验证用）**：`openwrt-ath79-generic-maselink_ap2600ifm-initramfs-kernel.bin`，9,258,626 B，uImage magic `0x27051956`，Linux 6.6.152
- **sysupgrade（正式刷写）**：`openwrt-ath79-generic-maselink_ap2600ifm-squashfs-sysupgrade.bin`，10,027,811 B
- 另含 `profiles.json`、`sha256sums`、`*.manifest`、`*.ipk`（含 kmod-ath9k / kmod-usb2）

## 四、硬件关键参数（真机验证时对照）

- **串口**：JP1 排针 3=RX, 5=TX, 6=GND，波特率 **115200**
- **SoC**：AR7161 @ 680MHz（MIPS 24Kc），memory 64MB（DTS `mem=64M`）
- **PHY**：IC+ IP1001，DTS 主选 MDIO **@20(0x14)**；若实测不通改 **@1**(=官方 AP-175 板值)，`reg` 与 `&eth0 phy-handle` 同步改（注释已标注）
- **LED（6 个，active_low）**：GPIO 0=d24 / 2=rf1 / 3=d24top / 4=rf2 / 5=rf2top / 7=rf1top
- **复位键**：GPIO 8，active_low
- **无线**：mini-PCIe AR9220（2.4G）/ AR9223（5G），compatible 均 `"pci168c,0029"`

## 五、下一步：真机验证（待设备在家）

### 5.1 RedBoot 内存加载（不写 flash，零风险）
```
RedBoot> load -r -b %{FREEMEMLO} openwrt-ath79-generic-maselink_ap2600ifm-initramfs-kernel.bin
RedBoot> go
```

### 5.2 验证清单
- [ ] 进入 shell，串口 115200 正常
- [ ] `dmesg | grep -i phy` 确认 PHY 扫描地址（预期 @20；若不通改 @1 重新编译）
- [ ] `iw list` 显示 phy0 + phy1 两块无线
- [ ] `ls /sys/class/leds/` 见 6 个 LED（green:d24/rf1/d24top/rf2/rf2top/rf1top）
- [ ] 复位键 GPIO 8 有反应
- [ ] `cat /proc/meminfo | grep MemTotal` ≈ 65536 kB（64MB）
- [ ] USB 是否可用 / 是否需要外部供电 GPIO（待实测）

## 六、遗留待决策/待实测（勿在代码里臆断）

| 项 | 说明 |
|----|------|
| 路线 A vs B | 当前实现=**路线 A 兼容布局**（DTS 分区已含 u-boot/firmware/hwinfo/u-boot-env，但 MAC 经 hwinfo@0x1c 的 **nvmem 引用暂未接入**，eth0/无线不带 nvmem-cells）。选 B（换 U-Boot）时再启用 MAC 派生 |
| MAC 布局 | 决策见 `迁移开发文档_v4.md` §3；与 bootloader 选择强相关，真机阶段再定 |
| 无线实卡型号 | 拆机确认真实 AR9220/AR9223，PR 阶段需按实际型号定 subsystem |
| USB 供电 GPIO | 待实测 |
| PHY@20 vs @1 | 若 @20 千兆不通，改 @1 重编译 |

## 七、给下一个 AI 的执行提示

- 目标仓库 `d:\vc\lede`（coolsnowwolf/lede fork），分支 `ap2600`，Linux 6.6.152
- 硬件参数**唯一权威**是旧 `mach-maselink-ap2600ifm.c`（作者 Weijie Gao = Breed 作者）；**严禁参考 aruba_ap-105**（硬件无关）
- Web 访问 GitHub Actions：https://github.com/wedone/lede/actions（需登录 wedone 账号，或直接用 `gh` CLI）
- 重新构建触发方式：改代码后 `git push origin ap2600`，或 web 端 workflow_dispatch

## 八、真机实测进展（2026-08-30，已接管串口 COM4@115200）

### 8.1 当前可运行状态
- **Breed (breed-ar7161-blank, r1416) → 自定义 ath79 OpenWrt 24.10.5 / 6.6.152 已成功启动**
- 分区（登录后 `/proc/mtd`）：`u-boot/firmware/kernel/rootfs/rootfs_data/hwinfo/u-boot-env`
- **WiFi(AR9280) 正常**，`br-lan=192.168.1.1`，可通过 WiFi 管理
- 遗留：**eth0 网口未通**

### 8.2 eth0 PHY 起不来的根因（已从串口 WARNING 栈精确定位）
- 现象：`ag71xx-legacy-mdio: probe of 19000000.eth:mdio failed with error -16`
  `ag71xx-legacy 19000000.eth: Could not connect to PHY device. Deferring probe.`
- 调用栈：`ag71xx_mdio_probe → device_node_get_regmap → of_syscon_register → __of_reset_control_get`
  WARNING 在 `drivers/reset/core.c:766 __reset_control_get_internal`
- 根因：mdio0 在 `ath79.dtsi` 用 `regmap = <&eth0>`，mdio 驱动把 eth0 当 syscon 解析时，
  `of_syscon_register` 会对 eth0 的 `resets`(reset#9 mac) 再做一个 **exclusive get**；
  而 ag71xx 主驱动已在 `ag71xx_main.c` 用 `devm_reset_control_get_exclusive("mac")` **独占**了同一条
  reset → 返回 `-EBUSY` → mdio 总线注册失败 → eth0 连不上 PHY。
- **修复（已提交触发 GitHub Actions）**：新增不带 resets 的独立节点 `mdio_syscon@19000000`
  （`ar7161_maselink_ap2600ifm.dts` 根下，reg=0x19000000 0x200），并让 `&mdio0 { regmap = <&mdio_syscon>; }`，
  使 mdio 不再触发 eth0 的 reset 独占解析。commit `84dd19db`（ap2600 分支）。

### 8.3 固件参数（Breed WebUI 直接刷机可行性依据）
- 当前 `openwrt-ath79-generic-maselink_ap2600ifm-squashfs-sysupgrade.bin` 头部：
  `magic=0x27051956(uImage), load=0x80060000, entry=0x80060000, name=ux-6.6.152`
- 已满足 Breed 识别 OpenWrt 的核心条件（固件以 uImage 开头 + kernel loadaddr=0x80060000 匹配 AR7161）
- **待专项**：进 Breed 后对齐其内置 firmware 分区设定（`boot flash 0x40000` 与默认 boot 变量），
  验证 WebUI“常规固件”能否一键刷；若否，需核对 Breed blank 版内置分区/启动参数并做相应适配。

### 8.4 loader 搜索偏移（OKLI 头位置）实测结论
- 现象：固件用 `loader-okli`（ath79 内嵌 lzma-loader）引导，loader 内 `CONFIG_FLASH_OFFS`
  决定从 flash 哪个偏移、按 0x1000 步进搜索 `OKLI` 内核头（`loader.c: lzma_init_data`）。
  若偏移不对会“Looking for OpenWrt image... not found”或命中旧布局残留 magic 导致 decompression failed。
- 根因：改 `generic.mk` 的 `LOADER_FLASH_OFFS` 后，GitHub Actions 增量编译缓存未重编 loader，
  真机仍用旧偏移 0x42000 搜索。
- **实测钉死偏移**：扫描 dl3 固件 `sysupgrade.bin` 大端 magic `0x4f4b4c49`，唯一命中在 **bin 偏移 0x2000**，
  对应 flash **0x52000**（firmware 起点 0x50000 + loader 段 0x1FC0 + 外层 uImage 头 0x40）。
- **修复（本次待提交）**：`generic.mk LOADER_FLASH_OFFS=0x52000`（已改）+ `config.h` 中
  `#define CONFIG_FLASH_OFFS 0x52000` 作硬编码兜底（受 `#ifndef` 保护，命令行 `-D` 优先），
  + workflow 强制清理 lzma-loader 缓存（`make target/linux/clean`、删 build_dir 下 lzma-loader）确保重编。
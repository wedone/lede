# MASELinK AP2600IFM ath79 移植 — 交接文档（给下一个 AI 会话）

> **如何恢复上下文**：新会话中让 AI 依次读取以下文件，即可完整接续本任务——
> 1. `d:\vc\lede\doc\MASELinK_AP2600IFM_ath79_迁移开发文档_v4.md`（需求/硬件/架构权威）
> 2. `d:\vc\lede\.trae\specs\add-maselink-ap2600ifm-ath79\spec.md` + `tasks.md` + `checklist.md`（已批准规格，Tasks/Checklist 已全部勾选）
> 3. 本文件（当前最新状态 + 下一步真机验证步骤）

---

## 一、任务状态总览（截至 2026-08-29）

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
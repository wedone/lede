# AP2600IFM — ar71xx(4.14) 固件 Breed 烧录与 AB 实测方案

> 适用范围：`ap2600-ar71xx` 分支 CI 产物（内核 4.14，ar71xx/generic，Breed ATH-SDK-16MB 布局）。
> 目的：与当前 ath79(6.6) 固件做同环境无线信号 AB 实测，验证"19.07 信号最强"是否真实存在。
> 本文档所有路径/格式均以本仓库代码与 CI 实测为准，刷机前请通读。

***

## 一、产物与格式（已从 legacy.mk 代码 + CI 实测核实）

| 项 | 值 |
| -- | -- |
| 产物名 | `openwrt-ar71xx-generic-maselink-ap2600ifm-squashfs-sysupgrade.bin` |
| 产物大小 | **7077892 字节**（CI run 33992447300 实测） |
| sha256 | `c0a5e4c548868ca0b065fff9a2dd8851d84c5e7eeb4183ee2a5fdcba2e20660d`（本地已核对一致） |
| 产物结构 | `[kernel 补齐到 2048k][rootfs squashfs-256k]` 纯拼接，**无** SYSUPGRADE_TAR metadata（见 `legacy.mk` `Sysupgrade/KRuImage` + `CatFiles`）；文件偏移 0 处 = uImage magic `0x27051956`，偏移 2048k 处 = `hsqs` |
| kernel | LZMA 压缩 uImage（`MkuImageLzma`），entry `0x80060000`，实测 1705212B |
| kernel cmdline | `board=maselink-ap2600ifm console=ttyS0,115200 mtdparts=spi0.0:320k(breed)ro,2048k(kernel),13824k(rootfs),15872k@0x50000(firmware) ahbskip=1`（由 patch-cmdline 写入） |
| 分区起点 | firmware **0x50000** 起（对齐 Breed ATH-SDK-16MB），kernel 落在 0x50000，rootfs 落在 0x250000 |
| 默认 LAN IP | `192.168.3.1/24`（uci-defaults 90_maselink_ap2600ifm 首次启动写入） |
| 登录凭据 | `root` / `password` |

> 说明：Breed 刷"固件"时把文件内容按二进制写入 firmware 分区起点（0x50000），本产物从文件偏移 0 起即 kernel，与 Breed 布局完全吻合，可直接刷。

***

## 二、烧录（Breed 路径）

### 前置

- PC 有线连接设备 LAN 口，网卡设静态 `192.168.1.2/24`（Breed 管理地址默认 `192.168.1.1`）。
- 从 CI artifact 下载产物（`gh run download <run_id> -n ar71xx-generic-firmware`），解压得到 sysupgrade.bin。

### 步骤

1. **进入 Breed**：设备断电 → 按住 RESET 上电 → 保持至 LAN 口灯闪（Breed 已接管）→ 松开。PC `ping 192.168.1.1` 应通。
2. 浏览器打开 `http://192.168.1.1`，进入 **固件更新** 页。
3. 固件文件选择 `openwrt-ar71xx-generic-maselink-ap2600ifm-squashfs-sysupgrade.bin`，勾选 **保留现有 Bootloader**（保持 Breed 不被覆盖）。
4. 点击更新，等待写入完成（WebUI 提示成功，Breed 自动重启）。
5. **首次启动较慢**（4.14 内核 + squashfs 展开），等待 ≥60s 后 ping `192.168.3.1`。

### 验证启动

```bash
ssh root@192.168.3.1        # password
cat /etc/openwrt_release    # 版本应为 19.07 系(SNAPSHOT) / 内核 4.14
uname -r                    # 4.14.xxx
cat /proc/mtd               # breed/kernel/rootfs/firmware 分区应可见
ip addr show br-lan         # 192.168.3.1/24
iwinfo                      # phy0/phy1 无线 UP, SSID 与 ath79 版一致
cat /sys/kernel/debug/ieee80211/phy*/ath9k/ 2>/dev/null   # ANI 状态(若 debugfs 可用)
```

### 回退

当前可用固件（ath79 6.6）随时可经 Breed 按同一流程刷回（用 `ap2600` 分支 CI 产物），或走项目既有 sysupgrade 在线流程（`doc/AP2600IFM_固件在线烧录指南.md`）。

***

## 三、AB 实测方案

> 变量控制：两版固件同机、同位置、同 SSID/信道/带宽/加密（无加密），仅固件版本不同。

### 准备

1. 刷 ar71xx 版并启动；用 `uci set wireless.@wifi-iface[0].ssid='ABTEST'` 等命令把无线参数配成与 ath79 版一致（或直接用各自默认）。
2. 记录 ath79 版基线（已留存数据）与 ar71xx 版新数据，采用**同一测量方法**。

### 测量项（各 ≥3 轮取均值）

| 项 | 方法 |
| -- | -- |
| RSSI 距离阶梯 | 固定点 1m/3m/5m/8m，终端 `iw dev <iface> link` 或 `iwinfo <iface> link` 读 RSSI |
| 吞吐 | iperf3：有线端跑 server，无线终端跑 client，2.4G/5G 各 ≥3 轮 |
| 稳定性 | 长 ping（5 分钟，丢包率）+ 重连测试 |

### 关注点

- EEPROM 天线增益两卡均为 0，发射功率两版一致（24/20dBm），**功率无差异**（已实测定论）。
- 唯一已知软差异：ANI 收敛间隔（4.14=300ms vs 6.6=1000ms），若信号体感有差，优先归因于此。
- 若 ar71xx 版明显更好 → 结论"19.07 信号更强成立"，可评估把 540-ANI 补丁（`ATH9K_ANI_POLLINTERVAL 1000→300`）移植到 6.6。

### 结果记录

| 版本 | 2.4G RSSI@5m | 5G RSSI@5m | 2.4G 吞吐 | 5G 吞吐 | 丢包率 |
| ---- | ----------- | ---------- | -------- | ------- | ----- |
| ath79 6.6 | | | | | |
| ar71xx 4.14 | | | | | |

结论：____（差异存在/不存在，来源：____）

***

## 四、风险与回退

- **Breed 无法引导**（最坏情况）：回到 Breed 刷回 ath79 版即可，设备无变砖风险（Breed 保留）。
- **rootfs 挂载失败**：现象为启动卡住/无 IP。核实 cmdline mtdparts 与分区偏移（本方案已按 0x50000 对齐，理论上无此问题）。
- **内核超 1536k**：CI 若报 "kernel is too big" 会删产物，需裁剪或调分区 budget。

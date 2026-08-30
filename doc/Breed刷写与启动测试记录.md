# MASELinK AP2600IFM — Breed 刷写与真机启动测试记录

> 本文档记录使用 **Breed 引导程序** 刷写并启动自编译 OpenWrt 固件的实测过程与结果，供后续会话接续，避免重复实验。
> 权威硬件来源：`mach-maselink-ap2600ifm.c`（作者 Weijie Gao = Breed 作者）；严禁参考 aruba_ap-105。

---

## 0. 时间线摘要

| 阶段 | 结果 |
|------|------|
| RedBoot 原厂加载 initramfs | 失败：gzip/decompress 后无输出 |
| 刷 Breed 到 mtd0 (RedBoot 区) | ✅ 成功，MD5 一致 |
| Breed 正确识别硬件 (AR7161/64MB/S25SL128/IP1001) | ✅ |
| Breed Web 刷 sysupgrade | ❌ "无法判断固件类型"（blank 板无布局表） |
| Breed shell `wget` + `flash write` 刷 sysupgrade | ✅ 刷写成功，`mem compare` No difference |
| Breed `boot flash 0x40000` 引导 | ✅ 内核完整启动 |
| eth0 PHY 探测 | ❌ `Could not connect to PHY device` @0x14(20) |

---

## 1. 网络/串口环境（本次实测）

- **设备**：MASELinK AP2600IFM，Atheros PB42，AR7161 @ 680MHz，64MB DDR，S25SL128 16MB
- **Breed 默认 IP**：`192.168.1.1`，Web 恢复台 80，**telnet 23**，串口 115200
- **电脑**：`192.168.1.2`（DHCP 动态，可能为 .242），HTTP 服务器 `python simple_http.py` 于 8000 端口
- **MQTT 电源控制（板子死机断电用）**：`10.0.0.3:1883`，user=`amqtt`/pass=`mymqtt`

#### MQTT 电源命令（2026-08-30 抓包实测确认）
板子电源接在 **DC1 的 power4 通道**（勿用 power3）：
- 命令 topic：`dc1/dc1_dafangct/cmnd/power4`，负载 `ON` / `OFF`
- 状态 topic：`dc1/dc1_dafangct/stat/power4`，返回 `on` / `off`
- 注意：HA 实体 `switch.dc1_dafangct_3` 对应 **power3**，板子电源实体是 **`switch.dc1_dafangct_4`**（不是 _3）
- 脚本：`bin/ap2600ifm/mqtt_power.py`（已固定 power4，含 `capture` 抓包模式：`python mqtt_power.py capture 60` 抓全量 topic）
- **串口**：COM4，Silicon Labs CP210x USB UART，115200 8N1

### 关键技巧：Breed 支持 telnet(23)，可用网络自动化完全接管 shell，无需依赖串口
- Breed 命令：`wget http://<PC>[:port]/<file> <loadaddr>` 下载到内存
- Breed `wget` 会**自动解析 uImage 头并解出内核**，`Saving to address 0x80000000`（忽略传入地址参数）

---

## 2. Breed 刷写步骤（已验证可行）

### 2.1 环境依赖
1. 电脑起 HTTP 服务器提供固件文件：
   ```sh
   python -m http.server 8000 --directory D:\VC\lede\bin\ap2600ifm
   ```
   > 若 Python http.server 与 Breed 不兼容（ConnectionReset），用极简自写服务器 `D:\VC\lede\bin\ap2600ifm\simple_http.py`（绑定本机实际 IP，每次 accept 后直接回 200+整个文件）。

### 2.2 telnet 进 Breed shell
```sh
python D:\VC\lede\bin\ap2600ifm\breed_wget_sysupgrade.py
```
（该脚本用 socket 连 192.168.1.1:23，发 `wget http://192.168.1.2:8000/<file> 0x81000000`）

### 2.3 串口 flash 写入（COM4，pyserial）
镜像加载后处于 `0x80000000`。写入前**必须先 erase**（Breed `flash write` 不自动擦除）：
```
flash erase 0x40000 0x9A0000
flash write 0x40000 0x80000000 0x9A0000
```
- sysupgrade 镜像：`openwrt-ath79-generic-maselink_ap2600ifm-squashfs-sysupgrade.bin`，10,027,811 B = `0x990323`，对齐 64KB → `0x9A0000`
- 目标偏移 `0x40000` = DTS `firmware` 分区起点（u-boot 0x0-0x40000 不动，Breed 已在其中）
- 验证：`flash read 0x40000 0x83000000 0x990000` 后 `mem compare 0x83000000 0x80000000 0x990000` → **No difference found**

> ⚠️ 教训：`flash read` 会把数据写进指定的内存 dst。若 dst 与已下载固件的源地址重叠（如都用了 0x80000000），会破坏源镜像。读回验证时用**不同的**内存地址（如 0x83000000）。

### 2.4 引导
```
boot flash 0x40000
```
Breed 识别出 U-Boot/uImage 头（Linux 6.6.152），自动解压并跳转到入口 0x80060000。

---

## 3. 启动结果（✅ 内核成功启动）

- **`MIPS: machine is MASELinK AP2600IFM`** — DTS compatible 生效
- Linux 6.6.152，AR7161 rev 2，CPU 680MHz
- MTD 分区与 DTS 完全一致：
  - `u-boot` 0x000000-0x040000
  - `firmware` 0x040000-0xfe0000 → 子分区 `kernel` 0x0-0x290000, `rootfs` 0x290000-0xfa0000, `rootfs_data` 0x990000-0xfa0000
  - `hwinfo` 0xfe0000-0xff0000, `u-boot-env` 0xff0000-0x1000000
- squashfs rootfs 挂载成功，procd/init 正常运行，进入 console
- **无线：`ath9k phy0: Atheros AR9280`** 识别成功（PCIe 0x11.0）
- 启动日志文件：`D:\VC\lede\doc\串口启动日志_ath79_第一版成功.txt`（如有）

---

## 4. 待修复问题（当前唯一阻塞）

### eth0 PHY 连接失败
日志：
```
ag71xx-legacy-mdio: probe of 19000000.eth:mdio failed with error -16
ag71xx-legacy 19000000.eth: Could not connect to PHY device. Deferring probe.
（反复 Deferring，PHY 一直连不上）
```

DTS 当前 `&mdio0` phy = `0x14`（20）。IP1001 PHY 只有 **1 或 20** 两个候选地址（交接文档中原 `@14` = `0x14` = 20，易误读）。

**下一步**：通过串口进 shell 查实际 PHY 地址（`mdio` 命令或 dmesg / sysfs），确认是 1 还是 20，再改 DTS `mdio0` phy-handle 重编译。

### 次要警告（非致命，但需关注）
```
WARNING at drivers/reset/core.c:766 __reset_control_get_internal
  -> of_syscon_register ... ag71xx_mdio_probe
```
reset_control 获取失败 error -16，可能与 PHY reset 资源冲突相关，修复 PHY 地址时一并评估。

---

## 5. 后续会话执行提示
- DTS：`d:\VC\lede\target\linux\ath79\dts\ar7161_maselink_ap2600ifm.dts`
- 修改 PHY 地址 → `git push origin ap2600` 触发 GitHub Actions 重编译 → 下载产物到 `bin/ap2600ifm/`
- 重新刷写流程见上文 §2
- 全套自动化脚本在 `D:\VC\lede\bin\ap2600ifm\*.py`
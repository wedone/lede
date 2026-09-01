# MASELinK AP2600IFM 技术说明书

> 面向使用者/维护者的完整操作手册。记录本设备基于 **OpenWrt 24.10.5（ath79）** 的运行方式、LED 含义、物理按钮功能、救急模式、刷机方法及常见故障排查。
>
> 开发过程详见《MASELinK\_AP2600IFM\_ath79\_迁移开发文档\_v4.md》。
> 最后更新：2026-09-01

***

## 一、设备概况

| 项          | 值                                                                 |
| ---------- | ----------------------------------------------------------------- |
| 品牌型号       | Comba MASELinK AP2600-IFM（京信）                                     |
| SoC        | Atheros AR7161 rev2（MIPS 24Kc, 680MHz）                            |
| 内存         | 64MB DDR                                                          |
| Flash      | 16MB（S25FL128P）                                                   |
| 千兆 PHY     | IC+ IP1001（MDIO 地址 **1**）                                         |
| 无线         | 2.4G = AR9223（`168c:0029/2091`）；5G = AR9220 双频卡（`168c:0029/2096`） |
| Bootloader | **Breed**（ath79 标准 uImage 引导，magic 0x27051956）                    |
| 操作系统       | OpenWrt 24.10.5（内核 6.6.152，`r0-3385b01c` 起）                       |
| **本机 MAC**  | `00:27:1d:04:b6:ad`（存于 hwinfo 分区偏移 0，`798afeb44` 起由 DTS nvmem 绑定 eth0） |
| **本机 SN**   | `0001102150865`（hwinfo 偏移 0x1c 起 ASCII）                             |

**固件特性（`a2b246a46`** **起）**：

* **默认 LAN IP**：`192.168.3.1/24`（避免与光猫默认 192.168.1.1 冲突；首次启动自动设置，覆盖用户已有配置时仅限未修改过 IP 的情况）

* **LuCI 主题**：Argon（默认）

* **纯 AP 精简**：剔除 vsftpd/DDNS/nlbwmon/turboacc/KMS/UPnP/PPPoE 等路由功能，可用内存提升约 4MB

***

## 二、连接与登录

### 2.1 网络连接

| 接口        | 地址                | 说明        |
| --------- | ----------------- | --------- |
| LAN（唯一网口） | `192.168.3.1/24`  | 默认管理地址    |
| 无线 (2.4G) | SSID `LEDE`，信道 1  | 2.4G AP   |
| 无线 (5G)   | SSID `LEDE`，信道 36 | 5G AP     |
| 无线密码      | 无（默认开放）           | 生产使用前务必设置 |

电脑连接 LAN 或无线后，浏览器访问 **<http://192.168.3.1>** 进 LuCI。

### 2.2 串口 / SSH / Web 登录凭据

| 入口             | 方式                            | 凭据                             |
| -------------- | ----------------------------- | ------------------------------ |
| **Web (LuCI)** | <http://192.168.3.1>          | 用户名 **root** / 密码 **password** |
| **SSH**        | `ssh root@192.168.3.1`（端口 22） | 用户名 **root** / 密码 **password** |
| **串口**         | COM4（115200,8N1）              | root 登录，密码 **password**        |

> 默认登录凭据：**root / password**。
> ⚠️ 生产使用前务必修改：`passwd` 更换 root 密码，并在 LuCI 设置无线 WPA2 密码。

***

## 三、LED 指示灯说明（实测映射 2026-09-01）

通过 GPIO 逐个闪烁实测确认，本板正面/背板共有 **3 颗可由系统控制的 LED**（另有 1 颗 POWER 灯为物理供电指示，不受系统控制；余 GPIO 无物理 LED）。

| LED 位置 | 元件丝印 | 功能丝印    | GPIO  | DTS 节点         | 系统行为                                    |
| ------ | ---- | ------- | ----- | -------------- | --------------------------------------- |
| 背板第二颗  | LED2 | **WPS** | GPIO5 | `green:rf2top` | **网络活动灯**：eth0 收发闪烁（WPS 原功能废弃，复用为网络指示灯） |
| 背板第三颗  | LED3 | 2.4G    | GPIO3 | `green:d24top` | **常亮**：开机默认点亮（`trigger=default-on`）     |
| 背板第四颗  | LED4 | 5G      | GPIO4 | `green:rf2`    | **常亮**：开机默认点亮（`trigger=default-on`）     |
| （不受控）  | LED1 | POWER   | —     | 无              | 硬件供电指示，常亮（若有）                           |

**未使用的 GPIO（无物理 LED，DTS 保留但不配置）**：GPIO0/2/7。

> 配置来源：`target/linux/ath79/generic/base-files/etc/board.d/01_leds` 的 `maselink,ap2600ifm` 段。
> 手动改灯：`echo 1 > /sys/class/leds/green:rf2top/brightness`；改 trigger 用 `/sys/class/leds/green:rf2top/trigger`。
>
> **实测结论（2026-09-01，AR922x）**：
>
> 1. **极性**：GPIO3/4 **实测高电平点亮**（DTS 原标 `GPIO_ACTIVE_LOW` 反了，已修为 `HIGH`）；GPIO5（LED2）实测低电平点亮，保持 `LOW`。极性错误会让 `brightness=1`（想亮）实际输出反电平 → 灯灭。
> 2. **常亮 trigger**：用 **`trigger=default-on`**（内核触发，开机即亮）。先前的 `ucidef_set_led_default`（`default=1` 无 trigger）方案有缺陷：`/etc/init.d/led` 点亮后 `echo none > trigger` 又将 LED 熄灭（表现为"启动亮几秒后常灭"）。
> 3. `phy0tpt`/`phy1tpt`（收发闪烁）空闲时灭灯；`phy0radio`/`phy0assoc` 在 AR922x 上实测不生效；"常亮+活动闪烁"组合在本硬件无法用单一 trigger 实现。

***

## 四、物理按钮功能（三档交互）

设备上的物理按键（GPIO8）在 OpenWrt 下有 **三档功能**，基于按住时长区分：

| 操作      | 时长     | 功能         | 说明                               |
| ------- | ------ | ---------- | -------------------------------- |
| **短按**  | <1s    | **重启设备**   | 立即 reboot                        |
| **长按**  | 按住约 5s | **进入救急模式** | 临时开启 LAN 管理通道（见 §五），不重启不断网       |
| **超长按** | ≥10s   | **恢复出厂**   | 清除 overlay 配置并重启（`factoryreset`） |

> 实现文件：
>
> * `target/linux/ath79/generic/base-files/etc/rc.button/reset`（三档判断）
>
> * `target/linux/ath79/generic/base-files/usr/sbin/ap2600ifm-rescue`（救急模式脚本）
>
> **仅 AP2600IFM（存在 rescue 脚本）的设备长按=救急**；其他 ath79 设备保持 OpenWrt 默认（长按=failsafe，松开≥5s=factoryreset）。
> 按钮 GPIO8 事件由 `gpio-button-hotplug` 驱动经 procd 触发。

***

## 五、救急模式（长按约 5s）—— 使用指南

### 5.1 适用场景

AP 运行在**纯桥接/瘦 AP 模式**时，LAN 无 DHCP。若网络异常导致无法访问管理页（IP 丢失、配置错误、有线 DHCP 失效等），可用此功能**临时打开一条管理通道**。

### 5.2 操作步骤

1. **长按物理按钮约 5 秒**（听到/看到串口打印 `RESCUE MODE`）；
2. 救急模式激活：管理 IP 统一为 `192.168.3.1/24`（与主 LAN IP 一致；主 IP 存在则直接复用，缺失才补设）+ 临时 DHCP（.100-.200）；
3. 电脑插网线到 LAN，**自动获取到 192.168.3.x 地址**；
4. 浏览器访问 **<http://192.168.3.1>** → 进入 LuCI 管理页。

### 5.3 特性与恢复

* **纯内存态**：不写入 flash/overlay，**重启后自动消失**，不影响原配置

* 救急 IP 与主 LAN IP 同为 `192.168.3.1`，无需记忆两套地址；网络配置完好时主 IP 直接复用，**不会改变原网络配置**

* 停止救急：SSH/串口执行 `/usr/sbin/ap2600ifm-rescue stop`

* 查看状态：`/usr/sbin/ap2600ifm-rescue status`

### 5.4 技术实现

```
管理 IP  192.168.3.1/24  -> 主 IP 存在则跳过叠加; 缺失(配置损坏)才 ip addr add 补设
临时 DHCP 192.168.3.100-200 -> 独立 dnsmasq 实例(/tmp/rescue-dnsmasq.conf)
标记文件 /tmp/rescue-mode / /tmp/rescue-added-ip(仅补设时生成, stop 据此避免误删主 IP)
```

***

## 六、刷机方法

### 6.1 分区布局（Breed ATH-SDK-16MB）

| 分区         | 偏移       | 大小       | 说明                           |
| ---------- | -------- | -------- | ---------------------------- |
| u-boot     | 0x000000 | 0x050000 | **Breed** bootloader         |
| firmware   | 0x050000 | 0xf90000 | OpenWrt（squashfs sysupgrade） |
| hwinfo     | 0xfe0000 | 0x010000 | 硬件信息：MAC@0x0（本机 `00:27:1d:04:b6:ad`）+ SN@0x1c（ASCII）。DTS 标 read-only，系统内不可写，改写需走 Breed |
| u-boot-env | 0xff0000 | 0x010000 | Breed 环境                     |

### 6.2 刷写 sysupgrade（正常升级）

**方法 A（Breed Web 上传，推荐）**：

1. 上电时进 Breed（见 §6.3）
2. Breed Web（192.168.1.1）→ 固件更新 → **ATH-SDK-16MB** 布局 → 上传 sysupgrade 镜像
3. 等待刷写完成自动重启

**方法 B（OpenWrt 内升级）**：

```
scp openwrt-ath79-generic-maselink_ap2600ifm-squashfs-sysupgrade.bin root@192.168.3.1:/tmp/
ssh root@192.168.3.1 'sysupgrade -v /tmp/*.bin'
```

### 6.3 进入 Breed

断电 → 重新上电，**立即狂按任意键**打断 autoboot（串口或 Breed 提示"Press any key to interrupt autoboot"）。Breed CLI 提示符：`breed>`。

常用 Breed 命令：

```
breed> flash erase 0x50000 0x9A0000     # 擦固件区
breed> wget http://192.168.1.2:8000/固件.bin 0x81000000
breed> flash write 0x50000 0x81000000 0x9A0000
```

> Breed 默认网络：192.168.1.1（电脑配 192.168.1.2 访问）。

### 6.4 从官方固件（RedBoot）刷入 Breed

若设备仍为**官方固件（RedBoot 引导）**，按以下步骤在 RedBoot 环境下写入 Breed（2026-08-29 实测成功，参考 `bin/ap2600ifm/redboot_http_load_breed.py`、`redboot_write_breed.py`）。

**前置**：Breed 文件 `breed-ar7161-blank.bin`（93155 字节 = 0x16BE3）；电脑起 HTTP 服务器放该文件；串口连到设备 RedBoot 控制台。

1. **进入 RedBoot**：设备上电，autoboot 时按 Ctrl-C 中断（提示 `enter ^C to abort`），落到 `RedBoot>` 提示符。
2. **设置网络**（RedBoot 需与电脑 HTTP 服务器同网段）：

   ```
   RedBoot> ip_address -l 192.168.1.1  -h 192.168.1.2
   ```
3. **HTTP 加载 Breed 到 RAM**（0x80080000，实测可用 RAM 范围）：

   ```
   RedBoot> load -r -b 0x80080000 -m http -h 192.168.1.2 -p 8011 breed-ar7161-blank.bin
   ```
4. **校验**（可选但建议）：`cksum -b 0x80080000 -l 0x16BE3`，应等于本地 POSIX cksum 值。
5. **擦除并写入 flash 0xBF000000**（Breed 覆写 RedBoot 所在区；擦除按 0x20000 对齐）：

   ```
   RedBoot> fis erase -f 0xBF000000 -l 0x20000
   RedBoot> fis write -f 0xBF000000 -b 0x80080000 -l 0x16BE3
   ```
6. **重启**：断电上电 → 出现 Breed 引导（"Boot and Recovery Environment..."）即成功。

> ⚠️ 写入 bootloader 前务必先备份原 RedBoot（`fis dump -f 0xBF000000 -l 0x40000` 或编程器回读），失败时可回退。
> 之后按 §6.3 进 Breed，用 ATH-SDK-16MB 布局刷 OpenWrt。

***

## 七、常见问题排查（FAQ）

### 7.1 无法访问 192.168.3.1？

1. 检查电脑 IP：若取到 192.168.3.x 但无法访问，可长按按钮约 5s 进入救急模式，使用 **<http://192.168.3.1>** 访问
2. 确认 AP 未被光猫占用 192.168.1.1（本设备默认 3.1 已规避）
3. 重新上电确认是否正常启动

### 7.2 5G 无线不工作？

* 先查 `dmesg | grep ath9k`：若 `phy1` 未出现，检查 5G 卡（AR9220）金手指/卡槽接触（曾因槽位接触不良导致 vendor 误读）

* 参考迁移文档 §14.4-14.6（已定论为卡槽接触问题，清理后恢复双频）

### 7.3 忘记 root 密码 / 配置损坏？

* **长按按钮约 5 秒进入救急模式** → <http://192.168.3.1> 管理，或 SSH 修复

* 或 **超长按 ≥10 秒恢复出厂**

* 或串口进 Breed 重新刷 sysupgrade

### 7.4 设备运行慢？

* 本设备为 680MHz/64MB 老硬件，已做纯 AP 精简（可用内存 \~11MB）

* 避免同时运行大量服务；LuCI 已用轻量 argon 主题

***

## 八、版本记录（固件相关）

| Commit      | 内容                                                                                            |
| ----------- | --------------------------------------------------------------------------------------------- |
| `3385b01c3` | A 方案：argon 主题、剔除路由包、默认 IP 192.168.3.1                                                         |
| `a2b246a46` | LED 实测映射（WPS→网络活动灯, 2.4G/5G 无线灯）                                                              |
| `a3af70f4a` | 按钮救急模式（短按重启/长按5s救急/超长按10s重置）                                                                  |
| `63318a9ec` | WIFI 灯"开机默认常亮"初版（`ucidef_set_led_default`）——**后经实测发现不可用**（见下条修正）                              |
| `0d880aef7` | 文档记录 WIFI 灯常亮实现（AR922x LED 实测结论）                                                              |
| `7836e513f` | **WIFI 灯常亮修正**（DTS GPIO3/4 极性 LOW→HIGH + trigger 改 `default-on`）+ **救急模式 IP 统一为 192.168.3.1** |
| `798afeb44` | **LAN MAC 固定修复**（eth0 绑定 hwinfo@0x0 真实 MAC）+ **救急模式执行权限修复**（rescue/reset 脚本 git 644→755，`[ -x ]` 判断此前恒失败）。另：本机 hwinfo 已于 2026-09-02 用 Breed 从社区备份 MAC 还原为本机原始数据（MAC `00:27:1d:04:b6:ad` / SN `0001102150865`，源文件 `E:\A-硬件相关\router\IP1001+ar7161+AR9220+AR9223\board-configmtd4.bin`） |

***

## 九、历史：ar71xx 时代的上游支持提交（查询备查）

本设备在 **ar71xx 时代**曾由社区（作者 Weijie Gao / hackpascal）提交支持，后随 ar71xx 移除未迁移到 ath79。本项目的 ath79 移植即从该提交的 `mach-*.c` 硬件参数反向翻译而来。

| 项    | 值                                                                                                                                   |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------- |
| 提交   | `0759aaa96d5adb9f68bd730e9e02a7485500547f`                                                                                          |
| 提交信息 | `add mw316 v1, sgr w500 ebi fit v3,comba ap2600i,comba ap2600ifm.`                                                                  |
| 作者   | imbrolla（<2664456645@qq.com>）                                                                                                       |
| 日期   | 2017-11-28（+0800）                                                                                                                   |
| 涉及文件 | `target/linux/ar71xx/files/arch/mips/ath79/mach-maselink-ap2600ifm.c`（147 行，板级定义：6 LED GPIO、RESET 按键 GPIO8、PHY、PLL）等 17 个文件，686 行新增 |

**与该提交的关系（本项目）**：

* **LED**：`mach-maselink-ap2600ifm.c` 定义 6 GPIO（0/2/3/4/5/7，active\_low）；本项目经实测修正映射（GPIO3=2.4G、GPIO4=5G、GPIO5=WPS网络灯），原提交中 GPIO0/2/7 实际无物理 LED

* **按键**：原提交 GPIO8 + `KEY_RESTART`（polled）；本项目 ath79 走 gpio-keys 中断，并扩展三档功能（§四）

* **PHY**：原提交假设 `phy_addr=20`，本项目实测修正为 **@1（IP1001）**

* **迁移**：ar71xx 自 OpenWrt 18.06 起被 ath79 取代；本项目在 `coolsnowwolf/lede` ath79 上重建支持（首个提交 `d94936e8a`）

> 保留此记录以备：查询原始硬件参数、对比 mach-RedBoot 行为、社区固件 `.c` 溯源。

***

***

*本文档随项目维护更新。技术细节变更时请同步更新。*

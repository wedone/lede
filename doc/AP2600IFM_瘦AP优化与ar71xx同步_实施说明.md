# AP2600IFM — 瘦AP优化与 ar71xx 同步 · 实施说明

> 应用分支：`ap2600-ar71xx`（ar71xx / 4.14）
> 需求来源：`doc/AP2600IFM_瘦AP优化与ar71xx同步_需求清单.md`
> 实施日期：2026-09-06

## 一、准确需求（B）落实情况

### B1. 中文支持 — 已实施

- 中文翻译包：luci.mk（lede 2021-06 锁仓 `a78b74784`）按模块生成 `luci-i18n-<模块名>-zh-cn`，且其默认选中由 `CONFIG_LUCI_LANG_zh-cn`（tristate）驱动；**上次构建固件实测无任何 luci-i18n-\* 包**（menuconfig 未勾选翻译时不会自动带入）。
- 处理：设备级 `DEVICE_PACKAGES` 显式装入 `luci-i18n-luci-base-zh-cn`（覆盖界面全部核心文案）与 `luci-i18n-opkg-zh-cn`（软件包页）。luci-mod-admin-full/network/status/system 在锁仓版本无独立 po/（文案集中在 luci-base）。包名注意：luci.mk 生成 `luci-i18n-<BASENAME>-zh-cn`（basename 去掉 luci-<type>- 前缀）。
- 默认语言：`package/lean/default-settings/zzz-default-settings` 原有 `uci set luci.main.lang=zh_cn`，无需改动。
- 验收：首次登录即中文。

### B2. 主题仅保留 luci-theme-design — 已实施

- 主题来源 `<https://github.com/0x676e67/luci-theme-design>` main 分支（README 明确「main 支持 lede 源码的 lua 版本」，与本分支锁定的 Lua 版 LuCI 匹配）v5.8.0-20240106。
- 集成位置：`package/luci-theme-design/`（luci.mk 模板，产出 `luci-theme-design` 包）。已剔除 dev/、preview/ 开发冗余目录。
- 兼容性核验：header.htm/footer.htm 仅使用 bootstrap 系老接口（`media`、`resource`、`ver.luciversion`、`luci.i18n.context.lang`、`disp.context`、`luci.model.uci`），对 2021-06 luci-base 无新 API 依赖；自带 jquery.min.js/design.js/script.js 与 favicon、manifest。
- 主题包自带 `root/etc/uci-defaults/30_luci-theme-design`（设 `luci.themes.Design` + `luci.main.mediaurlbase`）。
- 默认主题：设备级 `90_maselink_ap2600ifm` 同样显式设置 mediaurlbase，双保险幂等。
- bootstrap 特判：`luci-base` 的 LUCI_DEPENDS 不含任何 theme，已确认可安全移除 → 设备级 `-luci-theme-bootstrap`，固件内用户主题仅 design 一个。
- 验收：`opkg list-installed` 中 luci-theme-\* 仅 design；登录页为 design 风格；无 bootstrap/argon。

## 二、瘦AP优化方向（A）落实情况

### A1. 剔除设备无关组件 — 已实施（设备级，不影响同分支其他设备）

位置：`target/linux/ar71xx/image/generic-legacy-devices.mk` 的 `LegacyDevice/AP2600IFM`。

| 类别 | 剔除项 |
|---|---|
| NAT/防火墙 | `-luci-app-firewall -firewall -kmod-nf-nathelper -kmod-nf-nathelper-extra -kmod-ipt-raw -iptables -luci-app-upnp` |
| WAN/拨号 | `-ppp -ppp-mod-pppoe -luci-proto-ppp` |
| 下载/服务器 | `-vsftpd(luci-app-vsftpd) -luci-app-vlmcsd -luci-app-wol -ddns(luci-app-ddns、ddns-scripts_aliyun/dnspod) -luci-app-ssr-plus -luci-app-unblockmusic` |
| 监控/统计 | `-luci-app-nlbwmon -luci-app-accesscontrol -luci-app-autoreboot -luci-app-filetransfer -luci-app-arpbind -luci-app-sfe -luci-app-ramfree -luci-app-cpufreq -luci-app-webadmin -coremark` |
| 存储 | `-block-mount`（板子无 USB/存储硬件） |
| 主题 | `-luci-theme-bootstrap`（见 B2） |
| 聚合包 | `-luci`（其内 app/主题/proto 一律不收，改为显式补必需件） |

补回（瘦AP 必需）：
- Web 服务：`uhttpd uhttpd-mod-ubus`
- LuCI 模块：`luci-base luci-compat luci-mod-admin-full`（自动带 status/system/network）`luci-app-opkg luci-proto-ipv6`
- 无线：`wpad`（full-internal，含 11k/11v/11r；替换默认 `wpad-basic`，设备级 `-wpad-basic +wpad`）
- IPv6 基础设施（保留底线）：`odhcpd-ipv6only odhcp6c ip6tables`
- 翻译：见 B1

### A2. 无 WAN

- 板级网络定义 `target/linux/ar71xx/base-files/etc/board.d/02_network`：`maselink-ap2600ifm` → `ucidef_set_interface_lan "eth0"`（源头只声明 lan，无 wan/wan6）。
- LAN IP：`90_maselink_ap2600ifm` firstboot 设 `192.168.3.1/24`（避开光猫 192.168.1.1）。
- LAN MAC：`mach-maselink-ap2600ifm.c` 已从 hwinfo 分区（0xfe0000）读取固定 MAC。
- DHCP：lan 网段 DHCP（dnsmasq-full）保留，供调试。

### A3. 首次启动默认配置

`target/linux/ar71xx/base-files/etc/uci-defaults/90_maselink_ap2600ifm`（仅 maselink-ap2600ifm，不覆盖用户已存配置）：
- hostname=`AP2600IFM`
- LAN IP=192.168.3.1/24
- 无线漫游全面开启：全部 radio 写 `ieee80211k=1`、`bss_transition=1`（11v）、`ieee80211r=1` + `ft_psk_generate_local=1`（11r PSK 本地生成）
- 默认主题 mediaurlbase=`/luci-static/design`（B2）
- 时区/语言：`default-settings`（CST-8 / zh_cn）原有

### A4. 引导与稳定性回归项

- 引导：Breed（ATH-SDK-16MB 布局）、标准 uImage、入口 0x80060000 —— 该分支已实现（见 git log），本次未改动。
- MAC/救援模式/WIFI 灯：已在分支内实现，本次无回归面。
- IPv6：lede 4.14 默认 `# CONFIG_IPV6 is not set`，本次在 `target/linux/ar71xx/config-4.14` 追加 IPv6 内核项（对齐官方 19.07 ar71xx 与同源 cns3xxx config-4.19）。

## 三、因分支局限的处理说明（审计）

1. **wpad-basic → wpad**（full-internal）：lede 2021 的 hostapd 无官方 `wpad-full` 命名；全功能包名为 `wpad`（built-in full）。功能含 11k/11v/11r 全开。
2. **翻译包无聚合 `luci-i18n-zh-cn`**：该锁仓为模块化结构，按模块显式装 `luci-i18n-luci-base-zh-cn`（界面核心文案所在）+ `luci-app-opkg` 翻译。
3. **内核 IPv6**：lede 2021 全 target 默认关 IPv6，需平台级 config 显式开启（已做，仅 ar71xx 平台）。
4. **mac80211.sh 无漫游键**：旧版脚本不直接读 `ieee80211k` 等键，但 netifd 会把 wifi-iface 的 uci option 全量透传为 json，由 hostapd.sh（已含 11k/11v/11r 处理）读取，无需改脚本。
5. **luci-app-opkg 保留**：非需求明确排除项，体积小、便于后期维护安装组件。

## 四、验证计划（CI 构建后）

- [ ] 固件 manifest：无 firewall/ppp/luci-app-*/bootstrap/coremark/block-mount；有 luci-theme-design/wpad/odhcpd-ipv6only/odhcp6c/ip6tables/luci-i18n-*
- [ ] 内核 config-4.14：CONFIG_IPV6=y 生效
- [ ] 刷机实测：中文界面、design 主题、hostname、192.168.3.1、无 wan 接口、无线 11k/11v/11r 参数生效、DHCP 正常、IPv6 RA/DHCPv6 正常
- [ ] 固件体积：预计较原固件（7,077,892 B）进一步减小
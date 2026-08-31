# MASELinK AP2600IFM 稳定性测试方案

> AP2600IFM（OpenWrt 24.10.5, ath79）稳定性与性能验收测试。
> 独立于技术说明书；测试相关记录统一归此文件。
> 最后更新：2026-09-01

---

## 一、目的与工具约束

- **目的**：验证 AP2600IFM 长时间运行下的有线/无线稳定性与性能（有线、2.4G、5G 全链路）。
- **工具约束**：设备与电脑均**不装额外工具**（设备无 iperf、ethtool，软件源不可达），全部用内置命令（ping/nc/wget/iwinfo）+ 电脑 Python 完成。

---

## 二、测试环境

```
电脑A (有线) ──eth0: 192.168.3.x── [ AP2600IFM 桥接 ] ──无线── 电脑B (无线)
                         192.168.3.1
```

| 角色 | 连接 | 说明 |
| --- | --- | --- |
| 电脑A | 有线接 AP LAN | IP `192.168.3.x/24`（或 DHCP），管理台 |
| 电脑B | 无线连 `LEDE` | 分开测 2.4G / 5G，需 ping 通 192.168.3.1 |
| 管理台 | SSH 192.168.3.1 | root/password，或串口 COM4 |

---

## 三、测试项目一览

| # | 项目 | 方法 | 通过标准 |
| --- | --- | --- | --- |
| 1 | 有线丢包（长时） | ping 1000 包 | 丢包 0%，RTT 稳定 |
| 2 | 有线吞吐 | Python 套接字传输 | 接近千兆（≥700Mbps） |
| 3 | 2.4G 吞吐 | 无线端 Python 传输 | ≥20Mbps（HT20 单流参考） |
| 4 | 5G 吞吐 | 无线端 Python 传输 | ≥100Mbps（HT40 参考） |
| 5 | 无线信号/速率 | iwinfo 周期记录 | 信号稳定，无大幅波动 |
| 6 | 无线断连/重连 | 无线客户端反复断开重连 | 每次都能重连 |
| 7 | 混合负载稳定 | 有+无线同时传输 1h | 无断流、无重启 |
| 8 | 长时间运行 | 连续运行 ≥24h 观测 | 无重启/死机，内存不泄漏 |

---

## 四、详细步骤

### 4.1 有线丢包测试（设备端发起）

```sh
# 目标 = 电脑A IP
ping -c 1000 -i 0.1 192.168.3.2
```

通过标准：`1000 packets transmitted, 1000 received, 0% packet loss`，RTT max 抖动 < 5ms。

### 4.2 有线吞吐测试（电脑A = 接收端）

电脑A 起 HTTP 服务：
```bash
python -m http.server 8000
```

设备端发起：
```sh
time wget -q http://192.168.3.2:8000/bigfile.bin -O /dev/null
# 速率 = 文件大小 / 耗时; 或用 nc + Python 计时
```

通过标准：≥700Mbps（接近千兆；CPU 680MHz 为瓶颈时 ~500Mbps 可接受）。

### 4.3 2.4G 吞吐（电脑B 无线）

电脑B 连 2.4G（SSID `LEDE`），复用 4.2 方法。
通过标准：≥20Mbps（HT20 单流参考值 20-40Mbps）。

### 4.4 5G 吞吐（电脑B 无线）

电脑B 连 5G（SSID `LEDE`，5G 优先），复用 4.2 方法。
通过标准：≥100Mbps（HT40 参考值 80-150Mbps）。

> 若 2.4G/5G 同 SSID 无法区分连接频段，可临时将对应 radio 改 SSID（如 `LEDE-2G`/`LEDE-5G`）再测。

### 4.5 无线信号/速率监测

设备端：
```sh
# 每 10 秒记录 wlan0(2.4G) 与 wlan1(5G) 的信号/噪声/速率, 观察 30 分钟
while true; do
  iwinfo wlan0 info | grep -E 'ESSID|Signal|Noise|Bit Rate'
  iwinfo wlan1 info | grep -E 'ESSID|Signal|Noise|Bit Rate'
  sleep 10
done > /tmp/wlan_mon.log
```

通过标准：Signal/Noise 波动 < 15dBm，无大幅跳变。

### 4.6 无线断连/重连

电脑B 无线客户端断开→重连循环 20 次；设备端观察：
```sh
dmesg | grep -iE 'wlan0|wlan1|disassoc|assoc|deauth' | tail -30
```
通过标准：每次重连成功，无卡死。

### 4.7 混合负载稳定（1h）

- 电脑A：有线持续 TCP 流（如 wget 大文件循环）
- 电脑B：无线持续下载
- 设备端观察 `top`、`dmesg`、`free`

通过标准：无断流、无进程崩溃、无线不断连。

### 4.8 长时间运行（≥24h）

设备后台监控：
```sh
cat > /tmp/monitor.sh <<'EOF'
#!/bin/sh
while true; do
  echo "$(date) mem=$(free | awk 'NR==2{print $3"/"$2}') load=$(cat /proc/loadavg | cut -d' ' -f1) up=$(cat /proc/uptime | cut -d' ' -f1)" >> /tmp/stable.log
  sleep 300
done
EOF
chmod +x /tmp/monitor.sh; /tmp/monitor.sh &
```

24h 后检查：
- `/tmp/stable.log`：uptime 连续增长（无重启）、used 内存不持续上升（无泄漏）
- `dmesg`：无 panic / ath9k 复位错误

---

## 五、判定标准汇总

| 监测 | 正常 | 异常 |
| --- | --- | --- |
| 丢包 | 0% | 无线或有线持续丢包 |
| RTT | <10ms 稳定 | 抖动 >50ms 或超时 |
| 内存 | 可用保持 >8MB 不持续下降 | 持续下降（泄漏） |
| 进程 | procd/netifd/dnsmasq/hostapd 持续运行 | 崩溃/重启 |
| dmesg | 无 panic/ath9k 错误 | reset chip/phy 丢失 |

---

## 六、测试记录

| 日期 | 版本 | 项目 | 结果 | 备注 |
| --- | --- | --- | --- | --- |
| （待填） | | | | |

---

*本文档随项目维护更新。测试完成后填写 §六 记录。*
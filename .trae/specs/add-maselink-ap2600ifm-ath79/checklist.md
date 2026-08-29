# Checklist

## 基础核对
- [x] `ar7100.dtsi` 节点引用与骨架兼容（mdio0/eth0/pcie0/spi/gpio）
- [x] `loader-okli-compile` / `loader-kernel` 在本仓库 image 层可用（已 grep 定位定义处）
- [x] OKLI loader 定义与官方 AP-175 一致（照抄，含 magic 0x4f4b4c49 与 8128 参数）

## DTS 正确性
- [x] dtc 快速语法验证通过（预处理 include 后编译无 error/warning）
- [x] `ar7161_maselink_ap2600ifm.dts` 存在于 `target/linux/ath79/dts/`，compatible 为 "maselink,ap2600ifm", "qca,ar7161"
- [x] bootargs 含 `console=ttyS0,115200 mem=64M`，memory@0 为 64MB（0x04000000）
- [x] 6 个 LED 全部 active_low：GPIO 0(d24)/2(rf1)/3(d24top)/4(rf2)/5(rf2top)/7(rf1top)
- [x] reset 键 @ GPIO 8，active_low，KEY_RESTART
- [x] 无 i2c0/gpio_ext/TCA6416/LM75/24C256/DS1374 残留节点或引用（grep `gpio_ext` 无匹配）
- [x] mdio0 含 phy@14 reg=0x14；eth0 phy-handle 指向它；pll-data = 0x00110000 0x00001099 0x00991099
- [x] DTS 中 PHY 备用切换注释只提 @1（官方 AP-175 板值），无 AP-105 相关内容
- [x] 两个无线节点 compatible 均为 "pci168c,0029"，reg 0x8800 / 0x9000
- [x] 分区：u-boot(0x0,0x40000)/firmware(0x40000,0xfa0000)/hwinfo(0xfe0000,0x10000,只读,macaddr@1c mac-base)/u-boot-env(0xff0000,0x10000)

## generic.mk
- [x] `Device/maselink_ap2600ifm` 定义完整：IMAGE_SIZE 16000k、LOADER_TYPE bin、LOADER_FLASH_OFFS 0x42000、COMPILE loader-okli-compile、KERNEL magic 0x4f4b4c49
- [x] DEVICE_PACKAGES 仅 kmod-usb2
- [x] `TARGET_DEVICES += maselink_ap2600ifm` 已追加
- [x] generic.mk 中既有设备定义未被改动（diff 仅新增块）
- [x] 全程未引用 aruba_ap-105 的任何定义/参数

## 构建产物
- [x] ath79/generic 编译通过，dtb 无 error（GitHub Actions run 33236200734，52m02s 成功）
- [x] 产出含 maselink_ap2600ifm 的 initramfs 镜像，路径与大小已记录（bin/ap2600ifm/openwrt-ath79-generic-maselink_ap2600ifm-initramfs-kernel.bin，9,258,626 B，uImage magic 0x27051956，Linux 6.6.152）
- [x] 产出含 maselink_ap2600ifm 的 squashfs-sysupgrade 镜像，路径与大小已记录（bin/ap2600ifm/openwrt-ath79-generic-maselink_ap2600ifm-squashfs-sysupgrade.bin，10,027,811 B）

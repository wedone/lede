# Tasks

以实际可执行为准拆分（不按文档 Phase），硬件参数以 `mach-maselink-ap2600ifm.c` 为权威，骨架取自官方 openwrt master 的 `ar7161_aruba_ap-175.dts`。**全程不引用本仓库 aruba_ap-105 的任何内容。**

- [x] Task 1: 核对仓库基础
  - [x] 1.1 确认 `target/linux/ath79/dts/ar7100.dtsi` 中 mdio0/eth0/pcie0/spi/gpio 节点与官方 AP-175 DTS 引用一致，可直接复用骨架结构（eth0 默认 pll-data 恰与 mach 一致；pcie0 interrupt-map 0x8800/0x9000 与两无线 reg 对应）
  - [x] 1.2 grep 定位 `loader-okli-compile`、`loader-kernel` 的定义处，确认本仓库 image 层可用（image/Makefile:29/35；generic.mk:2331 Siemens WS-AP3610 同款 ar7161+OKLI 先例）
  - [x] 1.3 确认本仓库 dtc 预处理可用（staging_dir/host/bin/dtc 暂不存在——尚未构建过；DTS 语法验证并入 Task 4 编译阶段）

- [x] Task 2: 编写 `target/linux/ath79/dts/ar7161_maselink_ap2600ifm.dts`
  - [x] 2.1 文件头 SPDX（GPL-2.0-or-later OR MIT）+ 来源注释（基于官方 ar7161_aruba_ap-175.dts 改造）
  - [x] 2.2 顶层：compatible/model/bootargs(console=ttyS0,115200 mem=64M)/memory@0(0x04000000)/aliases(led-boot、led-failsafe、led-upgrade 指向 d24)
  - [x] 2.3 LED 6 个：GPIO 0(d24)/2(rf1)/3(d24top)/4(rf2)/5(rf2top)/7(rf1top)，全 GPIO_ACTIVE_LOW
  - [x] 2.4 按键：reset @ GPIO 8，GPIO_ACTIVE_LOW，KEY_RESTART
  - [x] 2.5 删除骨架中 i2c0、gpio_ext(TCA6416)、LM75、24C256、DS1374 及所有 &gpio_ext 引用
  - [x] 2.6 mdio0：phy@14 reg=<0x14>（注释：首选 @20，若实测不通改 @1=官方 AP-175 板值，reg/phy-handle 同步改）
  - [x] 2.7 eth0：phy-handle=&phy14、phy-mode=rgmii、pll-data=<0x00110000 0x00001099 0x00991099>；MAC nvmem 暂不接（注释保留 hwinfo@1c 方案）
  - [x] 2.8 pcie0：wifi@11,0 与 wifi@12,0，compatible 均为 "pci168c,0029"，reg 0x8800/0x9000
  - [x] 2.9 分区：u-boot(0x0,0x40000,只读)/firmware(0x40000,0xfa0000,denx,uimage)/hwinfo(0xfe0000,0x10000,只读,macaddr@1c mac-base)/u-boot-env(0xff0000,0x10000,只读)

- [x] Task 3: 修改 `target/linux/ath79/image/generic.mk`
  - [x] 3.1 追加 spec.md 给出的完整 `Device/maselink_ap2600ifm` 块（OKLI loader 全套）
  - [x] 3.2 确认 `TARGET_DEVICES += maselink_ap2600ifm` 已加入，diff 仅新增块、不动既有设备

- [x] Task 4: 编译验证（改用 GitHub Actions，本地 WSL 构建已中止）
  - [x] 4.1 DTS 语法独立验证通过（WSL dtc 1.6.1 编译生成 6326 字节 dtb，warning 均为 ar7100.dtsi 底层已知噪音）
  - [x] 4.2 新增 `.github/workflows/build-ap2600ifm.yml`（单设备构建 + firmware/failure-logs 产物上传），commit d94936e8a 推送 origin/ap2600
  - [x] 4.3 Actions run 33236200734 前期步骤全过：Free disk space / Checkout / Install deps / Update feeds / **Configure target（maselink_ap2600ifm 设备定义被正确识别）** / Download
  - [x] 4.4 Compile 阶段完成，产出 initramfs 与 squashfs-sysupgrade 镜像（Actions run 33236200734 总耗时 52m02s；initramfs 9,258,626 B / sysupgrade 10,027,811 B，uImage magic 0x27051956，Linux 6.6.152，产物已下载至 bin/ap2600ifm/）
  - [x] 4.5 复核 checklist 全项通过

# Task Dependencies
- Task 2 depends on Task 1
- Task 3 与 Task 2 可并行
- Task 4 depends on Task 2 + Task 3

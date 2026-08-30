/*
 * LZMA compressed kernel loader for Atheros AR7XXX/AR9XXX based boards
 *
 * Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

#define CONFIG_ICACHE_SIZE	(32 * 1024)
#define CONFIG_DCACHE_SIZE	(64 * 1024)
#define CONFIG_CACHELINE_SIZE	32

#ifndef CONFIG_FLASH_OFFS
/*
 * 兜底：MASELinK AP2600IFM 固件分区（ATH-SDK-16MB 布局 firmware 从 0x50000 起），
 * 固件内 loader 段 0x1FC0(8128) + 外层 uImage 头 0x40 后才是 OKLI 内核头，
 * 实测 OKLI magic(0x4f4b4c49) 烧录在 flash 0x52000。若编译时 FLASH_OFFS 命令行
 * 参数因缓存未传送到此处，则用此兜底值，让 loader 第一步即命中 OKLI 头，
 * 避免沿用旧布局 0x42000 导致 decompression failed。
 */
#define CONFIG_FLASH_OFFS	0x52000
#endif

#ifndef CONFIG_FLASH_MAX
#define CONFIG_FLASH_MAX	0
#endif

#ifndef CONFIG_FLASH_STEP
#define CONFIG_FLASH_STEP	0x1000
#endif

#endif /* _CONFIG_H_ */

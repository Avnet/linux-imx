/*
 * Copyright 2022 AVNET
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _DRM_MIPI_DSI_H__
#define _DRM_MIPI_DSI_H__

/* DSI mode flags */

/* video mode */
#define MIPI_DSI_MODE_VIDEO             (1 << 0)
/* video burst mode */
#define MIPI_DSI_MODE_VIDEO_BURST       (1 << 1)
/* video pulse mode */
#define MIPI_DSI_MODE_VIDEO_SYNC_PULSE  (1 << 2)
/* enable auto vertical count mode */
#define MIPI_DSI_MODE_VIDEO_AUTO_VERT   (1 << 3)
/* enable hsync-end packets in vsync-pulse and v-porch area */
#define MIPI_DSI_MODE_VIDEO_HSE         (1 << 4)
/* disable hfront-porch area */
#define MIPI_DSI_MODE_VIDEO_NO_HFP      (1 << 5)
/* disable hback-porch area */
#define MIPI_DSI_MODE_VIDEO_NO_HBP      (1 << 6)
/* disable hsync-active area */
#define MIPI_DSI_MODE_VIDEO_NO_HSA      (1 << 7)
/* flush display FIFO on vsync pulse */
#define MIPI_DSI_MODE_VSYNC_FLUSH       (1 << 8)
/* disable EoT packets in HS mode */
#define MIPI_DSI_MODE_NO_EOT_PACKET     (1 << 9)
/* device supports non-continuous clock behavior (DSI spec 5.6.1) */
#define MIPI_DSI_CLOCK_NON_CONTINUOUS   (1 << 10)
/* transmit data in low power */
#define MIPI_DSI_MODE_LPM               (1 << 11)
/* transmit data ending at the same time for all lanes within one hsync */
#define MIPI_DSI_HS_PKT_END_ALIGNED     (1<< 12)

#define MIPI_DSI_FMT_RGB888             0
#define MIPI_DSI_FMT_RGB666             1
#define MIPI_DSI_FMT_RGB666_PACKED      2
#define MIPI_DSI_FMT_RGB565             3

#define MIPI_CSI_FMT_RAW8               0x10
#define MIPI_CSI_FMT_RAW10              0x11

#endif /* _DRM_MIPI_DSI_H__ */

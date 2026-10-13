/*
 * SDRAM map of the STM32F469 Discovery: 16 MB at 0xC0000000.
 *
 * This header is the only place that assigns SDRAM regions; the display
 * driver, LVGL and the sdram module take their addresses from here.
 * SDRAM is outside the microcontroller and can be read with a probe, so no
 * secret may be stored in it.
 */
#ifndef SDRAM_LAYOUT_H
#define SDRAM_LAYOUT_H

#define SDRAM_BASE              0xC0000000u
#define SDRAM_SIZE              0x01000000u

/* Display framebuffers: three 2 MB slots, each holding one 480 x 800
 * ARGB8888 frame (1.5 MB). */
#define SDRAM_FB_SLOT_SIZE      0x00200000u
#define SDRAM_FB_BYTES          (480u * 800u * 4u)
#define SDRAM_FB0_ADDR          (SDRAM_BASE + 0u * SDRAM_FB_SLOT_SIZE)
#define SDRAM_FB1_ADDR          (SDRAM_BASE + 1u * SDRAM_FB_SLOT_SIZE)
#define SDRAM_FB2_ADDR          (SDRAM_BASE + 2u * SDRAM_FB_SLOT_SIZE)

/* LVGL memory pool (widgets, styles, label texts), outside the MicroPython
 * heap. */
#define SDRAM_LVGL_POOL_ADDR    (SDRAM_BASE + 0x00600000u)
#define SDRAM_LVGL_POOL_SIZE    0x00400000u

/* RAM disk for data from hosts (PSBTs, descriptors), up to the end. */
#define SDRAM_RAMDISK_ADDR      (SDRAM_LVGL_POOL_ADDR + SDRAM_LVGL_POOL_SIZE)
#define SDRAM_RAMDISK_SIZE      (SDRAM_BASE + SDRAM_SIZE - SDRAM_RAMDISK_ADDR)

#if SDRAM_FB_BYTES > SDRAM_FB_SLOT_SIZE
#error "framebuffer does not fit its SDRAM slot"
#endif
#if SDRAM_FB2_ADDR + SDRAM_FB_SLOT_SIZE > SDRAM_LVGL_POOL_ADDR
#error "framebuffers overlap the LVGL pool"
#endif
#if SDRAM_RAMDISK_ADDR + SDRAM_RAMDISK_SIZE != SDRAM_BASE + SDRAM_SIZE
#error "SDRAM regions do not end at the end of SDRAM"
#endif

#endif /* SDRAM_LAYOUT_H */

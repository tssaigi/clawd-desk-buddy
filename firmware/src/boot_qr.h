#pragma once

// The setup QR: a full-screen card with a QR code to this firmware's
// repository (CLAWD_REPO_URL), so whoever holds the device can find the setup
// guide and the source. It shows for BOOT_QR_SECONDS after every start (a
// restart or a power-on) and again when the panel wakes after it was asleep
// for BOOT_QR_AFTER_SLEEP_MIN or longer. A tap skips it.
//
// Turning it off (see SETUP.md, "Remove the startup QR"):
//   - at run time, kept across restarts:  serial command `bootqr off`  (`bootqr on` brings it back)
//   - at build time: -DCLAWD_BOOT_QR=0, or drop -DLV_USE_QRCODE=1 / -DCLAWD_REPO_URL from the env
// Without LVGL's QR widget or a URL the functions below are no-ops.

void boot_qr_init(void);              // after the pages are built: shows it if enabled
void boot_qr_tick(void);              // per loop: the timeout, and the long-sleep trigger
bool boot_qr_visible(void);
void boot_qr_dismiss(void);
void boot_qr_show(void);              // now, whatever the setting (the `bootqr show` command)
bool boot_qr_enabled(void);
void boot_qr_set_enabled(bool on);    // saved in NVS
bool boot_qr_available(void);         // compiled in

#include "hal.h"

void hal_loop() {}
void hal_window() {}
void hal_graphics() {}
void hal_image() {}
void hal_sound() {}
void hal_synth() {}
void hal_audioloop() {}
void hal_label (int x, int y, int w, int h, const char *str) {}
void hal_button(int x, int y, int w, int h, const char *str, const char *img1, const char* img2) {}
void hal_input (int x, int y, int w, int h, const char *str, bool multiline) {}
void hal_table (int x, int y, int w, int h) {}
void hal_sound_url(const char *address) {}
void hal_sound_bytes(const uint8_t *bytes, uint32_t length) {}



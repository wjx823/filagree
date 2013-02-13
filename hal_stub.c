#include "hal.h"

void hal_loop() {}
void hal_window(const char *icon_path) {}
void hal_graphics(const struct variable *shape) {}
void hal_image() {}
void hal_sound() {}
void hal_synth(const uint8_t *bytes, uint32_t length) {}
void hal_audioloop() {}
void hal_label (int x, int y, int w, int h,
                const char *str) {}
void hal_input (struct variable *uictx,
                int x, int y, int w, int h,
                const char *str, bool multiline) {}
void hal_button(struct context *context,
                struct variable *uictx,
                int x, int y, int w, int h,
                struct variable *logic,
                const char *str, const char *img) {}
void hal_table (struct context *context,
                struct variable *uictx,
                int x, int y, int w, int h,
                struct variable *list, struct variable *logic) {}
void hal_sound_url(const char *address) {}
void hal_sound_bytes(const uint8_t *bytes, uint32_t length) {}



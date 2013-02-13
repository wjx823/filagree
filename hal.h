// Hardware Abstraction Layer

#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <inttypes.h>
#include "struct.h"
#include "vm.h"

void hal_loop();
void hal_window(const char *icon_path);
void hal_image();
void hal_sound();
void hal_audioloop();
void hal_graphics(const struct variable *shape);
void hal_synth(const uint8_t *bytes, uint32_t length);
void hal_label (int x, int y, int w, int h,
                const char *str);
void hal_input (struct variable *uictx,
                int x, int y, int w, int h,
                const char *str, bool multiline);
void hal_button(struct context *context,
                struct variable *uictx,
                int x, int y, int w, int h,
                struct variable *logic,
                const char *str, const char *img);
void hal_table (struct context *context,
                struct variable *uictx,
                int x, int y, int w, int h,
                struct variable *list, struct variable *logic);

#endif
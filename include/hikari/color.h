#if !defined(HIKARI_COLOR_H)
#define HIKARI_COLOR_H

#include <stdint.h>

void
hikari_color_convert(float dst[static 4], uint32_t color);

void
hikari_color_lighten(float dst[static 4], const float src[static 4]);

void
hikari_color_darken(float dst[static 4], const float src[static 4]);

#endif

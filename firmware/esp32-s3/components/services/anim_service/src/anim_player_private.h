#ifndef ANIM_PLAYER_PRIVATE_H
#define ANIM_PLAYER_PRIVATE_H

#include "animation_service.h"
#include "lvgl.h"

#ifdef CONFIG_WATCHER_ANIM_FPS
#define EMOJI_ANIM_INTERVAL_MS (1000 / CONFIG_WATCHER_ANIM_FPS)
#else
#define EMOJI_ANIM_INTERVAL_MS 100
#endif

int emoji_anim_init(lv_obj_t *img_obj);
void emoji_anim_stop(void);
int emoji_anim_deinit(void);
int emoji_anim_prefetch_type(emoji_anim_type_t type);
void emoji_anim_set_direct_lcd_protected_region(int x1, int y1, int x2, int y2);
void emoji_anim_clear_direct_lcd_protected_region(void);

#endif

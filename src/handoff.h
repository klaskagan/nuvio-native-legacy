#ifndef NV_HANDOFF_H
#define NV_HANDOFF_H
#include <SDL2/SDL.h>
#if defined(NV_WEBOS) || defined(NV_ANDROID)
#define NV_HANDOFF_LOCAL_BUILD 1
#else
#define NV_HANDOFF_LOCAL_BUILD 0
#endif
#if defined(NV_WEBOS) || defined(NV_ANDROID)
/* Lifecycle delivery is thread safe. Other entry points belong to the UI
 * thread. */
void handoff_launch(const char *json);
void handoff_install_lifecycle(void);
void handoff_ready(void);
void handoff_tick(void);
int handoff_event(const SDL_Event *event);
void handoff_draw(void);
/* Called after the episode is finalized and before source selection. */
void handoff_player_opened(void);
int handoff_pending(void);
long handoff_edition_bonus(const char *text);
#else
static inline void handoff_launch(const char *json) { (void)json; }
static inline void handoff_install_lifecycle(void) {}
static inline void handoff_ready(void) {}
static inline void handoff_tick(void) {}
static inline int handoff_event(const SDL_Event *e) {
  (void)e;
  return 0;
}
static inline void handoff_draw(void) {}
static inline void handoff_player_opened(void) {}
static inline int handoff_pending(void) { return 0; }
static inline long handoff_edition_bonus(const char *s) {
  (void)s;
  return 0;
}
#endif
#endif

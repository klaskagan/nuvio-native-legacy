#include <SDL.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
enum { H_WAIT=1 };
typedef struct { unsigned seq; int stage,decision; } Transfer;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned generation=7;
static int focus;
static Transfer transfer,*active;
SDL_Window *SDL_GetMouseFocus(void) { return NULL; }
void SDL_GetWindowSize(SDL_Window *w,int *x,int *y) { (void)w; *x=1920; *y=1080; }
#include "handoff_ui.inc"
static void reset(void) { transfer=(Transfer){7,H_WAIT,0}; active=&transfer; focus=1; }
static void key(SDL_Keycode k) { SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_KEYDOWN;e.key.keysym.sym=k;assert(handoff_event(&e)); }
int main(void) {
 SDL_Keycode ok[]={SDLK_RETURN,SDLK_KP_ENTER,SDLK_SPACE,SDLK_SELECT};
 for(unsigned i=0;i<sizeof ok/sizeof ok[0];i++) { reset();key(ok[i]);assert(transfer.decision==1 && !focus); }
 reset();key(SDLK_RIGHT);assert(focus==2);key(SDLK_RETURN);assert(transfer.decision==-1);
 reset();key(SDLK_AC_BACK);assert(transfer.decision==-1);
 reset();key(SDLK_BACKSPACE);assert(transfer.decision==-1);
 reset();SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_MOUSEBUTTONUP;e.button.x=1050;e.button.y=650;assert(handoff_event(&e));assert(transfer.decision==-1);
 reset();generation=8;memset(&e,0,sizeof e);e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_RETURN;assert(!handoff_event(&e));assert(!transfer.decision);
 puts("handoff UI: PASS (remote OK variants, cancel, pointer and obsolete prompts)");
}

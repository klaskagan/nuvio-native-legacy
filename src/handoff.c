/* Local desktop receiver. Network runs on one worker; SDL/player only on UI.
 * No media URL is received: the existing title/source router remains in charge.
 */
#define _GNU_SOURCE
#include "handoff.h"
#if defined(NV_WEBOS) || defined(NV_ANDROID)
#include "app.h"
#include "catalogo.h"
#include "dados.h"
#include "gfx.h"
#include "handoff_protocol.h"
#include "js.h"
#include "perfis.h"
#include "player.h"
#include "rede.h"
#include "sessao.h"
#include "text.h"
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { H_WAIT = 1, H_START, H_LOADING, H_DONE, H_FAILED };
typedef struct {
  HandoffLaunch launch;
  HandoffRequest request;
  unsigned seq;
  int stage, decision, trusted;
  int opened, playing, mediaReady, mediaFailed, seekAttempts;
  double position, stablePosition, duration;
  Uint32 stableSince, lastSeek;
  uint64_t profileSession;
} Transfer;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static unsigned generation;
static HandoffLaunch queued;
static Transfer *active;
static int started, queuedValid, focus, ready;
static unsigned uiOwned, uiStarted, inflightSeq, recentIndex;
static char inflight[65], recentIds[128][65];
static double recentExpiry[128];
static double epoch(void) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return t.tv_sec * 1000. + t.tv_nsec / 1000000.;
}
static int cancelled(void *u) {
  Transfer *t = u;
  int stop;
  pthread_mutex_lock(&lock);
  stop = generation != t->seq;
  pthread_mutex_unlock(&lock);
  return stop || epoch() >= t->request.expires;
}
static char *exchange(Transfer *t, const char *phase, double position,
                      const char *failure) {
  char url[160], auth[96], body[512];
  RedeResposta r;
  RedePedido p = {0};
  snprintf(url, sizeof url, "%s%s", t->launch.endpoint, phase ? "/status" : "");
  snprintf(auth, sizeof auth, "Authorization: Bearer %s", t->launch.token);
  const char *headers[] = {auth, "Content-Type: application/json", NULL};
  p.url = url;
  p.metodo = phase ? "POST" : "GET";
  p.cabecalhos = headers;
  p.prazo_ms = 6000;
  p.max_bytes = 16384;
  p.max_cabecalhos = 16384;
  p.tls_chave_pin = t->launch.pin;
  /* During first fetch expiry is not known yet. Generation still cancels I/O.
   */
  if (t->request.expires) {
    p.parar = cancelled;
    p.parar_usuario = t;
  }
  if (phase) {
    snprintf(body, sizeof body,
             "{\"requestId\":\"%s\",\"targetDeviceId\":\"%s\",\"phase\":\"%s\","
             "\"positionMs\":%.0f%s%s%s}",
             t->request.id, t->request.target, phase, position,
             failure ? ",\"failureCode\":\"" : "", failure ? failure : "",
             failure ? "\"" : "");
    p.corpo = body;
    p.n_corpo = strlen(body);
  }
  int ok = rede_pedir(&p, &r) && r.status == 200;
  if (!ok) {
    char diagnostic[128];
    snprintf(diagnostic, sizeof diagnostic,
             "network request=%.8s curl=%d status=%d error=%d\n", t->launch.id,
             r.curl_erro, r.status, r.erro);
    dados_gravar_leve("handoff-receiver.txt", diagnostic);
  }
  char *result = ok ? r.corpo : NULL;
  if (ok)
    r.corpo = NULL;
  rede_resposta_limpar(&r);
  return result;
}
static int status(Transfer *t, const char *phase, double pos,
                  const char *failure) {
  char *s = exchange(t, phase, pos, failure);
  int ok = s != NULL;
  free(s);
  return ok;
}
static int trusted(Transfer *t) {
  char line[160];
  char *saved = dados_ler("handoff-trust.txt");
  snprintf(line, sizeof line, "%s %s\n", t->launch.sender, t->launch.pin);
  int ok = saved && strstr(saved, line) &&
           (strstr(saved, line) == saved || strstr(saved, line)[-1] == '\n');
  free(saved);
  return ok;
}
static void trust(Transfer *t) {
  char line[160], buf[4096];
  char *saved = dados_ler("handoff-trust.txt");
  snprintf(line, sizeof line, "%s %s\n", t->launch.sender, t->launch.pin);
  if (saved && strlen(saved) < sizeof buf - 160)
    snprintf(buf, sizeof buf, "%s%s", saved, line);
  else
    snprintf(buf, sizeof buf, "%s", line);
  free(saved);
  dados_gravar("handoff-trust.txt", buf);
}
/* Persist recent request ids, bounded by the two-minute request lifetime. */
static int replay(Transfer *t, int remember) {
  char buf[16384] = "", id[65];
  double until;
  char *saved = dados_ler("handoff-seen.txt"), *p = saved;
  int seen = 0, count = 0;
  while (p && *p) {
    int n = 0;
    if (sscanf(p, "%64s %lf%n", id, &until, &n) != 2 || !n)
      break;
    if (until > epoch()) {
      if (!strcmp(id, t->request.id))
        seen = 1;
      if (count++ < 128) {
        size_t len = strlen(buf);
        snprintf(buf + len, sizeof buf - len, "%s %.0f\n", id, until);
      }
    }
    p = strchr(p, '\n');
    if (p)
      p++;
  }
  free(saved);
  if (count >= 128)
    return 1;
  if (remember && !seen) {
    size_t len = strlen(buf);
    snprintf(buf + len, sizeof buf - len, "%s %.0f\n", t->request.id,
             t->request.expires);
    dados_gravar("handoff-seen.txt", buf);
  }
  return seen;
}
static void *worker(void *unused) {
  (void)unused;
  for (;;) {
    Transfer t = {0};
    pthread_mutex_lock(&lock);
    inflight[0] = 0;
    while (!queuedValid)
      pthread_cond_wait(&wake, &lock);
    t.launch = queued;
    t.seq = generation;
    inflightSeq = t.seq;
    snprintf(inflight, sizeof inflight, "%s", t.launch.id);
    queuedValid = 0;
    active = NULL;
    pthread_mutex_unlock(&lock);
    char *json = exchange(&t, NULL, 0, NULL);
    if (!json)
      continue;
    int valid = handoff_parse_request(json, &t.launch, epoch(), &t.request);
    free(json);
    if (!valid)
      continue;
    if (replay(&t, 0)) {
      status(&t, "FAILED", 0, "DUPLICATE_REQUEST");
      continue;
    }
    if (!status(&t, "RECEIVED", 0, NULL) ||
        !status(&t, "AWAITING_USER", 0, NULL))
      continue;
    dados_gravar_leve("handoff-receiver.txt", "request validated\n");
    t.trusted = trusted(&t);
    t.stage = H_WAIT;
    printf("[handoff] request validated\n");
    pthread_mutex_lock(&lock);
    if (generation == t.seq)
      active = &t;
    pthread_mutex_unlock(&lock);
    int approved = 0, loading = 0;
    Uint32 lastPoll = 0, lastDiagnostic = 0;
    while (!cancelled(&t)) {
      char note[200] = "";
      pthread_mutex_lock(&lock);
      int decision = t.decision, opened = t.opened, playing = t.playing;
      double pos = t.position;
      if (SDL_GetTicks() - lastDiagnostic >= 5000) {
        snprintf(note, sizeof note,
                 "request=%.8s stage=%d positionMs=%.0f durationMs=%.0f "
                 "ready=%d failed=%d seeks=%d\n",
                 t.launch.id, t.stage, t.position, t.duration, t.mediaReady,
                 t.mediaFailed, t.seekAttempts);
        lastDiagnostic = SDL_GetTicks();
      }
      pthread_mutex_unlock(&lock);
      if (note[0])
        dados_gravar_leve("handoff-receiver.txt", note);
      if (decision < 0) {
        status(&t, "FAILED", 0,
               decision == -2   ? "PROFILE_REQUIRED"
               : decision == -3 ? "EDITION_MISMATCH"
               : decision == -4 ? "SEEK_FAILED"
                                : "USER_DECLINED");
        break;
      }
      if (!approved && decision == 1) {
        char *fresh = exchange(&t, NULL, 0, NULL);
        if (!fresh)
          break;
        free(fresh);
        if (!status(&t, "RESOLVING", 0, NULL))
          break;
        trust(&t);
        replay(&t, 1);
        approved = 1;
        pthread_mutex_lock(&lock);
        if (generation == t.seq) {
          unsigned slot = recentIndex++ % 128;
          snprintf(recentIds[slot], sizeof recentIds[slot], "%s", t.request.id);
          recentExpiry[slot] = t.request.expires;
          t.stage = H_START;
        }
        pthread_mutex_unlock(&lock);
      }
      if (approved && opened && !loading) {
        if (!status(&t, "LOADING", 0, NULL))
          break;
        loading = 1;
      }
      if (loading && playing) {
        if (!status(&t, "PLAYING", pos, NULL))
          break;
        pthread_mutex_lock(&lock);
        t.stage = H_DONE;
        printf("[handoff] playback confirmed\n");
        pthread_mutex_unlock(&lock);
        break;
      }
      if (SDL_GetTicks() - lastPoll >= 1000) {
        char *poll = exchange(&t, NULL, 0, NULL);
        lastPoll = SDL_GetTicks();
        if (!poll)
          break;
        free(poll);
      }
      SDL_Delay(50);
    }
    pthread_mutex_lock(&lock);
    if (t.stage != H_DONE)
      t.stage = H_FAILED;
    /* Keep the completed snapshot alive until the UI has consumed it. */
    while (active == &t && generation == t.seq)
      pthread_cond_wait(&wake, &lock);
    if (active == &t)
      active = NULL;
    pthread_mutex_unlock(&lock);
  }
  return NULL;
}
void handoff_launch(const char *json) {
  HandoffLaunch l;
  if (!handoff_parse_launch(json, &l))
    return;
  pthread_mutex_lock(&lock);
  for (unsigned i = 0; i < 128; i++)
    if (recentExpiry[i] > epoch() && !strcmp(recentIds[i], l.id)) {
      pthread_mutex_unlock(&lock);
      return;
    }
  if (inflightSeq == generation && !strcmp(inflight, l.id)) {
    pthread_mutex_unlock(&lock);
    return;
  }
  if ((active && !strcmp(active->launch.id, l.id)) ||
      (queuedValid && !strcmp(queued.id, l.id))) {
    pthread_mutex_unlock(&lock);
    return;
  }
  dados_gravar_leve("handoff-receiver.txt", "launch accepted\n");
  queued = l;
  queuedValid = 1;
  generation++;
  printf("[handoff] launch received\n");
  if (!started && ready) {
    pthread_t thread;
    if (!pthread_create(&thread, NULL, worker, NULL)) {
      pthread_detach(thread);
      started = 1;
    }
  }
  pthread_cond_broadcast(&wake);
  pthread_mutex_unlock(&lock);
}
/* Retail LG SDL owns the native lifecycle subscription and does not expose
 * its launch parameters as an SDL event. It delivers the bounded JSON to the
 * public SDL log callback. Consume that payload without printing it; all normal
 * SDL lifecycle/window processing stays intact. Never forward JSON system logs:
 * launch parameters can contain credentials even when this receiver ignores
 * them. There is no private-symbol interposition or extra listener on the LAN.
 */
#ifdef NV_WEBOS
static SDL_LogOutputFunction previousLog;
static void *previousLogData;
static void lifecycleLog(void *unused, int category, SDL_LogPriority priority,
                         const char *message) {
  (void)unused;
  if (category == SDL_LOG_CATEGORY_SYSTEM) {
    const char *json = strchr(message, '{');
    if (json) {
      char event[32] = "";
      if (strnlen(json, 16385) <= 16384 &&
          js_texto_raiz(json, "event", event, sizeof event) &&
          (!strcmp(event, "launch") || !strcmp(event, "relaunch"))) {
        handoff_launch(json);
        char decoded[4097];
        if (js_texto(json, NULL, "parameters", decoded, sizeof decoded))
          handoff_launch(decoded);
      }
      return;
    }
  }
  if (previousLog)
    previousLog(previousLogData, category, priority, message);
}
void handoff_install_lifecycle(void) {
  SDL_LogGetOutputFunction(&previousLog, &previousLogData);
  SDL_LogSetOutputFunction(lifecycleLog, NULL);
  SDL_LogSetPriority(SDL_LOG_CATEGORY_SYSTEM, SDL_LOG_PRIORITY_INFO);
}
#else
void handoff_install_lifecycle(void) {}
#include <jni.h>
JNIEXPORT void JNICALL
Java_space_nuvio_nativelegacy_NuvioActivity_nativeHandoff(JNIEnv *env,
    jobject activity, jstring payload) {
  (void)activity;
  if (!payload || (*env)->GetStringUTFLength(env, payload) > 16384) return;
  const char *json = (*env)->GetStringUTFChars(env, payload, NULL);
  if (json) {
    handoff_launch(json);
    (*env)->ReleaseStringUTFChars(env, payload, json);
  }
}
#endif
void handoff_ready(void) {
  pthread_mutex_lock(&lock);
  ready = 1;
  if (queuedValid && !started) {
    pthread_t thread;
    if (!pthread_create(&thread, NULL, worker, NULL)) {
      pthread_detach(thread);
      started = 1;
    }
  }
  pthread_cond_broadcast(&wake);
  pthread_mutex_unlock(&lock);
}
int handoff_pending(void) {
  int yes;
  pthread_mutex_lock(&lock);
  yes = active && active->seq == generation &&
        (active->stage == H_START || active->stage == H_LOADING);
  pthread_mutex_unlock(&lock);
  return yes;
}
static void normalized(const char *s, char *out, size_t n) {
  size_t i = 0;
  for (; *s && i + 1 < n; s++)
    if (isalnum((unsigned char)*s))
      out[i++] = (char)tolower((unsigned char)*s);
  out[i] = 0;
}
long handoff_edition_bonus(const char *text) {
  char hint[128] = "", raw[128] = "", haystack[4096];
  pthread_mutex_lock(&lock);
  if (active && active->seq == generation &&
      (active->stage == H_START || active->stage == H_LOADING))
    snprintf(raw, sizeof raw, "%s", active->request.edition);
  pthread_mutex_unlock(&lock);
  if (!raw[0])
    return 0;
  normalized(raw, hint, sizeof hint);
  normalized(text, haystack, sizeof haystack);
  return hint[0] && strstr(haystack, hint) ? 1000000L : 0;
}
void handoff_player_opened(void) {
  pthread_mutex_lock(&lock);
  if (active && active->seq == uiStarted && active->stage == H_LOADING) {
    const CatItem *c = cat_item(player_indice());
    int s, e;
    player_episodio_atual(&s, &e);
    if (c &&
        handoff_content_matches(active->request.content, active->request.series,
                                c->imdb, c->tmdb, !strcmp(c->tipo, "series")) &&
        (!active->request.series ||
         (s == active->request.season && e == active->request.episode))) {
      player_do_inicio();
      player_definir_retomada_fonte(active->request.position / 1000.);
      active->opened = 1;
      uiOwned = active->seq;
    }
  }
  pthread_mutex_unlock(&lock);
}
void handoff_tick(void) {
  pthread_mutex_lock(&lock);
  Transfer *t = active;
  if (!t || t->seq != generation) {
    int stop = uiOwned != 0;
    uiOwned = 0;
    pthread_mutex_unlock(&lock);
    if (stop && player_aberto())
      player_encerrar();
    return;
  }
  if (uiOwned && uiOwned != t->seq) {
    uiOwned = 0;
    pthread_mutex_unlock(&lock);
    if (player_aberto())
      player_encerrar();
    return;
  }
  if (t->stage == H_DONE || t->stage == H_FAILED) {
    int stop = t->stage == H_FAILED && uiOwned == t->seq;
    uiOwned = 0;
    active = NULL;
    focus = 0;
    pthread_cond_broadcast(&wake);
    pthread_mutex_unlock(&lock);
    if (stop && player_aberto())
      player_encerrar();
    return;
  }
  if (t->stage == H_WAIT && t->trusted && !app_no_login() &&
      perfis_precisa_escolher() && perfis_dono()[0] &&
      !strcmp(perfis_dono(), t->request.account) &&
      perfis_ativo() == t->request.profile && perfis_pode_dispensar()) {
    char owner[64];
    snprintf(owner, sizeof owner, "%s", t->request.account);
    int profile = t->request.profile;
    pthread_mutex_unlock(&lock);
    extern int app_handoff_restore_profile(const char *, int);
    app_handoff_restore_profile(owner, profile);
    return;
  }
  if (t->stage == H_WAIT && !app_no_login() && !perfis_precisa_escolher() &&
      perfis_dono()[0]) {
    if (!sessao_logada() || strcmp(perfis_dono(), t->request.account) ||
        perfis_ativo() != t->request.profile)
      t->decision = -2;
    else if (t->trusted && !player_aberto())
      t->decision = 1;
    else if (!focus)
      focus = 1;
  }
  if (t->stage == H_START) {
    t->profileSession = perfis_sessao_geracao();
    t->stage = H_LOADING;
    uiStarted = t->seq;
    printf("[handoff] opening title\n");
    char content[64];
    snprintf(content, sizeof content, "%s", t->request.content);
    int s = t->request.season, e = t->request.episode;
    pthread_mutex_unlock(&lock);
    extern void app_handoff_start(const char *, int, int);
    app_handoff_start(content, s, e);
    return;
  }
  if (t->stage == H_LOADING) {
    if (!perfis_sessao_atual(t->profileSession)) {
      t->decision = -2;
    } else if (t->opened) {
      const CatItem *c = cat_item(player_indice());
      int s, e;
      player_episodio_atual(&s, &e);
      if (!player_aberto() || !c ||
          !handoff_content_matches(t->request.content, t->request.series,
                                   c->imdb, c->tmdb,
                                   !strcmp(c->tipo, "series")) ||
          (t->request.series &&
           (s != t->request.season || e != t->request.episode)))
        t->decision = -1;
      double actualDuration = video_duracao() * 1000.;
      t->duration = actualDuration;
      t->mediaFailed = video_falhou();
      if (video_pronto() && actualDuration > 0 &&
          fabs(actualDuration - t->request.duration) >
              fmax(60000., t->request.duration * .03))
        t->decision = -3;
      double pos = video_pos() * 1000.;
      int ready = video_tocando() && player_handoff_ready() &&
                  fabs(pos - t->request.position) <= 2000;
      t->position = pos;
      t->mediaReady = player_handoff_ready();
      if (!ready) {
        t->stableSince = 0;
        t->playing = 0;
        if (video_pronto() && video_tocando() && player_handoff_ready() &&
            fabs(pos - t->request.position) > 2000 &&
            SDL_GetTicks() - t->lastSeek >= 2000) {
          t->lastSeek = SDL_GetTicks();
          if (t->seekAttempts++ < 6)
            video_buscar(t->request.position / 1000.);
          else
            t->decision = -4;
        }
      } else if (!t->stableSince) {
        t->stableSince = SDL_GetTicks();
        t->stablePosition = pos;
      } else if (SDL_GetTicks() - t->stableSince >= 750 &&
                 pos >= t->stablePosition + 100) {
        t->playing = 1;
        t->position = pos;
      }
    }
  }
  pthread_mutex_unlock(&lock);
}
int handoff_event(const SDL_Event *e) {
  pthread_mutex_lock(&lock);
  int modal =
      focus && active && active->seq == generation && active->stage == H_WAIT;
  if (modal && e->type == SDL_KEYDOWN) {
    SDL_Keycode k = e->key.keysym.sym;
    if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_TAB)
      focus = focus == 1 ? 2 : 1;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE || k == SDLK_SELECT) {
      active->decision = focus == 1 ? 1 : -1;
      focus = 0;
    }
    if (k == SDLK_ESCAPE || k == SDLK_AC_BACK || k == SDLK_BACKSPACE ||
        e->key.keysym.scancode == 482) {
      active->decision = -1;
      focus = 0;
    }
  }
  if (modal && (e->type == SDL_MOUSEMOTION || e->type == SDL_MOUSEBUTTONUP)) {
    int w = 1920, h = 1080;
    SDL_Window *window = SDL_GetMouseFocus();
    if (window)
      SDL_GetWindowSize(window, &w, &h);
    float x = (e->type == SDL_MOUSEMOTION ? e->motion.x : e->button.x) *
              1920.f / (w ? w : 1920);
    float y = (e->type == SDL_MOUSEMOTION ? e->motion.y : e->button.y) *
              1080.f / (h ? h : 1080);
    if (y >= 620 && y <= 690) {
      if (x >= 510 && x <= 910)
        focus = 1;
      else if (x >= 960 && x <= 1360)
        focus = 2;
      if (e->type == SDL_MOUSEBUTTONUP &&
          ((x >= 510 && x <= 910) || (x >= 960 && x <= 1360))) {
        active->decision = focus == 1 ? 1 : -1;
        focus = 0;
      }
    }
  }
  pthread_mutex_unlock(&lock);
  return modal && (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP ||
                   e->type == SDL_MOUSEBUTTONDOWN ||
                   e->type == SDL_MOUSEBUTTONUP || e->type == SDL_MOUSEMOTION);
}
void handoff_draw(void) {
  char title[2049], code[64];
  int f = 0, tr = 0;
  pthread_mutex_lock(&lock);
  if (focus && active && active->seq == generation && active->stage == H_WAIT) {
    f = focus;
    tr = active->trusted;
    snprintf(title, sizeof title, "%s", active->request.title);
    snprintf(code, sizeof code, "Desktop code: %.6s", active->launch.pin + 8);
  }
  pthread_mutex_unlock(&lock);
  if (!f)
    return;
  gfx_cor((GfxRect){0, 0, 1920, 1080}, 0, 0, 0, 0, .65f);
  gfx_chrome((GfxRect){460, 330, 1000, 420}, 24.f / 420.f, 1);
  txt_desenhar(
      txt_linha(TXT_TITULO2,
                tr ? "Continue watching on this TV?" : "Pair Nuvio Desktop?",
                255, 255, 255, 255),
      510, 375);
  txt_desenhar(txt_linha_corta(TXT_BODY, title, 235, 235, 235, 255, 900), 510,
               455);
  if (!tr)
    txt_desenhar(txt_linha(TXT_BODY, code, 200, 200, 200, 255), 510, 515);
  for (int i = 1; i <= 2; i++) {
    float x = i == 1 ? 510 : 960;
    gfx_cor((GfxRect){x, 620, 400, 70}, 14.f / 70.f, f == i ? .95f : .2f,
            f == i ? .95f : .2f, f == i ? .95f : .2f, 1);
    int c = f == i ? 15 : 255;
    txt_desenhar(
        txt_linha(TXT_BODY, i == 1 ? "Continue" : "Cancel", c, c, c, 255),
        x + 30, 637);
  }
}

#endif

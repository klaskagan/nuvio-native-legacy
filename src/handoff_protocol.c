#include "handoff_protocol.h"
#include "deeplink.h"
#include "js.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Locate direct fields only, reject duplicates and malformed separators. */
static const char *field(const char *s, const char *key) {
  const char *found = NULL, *p = s;
  char k[128];
  if (!s || strlen(s) > 16384)
    return NULL;
  while (isspace((unsigned char)*p))
    p++;
  if (*p++ != '{')
    return NULL;
  for (int n = 0; n < 64; n++) {
    while (isspace((unsigned char)*p))
      p++;
    if (*p == '}')
      return found;
    if (*p != '"' || !js_cadeia(p, k, sizeof k))
      return NULL;
    p++;
    while (*p && *p != '"') {
      if (*p == '\\' && p[1])
        p++;
      p++;
    }
    if (!*p++)
      return NULL;
    while (isspace((unsigned char)*p))
      p++;
    if (*p++ != ':')
      return NULL;
    while (isspace((unsigned char)*p))
      p++;
    if (!strcmp(key, k)) {
      if (found)
        return NULL;
      found = p;
    }
    if (*p == '{' || *p == '[') {
      p = js_fim(p);
      if (!p)
        return NULL;
    } else if (*p == '"') {
      p++;
      while (*p && *p != '"') {
        if (*p == '\\' && p[1])
          p++;
        p++;
      }
      if (!*p++)
        return NULL;
    } else {
      const char *a = p;
      while (*p && *p != ',' && *p != '}')
        p++;
      if (p == a)
        return NULL;
    }
    while (isspace((unsigned char)*p))
      p++;
    if (*p == '}')
      return found;
    if (*p++ != ',')
      return NULL;
  }
  return NULL;
}
static int text(const char *s, const char *key, char *out, size_t cap) {
  const char *v = field(s, key);
  return v && *v == '"' && js_cadeia(v, out, cap) && out[0];
}
static double number(const char *s, const char *key) {
  const char *v = field(s, key);
  char *e;
  double d;
  if (!v || (!isdigit((unsigned char)*v) && *v != '-'))
    return NAN;
  d = strtod(v, &e);
  while (isspace((unsigned char)*e))
    e++;
  return (*e == ',' || *e == '}') && isfinite(d) ? d : NAN;
}
static int ident(const char *s) {
  size_t n = strlen(s);
  if (n < 1 || n > 64)
    return 0;
  for (; *s; s++)
    if (!isalnum((unsigned char)*s) && *s != '-')
      return 0;
  return 1;
}
int handoff_parse_launch(const char *json, HandoffLaunch *o) {
  char raw[4097];
  const char *s = json;
  unsigned a, b, c, d, port;
  int n = 0;
  memset(o, 0, sizeof *o);
  if (!json || strlen(json) > 4096)
    return 0;
  if (js_bruto(json, NULL, "handoff", raw, sizeof raw))
    s = raw;
  if (!text(s, "endpoint", o->endpoint, sizeof o->endpoint) ||
      !text(s, "publicKeyPin", o->pin, sizeof o->pin) ||
      !text(s, "token", o->token, sizeof o->token) ||
      !text(s, "requestId", o->id, sizeof o->id) ||
      !text(s, "senderDeviceId", o->sender, sizeof o->sender) ||
      !text(s, "targetDeviceId", o->target, sizeof o->target))
    return 0;
  if (sscanf(o->endpoint, "https://%u.%u.%u.%u:%u/v1/handoff%n", &a, &b, &c, &d,
             &port, &n) != 5 ||
      !n || o->endpoint[n] || a > 255 || b > 255 || c > 255 || d > 255 ||
      port < 1 || port > 65535 ||
      !(a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
        (a == 169 && b == 254)))
    return 0;
  /* Canonical numeric IP prevents alternative URL interpretations by libcurl.
   */
  char canonical[128];
  snprintf(canonical, sizeof canonical, "https://%u.%u.%u.%u:%u/v1/handoff", a,
           b, c, d, port);
  if (strcmp(canonical, o->endpoint))
    return 0;
  if (strlen(o->token) != 64 || strlen(o->pin) != 52 ||
      strncmp(o->pin, "sha256//", 8) || o->pin[51] != '=')
    return 0;
  for (int i = 0; i < 64; i++)
    if (!strchr("0123456789abcdef", o->token[i]))
      return 0;
  for (int i = 8; i < 51; i++)
    if (!isalnum((unsigned char)o->pin[i]) && o->pin[i] != '+' &&
        o->pin[i] != '/')
      return 0;
  return ident(o->id) && ident(o->sender) && ident(o->target);
}
int handoff_parse_request(const char *s, const HandoffLaunch *l, double now,
                          HandoffRequest *o) {
  char type[16], expected[96], url[160];
  DeepLink dl;
  double profile, season, episode;
  memset(o, 0, sizeof *o);
  if (!s || number(s, "version") != 1 ||
      !text(s, "requestId", o->id, sizeof o->id) || strcmp(o->id, l->id) ||
      !text(s, "senderDeviceId", o->sender, sizeof o->sender) ||
      strcmp(o->sender, l->sender) ||
      !text(s, "targetDeviceId", o->target, sizeof o->target) ||
      strcmp(o->target, l->target) ||
      !text(s, "accountId", o->account, sizeof o->account) ||
      !ident(o->account) ||
      !text(s, "contentId", o->content, sizeof o->content) ||
      !text(s, "videoId", o->video, sizeof o->video) ||
      !text(s, "title", o->title, sizeof o->title) ||
      !text(s, "contentType", type, sizeof type))
    return 0;
  o->series = !strcmp(type, "series");
  if (!o->series && strcmp(type, "movie"))
    return 0;
  profile = number(s, "profileId");
  season = number(s, "season");
  episode = number(s, "episode");
  if (!isfinite(profile) || profile < 1 || profile > 6 ||
      floor(profile) != profile)
    return 0;
  o->profile = (int)profile;
  if (o->series) {
    if (!isfinite(season) || season < 0 || season > 9999 ||
        floor(season) != season || !isfinite(episode) || episode < 1 ||
        episode > 9999 || floor(episode) != episode)
      return 0;
    o->season = (int)season;
    o->episode = (int)episode;
  }
  snprintf(url, sizeof url, "nuvio-legacy://play/%s/%s", type, o->content);
  if (!deeplink_parse(url, &dl))
    return 0;
  if (o->series)
    snprintf(expected, sizeof expected, "%s:%d:%d", o->content, o->season,
             o->episode);
  else
    snprintf(expected, sizeof expected, "%s", o->content);
  if (strcmp(expected, o->video))
    return 0;
  o->created = number(s, "createdAtEpochMs");
  o->expires = number(s, "expiresAtEpochMs");
  o->position = number(s, "positionMs");
  o->duration = number(s, "durationMs");
  if (!isfinite(o->created) || !isfinite(o->expires) ||
      !isfinite(o->position) || !isfinite(o->duration) ||
      o->created > now + 5000 || o->expires <= now ||
      o->expires - o->created < 1 || o->expires - o->created > 120000 ||
      o->position < 0 || o->position >= o->duration ||
      o->duration > 2147483647.)
    return 0;
  text(s, "editionHint", o->edition, sizeof o->edition);
  return 1;
}

int handoff_content_matches(const char *content, int series, const char *imdb,
                            long tmdb, int catalog_series) {
  char url[160];
  DeepLink d;
  if (series != catalog_series)
    return 0;
  snprintf(url, sizeof url, "nuvio-legacy://meta/%s/%s",
           series ? "series" : "movie", content);
  if (!deeplink_parse(url, &d))
    return 0;
  return d.imdb[0] ? imdb && !strcmp(d.imdb, imdb)
                   : d.tmdb > 0 && d.tmdb == tmdb;
}

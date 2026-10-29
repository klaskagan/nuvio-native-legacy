#ifndef NV_HANDOFF_PROTOCOL_H
#define NV_HANDOFF_PROTOCOL_H
#include <stddef.h>
typedef struct {
  char endpoint[128], pin[53], token[65], id[65], sender[65], target[65];
} HandoffLaunch;
typedef struct {
  char id[65], sender[65], target[65], account[65], content[64], video[96],
      title[2049], edition[64];
  int profile, season, episode, series;
  double created, expires, position, duration;
} HandoffRequest;
int handoff_parse_launch(const char *json, HandoffLaunch *out);
int handoff_parse_request(const char *json, const HandoffLaunch *launch,
                          double now, HandoffRequest *out);

int handoff_content_matches(const char *content, int series, const char *imdb,
                            long tmdb, int catalog_series);
#endif

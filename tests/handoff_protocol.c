#include "handoff_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *launch =
    "{\"endpoint\":\"https://192.168.1.2:4444/v1/"
    "handoff\",\"publicKeyPin\":\"sha256//"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\",\"token\":"
    "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
    "\"requestId\":\"request-1\",\"senderDeviceId\":\"desktop-1\","
    "\"targetDeviceId\":\"tv-1\"}";
static const char *request =
    "{\"version\":1,\"requestId\":\"request-1\",\"senderDeviceId\":\"desktop-"
    "1\",\"targetDeviceId\":\"tv-1\",\"accountId\":\"user-1\",\"profileId\":1,"
    "\"contentType\":\"movie\",\"contentId\":\"tt0111161\",\"videoId\":"
    "\"tt0111161\",\"title\":\"Movie\",\"positionMs\":900000,\"durationMs\":"
    "7200000,\"createdAtEpochMs\":1000000,\"expiresAtEpochMs\":1120000}";
int main(void) {
  HandoffLaunch l;
  HandoffRequest r;
  char buf[5000];
  assert(handoff_parse_launch(launch, &l));
  assert(handoff_parse_request(request, &l, 1000500, &r));
  assert(r.position == 900000);
  assert(!handoff_parse_request(request, &l, 1120000, &r));
  snprintf(buf, sizeof buf, "{\"endpoint\":\"http://127.0.0.1/\",%s",
           launch + 1);
  assert(!handoff_parse_launch(buf, &l));
  assert(handoff_parse_launch(launch, &l));
  strcpy(l.id, "another");
  assert(!handoff_parse_request(request, &l, 1000500, &r));
  assert(handoff_parse_launch(launch, &l));
  snprintf(buf, sizeof buf, "{\"profileId\":2,%s", request + 1);
  assert(!handoff_parse_request(buf, &l, 1000500, &r));
  snprintf(buf, sizeof buf, "{\"positionMs\":1e999,%s", request + 1);
  assert(!handoff_parse_request(buf, &l, 1000500, &r));
  assert(handoff_content_matches("tt0111161", 0, "tt0111161", 1, 0));
  assert(handoff_content_matches("tmdb:603", 0, "tt0133093", 603, 0));
  assert(!handoff_content_matches("tmdb:603", 1, "tt0133093", 603, 0));
  assert(!handoff_content_matches("tt0111161", 0, "tt0133093", 603, 0));
  puts("handoff protocol: PASS (identity, expiry, exact position, duplicate "
       "fields, local endpoint)");
}

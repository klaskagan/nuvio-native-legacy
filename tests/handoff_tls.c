#include "rede.h"
#include <assert.h>
#include <stdio.h>
int main(int argc, char **argv) {
  assert(argc == 3);
  RedePedido p = {0};
  RedeResposta r;
  const char *headers[] = {"Authorization: Bearer synthetic-fixture-token",
                           NULL};
  p.url = argv[1];
  p.tls_chave_pin = argv[2];
  p.cabecalhos = headers;
  p.prazo_ms = 3000;
  p.max_bytes = 16384;
  assert(rede_pedir(&p, &r) && r.status == 200);
  rede_resposta_limpar(&r);
  p.tls_chave_pin = "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  assert(!rede_pedir(&p, &r) && r.curl_erro == 90 && !r.corpo);
  rede_resposta_limpar(&r);
  p.tls_chave_pin = argv[2];
  p.seguir = 1;
  assert(!rede_pedir(&p, &r) && r.erro == REDE_ENTRADA);
  rede_resposta_limpar(&r);
  p.seguir = 0;
  p.url = "http://127.0.0.1/";
  assert(!rede_pedir(&p, &r) && r.erro == REDE_ENTRADA);
  rede_resposta_limpar(&r);
  puts("handoff pinned TLS: PASS (valid key, wrong key blocked, no HTTP or "
       "redirects)");
}

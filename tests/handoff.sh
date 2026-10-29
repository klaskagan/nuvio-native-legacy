#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d "${TMPDIR:-/tmp}/nuvio-handoff.XXXXXX")
srv="";trap '[ -z "$srv" ] || kill "$srv" 2>/dev/null || true; rm -rf "$tmp"' EXIT
cc -O1 -g -Isrc tests/handoff_protocol.c src/handoff_protocol.c src/js.c src/deeplink.c -pthread -lm -o "$tmp/protocol"
"$tmp/protocol"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost -keyout "$tmp/key.pem" -out "$tmp/cert.pem" > "$tmp/cert.log" 2>&1
pin="sha256//$(openssl x509 -in "$tmp/cert.pem" -pubkey -noout | openssl pkey -pubin -outform DER | openssl dgst -sha256 -binary | openssl base64 -A)"
python3 tests/rede_pedido_server.py "$tmp/ports" "$tmp/cert.pem" "$tmp/key.pem" &
srv=$!
for i in $(seq 50); do [ -s "$tmp/ports" ] && break; sleep 0.1; done
test -s "$tmp/ports"
flags=(-O1 -g -Isrc -pthread);if [ "$(uname)" != Darwin ]; then flags+=(-ldl);fi
cc "${flags[@]}" tests/handoff_tls.c src/rede.c src/redeurl.c -o "$tmp/tls"
"$tmp/tls" "$(sed -n '2p' "$tmp/ports")/ok" "$pin"

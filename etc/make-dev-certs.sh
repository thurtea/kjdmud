#!/bin/sh
# Self-signed localhost pair for ports 1126 and 1128. Not for production.
set -e
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
openssl req -x509 -newkey rsa:2048 \
  -keyout "$root/etc/dev-key.pem" \
  -out "$root/etc/dev-cert.pem" \
  -days 365 -nodes \
  -subj "/CN=localhost"
echo "wrote $root/etc/dev-cert.pem and $root/etc/dev-key.pem"

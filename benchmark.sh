#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$project_dir"

cleanup() {
  docker compose down -v --remove-orphans
}
trap cleanup EXIT

docker compose down -v --remove-orphans
docker compose build
docker compose up -d --wait
MSYS_NO_PATHCONV=1 docker compose exec -T postgres \
  psql -X -v ON_ERROR_STOP=1 -U postgres -d mdydate_lab \
  -f /project/lab/compare-indexes.sql
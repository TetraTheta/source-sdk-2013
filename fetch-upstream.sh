#!/usr/bin/env bash
set -euo pipefail

# Fetch latest commits from the two upstream repositories and place them into
# local branches 'upstream/ez2' and 'upstream/mapbase'

EZ2_REMOTE_NAME="remote-ez2"
EZ2_REMOTE_URL="https://github.com/entropy-zero/source-sdk-2013.git"
MAPBASE_REMOTE_NAME="remote-mapbase"
MAPBASE_REMOTE_URL="https://github.com/mapbase-source/source-sdk-2013.git"

cd "$(git rev-parse --show-toplevel)"

if git remote get-url "$EZ2_REMOTE_NAME" >/dev/null 2>&1; then
  git remote set-url "$EZ2_REMOTE_NAME" "$EZ2_REMOTE_URL"
else
  git remote add "$EZ2_REMOTE_NAME" "$EZ2_REMOTE_URL"
fi

if git remote get-url "$MAPBASE_REMOTE_NAME" >/dev/null 2>&1; then
  git remote set-url "$MAPBASE_REMOTE_NAME" "$MAPBASE_REMOTE_URL"
else
  git remote add "$MAPBASE_REMOTE_NAME" "$MAPBASE_REMOTE_URL"
fi

echo "Cloning 'upstream/ez2'..."
git fetch --no-tags "$EZ2_REMOTE_NAME" +refs/heads/ez2/mapbase:refs/heads/upstream/ez2
echo "Cloning 'upstream/mapbase'..."
git fetch --no-tags "$MAPBASE_REMOTE_NAME" +refs/heads/master:refs/heads/upstream/mapbase

echo "Fetched and updated:"
echo "  - upstream/ez2  <= $EZ2_REMOTE_NAME/ez2/mapbase"
echo "  - upstream/mapbase <= $MAPBASE_REMOTE_NAME/master"

read -r -n 1 -s -p "Press any key to continue..."
echo

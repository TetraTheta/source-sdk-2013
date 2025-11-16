#!/usr/bin/env bash
set -euo pipefail

# !!! DANGER !!!
# Delete all local branches except 'main', then create the 'upstream/*' and 'gm/*' branches.

cd "$(git rev-parse --show-toplevel)"
echo "WARNING: This script will DELETE ALL LOCAL BRANCHES except 'main'."
echo "It will then fetch specified upstream branches into local 'upstream/ez2' and 'upstream/mapbase'."
echo "After that, it will create 'gm/ez2' and 'gm/mapbase' from them."
echo
read -r -p "Type 'CONFIRM' to proceed: " CONFIRM
if [ "$CONFIRM" != "CONFIRM" ]; then
  echo "Aborting. No changes were made."
  exit 1
fi

if ! git show-ref --verify --quiet refs/heads/main; then
  echo "Creating 'main' branch with README.md"
  git switch --create main
  if [ ! -f README.md ]; then
    printf "%s\n" "# source-sdk-2013" >README.md
    git add README.md
    git commit -m "Initialize 'main' with README.md"
  else
    git add README.md
    if ! git rev-parse --verify --quiet HEAD; then
      git commit -m "Initialize 'main' with README.md"
    fi
  fi
fi

echo "Deleting every local branches except 'main'..."
for br in $(git for-each-ref --format='%(refname:short)' refs/heads/); do
  if [ "$br" != "main" ]; then
    git branch -D "$br"
  fi
done

EZ2_REMOTE_NAME="remote-ez2"
EZ2_REMOTE_URL="https://github.com/entropy-zero/source-sdk-2013.git"
MAPBASE_REMOTE_NAME="remote-mapbase"
MAPBASE_REMOTE_URL="https://github.com/mapbase-source/source-sdk-2013.git"

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

echo "Creating 'gm/ez2' from 'upstream/ez2'..."
if git show-ref --verify --quiet refs/heads/upstream/ez2; then
  git branch --force gm/ez2 upstream/ez2
else
  echo "'upstream/ez2' does not exist locally. aborting."
  exit 2
fi

echo "Creating 'gm/mapbase' from 'upstream/mapbase'..."
if git show-ref --verify --quiet refs/heads/upstream/mapbase; then
  git branch --force gm/mapbase upstream/mapbase
else
  echo "upstream/mapbase does not exist locally. aborting."
  exit 2
fi

git switch main

echo "Initialization complete. Current local branches:"
git branch --sort=-committerdate --format="  - %(refname:short)"
echo
echo "Notes:"
echo "- 'upstream/ez2' and 'upstream/mapbase' are now local branches matching the remotes."
echo "- 'gm/ez2' and 'gm/mapbase' are created from those upstream branches."
echo "- Remote branches on 'origin' (or other remotes) were NOT changed by this script."

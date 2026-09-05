#!/usr/bin/env bash
set -euo pipefail

# Rebase 'gm/ez2' onto 'upstream/ez2' and 'gm/mapbase' onto 'upstream/mapbase'.
# This rewrites the 'gm/*' branches so their commits appear on top of the upstreams.
# If a conflict occurs the script will stop so you can resolve it manually.

cd "$(git rev-parse --show-toplevel)"

declare -A MAP
MAP["gm/ez2"]="upstream/ez2"
MAP["gm/mapbase"]="upstream/mapbase"

for gbranch in "${!MAP[@]}"; do
  upstream="${MAP[$gbranch]}"

  if ! git show-ref --verify --quiet "refs/heads/$upstream"; then
    echo "Upstream branch '$upstream' not found locally. Run fetch-upstream.sh first."
    exit 1
  fi

  if ! git show-ref --verify --quiet "refs/heads/$gbranch"; then
    echo "Branch '$gbranch' not found locally; skipping."
    continue
  fi

  echo "Rebasing '$gbranch' onto '$upstream'..."
  git switch "$gbranch"

  # Rebase commits that are unique to gbranch onto upstream.
  # --autostash helps keep working tree state; on conflict, rebase stops.
  if ! git rebase --autostash "$upstream"; then
    echo "Rebase of '$gbranch' onto '$upstream' failed due to conflicts."
    echo "Resolve conflicts, then run: git rebase --continue"
    echo "Or to abort and restore original branch: git rebase --abort"
    exit 2
  fi

  echo "Rebase of '$gbranch' onto '$upstream' completed successfully."
done

if git show-ref --verify --quiet refs/heads/main; then
  git switch main >/dev/null
fi

echo "All done."

read -r -n 1 -s -p "Press any key to continue..."
echo

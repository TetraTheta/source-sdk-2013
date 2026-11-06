#!/usr/bin/env bash
set -euo pipefail

# Rebase upstream-based branches first, then replay RTSL on Map Labs.
# This rewrites the 'gm/*' branches so their commits appear on top of the upstreams.
# If a conflict occurs the script will stop so you can resolve it manually.

cd "$(git rev-parse --show-toplevel)"

run_rebase() {
  local rtsl_base=""
  if git show-ref --verify --quiet refs/heads/gm/rtsl; then
    if ! rtsl_base="$(git rev-parse --verify gm/rtsl^)" ||
       ! git merge-base --is-ancestor "$rtsl_base" gm/maplabs ||
       git rev-parse --verify --quiet gm/rtsl^2 >/dev/null; then
      echo "Expected one RTSL commit on the local Map Labs history."
      return 1
    fi
  fi

  declare -A MAP
  MAP["gm/ez2"]="upstream/ez2"
  MAP["gm/mapbase"]="upstream/mapbase"
  MAP["gm/maplabs"]="upstream/maplabs"

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

  if [[ -n "$rtsl_base" ]]; then
    echo "Rebasing 'gm/rtsl' onto 'gm/maplabs'..."
    if ! git rebase --autostash --onto gm/maplabs "$rtsl_base" gm/rtsl; then
      echo "Resolve conflicts, then run: git rebase --continue"
      echo "Or to abort and restore original branch: git rebase --abort"
      exit 2
    fi
  fi

  if git show-ref --verify --quiet refs/heads/main; then
    git switch main >/dev/null
  fi

}

# Parse the complete operation before switching away from main.
run_rebase

echo "All done."

read -r -n 1 -s -p "Press any key to continue..."
echo

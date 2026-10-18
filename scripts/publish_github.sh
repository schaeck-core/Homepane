#!/usr/bin/env bash
# Publishes the current commit to GitHub (schaeck-core/Homepane) without the
# development history: one commit per publish on GitHub's main, holding the
# files of HEAD (minus the Gitea-only paths below). Day-to-day development,
# test releases and the test web flasher stay on Gitea (origin).
#
#   scripts/publish_github.sh "Release v0.2.0"     # commit message
#
# Then create the release on GitHub (runs .github/workflows/release.yml):
#   gh release create v0.2.0 --repo schaeck-core/Homepane --target main --title v0.2.0 --notes "..."
set -euo pipefail

REMOTE_URL="https://github.com/schaeck-core/Homepane.git"
REMOTE="github"
EXCLUDE=(.gitea) # Gitea's own CI (internal hosts); not needed on GitHub
MESSAGE="${1:?usage: $0 \"commit message\"}"

cd "$(git rev-parse --show-toplevel)"
if [[ -n "$(git status --porcelain --untracked-files=no -- . ':!lvgl')" ]]; then
    echo "Uncommitted changes: commit them first (GitHub gets HEAD, not the working tree)." >&2
    exit 1
fi
git remote get-url "$REMOTE" >/dev/null 2>&1 || git remote add "$REMOTE" "$REMOTE_URL"
git fetch -q "$REMOTE" 2>/dev/null || true

# HEAD's tree without the excluded paths, built in a temporary index
INDEX="$(mktemp)"
trap 'rm -f "$INDEX"' EXIT
GIT_INDEX_FILE="$INDEX" git read-tree HEAD
for path in "${EXCLUDE[@]}"; do
    GIT_INDEX_FILE="$INDEX" git rm -q -r --cached --ignore-unmatch -- "$path"
done
TREE="$(GIT_INDEX_FILE="$INDEX" git write-tree)"

PARENT=()
if git rev-parse -q --verify "refs/remotes/$REMOTE/main" >/dev/null; then
    if [[ "$(git rev-parse "refs/remotes/$REMOTE/main^{tree}")" == "$TREE" ]]; then
        echo "GitHub already has these files."
        exit 0
    fi
    PARENT=(-p "refs/remotes/$REMOTE/main")
fi
# Public commits carry the GitHub account's no-reply address, not the one
# from the local git config.
export GIT_AUTHOR_NAME="${GITHUB_AUTHOR_NAME:-$(git config user.name)}"
export GIT_AUTHOR_EMAIL="${GITHUB_AUTHOR_EMAIL:-341057160+schaeck-core@users.noreply.github.com}"
export GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME" GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"
COMMIT="$(git commit-tree "$TREE" ${PARENT[@]+"${PARENT[@]}"} -m "$MESSAGE")"
git push "$REMOTE" "$COMMIT:refs/heads/main"
echo "GitHub main is now $COMMIT ($(git rev-parse --short HEAD) on Gitea)."

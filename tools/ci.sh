#!/usr/bin/env bash
# Everything a commit has to pass: firmware build (SDCC 4.5, which the firmware is
# sized for), USB descriptor checks, simulator tests, action text tests, and the setup
# page in Chrome, from the repository and as the built website.
# With RELEASE_TAG set it also writes .ci/firmware-note.md for the release notes and
# stops if the firmware changed without a new version (tools/package.py --notes).
# Used by .github/workflows/test.yml and release.yml; runs on Linux and WSL.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .ci

if ! sdcc --version 2>/dev/null | grep -q " 4\.5\.0 "; then
  if [ ! -x .ci/sdcc-4.5.0/bin/sdcc ]; then
    curl -fsSL -o .ci/sdcc.tar.bz2 \
      https://downloads.sourceforge.net/project/sdcc/sdcc-linux-amd64/4.5.0/sdcc-4.5.0-amd64-unknown-linux2.5.tar.bz2
    echo "3395722e3f8a31ce6a14c75fc419f586a3573b939f85efee5c88cdd35820a240  .ci/sdcc.tar.bz2" | sha256sum -c -
    tar xjf .ci/sdcc.tar.bz2 -C .ci
  fi
  export PATH="$PWD/.ci/sdcc-4.5.0/bin:$PATH"
fi

python3 firmware/build.py
python3 firmware/test/check_descriptors.py
python3 firmware/test/run_tests.py
python3 test/test_actions.py
if [ -n "${RELEASE_TAG:-}" ]; then
  python3 tools/package.py --notes "$RELEASE_TAG" | tee .ci/firmware-note.md
fi

if ! command -v google-chrome >/dev/null && [ -z "${CI:-}" ]; then
  echo "Google Chrome not found: setup page tests skipped (they always run on GitHub)"
  exit 0
fi
[ -x .ci/venv/bin/python ] || python3 -m venv .ci/venv
.ci/venv/bin/pip install -q -r test/requirements.txt
rm -rf .ci/site
python3 tools/package.py --site .ci/site
.ci/venv/bin/python test/test_web.py
.ci/venv/bin/python test/test_web.py --site .ci/site

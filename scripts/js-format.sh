#!/bin/bash

set -e

JS_DIRS=(
  # Main source files
  "src"
  # Example app
  "example/src"
)

if which prettier >/dev/null; then
  DIRS=$(printf "%s " "${JS_DIRS[@]}")
  find $DIRS -type f \( -name "*.js" -o -name "*.jsx" -o -name "*.ts" -o -name "*.tsx" -o -name "*.mjs" -o -name "*.cjs" \) -print0 | while read -d $'\0' file; do
    prettier --write "$file"
  done
  echo "JS/TS Format done!"
else
  echo "error: prettier not installed, install with 'npm install -g prettier' or 'yarn global add prettier'"
  exit 1
fi

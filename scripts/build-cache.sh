#!/usr/bin/env bash
# Shared compile-cache validation for build-core.sh and build-sim.sh.
#
# An object is reusable only when its effective compiler inputs match, every
# non-system header recorded by the compiler is current, and the SDK identity is
# unchanged. The dependency file comes from clang/gcc's -MMD output.

oga_cache_file_fingerprint() {
  local file="$1" output
  if command -v sha256sum >/dev/null 2>&1; then
    output="$(sha256sum -- "$file")" || return 1
    printf '%s\n' "${output%% *}"
  elif command -v shasum >/dev/null 2>&1; then
    output="$(shasum -a 256 "$file")" || return 1
    printf '%s\n' "${output%% *}"
  else
    output="$(cksum "$file")" || return 1
    set -- $output
    printf '%s:%s\n' "$1" "$2"
  fi
}

oga_cache_fingerprint() {
  local output
  if command -v sha256sum >/dev/null 2>&1; then
    output="$(printf '%s\0' "$@" | sha256sum)" || return 1
    printf '%s\n' "${output%% *}"
  elif command -v shasum >/dev/null 2>&1; then
    output="$(printf '%s\0' "$@" | shasum -a 256)" || return 1
    printf '%s\n' "${output%% *}"
  else
    output="$(printf '%s\0' "$@" | cksum)" || return 1
    set -- $output
    printf '%s:%s\n' "$1" "$2"
  fi
}

oga_cache_compiler_id() {
  local compiler="$1" resolved version binary_id
  resolved="$(command -v "$compiler")" || return 1
  version="$("$compiler" --version 2>&1)" || return 1
  binary_id="$(oga_cache_file_fingerprint "$resolved")" || return 1
  oga_cache_fingerprint "$compiler" "$resolved" "$version" "$binary_id"
}

oga_cache_sdk_id() {
  local sdk="$1" settings_id
  if [ -f "$sdk/SDKSettings.plist" ]; then
    settings_id="$(oga_cache_file_fingerprint "$sdk/SDKSettings.plist")" || return 1
  else
    # Without SDK metadata, disable reuse across build invocations rather than
    # risk treating changed SDK headers as current.
    settings_id="uncacheable:$$:$(date +%s)"
  fi
  oga_cache_fingerprint "$sdk" "$settings_id"
}

oga_cache_write_fingerprint() {
  local file="$1" fingerprint="$2" tmp="${1}.tmp.$$"
  printf '%s\n' "$fingerprint" > "$tmp" || { rm -f "$tmp"; return 1; }
  mv -f "$tmp" "$file"
}

oga_cache_is_valid() {
  local object="$1" depfile="$2" signature_file="$3" fingerprint="$4"
  local prerequisite existing
  shift 4

  [ -f "$object" ] && [ -f "$depfile" ] && [ -f "$signature_file" ] || return 1
  IFS= read -r existing < "$signature_file" || return 1
  [ "$existing" = "$fingerprint" ] || return 1

  for prerequisite in "$@"; do
    [ -f "$prerequisite" ] && [ "$object" -nt "$prerequisite" ] || return 1
  done

  command -v make >/dev/null 2>&1 || return 1
  make -s -q -f "$depfile" "$object" >/dev/null 2>&1
}

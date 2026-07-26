#!/usr/bin/env bash
set -euo pipefail

readonly usage='usage: wordcount.bash [--json] [--top N] [--max-word N] <file>'
readonly checksum_offset=2166136261
readonly checksum_prime=16777619
readonly -a letters=(
  a b c d e f g h i j k l m
  n o p q r s t u v w x y z
)

declare -a input_bytes=()
declare -a top_words=()
declare -a top_counts=()
declare -i total=0
declare -i unique=0
declare -i checksum_value=0

die() {
  printf '%s\n' "$1" >&2
  exit 1
}

parse_number() {
  local raw=$1
  local name=$2
  [[ $raw =~ ^[0-9]+$ ]] || die "wordcount.bash: $name must be a number"
  printf '%d' "$((10#$raw))"
}

normalize_max_word() {
  local value=$1
  if ((value == 0)); then
    printf '64'
  elif ((value < 4)); then
    printf '4'
  elif ((value > 1024)); then
    printf '1024'
  else
    printf '%d' "$value"
  fi
}

count_words() {
  local limit=$1
  local max_word=$2
  local byte lower word='' line
  local -A counts=()
  local -a sorted=()

  total=0
  for byte in "${input_bytes[@]}"; do
    lower=$((byte | 32))
    if ((lower >= 97 && lower <= 122)); then
      if ((${#word} < max_word)); then
        word+=${letters[lower - 97]}
      fi
    elif [[ -n $word ]]; then
      counts["$word"]=$((${counts["$word"]:-0} + 1))
      ((total += 1))
      word=''
    fi
  done
  if [[ -n $word ]]; then
    counts["$word"]=$((${counts["$word"]:-0} + 1))
    ((total += 1))
  fi

  unique=${#counts[@]}
  mapfile -t sorted < <(
    for word in "${!counts[@]}"; do
      printf '%d\t%s\n' "${counts["$word"]}" "$word"
    done | LC_ALL=C sort -t $'\t' -k1,1nr -k2,2
  )

  top_words=()
  top_counts=()
  for line in "${sorted[@]:0:limit}"; do
    top_counts+=("${line%%$'\t'*}")
    top_words+=("${line#*$'\t'}")
  done
}

mix_byte() {
  checksum_value=$(((checksum_value ^ $1) * checksum_prime & 0xffffffff))
}

mix_integer() {
  local value=$1
  local width=$2
  local index
  for ((index = 0; index < width; index += 1)); do
    mix_byte "$((value & 0xff))"
    value=$((value >> 8))
  done
}

checksum_result() {
  local word count index character byte
  checksum_value=$checksum_offset
  mix_integer "$total" 8
  mix_integer "$unique" 8
  for index in "${!top_words[@]}"; do
    word=${top_words[index]}
    count=${top_counts[index]}
    for ((character = 0; character < ${#word}; character += 1)); do
      printf -v byte '%d' "'${word:character:1}"
      mix_byte "$byte"
    done
    mix_integer "$count" 8
  done
}

render_json() {
  local index separator=''
  printf '{"total":%d,"unique":%d,"top":[' "$total" "$unique"
  for index in "${!top_words[@]}"; do
    printf '%s{"word":"%s","count":%d}' \
      "$separator" "${top_words[index]}" "${top_counts[index]}"
    separator=','
  done
  printf ']}\n'
}

render_text() {
  local index
  printf 'count word\n'
  for index in "${!top_words[@]}"; do
    printf '%d %s\n' "${top_counts[index]}" "${top_words[index]}"
  done
  printf 'total %d\nunique %d\n' "$total" "$unique"
}

top=10
max_word=1024
bench_runs=0
bench_warmups=0
json=false
path=''

while (($# > 0)); do
  case $1 in
    --json)
      json=true
      shift
      ;;
    --top | --max-word | --bench-runs | --bench-warmups)
      (($# >= 2)) || die "$usage"
      value=$(parse_number "$2" "$1")
      case $1 in
        --top) top=$value ;;
        --max-word) max_word=$value ;;
        --bench-runs) bench_runs=$value ;;
        --bench-warmups) bench_warmups=$value ;;
        *) die "$usage" ;;
      esac
      shift 2
      ;;
    --top=* | --max-word=* | --bench-runs=* | --bench-warmups=*)
      name=${1%%=*}
      value=$(parse_number "${1#*=}" "$name")
      case $name in
        --top) top=$value ;;
        --max-word) max_word=$value ;;
        --bench-runs) bench_runs=$value ;;
        --bench-warmups) bench_warmups=$value ;;
        *) die "$usage" ;;
      esac
      shift
      ;;
    -*)
      die "$usage"
      ;;
    *)
      [[ -z $path ]] || die "$usage"
      path=$1
      shift
      ;;
  esac
done

if [[ -z $path ]] || ((top == 0)); then
  die "$usage"
fi
max_word=$(normalize_max_word "$max_word")
if ! byte_dump=$(od -An -v -tu1 -- "$path" | awk '{ for (i = 1; i <= NF; i++) print $i }'); then
  die "wordcount.bash: cannot read $path"
fi
if [[ -n $byte_dump ]]; then
  mapfile -t input_bytes <<<"$byte_dump"
fi
unset byte_dump

if ((bench_runs > 0)); then
  for ((run = 0; run < bench_warmups; run += 1)); do
    count_words "$top" "$max_word"
    checksum_result
  done

  aggregate=$checksum_offset
  started=${EPOCHREALTIME/./}
  for ((run = 0; run < bench_runs; run += 1)); do
    count_words "$top" "$max_word"
    checksum_result
    result_checksum=$checksum_value
    checksum_value=$aggregate
    mix_integer "$result_checksum" 4
    aggregate=$checksum_value
  done
  elapsed=$((${EPOCHREALTIME/./} - started))
  printf '{"mean_ms":%d.%06d,"checksum":%d}\n' \
    "$((elapsed / bench_runs / 1000))" "$((elapsed * 1000 / bench_runs % 1000000))" "$aggregate"
else
  count_words "$top" "$max_word"
  if $json; then
    render_json
  else
    render_text
  fi
fi

#!/bin/bash

set -eu

usage_and_exit() {
  echo "  $0 NCOL command...
or
  cmd | $0 NCOL

  with NCOL > 0" >&2
  exit 1
}


#
# Check parameter
#

if [[ -t 0 ]]; then
  if (( $# < 2 )); then
    usage_and_exit
  fi
elif (( $# < 1 )); then
  usage_and_exit
fi

if [[ $1 = '-h' ]] || ! [[ $1 =~ ^[0-9]+$ ]] ; then
  usage_and_exit
fi


#
# Parser nline
#

declare -i nline="$1"

if (( nline <= 0 )); then
  usage_and_exit
fi
shift


#
# Read current line
#

exec 3<> /dev/tty
echo -ne "\033[6n" >&3 # cursor position request
# response format: \e[${line};${column}R
read -u 3 -s -d\[ _
read -u 3 -s -d R positions
exec 3>&-

declare -i current_line="${positions%;*}" height
read height _ <<<$(stty -F /dev/tty size)


#
# Insert new lines and update current_line / nline
#

if (( height - current_line < nline )); then
  if (( height < nline + 2 )); then
    nline=height-2
  fi

  newlines=$'\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n'
  declare -i count_nl=${#newlines} n=nline
  current_line=height-nline
  while (( n > 0 )); do
    if (( count_nl <= n )); then
      echo -n "$newlines"
      n+=-count_nl
    else
      echo -n "${newlines:0:$n}"
      n=0
    fi
  done
fi


#
# Set margin
#

reset_margin() {
  # save cursor position ; reset margins ; restore cursor position
  echo -en "\e[s\e[1;${height}r\e[u"
}

# set top and bottom margins (scroll) ; cursor position ; save cursor position
echo -en "\e[${current_line};$((current_line+nline))r\e[${current_line};1H\e7"

trap reset_margin EXIT


#
# Run command or read stdin
#

if (( $# )); then
  "$@"
else
  # >&1 <&0
  cat
fi

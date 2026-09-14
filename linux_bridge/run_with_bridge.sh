#!/bin/sh

bridge_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
helper="$bridge_dir/build/portal_sender"

if [ ! -x "$helper" ]; then
    echo "Portal helper is not built. Run: make -C linux_bridge portal" >&2
    exit 1
fi
if [ "$#" -eq 0 ]; then
    echo "Usage: $0 GAME_COMMAND [ARGUMENTS...]" >&2
    exit 2
fi

"$helper" --wait 60 &
helper_pid=$!

cleanup()
{
    kill "$helper_pid" 2>/dev/null || true
    wait "$helper_pid" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

"$@"
game_status=$?
exit "$game_status"

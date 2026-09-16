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

supervise_helper()
{
    child_pid=""

    stop_helper()
    {
        if [ -n "$child_pid" ]; then
            kill "$child_pid" 2>/dev/null || true
            wait "$child_pid" 2>/dev/null || true
        fi
        exit 0
    }
    trap stop_helper HUP INT TERM

    while :; do
        "$helper" --wait 60 &
        child_pid=$!
        wait "$child_pid"
        helper_status=$?
        child_pid=""

        # A normal exit means the captured window ended or the user requested
        # another selection. Portal cancellation/errors stop the supervisor.
        [ "$helper_status" -eq 0 ] || exit "$helper_status"
        sleep 1
    done
}

supervise_helper &
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

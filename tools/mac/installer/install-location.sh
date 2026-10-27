# Native Installer helper, sourced by package scripts. No game/UI is launched.
# Installer may pass a resolved user-home path in $2 or a home-domain target in $3.
# Resolve only the installer's current user, never a global /Applications folder.
resolve_install() {
    recipient=
    if [ "$(/usr/bin/id -u)" != 0 ]; then recipient=$(/usr/bin/id -un); fi
    for candidate_user in "${SUDO_USER:-}" "${USER:-}" "$(/usr/bin/stat -f '%Su' /dev/console 2>/dev/null || true)"; do
        [ -z "$recipient" ] || break
        [ -n "$candidate_user" ] || continue
        candidate_uid=$(/usr/bin/id -u "$candidate_user" 2>/dev/null || echo 0)
        case "$candidate_uid" in *[!0-9]*|'') continue ;; esac
        [ "$candidate_uid" -ge 500 ] || continue
        recipient=$candidate_user; break
    done
    [ -n "$recipient" ] || { echo 'Cannot resolve the recipient of a per-user installation.' >&2; return 1; }
    recipient_home=$(/usr/bin/dscl . -read "/Users/$recipient" NFSHomeDirectory | /usr/bin/sed 's/^NFSHomeDirectory: //')
    [ -d "$recipient_home" ] && [ ! -L "$recipient_home" ] || return 1
    install="$recipient_home/Applications/OpenCNC3D"
    # Native Installer must target this user's home domain. A system-volume path
    # is never accepted as a fallback for optional downloads or file replacement.
    case "${2:-}|${3:-}" in
        "$install|"*|"$recipient_home|"*|*"|$recipient_home"|*/Applications/OpenCNC3D\|*) ;;
        *) [ "${HOME:-}" = "$recipient_home" ] || return 1 ;;
    esac
    export recipient recipient_home install
}
assert_real_parents() {
    current=$install
    while [ "$current" != / ] && [ -n "$current" ]; do
        [ ! -L "$current" ] || { echo "Installation path is a link: $current" >&2; return 1; }
        current=${current%/*}; [ -n "$current" ] || current=/
    done
}
run_as_recipient() {
    if [ "$(/usr/bin/id -u)" = 0 ]; then
        /usr/bin/sudo -u "$recipient" /usr/bin/env HOME="$recipient_home" "$@"
    else
        [ "$(/usr/bin/id -un)" = "$recipient" ] || return 1
        "$@"
    fi
}

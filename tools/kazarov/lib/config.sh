# shellcheck shell=bash
# Project configuration for bin/kazarov: loads kazarov.conf, picks a
# configuration by name, and checks what it set. Sourced, not run. Expects
# KZ_ROOT and KZ_PROJECT_ROOT to be set by the driver, and lib/host.sh for
# kz_os_norm and kz_host_os.

# The variables a configuration may set; cleared before each one is selected.
KZ_CONFIG_ARRAYS="KZ_CMAKE_ARGS KZ_SETUP_TARGETS KZ_BUILD_TARGETS KZ_RUN_TARGETS KZ_LOCKS KZ_REQUIRE_ANY"
KZ_CONFIG_SCALARS="KZ_CONFIG KZ_WHERE KZ_HOST_OS KZ_BUILD_DIR KZ_PRESET KZ_SETUP_STAMP KZ_REQUIRE_MESSAGE KZ_RUN_ISO KZ_RUN_EXE KZ_RUN_OS"

# An absolute form of a path given relative to the project root.
kz_abs() {
    case $1 in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$KZ_PROJECT_ROOT" "$1" ;;
    esac
}

# Sources kazarov.conf and checks its basics. KZ_CONFIGS is optional; a
# project without it has nothing to name.
kz_load_project() {
    if [ ! -f "$KZ_PROJECT_ROOT/kazarov.conf" ]; then
        echo "kazarov: $KZ_PROJECT_ROOT/kazarov.conf not found" >&2
        exit 1
    fi
    # shellcheck disable=SC2034  # read by the driver and lib/host.sh
    KZ_CONFIGS=()
    # shellcheck disable=SC2034
    KZ_ENV_PASS=()
    # shellcheck source=/dev/null
    . "$KZ_PROJECT_ROOT/kazarov.conf"
    if [ -z "${KZ_PROJECT:-}" ]; then
        echo "kazarov: KZ_PROJECT is not set in kazarov.conf" >&2
        exit 1
    fi
    if ! declare -F kz_config >/dev/null; then
        echo "kazarov: kazarov.conf does not define kz_config" >&2
        exit 1
    fi
}

# Names the default configuration for build or run on this OS, from
# KZ_DEFAULT_<CMD>_<OS>, then KZ_DEFAULT_<CMD>. Prints the name, or nothing.
kz_default_config() {
    local cmd os var name
    cmd=$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')
    os=$(printf '%s' "${KZ_CALLER_OS:-$(kz_host_os)}" | tr '[:lower:]' '[:upper:]')
    var="KZ_DEFAULT_${cmd}_${os}"
    name=${!var:-}
    if [ -z "$name" ]; then
        var="KZ_DEFAULT_${cmd}"
        name=${!var:-}
    fi
    printf '%s\n' "$name"
}

# Selects a configuration by name through the project's kz_config. Returns 1
# when the project does not know the name. Sets the KZ_* variables it
# describes and leaves the rest unset.
kz_select() {
    local name=$1 arr sc
    for arr in $KZ_CONFIG_ARRAYS; do
        unset "$arr"
    done
    for sc in $KZ_CONFIG_SCALARS; do
        unset "$sc"
    done
    kz_config "$name" || return 1
    KZ_CONFIG=${KZ_CONFIG:-$name}
    return 0
}

# Checks the selected configuration has what the driver needs. Windows
# configurations are only delegated, so they need just the host script.
kz_check_selected() {
    case $KZ_WHERE in
        windows)
            if [ -z "${KZ_WINDOWS_HOST_SCRIPT:-}" ]; then
                echo "kazarov: $KZ_CONFIG needs KZ_WINDOWS_HOST_SCRIPT in kazarov.conf" >&2
                exit 1
            fi
            return 0 ;;
        host|container) ;;
        *)
            echo "kazarov: $KZ_CONFIG has an unknown KZ_WHERE '$KZ_WHERE'" >&2
            exit 1 ;;
    esac
    case ${KZ_BUILD_DIR:-} in
        ""|.|/|/*|..|../*|*/..|*/../*|*/.) echo "kazarov: $KZ_CONFIG needs a KZ_BUILD_DIR below the project root" >&2
            exit 1 ;;
    esac
    if [ -z "${KZ_PRESET:-}" ] && ! declare -p KZ_CMAKE_ARGS >/dev/null 2>&1; then
        echo "kazarov: $KZ_CONFIG sets neither KZ_CMAKE_ARGS nor KZ_PRESET" >&2
        exit 1
    fi
    if [ "$KZ_WHERE" = host ]; then
        if [ -z "${KZ_HOST_OS:-}" ]; then
            echo "kazarov: host configuration $KZ_CONFIG does not set KZ_HOST_OS" >&2
            exit 1
        fi
        if [ "$(kz_os_norm "$KZ_HOST_OS")" != "$(kz_host_os)" ]; then
            echo "kazarov: $KZ_CONFIG builds only on $KZ_HOST_OS" >&2
            exit 1
        fi
    fi
    return 0
}

# Checks a run has something to launch.
kz_check_runnable() {
    if [ -z "${KZ_RUN_ISO:-}" ] && [ -z "${KZ_RUN_EXE:-}" ]; then
        echo "kazarov: $KZ_CONFIG sets neither KZ_RUN_ISO nor KZ_RUN_EXE" >&2
        exit 1
    fi
}

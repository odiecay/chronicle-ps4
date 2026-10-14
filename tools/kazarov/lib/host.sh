# shellcheck shell=bash
# Host-side helpers for bin/kazarov: the container runtime, the image, and
# the machine's CPU count. Sourced, not run. Expects KZ_ROOT, KZ_PROJECT_ROOT
# and KZ_PROJECT to be set by the driver.

kz_os_norm() {
    case $(printf '%s' "$1" | tr '[:upper:]' '[:lower:]') in
        linux*) echo linux ;;
        darwin*|macos*) echo darwin ;;
        windows*|mingw*|msys*|cygwin*) echo windows ;;
        *) printf '%s' "$1" | tr '[:upper:]' '[:lower:]' ;;
    esac
}

kz_host_os() {
    kz_os_norm "$(uname -s)"
}

kz_in_msys() {
    [ "$(kz_host_os)" = windows ] && [ -n "${MSYSTEM:-}" ]
}

kz_nproc() {
    nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1
}

# Converts a host path for a bind mount; Git Bash needs the mixed Windows form.
kz_host_path() {
    if kz_in_msys; then
        cygpath -m "$1"
    else
        printf '%s\n' "$1"
    fi
}

# The image tag: the first 12 hex digits of the git tree of image/. When image/ has
# uncommitted edits or there is no commit to read, dirty-<hash of its files>.
kz_image_tag() {
    local status tree
    status=$(git -C "$KZ_ROOT" status --porcelain -- image 2>/dev/null) || status=x
    tree=$(git -C "$KZ_ROOT" rev-parse HEAD:image 2>/dev/null) || tree=
    if [ -n "$status" ] || [ -z "$tree" ]; then
        echo "dirty-$(kz_image_content_hash)"
    else
        echo "${tree:0:12}"
    fi
}

# The sha256 of stdin, as hex.
kz_sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum | cut -c1-64
    else
        shasum -a 256 | cut -c1-64
    fi
}

# The permission bits of a file, octal. stat -f is the BSD form.
kz_file_mode() {
    stat -c %a "$1" 2>/dev/null || stat -f %Lp "$1"
}

# Twelve hex digits over the path, mode and content of each file of image/ that git
# knows, tracked, untracked or ignored, in byte order; Python bytecode is skipped.
# A symlink hashes as its target text, not the content behind it. A missing file
# hashes as "missing".
kz_image_content_hash() {
    local f
    git -C "$KZ_ROOT" ls-files -co -z -- image | LC_ALL=C sort -z |
    while IFS= read -r -d '' f; do
        case $f in
            */__pycache__/*|*.pyc) continue ;;
        esac
        if [ -L "$KZ_ROOT/$f" ]; then
            printf '%s %s %s\n' "$(printf 'link:%s' "$(readlink -- "$KZ_ROOT/$f")" | kz_sha256)" \
                "$(kz_file_mode "$KZ_ROOT/$f")" "$f"
        elif [ -f "$KZ_ROOT/$f" ]; then
            printf '%s %s %s\n' "$(kz_sha256 < "$KZ_ROOT/$f")" "$(kz_file_mode "$KZ_ROOT/$f")" "$f"
        else
            printf 'missing %s\n' "$f"
        fi
    done | kz_sha256 | cut -c1-12
}

kz_image_name() {
    echo "kazarov-dev:$(kz_image_tag)"
}

# The docker context of the first running x86_64 colima profile, exported as
# DOCKER_CONTEXT. On an Apple-silicon Mac the compiler (wibo) needs a real
# x86_64 kernel, which Rosetta does not give it. Fails, with the reason in
# KZ_BUILDER_PROBLEM, when there is none.
kz_use_colima() {
    local profiles running
    if ! command -v colima >/dev/null 2>&1 || ! command -v docker >/dev/null 2>&1; then
        KZ_BUILDER_PROBLEM=no-colima
        return 1
    fi
    profiles=$(colima list --json 2>/dev/null | grep '"arch":"x86_64"') || true
    if [ -z "$profiles" ]; then
        KZ_BUILDER_PROBLEM=no-profile
        return 1
    fi
    running=$(printf '%s\n' "$profiles" | grep '"status":"Running"' | head -1)
    if [ -z "$running" ]; then
        KZ_COLIMA_PROFILE=$(printf '%s\n' "$profiles" | head -1 \
                            | sed 's/.*"name":"\([^"]*\)".*/\1/')
        KZ_BUILDER_PROBLEM=stopped
        return 1
    fi
    KZ_COLIMA_PROFILE=$(printf '%s\n' "$running" | sed 's/.*"name":"\([^"]*\)".*/\1/')
    if [ "$KZ_COLIMA_PROFILE" = default ]; then
        DOCKER_CONTEXT=colima
    else
        DOCKER_CONTEXT=colima-$KZ_COLIMA_PROFILE
    fi
    export DOCKER_CONTEXT
}

# Sets BUILDER to docker or podman. BUILDER set in the environment wins.
# Apple silicon goes to docker in colima; otherwise podman, then docker.
kz_pick_builder() {
    KZ_BUILDER_PROBLEM=
    if [ -n "${BUILDER:-}" ]; then
        return 0
    fi
    if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ]; then
        if [ -n "${DOCKER_CONTEXT:-}" ] || kz_use_colima; then
            BUILDER=docker
        fi
    elif command -v podman >/dev/null 2>&1; then
        BUILDER=podman
    elif command -v docker >/dev/null 2>&1; then
        BUILDER=docker
    fi
    if [ -z "${BUILDER:-}" ]; then
        KZ_BUILDER_PROBLEM=${KZ_BUILDER_PROBLEM:-none}
        return 1
    fi
    export BUILDER
}

kz_require_builder() {
    if kz_pick_builder; then
        return 0
    fi
    case $KZ_BUILDER_PROBLEM in
        stopped)
            echo "kazarov: the x86_64 colima VM '$KZ_COLIMA_PROFILE' is not running. Start it with:" >&2
            echo "  colima start --profile $KZ_COLIMA_PROFILE" >&2 ;;
        no-colima|no-profile)
            echo "kazarov: on an Apple-silicon Mac the build runs in an x86_64 colima VM." >&2
            echo "  brew install colima docker" >&2
            echo "  colima start --profile kazarov --arch x86_64 --cpu 4 --memory 8" >&2
            echo "Or name a runtime outright: BUILDER=docker DOCKER_CONTEXT=<context>" >&2 ;;
        *)
            echo "kazarov: podman or docker not found. Install one from:" >&2
            echo "  https://podman.io/docs/installation" >&2
            echo "  https://docs.docker.com/desktop/install" >&2 ;;
    esac
    exit 1
}

# Builds the image when it is missing, when its tag is dirty-*, or when
# REBUILD_IMAGE=1. The image is built from image/ alone, labelled with its tag.
# Sets KZ_IMAGE_REF to the image ID, which is what the containers run.
kz_ensure_image() {
    local tag name dirty=0
    kz_require_builder
    tag=$(kz_image_tag)
    name="kazarov-dev:$tag"
    case $tag in
        dirty-*) dirty=1 ;;
    esac
    if [ "$dirty" = 0 ] && [ "${REBUILD_IMAGE:-0}" != 1 ] \
       && "$BUILDER" image inspect "$name" >/dev/null 2>&1; then
        KZ_IMAGE_REF=$("$BUILDER" image inspect --format '{{.Id}}' "$name")
        return 0
    fi
    echo "kazarov: building the image $name (once per image/ change)"
    "$BUILDER" build --platform linux/amd64 \
        --build-arg "KZ_IMAGE_ID=$tag" \
        --label "org.themoonpeople.kazarov.image=$tag" \
        -t "$name" "$KZ_ROOT/image"
    KZ_IMAGE_REF=$("$BUILDER" image inspect --format '{{.Id}}' "$name")
}

# Says when the runtime's VM has fewer CPUs than the host, since that caps
# every parallel step without saying so. Never fails.
kz_report_parallelism() {
    local vm_cpus host_cpus
    vm_cpus=
    case ${BUILDER:-} in
        podman)
            vm_cpus=$(podman machine inspect --format '{{.Resources.CPUs}}' 2>/dev/null \
                      | head -1) || true ;;
        docker)
            vm_cpus=$(docker info --format '{{.NCPU}}' 2>/dev/null) || true ;;
    esac
    case $vm_cpus in
        ''|*[!0-9]*) return 0 ;;
    esac
    host_cpus=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 0)
    case $host_cpus in
        ''|*[!0-9]*) return 0 ;;
    esac
    [ "$host_cpus" -gt "$vm_cpus" ] || return 0
    echo "kazarov: the container VM has $vm_cpus of this host's $host_cpus CPUs; the build is only as parallel as that."
    if [ -n "${KZ_COLIMA_PROFILE:-}" ]; then
        echo "  colima stop --profile $KZ_COLIMA_PROFILE"
        echo "  colima start --profile $KZ_COLIMA_PROFILE --cpu $host_cpus"
    elif [ "$BUILDER" = podman ]; then
        echo "  podman machine stop"
        echo "  podman machine set --cpus $host_cpus"
        echo "  podman machine start"
    fi
    return 0
}

# Runs a command in a fresh container from the image. Arguments are the
# command. KZ_INTERACTIVE=1 adds -i; -t is added when stdout is a terminal.
# The project is mounted at /work/$KZ_PROJECT and the caller's working
# directory is mapped under it. Run from the host, not from inside.
kz_container_run() {
    local name host_root mount cwd_phys rel workdir kz_root_rel
    local -a flags=() env_args=()

    kz_require_builder
    if [ -z "${KZ_IMAGE_REF:-}" ]; then
        kz_ensure_image
    fi
    name=$KZ_IMAGE_REF

    host_root=$(kz_host_path "$KZ_PROJECT_ROOT")
    mount="$host_root:/work/$KZ_PROJECT"
    if [ "$BUILDER" = podman ] && [ "$(kz_host_os)" = linux ]; then
        mount="$mount:Z"
    fi

    cwd_phys=$(pwd -P)
    rel=${cwd_phys#"$KZ_PROJECT_ROOT"}
    case $rel in
        ''|/*) ;;
        *) rel= ;;
    esac
    case $cwd_phys in
        "$KZ_PROJECT_ROOT"|"$KZ_PROJECT_ROOT"/*) workdir="/work/$KZ_PROJECT$rel" ;;
        *) workdir="/work/$KZ_PROJECT" ;;
    esac

    flags+=(--rm --init --label org.themoonpeople.kazarov.run=1)
    flags+=(-v "$mount" -w "$workdir")
    if [ "$BUILDER" = docker ] && [ "$(kz_host_os)" = linux ]; then
        flags+=(--user "$(id -u):$(id -g)")
    fi
    if [ "${KZ_INTERACTIVE:-0}" = 1 ]; then
        flags+=(-i)
    fi
    if [ -t 1 ]; then
        flags+=(-t)
    fi

    kz_root_rel=${KZ_ROOT#"$KZ_LOGICAL_PROJECT"/}
    if [ "$kz_root_rel" = "$KZ_ROOT" ]; then
        echo "kazarov: the Kazarov checkout must sit under the project root (tools/kazarov)" >&2
        exit 1
    fi
    env_args+=(-e HOME=/tmp
               -e "KZ_PROJECT_ROOT=/work/$KZ_PROJECT"
               -e "KZ_HOST_OS=$(kz_host_os)"
               -e "KZ_IMAGE_TAG=$(kz_image_tag)"
               -e "JOBS=$JOBS")
    if [ -n "${CLEAN:-}" ]; then
        env_args+=(-e "CLEAN=$CLEAN")
    fi
    if [ -n "${KZ_RUN_OUT:-}" ]; then
        env_args+=(-e "KZ_RUN_OUT=$KZ_RUN_OUT")
    fi
    local var
    for var in ${KZ_ENV_PASS[@]+"${KZ_ENV_PASS[@]}"}; do
        if [ -n "${!var+x}" ]; then
            env_args+=(-e "$var=${!var}")
        fi
    done

    if kz_in_msys; then
        MSYS_NO_PATHCONV=1 "$BUILDER" run "${flags[@]}" "${env_args[@]}" \
            "$name" "/work/$KZ_PROJECT/$kz_root_rel/bin/kazarov" --inside "$@"
    else
        "$BUILDER" run "${flags[@]}" "${env_args[@]}" \
            "$name" "/work/$KZ_PROJECT/$kz_root_rel/bin/kazarov" --inside "$@"
    fi
}

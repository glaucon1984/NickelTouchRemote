#!/bin/sh
# Build KoboRoot.tgz on a host that has Docker (default: the ssh host "docker")
# using the NickelTC image, and copy the result to build/.
# Usage: tools/remote-build.sh [ssh-host] [make targets...]
set -eu
root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
host=${1:-docker}
[ "$#" -gt 0 ] && shift
targets=${*:-"all strip koboroot"}
remote=build/touchremote
version=$(git -C "$root" describe --tags --always --dirty 2>/dev/null || echo dev)
cd "$root"
tar czf - --exclude=./.git --exclude=./build --exclude='*.o' --exclude='*.moc' --exclude='*.so' \
    --exclude=KoboRoot.tgz --exclude=tests/gesture_test . |
    ssh "$host" "rm -rf $remote && mkdir -p $remote && tar xzf - -C $remote"
# One make per target: NickelHook's strip and koboroot do not depend on the library.
for target in $targets; do
    ssh "$host" "cd $remote && docker run --rm -v \"\$PWD:/src\" -w /src ghcr.io/pgaskin/nickeltc:1.0 make -j\$(nproc) VERSION=$version $target"
done
mkdir -p build
case " $targets " in
*" koboroot "*)
    scp -q "$host:$remote/KoboRoot.tgz" build/KoboRoot.tgz
    echo "build/KoboRoot.tgz ($version)"
    ;;
esac

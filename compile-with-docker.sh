#!/bin/sh
# Build inside the image; copy artifacts out with docker cp (no bind mount).
# Bind mounts often fail on macOS Docker Desktop ("mounts denied") unless the
# host path is in File Sharing — this script avoids that requirement.
set -e
mkdir -p compiled-firmware

docker build -t uvk5 .

cid=$(docker create uvk5 /bin/bash -c 'cd /app && make')
docker start -ai "$cid"
status=$(docker inspect -f '{{.State.ExitCode}}' "$cid")
if [ "$status" != "0" ]; then
	docker rm "$cid" >/dev/null
	exit "$status"
fi

for f in firmware firmware.bin firmware.packed.bin firmware.ld; do
	docker cp "$cid:/app/$f" compiled-firmware/
done
docker rm "$cid" >/dev/null

echo "Firmware written to compiled-firmware/"
ls -la compiled-firmware/

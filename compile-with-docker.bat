@echo off
if not exist compiled-firmware mkdir compiled-firmware
docker build -t uvk5 .
docker rm -f uvk5-build 2>nul
docker create --name uvk5-build uvk5 /bin/bash -c "cd /app && make clean && make"
docker start --attach --wait uvk5-build
if errorlevel 1 exit /b %errorlevel%
docker cp uvk5-build:/app/firmware compiled-firmware\
docker cp uvk5-build:/app/firmware.bin compiled-firmware\
docker cp uvk5-build:/app/firmware.packed.bin compiled-firmware\
docker cp uvk5-build:/app/firmware.ld compiled-firmware\
docker rm uvk5-build
echo Firmware written to compiled-firmware\
dir compiled-firmware
pause

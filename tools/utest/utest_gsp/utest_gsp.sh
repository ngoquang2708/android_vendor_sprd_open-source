#!/bin/sh


echo "start test gsp cfc"

sudo adb kill-server
sudo adb start-server
sudo adb root
sleep 2
sudo adb remount
sleep 2
adb shell mkdir /data/gsp
sudo adb push /home/apuser/work/shark_android/out/target/product/sp8830ea/system/xbin/utest_gsp /data/gsp/
sudo adb push /home/apuser/Videos/rgb888.raw /data/gsp/
sudo adb push /home/apuser/Videos/rgb565.raw /data/gsp/

sudo adb shell /data/gsp/utest_gsp -i /data/gsp/rgb888.raw -o /data/gsp/rgb888torgb888.raw -w 320 -h 240 -informat 1 -outformat 1
sudo adb shell /data/gsp/utest_gsp -i /data/gsp/rgb888.raw -o /data/gsp/rgb888torgb565.raw -w 320 -h 240 -informat 1 -outformat 3
sudo adb shell /data/gsp/utest_gsp -i /data/gsp/rgb888.raw -o /data/gsp/rgb888toyuv202p.raw -w 320 -h 240 -informat 1 -outformat 4

sudo adb shell /data/gsp/utest_gsp -i /data/gsp/rgb565.raw -o /data/gsp/rgb565torgb888.raw -w 320 -h 240 -informat 3 -outformat 1
sudo adb shell /data/gsp/utest_gsp -i /data/gsp/rgb565.raw -o /data/gsp/rgb565torgb565.raw -w 320 -h 240 -informat 3 -outformat 3
sudo adb shell /data/gsp/utest_gsp -i /data/gsp/rgb565.raw -o /data/gsp/rgb565toyuv202p.raw -w 320 -h 240 -informat 3 -outformat 4

sudo adb shell busybox cp /data/gsp/rgb888toyuv202p.raw /data/gsp/yuv4202p.raw
sudo adb shell /data/gsp/utest_gsp -i /data/gsp/yuv4202p.raw -o /data/gsp/yuv4202ptorgb888.raw -w 320 -h 240 -informat 4 -outformat 1
sudo adb shell /data/gsp/utest_gsp -i /data/gsp/yuv4202p.raw -o /data/gsp/yuv4202ptorgb565.raw -w 320 -h 240 -informat 4 -outformat 3
sudo adb shell /data/gsp/utest_gsp -i /data/gsp/yuv4202p.raw -o /data/gsp/yuv4202ptoyuv202p.raw -w 320 -h 240 -informat 4 -outformat 4

echo "stop test gsp cfc"


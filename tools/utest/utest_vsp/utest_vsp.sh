#!/bin/sh


echo "start test h263 codec"

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 128 -h 96 -bitrate 32 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 128 -h 96 -bitrate 256 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 176 -h 144 -bitrate 32 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 176 -h 144 -bitrate 256 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 320 -h 240 -bitrate 512 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 352 -h 288 -bitrate 128 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 352 -h 288 -bitrate 1024 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 704 -h 576 -bitrate 256 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 704 -h 576 -bitrate 4096 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 1408 -h 1152 -bitrate 2048 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.h263 -w 352 -h 288 -bitrate 512 -format 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.h263 -o /dev/null

echo "stop test h263 codec"








echo "start test mpeg4 codec"

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 64 -h 64 -framerate 5 -max_key_interval 1 -bitrate 32
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 64 -h 64 -framerate 5 -max_key_interval 0 -qp 4
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 64 -h 64 -framerate 10 -max_key_interval 10 -bitrate 64
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 64 -h 64 -framerate 10 -max_key_interval 30 -qp 8
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 176 -h 144 -framerate 10 -max_key_interval 1 -bitrate 128
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 176 -h 144 -framerate 10 -max_key_interval 1 -qp 1
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 320 -h 240 -max_key_interval 1 -bitrate 256
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 320 -h 240 -max_key_interval 1 -qp 31
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 352 -h 288 -bitrate 256
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 352 -h 288 -qp 4
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 640 -h 480 -bitrate 1024 -max_key_interval 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 640 -h 480 -qp 4
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 720 -h 576 -bitrate 1024 -max_key_interval 0
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 720 -h 576 -qp 31
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 720 -h 576 -bitrate 2048
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 1280 -h 720 -qp 1
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 720 -h 32
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 576 -h 720 -bitrate 2048
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 200 -h 400
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null



adb shell /system/xbin/utest_vsp_enc -i /data/test.yuv -o /data/test.m4v -w 352 -h 288
adb shell /system/xbin/utest_vsp_mpeg4dec -i /data/test.m4v -o /dev/null

echo "stop test mpeg4 codec"


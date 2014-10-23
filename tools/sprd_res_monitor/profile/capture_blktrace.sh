#!/bin/sh

logdir=`ls /storage/sdcard0/slog/ | busybox grep -o -E "[0-9]+-[0-9]+-[0-9]+-[0-9]+-[0-9]+-[0-9]+"`;
busybox cp -r /data/blktrace/*  /storage/sdcard0/slog/$logdir/misc/;
rm -r /data/blktrace/*;


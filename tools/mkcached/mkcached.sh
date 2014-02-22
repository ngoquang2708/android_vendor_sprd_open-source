#!/system/xbin/busybox sh

sd_cache_mounted=0

if ls /mnt/media_rw/sdcard0 >/dev/null
then
#with sdcard
    #while until sdcard mounted
    while   sleep 2
    do
      if  mount | busybox grep "vfat"
      then
        echo "sdcard mounted"
        break
      else
        echo "sdcard not mounted"
        mount -t ubifs /dev/ubi0_cache /cache
      fi
    done

    fsize=0
    #mount /mnt/media_rw/sdcard0/cachedisk to /cache
    if ls /mnt/media_rw/sdcard0/cachedisk >/dev/null
    then
        umount /cache
        if $(mount -t ext2 -o loop /mnt/media_rw/sdcard0/cachedisk /cache)
        then
          echo "mount sd cache succeed"
          sd_cache_mounted=1
        else
          echo "mount sd cache failed"
          mount -t ubifs /dev/ubi0_cache /cache
        fi
    else
        echo "need to create /mnt/media_rw/sdcard0/cachedisk"
        #create a new virtual partion
        diskfree=$(dfex |busybox grep /mnt/media_rw/sdcard0 |busybox awk '{print $4}')
        fsize=61440
        echo "diskfree size if $diskfree, need size is $fsize"
        if busybox [ $diskfree -gt $fsize ]
        then
          dd if=/dev/zero of=/mnt/media_rw/sdcard0/cachedisk bs=1024 count=$fsize
          #waiting for cachedisk maded
          echo "make sd cache"
          #format cachedisk with ext2
          mke2fs -b 4096 -F /mnt/media_rw/sdcard0/cachedisk
          umount /cache
          if $(mount -t ext2 -o loop /mnt/media_rw/sdcard0/cachedisk /cache)
          then
            echo "mount sd cache succeed"
            sd_cache_mounted=1
          else
            echo "mount sd cache failed"
            umount /cache
            mount -t ubifs /dev/ubi0_cache /cache
          fi
        else
          #no space for cache virtual partion
          echo "no space for cache virtual partion"
          umount /cache
          mount -t ubifs /dev/ubi0_cache /cache
        fi
    fi
else
#without sdcard
  echo "without sdcard"
fi

echo "sd_cache_mounted is $sd_cache_mounted"
if busybox [ 1 = $sd_cache_mounted ]
then
  mount_times=0
  mount_res=1
  while busybox [ 0 != $mount_res ]
  do
    echo "mount -t ubifs /dev/ubi0_cache /cache2"
    mount -t ubifs /dev/ubi0_cache /cache2
    mount_res=$?
    echo "mount_res=$mount_res"
    mount_times=$(busybox expr $mount_times + 1)
    if busybox [ $mount_times -gt 5 ]
    then
      echo "mount times greate than 5, error!"
      break
    fi
  done
fi
echo "setprop persist.sys.cache_on_sd $sd_cache_mounted"
setprop persist.sys.cache_on_sd $sd_cache_mounted

#echo "========mount state=========" >>/data/system/mkcached.log
#mount >>/data/system/mkcached.log
#echo "========mount state=========" >>/data/system/mkcached.log

if busybox [ 1 = $sd_cache_mounted ]
then
  chown system.cache /cache
  chmod 0770 /cache
  mkdir /cache/lost+found 0770
  chown root.root /cache/lost+found
  chmod 0770 /cache/lost+found
fi


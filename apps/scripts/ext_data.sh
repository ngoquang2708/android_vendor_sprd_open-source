#!/system/bin/sh

if [ "$1" = "-u" ]; then
	ifname=`getprop sys.ifconfig.up`
	ifconfig $ifname
	ifname=`getprop sys.data.noarp`
	ip $ifname
	setprop sys.ifconfig.up done
	setprop sys.data.noarp done
elif [ "$1" = "-d" ]; then
	ifname=`getprop sys.ifconfig.down`
	ifconfig $ifname
	setprop sys.ifconfig.down done
fi

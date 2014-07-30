#!/system/bin/sh

if [ "$1" = "-u" ]; then
	ifname=`getprop sys.data.setip`
	ip $ifname
	ifname=`getprop sys.data.setmtu`
	ip $ifname
	ifname=`getprop sys.ifconfig.up`
	ip $ifname
	ifname=`getprop sys.data.noarp`
	ip $ifname

##For Auto Test
	ethup=`getprop ril.gsps.eth.up`
	if [ "$ethup" = "1" ]; then
		ifname=`getprop sys.gsps.eth.ifname`
		localip=`getprop sys.gsps.eth.localip`
		pcv4addr=`getprop sys.gsps.eth.peerip`

		setprop ril.gsps.eth.up 0
		ip route add default via $localip dev $ifname
		iptables -D FORWARD -j natctrl_FORWARD
		iptables -D natctrl_FORWARD -j DROP
		iptables -t nat -A PREROUTING -i $ifname -j DNAT --to-destination $pcv4addr
		iptables -I FORWARD 1 -i $ifname -d $pcv4addr -j ACCEPT
		iptables -A FORWARD -i rndis0 -o $ifname -j ACCEPT
		iptables -t nat -A POSTROUTING -s $pcv4addr -j SNAT --to-source $localip
	fi

	setprop sys.ifconfig.up done
	setprop sys.data.noarp done
elif [ "$1" = "-d" ]; then
	ifname=`getprop sys.ifconfig.down`
	ip $ifname
	ifname=`getprop sys.data.clearip`
	ip $ifname
	setprop sys.ifconfig.down done

	ethdown=`getprop ril.gsps.eth.down`
	if [ "$ethdown" = "1" ]; then
                iptables -X
		setprop ril.gsps.eth.down 0
		setprop sys.gsps.eth.ifname ""
		setprop sys.gsps.eth.localip ""
		setprop sys.gsps.eth.peerip ""
	fi
fi

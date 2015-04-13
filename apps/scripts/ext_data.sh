#!/system/bin/sh

if [ "$1" = "-u" ]; then
        temp=`getprop sys.data.IPV6.disable`
        ifname=`getprop sys.data.net.addr`
        #for var in seth_lte0 seth_lte1 seth_lte2 seth_lte3 seth_lte4 seth_lte5; do
        `echo ${temp} > /proc/sys/net/ipv6/conf/$ifname/disable_ipv6`
        #done
	IF_ADDR=$(getprop sys.data.setip | busybox awk '{print $3}')
	IF_SETH=$ifname

        `echo -1 > /proc/sys/net/ipv6/conf/$ifname/accept_dad`
	ifname=`getprop sys.data.setip`
	ip $ifname
	ifname=`getprop sys.data.setmtu`
	ip $ifname
	ifname=`getprop sys.ifconfig.up`
	ip $ifname
	ifname=`getprop sys.data.noarp`
	ip $ifname
        ifname=`getprop sys.data.noarp.ipv6`
        ip $ifname

	`ip rule del table 150`
	`ip rule add from $IF_ADDR table 150`
	`ip route add default via $IF_ADDR table 150`

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
                iptables –I OUTPUT –o $ifname –p all ! –d $pcv4addr/24 –j DROP

                echo 1 > proc/sys/net/ipv6/conf/$ifname/disable_ipv6
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

elif [ "$1" = "-e" ]; then
        iptables -A FORWARD -p udp --dport 53 -j DROP
        iptables -A INPUT -p udp --dport 53 -j DROP
        iptables -A OUTPUT -p udp --dport 53 -j DROP
        ip6tables -A FORWARD -p udp --dport 53 -j DROP
        ip6tables -A INPUT -p udp --dport 53 -j DROP
        ip6tables -A OUTPUT -p udp --dport 53 -j DROP

elif [ "$1" = "-c" ]; then
        iptables -D FORWARD -p udp --dport 53 -j DROP
        iptables -D INPUT -p udp --dport 53 -j DROP
        iptables -D OUTPUT -p udp --dport 53 -j DROP
        ip6tables -D FORWARD -p udp --dport 53 -j DROP
        ip6tables -D INPUT -p udp --dport 53 -j DROP
        ip6tables -D OUTPUT -p udp --dport 53 -j DROP

elif [ "$1" = "-f" ]; then
    table1_idx=100
    table2_idx=101
    table3_idx=102

    IPV4_PDN1_ADDR=`getprop net.seth_lte0.ip`
    IPV4_PDN2_ADDR=`getprop net.seth_lte1.ip`
    IPV4_PDN3_ADDR=`getprop net.seth_lte2.ip`

    DEFAULT_IPV4_ADDR=$(ip route show | grep default | busybox head -1 | busybox sed -r 's/.+via ([^ ]+) .+/\1/')
    echo 1 > /proc/sys/net/ipv4/ip_forward

    table1_route=`ip route list table $table1_idx`
    table2_route=`ip route list table $table2_idx`
    table3_route=`ip route list table $table3_idx`

    `ip rule del table $table1_idx`
    `ip rule del table $table2_idx`
    `ip rule del table $table3_idx`
    
        if [ $DEFAULT_IPV4_ADDR == $IPV4_PDN1_ADDR ]; then
            `ip rule add from $IPV4_PDN2_ADDR table $table2_idx`
            `ip route add default via $IPV4_PDN2_ADDR table $table2_idx`
            `ip rule add from "$IPV4_PDN3_ADDR" table $table3_idx`
            `ip route add default via "$IPV4_PDN3_ADDR" table $table3_idx`
        elif [ $DEFAULT_IPV4_ADDR == $IPV4_PDN2_ADDR ]; then
            `ip rule add from $IPV4_PDN1_ADDR table $table1_idx`
            `ip route add default via $IPV4_PDN1_ADDR table $table1_idx`
            `ip rule add from $IPV4_PDN3_ADDR table $table3_idx`
            `ip route add default via $IPV4_PDN3_ADDR table $table3_idx`
        elif [ $DEFAULT_IPV4_ADDR == $IPV4_PDN3_ADDR ]; then
            `ip rule add from $IPV4_PDN1_ADDR table $table1_idx`
            `ip route add default via $IPV4_PDN1_ADDR table $table1_idx`
            `ip rule add from $IPV4_PDN2_ADDR table $table2_idx`
            `ip route add default via $IPV4_PDN2_ADDR table $table2_idx`    
        fi
      
fi

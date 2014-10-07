#!/system/bin/sh

term="/dev/pts/* "

#echo $1
args="$1"
modemtype=${args:1}
#echo $modemtype

setprop ril.${modemtype}.assert 1

phcountprop=ro.modem.${modemtype}.count
#echo $phcountprop
phone_count=`getprop $phcountprop`
#echo $phone_count

if [ "$phone_count" = "1" ]; then
	#echo "phonecount is 1"
	setprop ril.${modemtype}.sim.power 0
elif [ "$phone_count" = "2" ]; then
	#echo "phonecount is 2"
	setprop ril.${modemtype}.sim.power 0
	setprop ril.${modemtype}.sim.power1 0
elif [ "$phone_count" = "3" ]; then
	#echo "phonecount is 3"
	setprop ril.${modemtype}.sim.power 0
	setprop ril.${modemtype}.sim.power1 0
	setprop ril.${modemtype}.sim.power2 0
fi

if [ "$modemtype" = "t" ]; then
	#echo "modemtype is t"
	if [ "$phone_count" = "1" ]; then
		#echo "modemtype is t and phonecount is 1"
		ssda_mode=`getprop persist.radio.ssda.mode`
		if [ "$ssda_mode" = "svlte" ]; then
			#echo "modemtype is t and phonecount is 1 and ssdamode is svlte"
			setprop ril.l.sim.power 0
			setprop ril.service.l.enable -1
			setprop ril.lte.cereg.state -1
		fi
	fi
fi

phoneid=`getprop sys.phone.app`
#echo $phoneid
kill $phoneid

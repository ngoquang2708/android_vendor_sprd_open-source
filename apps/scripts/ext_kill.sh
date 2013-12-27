#!/system/bin/sh

term="/dev/pts/* "

if [ "$1" = "-t" ]; then
	setprop ril.t.assert 1
	phone_count=`getprop ro.modem.t.count`
	if [ "$phone_count" = "1" ]; then
		setprop ril.t.sim.power 0
	elif [ "$phone_count" = "2" ]; then
		setprop ril.t.sim.power 0
		setprop ril.t.sim.power1 0
	elif [ "$phone_count" = "3" ]; then
		setprop ril.t.sim.power 0
		setprop ril.t.sim.power1 0
		setprop ril.t.sim.power2 0
	fi
	phone=`getprop sys.phone.app`
	kill $phone
elif [ "$1" = "-w" ]; then
	setprop ril.w.assert 1
	phone_count=`getprop ro.modem.w.count`
	if [ "$phone_count" = "1" ]; then
		setprop ril.w.sim.power 0
	elif [ "$phone_count" = "2" ]; then
		setprop ril.w.sim.power 0
		setprop ril.w.sim.power1 0
	elif [ "$phone_count" = "3" ]; then
		setprop ril.w.sim.power 0
		setprop ril.w.sim.power1 0
		setprop ril.w.sim.power2 0
	fi
	phone=`getprop sys.phone.app`
	kill $phone
fi

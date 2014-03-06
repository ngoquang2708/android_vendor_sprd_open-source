#!/bin/bash

function add_to_file()
{
	echo "add_to_file"
	file=$1
	cd $2
	substring=$3
	replacement=$4
	var2=$5
	var1=$6
	is_add_tab=0  #是否加tab制表符,0表示不需要,表示从第is_add_tab行后开始tab制表符
	if [ $# -ge 7 ];then
		echo $7
		is_add_tab=$7
	fi

	declare -i n
	declare -i begin
	declare -i end
	declare -i m
	declare -i k  #k用于计算从哪行开始开始输入tab制表符
	k=0

	#if [ $# -lt 2 ]
	n=0
	begin=0
	file_t="$file""_bak"
	file_n="$file""_bak_b"    #避免屏幕输出
	is_one_line='skip_one_line'


	cat $file | while read  line
	do
		#echo $line
		n=n+1
		echo "$line" |grep -qi "$var2""\>" >$file_n  #字符串向后须全配比
		m=$?
		if [ $begin -gt 0 ];then
			echo "$line" | grep -qi "$var1" >$file_n
			if [ $? -eq 0 ];then
				end=$n
				#echo $begin >$file_t
				echo $end >$file_t
				echo "find"" end=""$end"
				echo $line
				break
			fi
		elif [ $m -eq 0 ];then
			begin=$n
			echo "find"" begin=""$begin"
			echo $line
			echo "$var1" | grep "$is_one_line" >$file_n
			if [ $? -eq 0 ];then
				echo "only add one line"
				line=${line//$substring/$replacement}
				substring_b=`tr '[a-z]' '[A-Z]' <<<"$substring"`
				replacement_b=`tr '[a-z]' '[A-Z]' <<<"$replacement"`
				line=${line//$substring_b/$replacement_b}
				echo $line
				sed -e "$n""a\\$line"  $file >$file_t
				cp $file_t $file
				rm $file_t -rf
				break
			fi
		fi
		#echo $n
	done


	end=0
	if [[ -f $file_t ]];then
		while read line
		do
	    end=$line
		done < $file_t
		rm $file_t -rf
	fi

	if [[ -f $file_n ]];then
		rm $file_n -rf
	fi

	echo "find end line is ""$end"
	begin=0
	if [ $end -gt 0 ];then
		cat $file | while read  line
		do
			echo "$line" | grep -qi "$var2" >$file_n
			m=$?
			if [ $begin -gt 0 ];then
				n=n+1
				echo "$line" | grep "$substring" >$file_n
				if [ $? -eq 0 ];then
					echo have
					line=${line//$substring/$replacement}
				else
					substring_b=`tr '[a-z]' '[A-Z]' <<<"$substring"`
					replacement_b=`tr '[a-z]' '[A-Z]' <<<"$replacement"`
					line=${line//$substring_b/$replacement_b}
				fi
				k=k+1
				if [ $is_add_tab -gt 0 ];then
					if [ $k -ge $is_add_tab ];then
						line_s="$line_s""\n\t$line"
					else
						line_s="$line_s""\n$line"
					fi
				else
					line_s="$line_s""\n$line"
				fi
				echo "$line" |grep -qi "$var1" >$file_n
				if [ $? -eq 0 ];then
					echo "begin to write to file"
					#echo $end
					#echo $line_s
					sed -e "$end""a\\$line_s"  $file >$file_t
					cp $file_t $file
					rm $file_t -rf
					break
				fi
			elif [ $m -eq 0 ];then
				n=n+1
				begin=$n
				echo "$line" | grep "$substring" >$file_n
				if [ $? -eq 0 ];then
					line=${line//$substring/$replacement}
				else
					substring_b=`tr '[a-z]' '[A-Z]' <<<"$substring"`
					replacement_b=`tr '[a-z]' '[A-Z]' <<<"$replacement"`
					line=${line//$substring_b/$replacement_b}
				fi

				if [ $k -gt $is_add_tab ];then
					line_s="\n\t$line"
				else
					line_s="\n$line"
				fi
				k=k+1
			else
				n=n+1
			fi
		done
	fi

	if [[ -f $file_n ]];then
		rm $file_n -rf
	fi
}

#用此函数,关键字不可以说最后一个串,否则被删掉一个字符,将出现错误
function add_to_file_e()
{
	file=$1
	cd $2
	substring=$3
	replacement=$4
	var2=$5
	var1=$6

	declare -i n
	declare -i begin
	declare -i end
	declare -i m

	#if [ $# -lt 2 ]
	n=0
	begin=0
	file_t="$file""_bak"
	file_m="$file""_bak_bak"  #computer lines
	file_n="$file""_bak_b"    #避免屏幕输出
	is_one_line='skip_one_line'

	sed 's/.$//' $file >$file_m  #删除每行的最后一个字符,可能是"\",引起计算行错误

	cat $file_m | while read line >$file_n
	do
		#echo $line
		n=n+1
		#echo $n
		echo "$line" |grep -qi "$var2""\>" >$file_n   #字符串向后须全配比
		m=$?
		if [ $begin -gt 0 ];then
			echo "$line" |grep -qi "$var1" >$file_n
			if [ $? -eq 0 ];then
				end=$n
				#echo $begin >$file_t
				echo $end >$file_t
				echo "find"" end=""$end"
				break
			fi
		elif [ $m -eq 0 ];then
			begin=$n
			echo "find"" begin=""$begin"
			echo "$var1" | grep "$is_one_line" >$file_n
			if [ $? -eq 0 ];then
				echo "only add one line"
				echo "maybe have error!!!!!!"
				exit 0
			fi
		fi
	done


	end=0
	if [[ -f $file_t ]];then
		while read line
		do
	    end=$line
		done < $file_t
		rm $file_t -rf
	fi

	if [[ -f $file_m ]];then
		rm $file_m -rf
	fi

	if [[ -f $file_n ]];then
		rm $file_n -rf
	fi

	echo "find end line is ""$end"
	begin=0
	if [ $end -gt 0 ];then
		cat $file | while read line
		do
			echo "$line" | grep -qi "$var2" >$file_n
			m=$?
			if [ $begin -gt 0 ];then
				n=n+1
				echo "$line" | grep "$substring" >$file_n
				if [ $? -eq 0 ];then
					#echo have
					line=${line//$substring/$replacement}
				else
					substring_b=`tr '[a-z]' '[A-Z]' <<<"$substring"`
					replacement_b=`tr '[a-z]' '[A-Z]' <<<"$replacement"`
					line=${line//$substring_b/$replacement_b}
				fi
				line_s="$line_s""\n\t$line"
				echo "$line" |grep -qi "$var1" >$file_n
				if [ $? -eq 0 ];then
					echo "begin to write to file"
					#echo $end
					#echo $line_s
					sed -e "$end""a\\$line_s"  $file >$file_t
					cp $file_t $file
					rm $file_t -rf
					break
				fi
			elif [ $m -eq 0 ];then
				n=n+1
				begin=$n
				echo "$line" | grep "$substring" >$file_n
				if [ $? -eq 0 ];then
					line=${line//$substring/$replacement}
				else
					substring_b=`tr '[a-z]' '[A-Z]' <<<"$substring"`
					replacement_b=`tr '[a-z]' '[A-Z]' <<<"$replacement"`
					line=${line//$substring_b/$replacement_b}
				fi
				line_s="\n$line"
			else
				n=n+1
			fi
		done
	fi

	if [[ -f $file_n ]];then
		rm $file_n -rf
	fi
}

function board_for_kernel()
{
	DEBUG_S=debug
	if [ $# -gt 0 ];then
		echo "$1" | grep "$DEBUG_S"
		if [ $? -eq 0 ];then
			PLATFORM=scx15
			BOARD_NAME_R=sp7715ea
			BOARD_NAME_N=sp7715eaopenphone
			workdir=$PWD
		fi
	fi

	echo -e "\n\nconfigure kernel begin......"

	#---------修改kernel\arch\arm\mach-sc\Makefile文件 begin-------
	cd $workdir
	file=Makefile
	substring=$BOARD_NAME_R
	begin_string=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
	replacement=$BOARD_NAME_N
	path="kernel/arch/arm/mach-sc"
	end_string='skip_one_line'
	add_to_file $file $path $substring $replacement $begin_string $end_string
	#---------修改kernel\arch\arm\mach-sc\Makefile文件 end-------


	#---------产生*native_defconfig文件 begin-------
	cd $workdir
	cd "kernel/arch/arm/configs"

	TEMP=$BOARD_NAME_R"-native_defconfig"
	echo $TEMP
	TEMP1=$BOARD_NAME_N"-native_defconfig"
	cp $TEMP $TEMP1
	file=$TEMP1

	TEMP1=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
	TEMP2=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_N"`
	sed -i s/$TEMP1/$TEMP2/g `grep $TEMP1 -rl --include="$file" ./`
	#---------产生*native_defconfig文件 end-------


	#---------产生*board-BOARD_NAME.c文件 begin-------
	cd $workdir
	cd kernel/arch/arm/mach-sc
	TEMP1="board-"$BOARD_NAME_R".c"
	TEMP2="board-"$BOARD_NAME_N".c"
	cp $TEMP1 $TEMP2

	cd include/mach
	TEMP1="__board-"$BOARD_NAME_R".h"
	TEMP2="__board-"$BOARD_NAME_N".h"
	cp $TEMP1 $TEMP2
	file=$TEMP2
	echo $file

	TEMP='sp'
	TEMP1=${BOARD_NAME_R#*$TEMP}
	TEMP1=`tr '[a-z]' '[A-Z]' <<<"$TEMP1"`
	TEMP1=$TEMP1"_"
	echo $TEMP1
	TEMP2=${BOARD_NAME_N#*$TEMP}
	TEMP2=`tr '[a-z]' '[A-Z]' <<<"$TEMP2"`
	TEMP2=$TEMP2"_"
	echo $TEMP2
	sed -i s/$TEMP1/$TEMP2/g `grep $TEMP1 -rl --include="$file" ./`
	#---------产生*board-BOARD_NAME.c文件end-------

	#---------修改board.h文件 begin-------
	cd $workdir
	file=board.h
	substring=$BOARD_NAME_R
	begin_string=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
	replacement=$BOARD_NAME_N
	path="kernel/arch/arm/mach-sc/include/mach"
	end_string=endif
	add_to_file $file $path $substring $replacement $begin_string $end_string
	#---------修改board.h文件 end-------


	#---------修改Kconfig文件 begin-------
	cd $workdir
	file=Kconfig
	substring=$BOARD_NAME_R
	begin_string=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
	replacement=$BOARD_NAME_N
	path="kernel/arch/arm/mach-sc"
	end_string='serial'
	add_to_file $file $path $substring $replacement $begin_string $end_string 2
	#---------修改Kconfig文件 end-------


	#---------增加'|| defined(CONFIG_MACH_BOARD_NAME_N)' begin-------
	cd $workdir
	cd kernel/
	BOARD_NAME_R_b=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
	BOARD_NAME_N_b=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_N"`
	substring="(CONFIG_MACH_$BOARD_NAME_R_b)"
	addstring=" || defined(CONFIG_MACH_$BOARD_NAME_N_b)"
	replacestring="$substring$addstring"
	echo -e $substring
	echo -e $addstring
	echo -e $replacestring
	#if [ `grep -Hrn  $substring ./` ];then
	#echo ok
	#fi
	if [ -z "`grep -Hrn --exclude-dir=".git" --include="*.c" --include="*.h" $substring ./`" ]
	then
		echo "NULL"
	else
		#sed -i "/$substring/s/$/$addstring/;" `grep $substring -rl --include="*.*" ./`
		#sed -i s/$TEMP1/$TEMP2/g `grep $TEMP1 -rl --include="*.*" ./`
		sed -i s/"$substring"/"$replacestring"/g `grep $substring -rl --include="*.*" ./`
		echo "===========================notice========================================"
		echo "===========================notice========================================"
		echo "maybe add \"|| defined(CONFIG_MACH_$BOARD_NAME_N_b)\" words!"
		echo "please check that if need!!!!!"
	fi
	#---------增加'|| defined(CONFIG_MACH_BOARD_NAME_N)' end-------

	echo "configure kernel end!"
}

function board_for_uboot()
{
	DEBUG_S=debug
	if [ $# -gt 0 ];then
		echo "$1" | grep "$DEBUG_S"
		if [ $? -eq 0 ];then
			PLATFORM=scx15
			BOARD_NAME_R=sp7715ea
			BOARD_NAME_N=sp7715eaopenphone
			workdir=$PWD
		fi
	fi

	#echo $BOARD_NAME_N

	PATH_R="u-boot/board/spreadtrum/""$BOARD_NAME_R"
	PATH_N="u-boot/board/spreadtrum/""$BOARD_NAME_N"

	echo -e "\n\nconfigure u-boot begin......"

	while [ 1 ]
	do
		if [ ! -d $PATH_R ];then
			echo "$PATH_R not exist,configure u-boot fail!"
			break
		fi

		if [  -d $PATH_N ];then
			echo "$PATH_N exist,will rm it"
			rm $PATH_N -rf
		fi
		mkdir $PATH_N
		cp $PATH_R/* $PATH_N/ -rf

		PATH_M="u-boot/include/configs/"
		file_r="$PATH_M""$BOARD_NAME_R"".h"
		file_n="$PATH_M""$BOARD_NAME_N"".h"

		if [ ! -f $file_r ];then
			echo "$file_r not exist,configure u-boot fail!"
			break
		fi

		cp $file_r $file_n

		BOARD_NAME_R_b=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
		#echo $BOARD_NAME_R_b
		BOARD_NAME_N_b=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_N"`
		#sed -i s/$BOARD_NAME_R_b/$BOARD_NAME_N_b/g `grep $BOARD_NAME_N_b $file_n`
		#echo $BOARD_NAME_N
		#echo $PATH_M
		sed -i s/$BOARD_NAME_R_b/$BOARD_NAME_N_b/g `grep $BOARD_NAME_R_b -rl --include="$BOARD_NAME_N"".h" $PATH_M`


	#---------修改Makefile文件 begin-------
		cd $workdir
		file=Makefile
		substring="$BOARD_NAME_R"
		begin_string=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
		begin_string="$begin_string""_config"
		replacement="$BOARD_NAME_N"
		path="./u-boot"
		end_string='arm armv7'
		add_to_file_e $file $path $substring $replacement $begin_string $end_string
	#---------修改文件 end-------


	#---------增加'|| defined(CONFIG_BOARD_NAME_N)' begin-------
	cd $workdir
	cd u-boot/
	BOARD_NAME_R_b=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
	BOARD_NAME_N_b=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_N"`
	substring="(CONFIG_$BOARD_NAME_R_b)"
	addstring=" || defined(CONFIG_$BOARD_NAME_N_b)"
	replacestring=$substring$addstring
	#echo -e $substring
	#echo -e $addstring
	if [ -z "`grep -Hrn --exclude-dir=".git" --include="*.c" --include="*.h" $substring ./`" ]
	then
		echo "NULL"
	else
		#sed -i "/$substring/s/$/$addstring/;" `grep $substring -rl --include="*.*" ./`
		sed -i s/"$substring"/"$replacestring"/g `grep $substring -rl --include="*.*" ./`
		echo "===========================notice========================================"
		echo "===========================notice========================================"
		echo "maybe add \"|| defined(CONFIG_$BOARD_NAME_N_b)\" words!"
		echo "please check that if need!!!!!"
	fi
	#---------增加'|| defined(CONFIG_BOARD_NAME_N)' end-------

	break
	done

	echo "configure u-boot OK!"
}

function board_for_chipram()
{
	DEBUG_S=debug
	if [ $# -gt 0 ];then
		echo "$1" | grep "$DEBUG_S"
		if [ $? -eq 0 ];then
			PLATFORM=scx15
			BOARD_NAME_R=sp7715ea
			BOARD_NAME_N=sp7715eaopenphone
			workdir=$PWD
		fi
	fi

	#echo $BOARD_NAME_N

	echo -e "\n\nconfigure chipram begin......"

	while [ 1 ]
	do
		PATH_ROOT="chipram"
		if [ ! -d $PATH_ROOT ];then
			echo "$PATH_ROOT not exist,configure chipram fail!"
			break
		fi

		PATH_M="chipram/include/configs/"
		file_r="$PATH_M""$BOARD_NAME_R"".h"
		file_n="$PATH_M""$BOARD_NAME_N"".h"

		if [ ! -f $file_r ];then
			echo "$file_r not exist,configure chipram fail!"
			break
		fi

		cp $file_r $file_n

		BOARD_NAME_R_b=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
		#echo $BOARD_NAME_R_b
		BOARD_NAME_N_b=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_N"`
		#sed -i s/$BOARD_NAME_R_b/$BOARD_NAME_N_b/g `grep $BOARD_NAME_N_b $file_n`
		#echo $BOARD_NAME_N
		#echo $PATH_M
		sed -i s/$BOARD_NAME_R_b/$BOARD_NAME_N_b/g `grep $BOARD_NAME_R_b -rl --include="$BOARD_NAME_N"".h" $PATH_M`


	#---------修改Makefile文件 begin-------
		cd $workdir
		file=Makefile
		substring="$BOARD_NAME_R"
		begin_string=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
		begin_string="$begin_string""_config"
		replacement="$BOARD_NAME_N"
		path="./chipram"
		end_string='arm armv7'
		add_to_file_e $file $path $substring $replacement $begin_string $end_string
	#---------修改文件 end-------


	#---------增加'|| defined(CONFIG_BOARD_NAME_N)' begin-------
	cd $workdir
	cd chipram/
	BOARD_NAME_R_b=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_R"`
	BOARD_NAME_N_b=`tr '[a-z]' '[A-Z]' <<<"$BOARD_NAME_N"`
	substring="(CONFIG_$BOARD_NAME_R_b)"
	addstring=" || defined(CONFIG_$BOARD_NAME_N_b)"
	replacestring=$substring$addstring
	#echo -e $substring
	#echo -e $addstring
	if [ -z "`grep -Hrn --exclude-dir=".git" --include="*.c" --include="*.h" $substring ./`" ]
	then
		echo "NULL"
	else
		#sed -i "/$substring/s/$/$addstring/;" `grep $substring -rl --include="*.*" ./`
		sed -i s/"$substring"/"$replacestring"/g `grep $substring -rl --include="*.*" ./`
		echo "===========================notice========================================"
		echo "===========================notice========================================"
		echo "maybe add \"|| defined(CONFIG_$BOARD_NAME_N_b)\" words!"
		echo "please check that if need!!!!!"
	fi
	#---------增加'|| defined(CONFIG_BOARD_NAME_N)' end-------

	break
	done

	echo "configure chipram OK!"
}


function board_for_device()
{
	DEBUG_S=debug
	if [ $# -gt 0 ];then
		echo "$1" | grep "$DEBUG_S"
		if [ $? -eq 0 ];then
			PLATFORM=scx15
			BOARD_NAME_R=sp7715ea
			BOARD_NAME_N=sp7715eaopenphone
			workdir=$PWD
		fi
	fi

	export PATH_R="device/sprd/$PLATFORM""_""$BOARD_NAME_R"

	#echo $PATH_R

	if [ ! -d $PATH_R ];then
		echo "reference board not exist,please check againxxx!"
		exit 0
	fi

	echo "configure device begin......"

	PATH_N="device/sprd/$PLATFORM""_""$BOARD_NAME_N"
	if  [ -d $PATH_N ];then
		rm $PATH_N -rf
	fi

	mkdir $PATH_N

	cp $PATH_R/* $PATH_N/ -rf

	cd $PATH_N

	if  [ $BOARD_TYPE = 1 ];then
		TEMP=$PLATFORM"_""$BOARD_NAME_R""base.mk"
		if [ ! -f $TEMP ];then
			echo "$TEMP"" not exist,maybe something error!\nplease check!"
			exit 0
		fi
		TEMP1="xxx122222"
		mkdir $TEMP1
		TEMP2=$PLATFORM"_""$BOARD_NAME_N""base.mk"
		cp $TEMP $TEMP1/$TEMP2

		TEMP=$PLATFORM"_""$BOARD_NAME_R""plus.mk"
		TEMP2=$PLATFORM"_""$BOARD_NAME_N""plus.mk"

		if [  -f $TEMP ];then
			cp $TEMP $TEMP1/$TEMP2
		fi
	elif [ $BOARD_TYPE = 2 ];then
		TEMP=$PLATFORM"_""$BOARD_NAME_R""plus.mk"
		if [ ! -f $TEMP ];then
			echo "$TEMP"" not exist,maybe something error!\nplease check!"
			exit 0
		fi
		TEMP1="xxx122222"
		mkdir $TEMP1
		TEMP2=$PLATFORM"_""$BOARD_NAME_N"".mk"
		cp $TEMP $TEMP1/$TEMP2
	else
		echo "maybe choice error before"
		exit 0
	fi

	find -maxdepth 1 -name "*$BOARD_NAME_R*" | xargs rm -rf

	cp $TEMP1/* ./
	rm $TEMP1 -rf

	#---------替换board名关键字begin----------------
	sed -i s/$BOARD_NAME_R/$BOARD_NAME_N/g `grep $BOARD_NAME_R -rl --include="*.*" ./`

	TEMP=$BOARD_NAME_R
	TEMP1=`tr '[a-z]' '[A-Z]' <<<"$TEMP"`

	TEMP=$BOARD_NAME_N
	TEMP2=`tr '[a-z]' '[A-Z]' <<<"$TEMP"`

	sed -i s/$TEMP1/$TEMP2/g `grep $TEMP1 -rl --include="*.*" ./`
	#---------替换board名关键字end--------------------


	#---------删除vendorsetup.sh AndroidProducts中其他衍生的board-------
	file=vendorsetup.sh
	if [ $BOARD_TYPE = 1 ];then
		TEMP=asgii77jusua778
		TEMP1=$PLATFORM"_""$BOARD_NAME_N""base-"
	fi
	TEMP2=$PLATFORM"_""$BOARD_NAME_N""plus-"
	TEMP3=kjjs8j8jjshh7

	if [ $BOARD_TYPE = 1 ];then
		sed -i s/$TEMP1/$TEMP/g `grep $TEMP1 -rl --include="$file" ./`
	fi
	sed -i s/$TEMP2/$TEMP3/g `grep $TEMP2 -rl --include="$file" ./`

	sed -i -e /$BOARD_NAME_N/d $file

	if [ $BOARD_TYPE = 1 ];then
		sed -i s/$TEMP/$TEMP1/g `grep $TEMP -rl --include="$file" ./`
	fi
	sed -i s/$TEMP3/$TEMP2/g `grep $TEMP3 -rl --include="$file" ./`

	file=AndroidProducts.mk
	if [ $BOARD_TYPE = 1 ];then
		TEMP=asgii77jusua778
		TEMP1=$PLATFORM"_""$BOARD_NAME_N""base.mk"
	fi
	TEMP2=$PLATFORM"_""$BOARD_NAME_N""plus.mk"
	TEMP3=kjjs8j8jjshh7

	if [ $BOARD_TYPE = 1 ];then
		sed -i s/$TEMP1/$TEMP/g `grep $TEMP1 -rl --include="$file" ./`
	fi
	sed -i s/$TEMP2/$TEMP3/g `grep $TEMP2 -rl --include="$file" ./`

	#删除包含BOARD_NAME_N*.mk的行
	sed -i -e /"$BOARD_NAME_N".*".mk"\/d $file


	if [ $BOARD_TYPE = 1 ];then
		sed -i s/$TEMP/$TEMP1/g `grep $TEMP -rl --include="$file" ./`
	fi
	sed -i s/$TEMP3/$TEMP2/g `grep $TEMP3 -rl --include="$file" ./`

	#---------删除vendorsetup.sh AndroidProducts中其他衍生的board-------



	#---------删除AndroidProducts最后的\字符-------
	TEMP=`tail -1 $file`
	TEMP1='\'
	TEMP2=${#TEMP}-1
	TEMP3=${TEMP:$TEMP2:1}

	if [[ $TEMP3 == '\' ]];then
		#echo ''\' is del'
		sed -i '$s/.$//' $file
		TEMP2=${#TEMP}-2
		TEMP3=${TEMP:$TEMP2:1}
		if [[ $TEMP3 == ' ' ]];then
		sed -i '$s/.$//' $file
		fi
	fi
	#---------删除AndroidProducts最后的\字符-------

	if [ $BOARD_TYPE = 2 ];then
		TEMP1="$BOARD_NAME_N""plus"
		sed -i s/$TEMP1/$BOARD_NAME_N/g `grep $TEMP1 -rl --include="*.mk" ./`
		sed -i s/$TEMP1/$BOARD_NAME_N/g `grep $TEMP1 -rl --include="*.sh" ./`

		file=$PLATFORM"_""$BOARD_NAME_N"".mk"
		TEMP1="ro.msms.phone_count=2"
		TEMP2="ro.msms.phone_count=3"
		sed -i s/$TEMP1/$TEMP2/g `grep $TEMP1 -rl --include="$file" ./`

		TEMP1="persist.msms.phone_count=2"
		TEMP2="persist.msms.phone_count=3"
		sed -i s/$TEMP1/$TEMP2/g `grep $TEMP1 -rl --include="$file" ./`

		TEMP1="ro.modem.w.count=2"
		TEMP2="ro.modem.w.count=3"
		sed -i s/$TEMP1/$TEMP2/g `grep $TEMP1 -rl --include="$file" ./`
	fi

	#---------增加vendor/sprd/open-source/res/productinfo目录下的ini文件begin-------
	cd $workdir
	cd "vendor/sprd/open-source/res/productinfo"
	if [ $BOARD_TYPE = 1 ];then
		TEMP1=$PLATFORM"_""$BOARD_NAME_R""plus""_connectivity_configure.ini"
		TEMP2=$PLATFORM"_""$BOARD_NAME_N""plus""_connectivity_configure.ini"
		if [ -f $TEMP1 ];then
			cp $TEMP1 $TEMP2
		fi
		TEMP1=$PLATFORM"_""$BOARD_NAME_R""base""_connectivity_configure.ini"
		TEMP2=$PLATFORM"_""$BOARD_NAME_N""base""_connectivity_configure.ini"
		if [ -f $TEMP1 ];then
			cp $TEMP1 $TEMP2
		fi
	elif [ $BOARD_TYPE = 2 ];then
		TEMP1=$PLATFORM"_""$BOARD_NAME_R""plus""_connectivity_configure.ini"
		TEMP2=$PLATFORM"_""$BOARD_NAME_N""_connectivity_configure.ini"
		if [ -f $TEMP1 ];then
			cp $TEMP1 $TEMP2
		fi
	fi
	#---------增加vendor/sprd/open-source/res/productinfo目录下的ini文件end-------
	echo "configure device end!"
}

function printPlatform()
{
	echo "==================================================================="
	echo "please input platform name:"
	echo "1. scx15 for dolphin"
	echo "2. scx35 for shark"
	echo "==================================================================="
	read PLATFORM_CHOOSE
}

function selectBoardType()
{
	echo "==================================================================="
	echo "please select board tyep:"
	echo "1. normal(for example:base or plus)"
	echo "2. trisim(three sim cards)"
	echo "==================================================================="
	read BOARD_TYPE
}



workdir=$PWD

if [ ! -d "./kernel" ];then
	echo "board_clone.sh maybe is not in correct dir,please put it to android top dir!!"
	exit 0
fi

if [ ! -d "./u-boot" ];then
	echo "board_clone.sh maybe is not in correct dir,please put it to android top dir!!"
	exit 0
fi

if [ ! -d "./device/sprd" ];then
	echo "board_clone.sh maybe is not in correct dir,please put it to android top dir!!"
	exit 0
fi


PLATFORM_CHOOSE=
BOARD_TYPE=
while true
do
	printPlatform
	if (echo -n $PLATFORM_CHOOSE | grep -q -e "^[1-9][1-9]*$")
	then
		if [ "$PLATFORM_CHOOSE" -gt "0" ]
		then
			case $PLATFORM_CHOOSE in
				1)PLATFORM="scx15"
				echo "you have choose scx15!"
				;;
				2)PLATFORM="scx35"
				echo "you have choose scx35!"
				;;
				*)echo "Invalid choice !"
				exit 0
			esac
		fi
		break
	else
		echo  "you don't hava choose platform!"
		exit 0
	fi
done

while true
do
	selectBoardType
	if (echo -n $BOARD_TYPE | grep -q -e "^[1-9][1-9]*$")
	then
		if [ "$BOARD_TYPE" -gt "0" ]
		then
			case $BOARD_TYPE in
				1)echo "you have choose normal board!"
				;;
				2)echo "you have choose trisim board!"
				;;
				*)echo "Invalid choice !"
				exit 0
			esac
		fi
		break
	else
		echo  "you don't hava choose board type!"
		exit 0
	fi
done

echo $PLATFORM
echo $"board type is ""$BOARD_TYPE"


if [ $# -lt 2 ]
then
	echo "please input reference board name,for example:sp7715ea"
	read BOARD_NAME_R
	if [ $BOARD_TYPE = 2 ];then
		echo "please input new board name,for example:sp7715eatrisim"
	else
		echo "please input new board name,for example:sp7715ga"
	fi
	read BOARD_NAME_N
else
	BOARD_NAME_R=$1
	BOARD_NAME_N=$2
fi

export PATH_R="device/sprd/$PLATFORM""_""$BOARD_NAME_R"

echo $PATH_R

if [ ! -d $PATH_R ];then
	echo "$PATH_R not exist!"
	echo "reference board not exist,please check again!"
	exit 0
fi

cd $workdir
board_for_device

cd $workdir
board_for_kernel

cd $workdir
board_for_uboot

cd $workdir
board_for_chipram
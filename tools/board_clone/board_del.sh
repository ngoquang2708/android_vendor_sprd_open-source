#!/bin/bash

#You are running board_del.sh to delete a board automaticly.

#please input a board name like this: sp7730gga or sp8815ga

function select_product()
{
	echo "==================================================================="
	echo "please select product name:"
	echo "for example: sp8830ec or sp7715ga or sp7731gea or sp9630ea"
	echo "==================================================================="
	read PRODUCT_NAME
	#输入待删board名
}

function which_is_selected()
{
	echo "==================================================================="
	echo "you have selected :"
	echo "$PRODUCT_NAME"
	echo "==================================================================="
	#确认已输入board名
}

function wrong_board_name()
{
	selected_board_path_scx15="device/sprd/scx15_$PRODUCT_NAME"
	selected_board_path_scx35="device/sprd/scx35_$PRODUCT_NAME"
	selected_board_path_scx35l="device/sprd/scx35l_$PRODUCT_NAME"
	temp=0
	if [ ! -d "$selected_board_path_scx15" ]&&[ ! -d "$selected_board_path_scx35" ]&&[ ! -d "$selected_board_path_scx35l" ];then
		echo "selected board not exist,please check againxxx!!!!!"
		exit 0
	fi
	if [  -d $selected_board_path_scx15 ];then
		temp=temp+1
	fi
	if [  -d "$selected_board_path_scx35" ];then
		temp=temp+1
	fi
	if [  -d "$selected_board_path_scx35l" ];then
		temp=temp+1
		echo scx5l
	fi
	if [ $temp -qe 0 ];then
		echo "selected board not exist,please check againxxx!!!!!"
		exit 0
	fi
	if [ $temp -gt 1 ];then
		echo "have two board,conflict,so exit!!!!!"
		exit 0
	fi
}	
	
function board_del_from_device()
{
	DEVICE_PATH_scx15="device/sprd/scx15""_""$PRODUCT_NAME"
	DEVICE_PATH_scx35="device/sprd/scx35""_""$PRODUCT_NAME"
	DEVICE_PATH_scx35l="device/sprd/scx35l""_""$PRODUCT_NAME"
	if [ ! -d $DEVICE_PATH_scx15 ];then
		echo "scx15 platform do not have this board!"
		else
		echo "$DEVICE_PATH_scx15  exist!"
		echo -e "del_product_device begin......"
		rm -rf $DEVICE_PATH_scx15
		#删除device/sprd下面的board名目录文件夹，dolphin相关
		echo "======del_product_device end======"
	fi
	if [ ! -d $DEVICE_PATH_scx35 ];then
		echo "scx35 platform do not have this board!"
		else
		echo "$DEVICE_PATH_scx35 exist!"
		echo -e "del_product_device begin......"
		rm -rf $DEVICE_PATH_scx35
		#删除device/sprd下面的board名目录文件夹，shark和tshark相关
		echo "======del_product_device end======"
	fi
	if [ ! -d $DEVICE_PATH_scx35l ];then
		echo "scx35l platform do not have this board!"
		else
		echo "$DEVICE_PATH_scx35l exist!"
		echo -e "del_product_device begin......"
		rm -rf $DEVICE_PATH_scx35l
		#删除device/sprd下面的board名目录文件夹，sharkl
		echo "======del_product_device end======"
	fi
}

function board_del_from_uboot()	
{
	UBOOT_BOARD_PATH="u-boot/board/spreadtrum/""$PRODUCT_NAME"
	if [ ! -d $UBOOT_BOARD_PATH ];then
		echo "uboot do not have this board!"
		else 
		echo "$UBOOT_BOARD_PATH  exist in uboot!"
		echo -e "del_product_uboot_board_file begin......"
		rm -rf $UBOOT_BOARD_PATH
		#删除uboot下面的board名目录文件夹
	fi
	UBOOT_CONFIG_FILE="u-boot/include/configs/""$PRODUCT_NAME"".h"
	if [ ! -f $UBOOT_CONFIG_FILE ];then
		echo "uboot do not have this UBOOT_CONFIG_FILE!"
		else
		echo "$UBOOT_CONFIG_FILE  exist in uboot!"
		echo -e "del_product_uboot_config_file begin......"
		rm $UBOOT_CONFIG_FILE	
		echo "======del_product_uboot end====="
		#删除uboot下面的board名头文件
	fi
	#sed -i '/'"$PRODUCT_NAME"'_config/d' u-boot/Makefile
	#sed -i '/ '"$PRODUCT_NAME"' /d' u-boot/Makefile
	
	sed -i '/'"$PRODUCT_NAME"'_config/,/_config/{//!d}' u-boot/Makefile  #删除指定一段文本，起始行和结束行除外
	sed -i '/'"$PRODUCT_NAME"'_config/d' u-boot/Makefile
	
	PRODUCT_NAME_C=`tr '[a-z]' '[A-Z]' <<<"$PRODUCT_NAME"`
	DEFINED_BOARD_MACRO_04=" || (defined CONFIG_MACH_""$PRODUCT_NAME_C"")"
	
	DEFINED_BOARD_MACRO_03="defined(CONFIG_""$PRODUCT_NAME_C"")"

	sed -i 's/'"$DEFINED_BOARD_MACRO_04"'//' `grep $DEFINED_BOARD_MACRO_04 -rl --exclude-dir=".git" u-boot`	
	echo "$DEFINED_BOARD_MACRO_04"
	sed -i 's/ || '"$DEFINED_BOARD_MACRO_03"'//' `grep $DEFINED_BOARD_MACRO_03 -rl --exclude-dir=".git" u-boot`	
	echo "$DEFINED_BOARD_MACRO_03"
}

function board_del_from_chipram()	
{	
	CHIPRAM_CONFIG_FILE="chipram/include/configs/""$PRODUCT_NAME"".h"
	if [ ! -f $CHIPRAM_CONFIG_FILE ];then
		echo "CHIPRAM do not have this CHIPRAM_CONFIG_FILE!"
		else 
		echo "$CHIPRAM_CONFIG_FILE  exist in chipram!"
		echo -e "del_product_chipram_config_file begin......"
		rm $CHIPRAM_CONFIG_FILE
		#删除chipram下面的board名头文件
		echo "======del_product_chipram end====="	
	fi
	#sed -i '/'"$PRODUCT_NAME"'_config/d' chipram/Makefile
	#sed -i '/ '"$PRODUCT_NAME"' /d' chipram/Makefile
	
	sed -i '/'"$PRODUCT_NAME"'_config/,/_config/{//!d}' chipram/Makefile
	sed -i '/'"$PRODUCT_NAME"'_config/d' chipram/Makefile
	
	PRODUCT_NAME_C=`tr '[a-z]' '[A-Z]' <<<"$PRODUCT_NAME"`
	DEFINED_BOARD_MACRO_04=" || (defined CONFIG_MACH_""$PRODUCT_NAME_C"")"
	DEFINED_BOARD_MACRO_03="defined(CONFIG_""$PRODUCT_NAME_C"")"

	sed -i 's/'"$DEFINED_BOARD_MACRO_04"'//' `grep $DEFINED_BOARD_MACRO_04 -rl chipram`
	echo "$DEFINED_BOARD_MACRO_04"
	sed -i 's/ || '"$DEFINED_BOARD_MACRO_03"'//' `grep $DEFINED_BOARD_MACRO_03 -rl --exclude-dir=".git" chipram`
	echo "$DEFINED_BOARD_MACRO_03"
}

function board_del_from_kernel()	
{
	KERNEL_DECONFIG_FILE_DT="kernel/arch/arm/configs/""$PRODUCT_NAME""_dt_defconfig"
	KERNEL_DECONFIG_FILE_DT2="kernel/arch/arm/configs/""$PRODUCT_NAME""-dt_defconfig"
	KERNEL_DECONFIG_FILE_NATIVE="kernel/arch/arm/configs/""$PRODUCT_NAME""-native_defconfig"
	KERNEL_DECONFIG_FILE="kernel/arch/arm/configs/""$PRODUCT_NAME""_defconfig"
	if [ ! -f $KERNEL_DECONFIG_FILE_DT ];then
		echo "kernel do not have this KERNEL_DECONFIG_FILE_DT!"
		else 
		echo "$KERNEL_DECONFIG_FILE_DT  exist in kernel!"
		echo -e "del_product_kernel_deconfig_file_dt ......"
		rm $KERNEL_DECONFIG_FILE_DT
		#删除kernel下面的dt版本defconfig文件
	fi
	if [ ! -f $KERNEL_DECONFIG_FILE_DT2 ];then
		echo "kernel do not have this KERNEL_DECONFIG_FILE-DT!"
		else 
		echo "$KERNEL_DECONFIG_FILE-DT  exist in kernel!"
		echo -e "del_product_kernel_deconfig_file-dt ......"
		rm $KERNEL_DECONFIG_FILE_DT2
		#删除kernel下面的dt版本defconfig文件
	fi
	if [ ! -f $KERNEL_DECONFIG_FILE_NATIVE ];then
		echo "kernel do not have this KERNEL_DECONFIG_FILE_NATIVE!"
		else 
		echo "$KERNEL_DECONFIG_FILE_NATIVE  exist in kernel!"
		echo -e "del_product_kernel_deconfig_file_native ......"
		rm $KERNEL_DECONFIG_FILE_NATIVE
		#删除kernel下面的native版本defconfig文件
	fi
	if [ ! -f $KERNEL_DECONFIG_FILE ];then
		echo "kernel do not have this KERNEL_DECONFIG_FILE!"
		else 
		echo "$KERNEL_DECONFIG_FILE  exist in kernel!"
		echo -e "del_product_kernel_deconfig_file ......"
		rm $KERNEL_DECONFIG_FILE
		#删除kernel下面的defconfig文件
	fi
	KERNEL_SCX15_DTS_FILE="kernel/arch/arm/boot/dts/""sprd-scx15_""$PRODUCT_NAME"".dts"
	KERNEL_SCX35_DTS_FILE="kernel/arch/arm/boot/dts/""sprd-scx35_""$PRODUCT_NAME"".dts"
	KERNEL_SCX35L_DTS_FILE="kernel/arch/arm/boot/dts/""sprd-scx35l_""$PRODUCT_NAME"".dts"
	if [ ! -f $KERNEL_SCX15_DTS_FILE ];then
		echo "kernel do not have this KERNEL_SCX15_DTS_FILE!"
		else 
		echo "$KERNEL_SCX15_DTS_FILE  exist in kernel!"
		echo -e "del_product_kernel_SCX15_DTS_FILE ......"
		rm $KERNEL_SCX15_DTS_FILE
		#删除kernel下面board命名的dts文件,dolphin相关
	fi
	if [ ! -f $KERNEL_SCX35_DTS_FILE ];then
		echo "kernel do not have this KERNEL_SCX35_DTS_FILE!"
		else 
		echo "$KERNEL_SCX35_DTS_FILE  exist in kernel!"
		echo -e "del_product_kernel_SCX35_DTS_FILE ......"
		rm $KERNEL_SCX35_DTS_FILE
		#删除kernel下面board命名的dts文件,shark和tshark相关
	fi
	if [ ! -f $KERNEL_SCX35L_DTS_FILE ];then
		echo "kernel do not have this KERNEL_SCX35L_DTS_FILE!"
		else 
		echo "$KERNEL_SCX35_DTS_FILE  exist in kernel!"
		echo -e "KERNEL_SCX35L_DTS_FILE ......"
		rm $KERNEL_SCX35L_DTS_FILE
		#删除kernel下面board命名的dts文件,sharkl
	fi
	KERNEL_BOARD_C="kernel/arch/arm/mach-sc/""board-""$PRODUCT_NAME"".c"
	if [ ! -f $KERNEL_BOARD_C ];then
		echo "KERNEL do not have this KERNEL_BOARD_C file!"
		else 
		echo "$KERNEL_BOARD_C  exist in kernel!"
		echo -e "del_product_kernel_KERNEL_BOARD_C file begin......"
		rm $KERNEL_BOARD_C
		#删除kernel下面board-sp****.c文件
	fi
	KERNEL_BOARD_H="kernel/arch/arm/mach-sc/include/mach/""__board-""$PRODUCT_NAME"".h"
	if [ ! -f $KERNEL_BOARD_H ];then
		echo "KERNEL do not have this KERNEL_BOARD_H file!"
		else
		echo "$KERNEL_BOARD_H  exist in kernel!"
		echo -e "del $KERNEL_BOARD_H  begin......"
		rm $KERNEL_BOARD_H
		#删除kernel下面__board-sp****.h文件
	fi

	PRODUCT_NAME_C=`tr '[a-z]' '[A-Z]' <<<"$PRODUCT_NAME"`
	#大写board名

	DEFINED_BOARD_MACRO_04=" || (defined CONFIG_MACH_""$PRODUCT_NAME_C"")"
	echo $DEFINED_BOARD_MACRO_04
	sed -i 's/'"$DEFINED_BOARD_MACRO_04"'//' `grep $DEFINED_BOARD_MACRO_04 -rl --exclude-dir=".git" kernel`

	DEFINED_BOARD_MACRO_03="defined(CONFIG_MACH_""$PRODUCT_NAME_C"")"
	#大写board名的宏定义名
	echo "$DEFINED_BOARD_MACRO_03"
	sed -i 's/ || '"$DEFINED_BOARD_MACRO_03"'//' `grep $DEFINED_BOARD_MACRO_04 -rl --exclude-dir=".git" kernel`
	#删除board.h文件中，大写board名的宏定义名的字符串

	declare -i line_index  #用于计算行号
	#IF_DEF_PRODUCT_NAME="#ifdef CONFIG_MACH_""$PRODUCT_NAME_C"
	TEMP="CONFIG_MACH_""$PRODUCT_NAME_C"
	line_index=`sed -n "/$TEMP$""/=" kernel/arch/arm/mach-sc/include/mach/board.h`  #向后配比，避免sp7730ec和sp7730ectrisim或者sp7730ecopenphone干扰
	echo $line_index
	if [ $line_index = 0 ];then
		echo "something error!!!"
	else
		line_index=line_index-1
		echo $line_index
		sed -i "$line_index""d" kernel/arch/arm/mach-sc/include/mach/board.h
		sed -i '/'"CONFIG_MACH_$PRODUCT_NAME_C"'$/,/#endif/d' kernel/arch/arm/mach-sc/include/mach/board.h
		#删除board.h文件中，大写board名起始，到接下来第一个#endif截至，中间全部内容
		#且完全匹配该“大写board名”，例如“大写board名_LC”版本不删
	fi

	PRODUCT_NAME_C_DTS="(CONFIG_MACH_""$PRODUCT_NAME_C"")"
	sed -i '/'"$PRODUCT_NAME_C_DTS"'/d' kernel/arch/arm/boot/dts/Makefile
	#删除kernel/arch/arm/boot/dts/Makefile中，大写board名的宏定义所在行的一整行
	BOARD_BASED="board"
	MACH_PRODUCT_NAME_C="MACH_""&PRODUCT_NAME_C"

	TEMP="MACH_""$PRODUCT_NAME_C"
	line_index=`sed -n "/$TEMP$""/=" kernel/arch/arm/mach-sc/Kconfig`
	if [ $line_index = 0 ];then
		echo "something error!!!"
	else
		line_index=line_index-1
		echo $line_index
		sed -i "$line_index""d" kernel/arch/arm/mach-sc/Kconfig
		sed -i '/MACH_'"$PRODUCT_NAME_C"'$/,/'"$BOARD_BASED"'/d' kernel/arch/arm/mach-sc/Kconfig
		#删除kconfig中，大写board名定义的一段，完整删除，完全匹配
	fi

	sed -i '/board-'"$PRODUCT_NAME"'.o/d' kernel/arch/arm/mach-sc/Makefile
	#删除kernel/arch/arm/mach-sc/Makefile中,sp****.o所在行
}


if [ ! -d "./kernel" ];then
	echo "board_del.sh maybe is not in correct dir,please put it to android top dir!!"
	exit 0
fi
if [ ! -d "./u-boot" ];then
	echo "board_del.sh maybe is not in correct dir,please put it to android top dir!!"
	exit 0
fi
if [ ! -d "./chipram" ];then
	echo "board_del.sh maybe is not in correct dir,please put it to android top dir!!"
	exit 0
fi
if [ ! -d "./device/sprd" ];then
	echo "board_del.sh maybe is not in correct dir,please put it to android top dir!!"
	exit 0
fi

PRODUCT_NAME=

select_product

which_is_selected

wrong_board_name

board_del_from_device

board_del_from_uboot

board_del_from_chipram

board_del_from_kernel

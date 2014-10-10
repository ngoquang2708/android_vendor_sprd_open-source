//

擦/写同一逻辑块eb的负载均衡情况

1.编译测试版本，建立测试分区
需要使用nand版本，测试时增加测试分区，对应配置文件需要修改，如：文件sp7730gga.h中内容
#define MTDPARTS_DEFAULT "mtdparts=sprd-nand:256k(spl),768k(2ndbl),512k(kpanic),-(ubipac)"
改为
#define MTDPARTS_DEFAULT "mtdparts=sprd-nand:256k(spl),768k(2ndbl),512k(kpanic),32m(testubi),-(ubipac)"

make bootloader -j4后，生成两个文件 fdl2.bin, u-boot.bin用来替换scx35_sp7730ggacuccspecBplus_UUI_dt-userdebug-native.pac中对应文件

2.编译源文件生成文件ubiwltest

3.建立配置文件tang.sh

stop
/data/ubiwltest 8
/data/ubiwltest 1
/data/ubiwltest 4
/data/ubiwltest 6 4
start

上面命令行参数可配置为
/data/ubiwltest 6 4(8/64/128/1024/4096/8192)

4.建立批处理文件(windows系统)testubi.bat

adb devices

adb root
adb remount

adb push ubiwltest ./data
adb shell chmod 755 ./data/ubiwltest

adb push tang.sh ./system/bin
adb shell chmod 777 ./system/bin/tang.sh

adb shell ./system/bin/tang.sh

adb pull ./data/mtd_test.txt
adb pull ./data/test_wl.txt
adb pull ./data/test_wl_diff.txt

5.最终执行结果见文件mtd_test.txt
attach finish ubi_num = 1, s_leb_size=3e000
do_test_wl start
do_test_wl finish! num_of_times=4096.
pebnums = 128, mean = 8192, deviation=0.067628.
min_ec = 5104, max_ec = 10071
均方差deviation越小越好

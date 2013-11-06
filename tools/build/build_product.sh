#
# This is a build script for a product package
#

if [ $# -ne 5 -o "$1" = "-h" ]; then
	echo "Usage: $0 <product> <variant> <vlx> <outdir> <-jN>"
	exit 1
fi

if [ ! -f Makefile -o ! -f build/core/main.mk ]; then
	echo "Current directory is not an android folder!"
	exit 1
fi

PROD=$1
VAR=$2
VLX=$3
OUTDIR=$4
JOBS=$5

LOG=$OUTDIR/$PROD-$VAR-$VLX.build.log
PAK=$OUTDIR/$PROD-$VAR-$VLX.tar.gz

echo "==== $PROD-$VAR-$VLX Start ===="

echo "==== $ANDROID_PRODUCT_OUT/$PROD-$VAR-$VLX ====" > $LOG
date >> $LOG
echo "==== ====" >> $LOG

# setenv & choose product
. build/envsetup.sh >>$LOG 2>&1
choosecombo 1 $PROD $VAR >>$LOG 2>&1
# failure handle
if [ $? -ne 0 ]; then
	echo "==== ====" >> $LOG
	date >> $LOG
	echo "==== Build Failed ====" >> $LOG

	exit 1
fi

# do clean
uclean >>/dev/null 2>&1
kclean >>/dev/null 2>&1
make clean >>/dev/null 2>&1

# do make
kheader >>$LOG 2>&1
make KALLSYMS_EXTRA_PASS=1 $JOBS >>$LOG 2>&1
# failure handle
if [ $? -ne 0 ]; then
	echo "==== ====" >> $LOG
	date >> $LOG
	echo "==== Build Failed ====" >> $LOG

	exit 1
fi

cd $ANDROID_PRODUCT_OUT
cp obj/KERNEL/vmlinux symbols/
find . -name "*.img" -o -name "*.bin" -o -name "installed-files.txt" -maxdepth 1| xargs tar -zcf $PAK 2>/dev/null
cd -

echo "==== ====" >> $LOG
date >> $LOG
echo "==== Build Successfully ====" >> $LOG

echo "==== $PROD-$VAR-$VLX Done ===="

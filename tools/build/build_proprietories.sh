#
# This is a build script for an idh package
#

if [ $# -ne 3 -o "$1" = "-h" ]; then
	echo "Usage: $0 <platform> <board> <outdir>"
	exit 1
fi

if [ ! -f Makefile -o ! -f build/core/main.mk ]; then
	echo "Current directory is not an android folder!"
	exit 1
fi

TOPDIR=`pwd`

PLATFORM=$1
BOARD=$2
OUTDIR=`readlink -f $3`

cd $TOPDIR/out/target/product
mkdir -p $PLATFORM

cd $BOARD
cat $TOPDIR/vendor/sprd/proprietories/$PLATFORM/prop.list | cpio -pdum ../$PLATFORM

cd ..
tar zcf $OUTDIR/proprietories-$PLATFORM.tar.gz $PLATFORM
rm -rf $PLATFORM

cd $TOPDIR

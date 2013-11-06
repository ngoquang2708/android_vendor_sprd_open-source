#
# This is a build script for an idh package
#

if [ $# -ne 2 -o "$1" = "-h" ]; then
	echo "Usage: $0 <board> <outdir>"
	exit 1
fi

if [ ! -f Makefile -o ! -f build/core/main.mk ]; then
	echo "Current directory is not an android folder!"
	exit 1
fi

BOARD=$1
OUTDIR=$2

FLIST=`sed -e "s/^/$BOARD\//g" vendor/sprd/proprietories/$BOARD/prop.list`
cd out/target/product
tar zcf $OUTDIR/proprietories-$BOARD.tar.gz $FLIST
cd -

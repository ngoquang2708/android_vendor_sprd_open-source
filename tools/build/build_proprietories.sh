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
PROJECT=$2
OUTDIR=`readlink -f $3`
BOARD=""
if [ -d "$TOPDIR/device/sprd" ]; then
  cd $TOPDIR/device/sprd
  BOARD=$(find . -type f -name "$PROJECT.mk"|grep -Po "/(.*)/"|sed 's/\///g')
  if [ -z "$BOARD" ]; then
    echo "The board $BOARD does not exist."
    exit 1
  fi
fi
  


cd $TOPDIR/out/target/product
mkdir -p $PLATFORM
cpio_dir="$(readlink -f $PLATFORM)"

# For sources
cd $TOPDIR
source_release_list=
for i in $(sed '/^system\//d' $TOPDIR/vendor/sprd/proprietories/$PLATFORM/prop.list); do
	if [ -d ${i} ]; then
		source_release_list="$(find ${i} | sed 1d)$(printf "\n${source_release_list}")"
	elif [ -f ${i} ]; then
		source_release_list="${source_release_list}$(printf "\n${i}")"
	fi
done
[ "${source_release_list}" ] && {
	mkdir ${cpio_dir}/sources
	echo "${source_release_list}" | cpio -pdum ${cpio_dir}/sources/
}
cd - >/dev/null

# For binaries
cd $BOARD
# cat $TOPDIR/vendor/sprd/proprietories/$PLATFORM/prop.list | cpio -pdum ../$PLATFORM
sed '/^system\//!d' $TOPDIR/vendor/sprd/proprietories/$PLATFORM/prop.list | cpio -pdum ${cpio_dir}

cd ..
tar zcf $OUTDIR/proprietories-$PLATFORM.tar.gz $PLATFORM
rm -rf $PLATFORM

cd $TOPDIR

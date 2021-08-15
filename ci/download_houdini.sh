#!/usr/bin/env bash

set -ex

HOUDINI_MAJOR="$1"
GOLD="$2"
PLATFORM="$(uname -s)"

pip install --user requests
# Downloads the houdini pakage to a hou.tar.gz archive
python ci/download_houdini.py $HOUDINI_MAJOR $GOLD

# Configure the destination dir structure (these will be nested in hou/)
mkdir -p hou

# @note - the bin/houdini folders are unused but exist so that the
#  houdini_source script detects the correct folder layout
if [ "$PLATFORM" == "Darwin" ]; then
    TARGET_ROOT=Frameworks/Houdini.framework/Versions/Current
    TOOLKIT_DST=$TARGET_ROOT/Resources/toolkit
    LIBRARIES_DST=$TARGET_ROOT/Libraries
    mkdir -p hou/$TARGET_ROOT/Resources/bin hou/$TARGET_ROOT/Resources/houdini
elif [ "$PLATFORM" == "Linux" ]; then
    TARGET_ROOT=.
    TOOLKIT_DST=toolkit
    LIBRARIES_DST=dsolib
    mkdir -p hou/bin hou/houdini
fi

mkdir -p hou/$TOOLKIT_DST
mkdir -p hou/$LIBRARIES_DST

# Extract data and configure source paths (also copy some custom files)
if [ "$PLATFORM" == "Darwin" ]; then
    brew install p7zip # used to extract the .dmg without mounting
    7z x hou.tar.gz # this is actually a .dmg, should create a file called Houdini
    pkgutil --expand-full Houdini/Houdini.pkg Houdini/Extracted
    EXTRACTED_ROOT=Houdini/Extracted/Framework.pkg/Payload/Houdini.framework/Versions/Current
    TOOLKIT_SRC=$EXTRACTED_ROOT/Resources/toolkit
    LIBRARIES_SRC=$EXTRACTED_ROOT/Libraries
    # Copy the setup scripts
    cp $EXTRACTED_ROOT/Resources/houdini_setup* hou/$TARGET_ROOT/Resources/.
elif [ "$PLATFORM" == "Linux" ]; then
    tar -xzf hou.tar.gz
    mv houdini* Houdini # The root folder will named something like houdini-18.5.633-linux_x86_64_gcc6.3
    cd Houdini && tar -xzf houdini.tar.gz && cd -
    TOOLKIT_SRC=Houdini/toolkit
    LIBRARIES_SRC=Houdini/dsolib
    # Copy the setup scripts
    cp Houdini/houdini_setup* hou/.
fi

# cleanup the original download
rm hou.tar.gz

# copy required files into hou dir
cp -r $TOOLKIT_SRC/cmake hou/$TOOLKIT_DST/.
cp -r $TOOLKIT_SRC/include hou/$TOOLKIT_DST/.
cp -r $LIBRARIES_SRC/libHoudini* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libopenvdb_sesi* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libblosc* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libhboost* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libz* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libbz2* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libtbb* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libHalf* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libjemalloc* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/liblzma* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libIex* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libImath* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libIlmThread* hou/$LIBRARIES_DST/.
cp -r $LIBRARIES_SRC/libIlmImf* hou/$LIBRARIES_DST/.

# write hou into hou.tar.gz and cleanup
tar -czvf hou.tar.gz hou

# move hou.tar.gz into hou subdirectory
rm -rf hou/*
mv hou.tar.gz hou

# inspect size of tarball
ls -lart hou/hou.tar.gz

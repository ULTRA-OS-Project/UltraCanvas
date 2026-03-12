#/bin/bash
VERSION=`date +%Y.%m.%d`
OUTPUTDIR=~/projects/UCAppImage
PROJECTDIR=~/projects/UltraCanvas
EXECUTABLE=$PROJECTDIR/cmake-build-release/bin/UltraCanvasTexter

rm -rf $OUTPUTDIR/AppDir \
&& mkdir $OUTPUTDIR/AppDir \
&& mkdir $OUTPUTDIR/AppDir/lib \
&& mkdir $OUTPUTDIR/AppDir/lib/x86_64-linux-gnu \
&& mkdir $OUTPUTDIR/AppDir/etc \
&& cp -r /etc/ImageMagick-6 $OUTPUTDIR/AppDir/etc \
&& cp -r /usr/lib/x86_64-linux-gnu/ImageMagick-6.9.11 $OUTPUTDIR/AppDir/lib/x86_64-linux-gnu \
&& linuxdeploy-x86_64.AppImage --appdir $OUTPUTDIR/AppDir \
    --executable $EXECUTABLE \
    --desktop-file $PROJECTDIR/appimage/uctexter.desktop \
    --icon-file $PROJECTDIR/appimage/uctexter.appimage.png \
    --custom-apprun $PROJECTDIR/appimage/AppRunTexter \
&& sed -i -e 's#/usr#././#g'  $OUTPUTDIR/AppDir/usr/lib/libMagickCore-6.Q16.so.6 \
&& mkdir $OUTPUTDIR/AppDir/usr/share/UltraCanvas \
&& cp -r media $OUTPUTDIR/AppDir/usr/share/UltraCanvas \
&& cd $OUTPUTDIR \
&& appimagetool --runtime-file $PROJECTDIR/appimage/runtime-x86_64 AppDir UCTexter-$VERSION-x86_64.AppImage

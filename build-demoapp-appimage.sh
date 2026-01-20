#/bin/bash
VERSION=`date +%Y.%m.%d`
OUTPUTDIR=~/projects/UCAppImage
PROJECTDIR=~/projects/UltraCanvas
rm -rf $OUTPUTDIR/AppDir \
&& mkdir $OUTPUTDIR/AppDir \
&& mkdir $OUTPUTDIR/AppDir/Examples \
&& cp -r Docs media $OUTPUTDIR/AppDir\
&& cp -r Examples/* $OUTPUTDIR/AppDir/Examples/ \
&& mkdir $OUTPUTDIR/AppDir/lib \
&& mkdir $OUTPUTDIR/AppDir/lib/x86_64-linux-gnu \
&& mkdir $OUTPUTDIR/AppDir/etc \
&& cp -r /etc/ImageMagick-6 $OUTPUTDIR/AppDir/etc \
&& cp -r /usr/lib/x86_64-linux-gnu/ImageMagick-6.9.11 $OUTPUTDIR/AppDir/lib/x86_64-linux-gnu \
&& linuxdeploy-x86_64.AppImage --appdir $OUTPUTDIR/AppDir \
    --executable $PROJECTDIR/UltraCanvasDemo \
    --desktop-file $PROJECTDIR/appimage/ucdemoapp.desktop \
    --icon-file $PROJECTDIR/appimage/ucdemoapp.appimage.png \
    --custom-apprun $PROJECTDIR/appimage/AppRun \
&& sed -i -e 's#/usr#././#g'  $OUTPUTDIR/AppDir/usr/lib/libMagickCore-6.Q16.so.6 \
&& cd $OUTPUTDIR \
&& appimagetool --runtime-file $PROJECTDIR/appimage/runtime-x86_64 AppDir UCDemo-$VERSION-x86_64.AppImage

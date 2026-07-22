#/bin/bash
OUTPUTDIR=~/projects/UCAppImage
PROJECTDIR=~/projects/UltraCanvas
VERSION=$(sed -nE '1s/^#### [0-9-]+ \*([0-9]+\.[0-9]+\.[0-9]+)\*.*/\1/p' "$PROJECTDIR/Docs/UltraCanvas/CHANGELOG.md")
if [ -z "$VERSION" ]; then
    echo "Error: could not parse version from $PROJECTDIR/Docs/UltraCanvas/CHANGELOG.md (expected '#### YYYY-MM-DD *x.y.z*')" >&2
    exit 1
fi
BUILDDIR=$PROJECTDIR/cmake-build-release
EXECUTABLE=$BUILDDIR/bin/UltraCanvasDemo

rm -rf $OUTPUTDIR/AppDir \
&& mkdir $OUTPUTDIR/AppDir \
&& mkdir $OUTPUTDIR/AppDir/lib \
&& mkdir $OUTPUTDIR/AppDir/lib/x86_64-linux-gnu \
&& mkdir $OUTPUTDIR/AppDir/etc \
&& mkdir $OUTPUTDIR/AppDir/usr \
&& mkdir $OUTPUTDIR/AppDir/usr/lib \
&& cp -r /etc/ImageMagick-6 $OUTPUTDIR/AppDir/etc \
&& cp -r /usr/lib/x86_64-linux-gnu/ImageMagick-* $OUTPUTDIR/AppDir/lib/x86_64-linux-gnu \
&& cp $BUILDDIR/lib/*.so $OUTPUTDIR/AppDir/usr/lib \
&& cp /usr/lib/x86_64-linux-gnu/libMagickCore-* $OUTPUTDIR/AppDir/usr/lib \
&& linuxdeploy-x86_64.AppImage --appdir $OUTPUTDIR/AppDir \
    --executable $EXECUTABLE \
    --desktop-file $PROJECTDIR/appimage/ucdemoapp.desktop \
    --icon-file $PROJECTDIR/appimage/ucdemoapp.appimage.png \
    --custom-apprun $PROJECTDIR/appimage/AppRunDemo \
&& sed -i -e 's#/usr#././#g'  $OUTPUTDIR/AppDir/usr/lib/libMagickCore-6.Q16.so.6 \
&& mkdir $OUTPUTDIR/AppDir/usr/share/UltraCanvas \
&& cp -r media Docs $OUTPUTDIR/AppDir/usr/share/UltraCanvas \
&& mkdir $OUTPUTDIR/AppDir/usr/share/UltraCanvas/DemoApp \
&& cp -r Apps/DemoApp/*.cpp $OUTPUTDIR/AppDir/usr/share/UltraCanvas/DemoApp/ \
&& cd $OUTPUTDIR \
&& appimagetool --runtime-file $PROJECTDIR/appimage/runtime-x86_64 AppDir UCDemo-Linux-$VERSION-x86_64.AppImage

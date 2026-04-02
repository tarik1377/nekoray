#!/bin/bash

sudo apt-get install fuse -y

cp -r linux64 greenrhythm.AppDir

# The file for Appimage

rm greenrhythm.AppDir/launcher

cat >greenrhythm.AppDir/greenrhythm.desktop <<-EOF
[Desktop Entry]
Name=GreenRhythm
Exec=echo "greenrhythm started"
Icon=greenrhythm
Type=Application
Categories=Network
EOF

cat >greenrhythm.AppDir/AppRun <<-EOF
#!/bin/bash
echo "PATH: \${PATH}"
echo "greenrhythm running on: \$APPDIR"
LD_LIBRARY_PATH=\${APPDIR}/usr/lib QT_PLUGIN_PATH=\${APPDIR}/usr/plugins \${APPDIR}/greenrhythm -appdata "\$@"
EOF

chmod +x greenrhythm.AppDir/AppRun

# build

curl -fLSO https://github.com/AppImage/AppImageKit/releases/latest/download/appimagetool-x86_64.AppImage
chmod +x appimagetool-x86_64.AppImage
./appimagetool-x86_64.AppImage greenrhythm.AppDir

# clean

rm appimagetool-x86_64.AppImage
rm -rf greenrhythm.AppDir

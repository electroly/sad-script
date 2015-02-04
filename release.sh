mkdir release || true
rm -f release/*

make EXTRAFLAGS=-DNDEBUG clean all test
rm -rf /tmp/sad-script
mkdir /tmp/sad-script
cp src/sad-script.c /tmp/sad-script/
cp src/sad-script.h /tmp/sad-script/
cp bin/sad.exe /tmp/sad-script/
cp bin/prelude.sad /tmp/sad-script/
pushd /tmp/
zip -r sad-script.zip sad-script/
popd
mv /tmp/sad-script.zip release/
rm -rf /tmp/sad-script

make -f Makefile.emcc EXTRAFLAGS=-DNDEBUG clean all test bin/sad-script
cp bin/sad-script.js release/
cp src/try.html release/
cp src/prelude.sad release/
cp src/share.php release/
cp language.txt release/

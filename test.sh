#!/usr/bin/env bash
rm -rf mountpoint/tests
mkdir mountpoint/tests
rmdir mountpoint/tests
mkdir mountpoint/tests
mv mountpoint/tests mountpoint/tests.bk
mv mountpoint/tests.bk mountpoint/tests
ln -s mountpoint/tests mountpoint/tests.link
rm mountpoint/tests.link

touch mountpoint/tests/a.txt
rm mountpoint/tests/a.txt
touch mountpoint/tests/a.txt
echo ffffffffffff > mountpoint/tests/a.txt
echo ffffffffffff >> mountpoint/tests/a.txt
echo xx > mountpoint/tests/a.txt
truncate mountpoint/tests/a.txt --size 4096
truncate mountpoint/tests/a.txt --size 0
sync mountpoint/tests/a.txt

ln -s mountpoint/tests/a.txt mountpoint/tests/b.txt
rm mountpoint/tests/b.txt
ln mountpoint/tests/a.txt mountpoint/tests/b.txt
rm mountpoint/tests/b.txt
cp mountpoint/tests/a.txt mountpoint/tests/b.txt
rm mountpoint/tests/b.txt

mv mountpoint/tests/a.txt mountpoint/tests/b.txt
mv mountpoint/tests/b.txt mountpoint/tests/a.txt

chmod 777 mountpoint/tests/a.txt

stat mountpoint/tests/a.txt > /dev/null
df -lh --all|grep mountpoint > /dev/null    


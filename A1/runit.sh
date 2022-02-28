#!/bin/bash
root="/tmp/yangj295"
image="img1"
image_size=64KiB
inodes=16


echo "Create an image:"
fusermount -u ${root}
truncate -s ${image_size} ${image}
echo " "

echo "Format the image:"
make
./mkfs.a1fs -f -i ${inodes} ${image}
./a1fs ${image} ${root}
echo " "

echo "A few operations --"
echo " "

echo "Initial state of the fs:"
stat -f ${root}
ls -la ${root}
echo " "

echo "Create some directories:" 
mkdir ${root}/dir1		
mkdir ${root}/dir2
mkdir ${root}/dir2/dir3		         
ls -la ${root}
echo " "

echo "Create files and add some contents to it:"
touch ${root}/file1				          
echo "this is file1" >> ${root}/file1
echo "this is file2" > ${root}/file2
echo "more content" >> ${root}/file2
ls -la ${root}
echo " "

echo "See the contents of the file:"
cat ${root}/file1
cat ${root}/file2
tail -c 6 ${root}/file1
echo " "

echo "See the current stat of the root dir:"
stat -f ${root}
echo " "

echo "Truncate on file1--"
echo " "
echo "Extend file1 to 50:"
truncate -s 50 ${root}/file1		  # extend the file
ls -la	${root}
echo " "
echo "Shrink file1 to 3:"
truncate -s 3 ${root}/file1	          # shrink the file
ls -la	${root}	
echo " "			           

echo "Remove operations:"
echo " "
echo "Remove file1 and dir1:"
rm -f ${root}/file1
rmdir ${root}/dir1		            
stat -f ${root}
ls -la	${root}
echo " "

echo "Unmount the fs"
fusermount -u ${root}
echo " "

echo "Mount the fs again and display some content of the fs"
./a1fs img1 ${root}
ls -la ${root}
stat -f ${root}
echo " "

echo "Unmount the file system"
fusermount -u ${root}

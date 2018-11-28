#This script is intended to be run from the coreutils repository directory
#like so: ./tee_benchmark.sh
#
#Please read over the script and make any modifications necessary to suit your environment
#Things like USB drives will need created or skipped, and so on.
#The script deletes any files it creates after each benchmark.

#first benchmark
echo "Creating a 2GB file and copying it to two other files..."
dd if=/dev/zero of=twogigs bs=1M count=2000 status=none
echo "Running with Tee..."
time cat twogigs | tee file1 file2
rm file1 file2
echo "Running with Teep..."
time cat twogigs | src/tee file1 file2
rm file1 file2

echo "Running with Tee..."
time cat twogigs | tee file1 file2
rm file1 file2

#second benchmark
echo "Sending the 2GB file to two different checksum programs, and gzipping it..."
echo "With Tee..."
time cat twogigs | tee >(md5sum > twogigs.md5) >(sha256sum > twogigs.sha) | gzip -c > twogigs.gz
rm twogigs.md5 twogigs.sha twogigs.gz
echo "With Teep..."
time cat twogigs | src/tee >(md5sum > twogigs.md5) >(sha256sum > twogigs.sha) | gzip -c > twogigs.gz
rm twogigs.md5 twogigs.sha twogigs.gz

echo "With Tee..."
time cat twogigs | tee >(md5sum > twogigs.md5) >(sha256sum > twogigs.sha) | gzip -c > twogigs.gz
rm twogigs.md5 twogigs.sha twogigs.gz

#done with the twogigs file so delete it
rm twogigs

#third benchmark
echo "Creating and sending a 512MB file to three different disks..."
dd if=/dev/zero of=fivetwelve bs=1M count=512 status=none
#you will need to edit these paths to fit your disk names
disk1="/media/jaywalker/usb1"
disk2="/media/jaywalker/usb2"
disk3="/media/jaywalker/usb3"
echo "With Tee..."
time (cat fivetwelve | tee ${disk1}/fivetwelve ${disk2}/fivetwelve ${disk3}/fivetwelve && sync)
rm ${disk1}/fivetwelve ${disk2}/fivetwelve ${disk3}/fivetwelve

echo "With Teep..."
time (cat fivetwelve | tee ${disk1}/fivetwelve ${disk2}/fivetwelve ${disk3}/fivetwelve && sync)
rm ${disk1}/fivetwelve ${disk2}/fivetwelve ${disk3}/fivetwelve

echo "With Tee..."
time (cat fivetwelve | tee ${disk1}/fivetwelve ${disk2}/fivetwelve ${disk3}/fivetwelve && sync)
rm ${disk1}/fivetwelve ${disk2}/fivetwelve ${disk3}/fivetwelve

#done with that file too
rm fivetwelve

echo ""; echo "Finished running benchmarks."

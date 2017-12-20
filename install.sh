#!/bin/sh

cp *.so /usr/local/lib/ 
cp ./mjpg_streamer /usr/local/bin/

if [ -d "/run/mjpg" ]
then
	rm -rf /run/mjpg
fi

mkdir /run/mjpg
mkfifo /run/mjpg/input_pipe
chmod o+wr /run/mjpg/input_pipe
echo "#!/bin/sh" > input_file.sh
echo  "mjpg_streamer -o \"output_http.so -w /run/mjpg/www \" -i \"input_file.so\" -b "  >> input_file.sh
chmod o+x input_file.sh
chmod u+x input_file.sh
cp input_file.sh /usr/local/bin/mjpg_file 
cp -r ./www /run/mjpg


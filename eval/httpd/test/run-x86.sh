#! /bin/bash

# Setup signal trap
sigterm_handler() { 
  echo "Shutdown signal received."
  rm -f parallel-tmp
  exit 1
}
trap 'trap " " SIGINT SIGTERM SIGHUP; kill 0; wait; sigterm_handler' SIGINT SIGTERM SIGHUP


# Kill launched httpd or service occupying port
PID=`sudo lsof -i -P -n | grep "\b80\b" | awk '{print $2}'`
if [[ "" !=  "$PID" ]]; then
  echo "Kill $PID"
  sudo kill -9 $PID
fi

# Test function using ApacheBench
test() {
  # warm up
  parallel :::  "ab -c 2 -n 1000 https://localhost/"\
          "ab -c 2 -n 1000 https://localhost/" \
          "ab -c 2 -n 1000 https://localhost/" \
          "ab -c 2 -n 1000 https://localhost/" > /dev/null 2>&1
  
  parallel :::  "ab -c 4 -n 1000 https://localhost/"\
          "ab -c 4 -n 1000 https://localhost/" \
          "ab -c 4 -n 1000 https://localhost/" \
          "ab -c 4 -n 1000 https://localhost/" > /dev/null 2>&1

  parallel :::  "ab -c 4 -n 1000 https://localhost/"\
          "ab -c 4 -n 1000 https://localhost/" \
          "ab -c 4 -n 1000 https://localhost/" \
          "ab -c 4 -n 1000 https://localhost/" \
          "ab -c 4 -n 1000 https://localhost/" \
          "ab -c 4 -n 1000 https://localhost/" \
          "ab -c 6 -n 1000 https://localhost/" \
          "ab -c 6 -n 1000 https://localhost/" \
          "ab -c 6 -n 1000 https://localhost/" \
          "ab -c 6 -n 1000 https://localhost/" > /dev/null 2>&1
  
  # 4 threads
  parallel :::  "ab -c 1 -n 10000 https://localhost/"\
          "ab -c 1 -n 10000 https://localhost/" \
          "ab -c 1 -n 10000 https://localhost/" \
          "ab -c 1 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  cp parallel-tmp $1-4-threads
  rm parallel-tmp
  echo "[complete 4 threads]"

  # 8 threads
  parallel :::  "ab -c 2 -n 10000 https://localhost/"\
          "ab -c 2 -n 10000 https://localhost/" \
          "ab -c 2 -n 10000 https://localhost/" \
          "ab -c 2 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 8 threads]"

  # 12 threads
  parallel :::  "ab -c 3 -n 10000 https://localhost/"\
          "ab -c 3 -n 10000 https://localhost/" \
          "ab -c 3 -n 10000 https://localhost/" \
          "ab -c 3 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 12 threads]"

  # 16 threads
  parallel :::  "ab -c 3 -n 10000 https://localhost/"\
          "ab -c 3 -n 10000 https://localhost/" \
          "ab -c 3 -n 10000 https://localhost/" \
          "ab -c 3 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 16 threads]"

  # 20 threads
  parallel :::  "ab -c 4 -n 10000 https://localhost/"\
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 20 threads]"

  # 24 threads
  parallel :::  "ab -c 4 -n 10000 https://localhost/"\
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 24 threads]"

  # 28 threads
  parallel :::  "ab -c 4 -n 10000 https://localhost/"\
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 28 threads]"

  # 32 threads
  parallel :::  "ab -c 4 -n 10000 https://localhost/"\
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 32 threads]"

  # 36 threads
  parallel :::  "ab -c 4 -n 10000 https://localhost/"\
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 36 threads]"

  # 40 threads
  parallel :::  "ab -c 4 -n 10000 https://localhost/"\
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 6 -n 10000 https://localhost/" \
          "ab -c 6 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 40 threads]"

  # 44 threads
  parallel :::  "ab -c 4 -n 10000 https://localhost/"\
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 6 -n 10000 https://localhost/" \
          "ab -c 6 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 44 threads]"

  # 48 threads
  parallel :::  "ab -c 4 -n 10000 https://localhost/"\
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 4 -n 10000 https://localhost/" \
          "ab -c 6 -n 10000 https://localhost/" \
          "ab -c 6 -n 10000 https://localhost/" \
          "ab -c 6 -n 10000 https://localhost/" \
          "ab -c 6 -n 10000 https://localhost/" \
      > parallel-tmp 2>&1

  python process.py $1

  rm parallel-tmp
  echo "[complete 48 threads]"
}


# Get user input for test file size
file_size=1
echo 'Please enter test file size (KB) for httpd (for example, enter '1' for 1KB, enter '128' for 128KB): '
read file_size
echo 'file_size: '$file_size' KB'


# Generate corresponding htdoc file for httpd distribution
echo 'Generate corresponding htdoc file for httpd distribution ...'
truncate -s ''$file_size'K' PREFIX-HTTPD/httpd/htdocs/test
echo 'Generate corresponding htdoc file for httpd distribution done!'


# Create result dir
cur_time=$(date "+%Y-%m%d-%H%M%S")
res_dir=''$cur_time'-'$file_size'KB'
mkdir -p results/$res_dir


# Launch orginal httpd
echo 'Launch original unprotected httpd ...'
sudo LD_LIBRARY_PATH="PREFIX-OPENSSL-ORIGIN/openssl/lib64/" \
PREFIX-HTTPD/httpd/bin/apachectl \
-k start -X &
echo 'Launch original unprotected httpd done!'

echo 'Start testing original unprotected httpd using ApacheBench ...'
res_file_origin=results/$res_dir/origin
test $res_file_origin
echo 'End testing original unprotected httpd using ApacheBench!'

echo 'Stop original unprotected httpd ...'
PID=`sudo lsof -i -P -n | grep "\b80\b" | awk '{print $2}'`
if [[ "" !=  "$PID" ]]; then
  echo "Kill $PID"
  sudo kill -9 $PID
fi


# Launch VDom httpd
echo 'Launch VDom protected httpd ...'
sudo LD_LIBRARY_PATH="PREFIX-OPENSSL-VDOM/openssl/lib64/" \
PREFIX-HTTPD/httpd/bin/apachectl \
-k start -X > /dev/null 2>&1 &
echo 'Launch VDom protected httpd done!'

echo 'Start testing VDom protected httpd using ApacheBench ...'
res_file_vdom=results/$res_dir/vdom
test $res_file_vdom
echo 'End testing VDom protected httpd using ApacheBench!'

echo 'Stop VDom protected httpd ...'
PID=`sudo lsof -i -P -n | grep "\b80\b" | awk '{print $2}'`
if [[ "" !=  "$PID" ]]; then
  echo "Kill $PID"
  sudo kill -9 $PID
fi


echo 'Plot result figure ...'
python plot.py results/$res_dir $file_size
echo 'Plot result figure done!'

echo 'Please check results in ./results/'$res_dir''






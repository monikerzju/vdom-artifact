#! /bin/bash

# Setup signal trap
sigterm_handler() { 
  echo "Shutdown signal received."
  rm -f sysbench-tmp
  exit 1
}
trap 'trap " " SIGINT SIGTERM SIGHUP; kill 0; wait; sigterm_handler' SIGINT SIGTERM SIGHUP


# Kill launched httpd or service occupying port
PID=`sudo lsof -i -P -n | grep "\b3306\b" | awk '{print $2}'`
if [[ "" !=  "$PID" ]]; then
  echo "Kill $PID"
  sudo kill -9 $PID
fi
sleep 3


# Test function using sysbench
test() {
  # prepare data
  sysbench \
  --db-driver=mysql \
  --mysql-user=sbtest_user \
  --mysql_password=YOUR_PASSWORD \
  --mysql-db=sbtest \
  --mysql-host=127.0.0.1 \
  --mysql-port=3306 \
  --tables=10 \
  /usr/share/sysbench/oltp_common.lua cleanup > /dev/null 2>&1

  sleep 3

  sysbench \
  --db-driver=mysql \
  --mysql-user=sbtest_user \
  --mysql_password=YOUR_PASSWORD \
  --mysql-db=sbtest \
  --mysql-host=127.0.0.1 \
  --mysql-port=3306 \
  --tables=10 \
  --table-size=1000 \
  --mysql_storage_engine=MEMORY \
  /usr/share/sysbench/oltp_common.lua prepare > /dev/null 2>&1

  sleep 3

	# warm up
  for i in {3..24..2}
  do
    sysbench \
      --db-driver=mysql \
      --mysql-user=sbtest_user \
      --mysql_password=YOUR_PASSWORD \
      --mysql-db=sbtest \
      --mysql-host=127.0.0.1 \
      --mysql-port=3306 \
      --tables=10 \
      --table-size=1000 \
      --threads=$i \
      --time=1 \
      --report-interval=1 \
      --range_selects=false \
      --mysql-ignore-errors=1062 \
      /usr/share/sysbench/oltp_read_write.lua run > /dev/null 2>&1

      sleep 3
  done

  sleep 10

	# run sysbench test
  for i in {4..24..4}
  do
    echo "[start $i threads]"
    sysbench \
      --db-driver=mysql \
      --mysql-user=sbtest_user \
      --mysql_password=YOUR_PASSWORD \
      --mysql-db=sbtest \
      --mysql-host=127.0.0.1 \
      --mysql-port=3306 \
      --tables=10 \
      --table-size=1000 \
      --threads=$i \
      --time=25 \
      --report-interval=1 \
      --range_selects=false \
      --mysql-ignore-errors=1062 \
      /usr/share/sysbench/oltp_read_write.lua run > sysbench-tmp

      python process.py $1
		  rm sysbench-tmp
      sleep 10
  done
}


# Create result dir
cur_time=$(date "+%Y-%m%d-%H%M%S")
res_dir=$cur_time
mkdir -p results/$res_dir

# Launch VDom mysql
echo 'Launch VDom protected mysql ...'
PREFIX-MYSQL-VDOM/usr/local/mysql/bin/mysqld \
  --user=mysql \
  --basedir=PREFIX-MYSQL-VDOM/usr/local/mysql \
  --datadir=PREFIX-MYSQL-VDOM/usr/local/mysql/data \
  --plugin-dir=PREFIX-MYSQL-VDOM/usr/local/mysql/lib/plugin \
  --thread-cache-size=60 & > /dev/null 2>&1 &
sleep 30
echo 'Launch VDom protected mysql done!'

echo 'Start testing VDom protected mysql using sysbench ...'
PREFIX-MYSQL-VDOM/usr/local/mysql/bin/mysql -u sbtest_user -pYOUR_PASSWORD -e'exit' > /dev/null 2>&1
sleep 10
res_file_vdom=results/$res_dir/vdom
test $res_file_vdom
echo 'End testing VDom protected mysql using sysbench!'

echo 'Stop VDom protected mysql ...'
PID=`sudo lsof -i -P -n | grep "\b3306\b" | awk '{print $2}'`
if [[ "" !=  "$PID" ]]; then
  echo "Kill $PID"
  sudo kill -9 $PID
fi

sleep 15

PID=`sudo lsof -i -P -n | grep "\b3306\b" | awk '{print $2}'`
if [[ "" !=  "$PID" ]]; then
  echo "Kill $PID"
  sudo kill -9 $PID
fi

# Launch orginal mysql
echo 'Launch original unprotected mysql ...'
PREFIX-MYSQL-ORIGIN/usr/local/mysql/bin/mysqld \
  --user=mysql \
  --basedir=PREFIX-MYSQL-ORIGIN/usr/local/mysql \
  --datadir=PREFIX-MYSQL-ORIGIN/usr/local/mysql/data \
  --plugin-dir=PREFIX-MYSQL-ORIGIN/usr/local/mysql/lib/plugin \
  --thread-cache-size=60 > /dev/null 2>&1 &
sleep 30
echo 'Launch original unprotected mysql done!'

echo 'Start testing original unprotected mysql using sysbench ...'
PREFIX-MYSQL-ORIGIN/usr/local/mysql/bin/mysql -u sbtest_user -pYOUR_PASSWORD -e'exit' > /dev/null 2>&1
sleep 10
res_file_origin=results/$res_dir/origin
test $res_file_origin
echo 'End testing original unprotected mysql using sysbench!'

echo 'Stop original unprotected mysql ...'
PID=`sudo lsof -i -P -n | grep "\b3306\b" | awk '{print $2}'`
if [[ "" !=  "$PID" ]]; then
  echo "Kill $PID"
  sudo kill -9 $PID
fi


echo 'Plot result figure ...'
python plot.py results/$res_dir
echo 'Plot result figure done!'

echo 'Please check results in ./results/'$res_dir''
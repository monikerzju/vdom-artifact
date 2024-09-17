## Overview
MySQL is a single-process multi-thread program, which spawns a new thread or reuses a thread in the thread cache to handle an incoming client connection. We modify MySQL to protect the stack of each connection handler thread. VDom puts each thread’s stack into a separate domain, ensuring that a thread’s stack cannot be accessed by other threads. We also use VDom to protect in-memory data for MySQL MEMORY storage engine that creates in-memory tables to achieve fast access and low latency. VDom ensures that the database can only be accessed by the MEMORY storage engine code. 

We use `sysbench` to to test the performance overhead of VDom protection. The version we use: MySQL 8.0, sysbench 1.0.20.

The following chapters introduce how to build and install MySQL, and how to use sysbench to test the throughput of the original and VDom protected MySQL. 

## File tree structure
We run httpd experiments on Intel X86 and ARM architectures. Since Intel and ARM memory domains vary in granularity (4KB on Intel, 2MB on ARM), MySQL code on the two architectures has some minor differences when allocating domains for stacks and in-memory data. 

We provide source code of MySQL of two versions: the unmodified (origin) version and the modified VDom protected version. Each architecture has its own source code of the modified VDom protected MySQL. 

The file tree structure is organised as follows:
- `mysql-src`
	- `origin`: the source code of unmodified MySQL. The code is applicable on both architectures.
	- `vdom`
		- `ARM`: the source code of modified VDom protected MySQL, which is applicable on ARM architectures.
		- `X86`: the source code of modified VDom protected MySQL, which is applicable on X86 architectures.
- `test`: contains scripts used to test the throughput of the original MySQL and VDom protected MySQL. Please refer to `README.md` under `test` to understand how to test and how to reproduce the results of our paper.

## Preliminary
Following packages are needed (You may also miss other packages, but that will be easy to resolve):
```
apt-get install libpcre3-dev expat libexpat1-dev apache2-utils
```

## Build and install MySQL
The installation of MySQL is the same for all 3 versions of MySQL source code (original, VDom protected X86, VDom protected ARM).

You may refer to the document for further advice: https://dev.mysql.com/doc/refman/8.0/en/installing-source-distribution.html.

### Configure the Distribution
Assume MySQL source code directory is `PREFIX-SRC`. Execute the following commands:
```
cd PREFIX-SRC
mkdir bld && cd bld
cmake .. -DWITH_BOOST=PREFIX-SRC/boost
```

### Build and install the distribution
Assume we want to install MySQL distribution into directory `PREFIX-MYSQL`:
```
# under bld/
make -j
make install DESTDIR="PREFIX-MYSQL"
```

## Initialize MySQL Data Directory
After MySQL is installed, the data directory must be initialized. 

You may refer to the document for further advice: https://dev.mysql.com/doc/refman/8.0/en/data-directory-initialization.html.

Execute the following commands:
```
# in one shell
cd PREFIX-MYSQL/usr/local/mysql
mkdir mysql-files
sudo chown mysql:mysql mysql-files
sudo chmod 750 mysql-files
bin/mysqld --initialize --user=mysql
bin/mysqld_safe --user=mysql --skip-grant-tables # launch mysqld

# open another shell
bin/mysql -u root -p # connect to mysqld
## now you should enter mysql shell
FLUSH PRIVILEGES; 
ALTER USER 'root'@'localhost' IDENTIFIED BY 'YOUR_PASSWORD'; # replace 'YOUR_PASSWORD' with your password, which is the password of user 'mysql'
create database sbtest; # create database we will use in our sysbench test
create user sbtest_user identified by 'YOUR_PASSWORD'; # replace 'YOUR_PASSWORD' with your password, which is the password of user 'sbtest_user'
grant all on sbtest.* to `sbtest_user`@`%`;
show grants for sbtest_user;
```

## Launch MySQL
- Launch MySQL server (mysqld):
```
PREFIX-MYSQL/usr/local/mysql/bin/mysqld \
  --user=mysql \
  --basedir=PREFIX-MYSQL/usr/local/mysql \
  --datadir=PREFIX-MYSQL/usr/local/mysql/data \
  --plugin-dir=PREFIX-MYSQL/usr/local/mysql/lib/plugin
```
- Connect to MySQL:
```
PREFIX-MYSQL/usr/local/mysql/bin/mysql -u root -pYOUR_PASSWORD
```
- Close MySQL
```
PREFIX-MYSQL/usr/local/mysql/bin/mysqladmin shutdown -u root -pYOUR_PASSWORD
```

## Use sysbench to test MySQL throughput
We need to install sysbench to test mysql performance:
```
sudo apt install sysbench
```
On ARM, we may need to build sysbench from source. 

Use the following commands to test MySQL heap storage engine:
```
# clean up data
sysbench \
	--db-driver=mysql \
	--mysql-user=sbtest_user \
	--mysql_password=YOUR_PASSWORD \
	--mysql-db=sbtest \
	--mysql-host=127.0.0.1 \
	--mysql-port=3306 \
	--tables=10 \
	/usr/share/sysbench/oltp_common.lua cleanup

# prepare the data
sysbench \
	--db-driver=mysql \
	--mysql-user=sbtest_user \
	--mysql_password=YOUR_PASSWORD \
	--mysql-db=sbtest \
	--mysql-host=127.0.0.1 \
	--mysql-port=3306 \
	--tables=10 \
	--table-size=10000 \
	--mysql_storage_engine=MEMORY \
	/usr/share/sysbench/oltp_common.lua prepare

# run the test with 15 threads
sysbench \
		--db-driver=mysql \
		--mysql-user=sbtest_user \
		--mysql_password=YOUR_PASSWORD \
		--mysql-db=sbtest \
		--mysql-host=127.0.0.1 \
		--mysql-port=3306 \
		--tables=10 \
		--table-size=10000 \
		--threads=15 \
		--time=10 \
		--report-interval=1 \
		--range_selects=false \
		--mysql-ignore-errors=1062 \
		/usr/share/sysbench/oltp_read_write.lua run 
```
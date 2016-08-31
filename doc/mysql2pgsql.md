# mysql2pgsql
工具 mysql2pgsql 支持不落地的把 MYSQL 中的表迁移到 Greenplum/PostgreSQL/PPAS。此工具的原理是，同时连接源端mysql数据库，和目的端Greenplum/PostgreSQL/PPAS数据库，从mysql库中通过查询得到要导出的数据，然后通过COPY命令导入到目的端。此工具支持多线程导入（每个工作线程负责导入一部分数据库表）。

# 参数配置
修改配置文件 my.cfg，配置源和目的库连接信息

	1. 源库 mysql 连接信息
		[src.mysql]
		host = "192.168.1.1"
		port = "3306"
		user = "test"
		password = "test"
		db = "test"
		encodingdir = "share"
		encoding = "utf8"

	2. 目的库 pgsql （包括 Postgresql、PPAS 和 Greenplum ）连接信息
		[desc.pgsql]
		connect_string = "host=192.168.1.1 dbname=test port=5888  user=test password=pgsql"

#注意
	1. 源库 mysql 的连接信息中，用户需要有对所有用户表的读权限
	2. 目的库 pgsql 的连接信息，用户需要对目标表有写的权限

# mysql2pgsql用法

```
./mysql2pgsql -l <tables_list_file> -d -j <number of threads>

```

其中参数的意义如下：

-l 为可选参数，指定一个文本文件，文件中含有需要同步的表；如果不指定此参数，则同步配置文件中指定的数据库下的所有表。```<tables_list_file>```为一个文件名，里面含有需要同步的表集合以及表上查询的条件，其内容格式示例如下：

```
table1 : select * from table_big where column1 < '2016-08-05'
table2 : 
table3
table4: select column1, column2 from tableX where column1 != 10
table5: select * from table_big where column1 >= '2016-08-05'
```

-d 为可选参数，表示只生成目的表的建表DDL语句，不实际进行数据同步。

-j 为可选参数，指定使用多少线程进行数据同步；如果不指定此参数则会使用5个线程并发。

#典型用法

	1 全库迁移
	
	1）通过下面的命令，获取目的端对应的表的DDL
	
	./mysql2pgsql -d
	
	然后根据这些DDL，再加入distribution key等信息，在目的端创建表。
	
	2）执行下面的命令，同步所有表：
	
	./mysql2pgsql
	
	此命令会把配置文件中所指定的数据库中的所有mysql表数据迁移到目的端。过程中使用5个线程（即缺省线程数为5），读取和导入所有涉及的表数据。
	
	2. 部分表迁移
	
	1）编辑一个新文件tab_list.txt，放入如下内容：
	```
	t1
	t2 : select * from t2 where c1 > 138888
	```
    2) 执行下面的命令，同步指定的t1和t2表（注意t2表只迁移符合c1 > 138888条件的数据）：
    
    ./mysql2pgsql -l tab_list.txt
    
    

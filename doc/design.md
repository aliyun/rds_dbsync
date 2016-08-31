# PostgreSQL 增量同步方案详细设计

## 一:方案目的

	通过PG实例的增量数据同步方案,解决PG在数据迁移中需要进行全量同步,需要长时间停服务的问题.
	通过该方案,打通PG和其他数据产品间的数据通道,做到PG和其他数据产品和异构数据库间的实时的同步.

## 二:增量同步方案的技术背景

	该方案基于PostgreSQL 9.4版本的逻辑流复制技术.
	PG9.4 可以做到,利用逻辑流复制,把表的增量数据,以自定义格式的形式组织起来被客户端订阅.
	自定义格式的增量数据是逻辑流复制的关键,通过PG内核给出了开放的接口(5个回调函数),可以把增量数据以任意的形式输出.
	PG的客户端根据需要按照服务端规定的格式解析,就能得到完整的增量数据.
	
## 三:订阅增量数据的订阅规则

	PG增量同步方案只能获取对应DB中表的变化信息.
	一张表能够被逻辑流复制订阅,需要满足下列三个条件之一
	1)这张表的流复制方式为全复制,即 REPLICA = FULL
		可以通过DDL语句 alter table t REPLICA FULL 定制对应的表
		这个选项使得对应表中每一行的变化数据被完整的记录到WAL中.所以带来了较多的IO负担.
	2)这张表具有主键约束
		有主键的表的变化信息会被记录到WAL中,相对于REPLICA FULL模式,old row没有变化的列将不会被记录到WAL中,对IO的影响相对FULL更小.
	3)为该表指定一个非空的唯一索引,作为REPLICA INDEX.
		可以通过DDL来定制相应的表  
		CREATE UNIQUE INDEX idx ON d(a); 
		alter table t ALTER COLUMN a set not null;
		alter table t REPLICA IDENTITY USING INDEX idx;
		该模式记录到WAL中的数据和主键模式相当.
	可以在pg_class的 relreplident 列中看到对应的表处于什么模式.
	PG的流复制的规则类似于MYSQL binlog模式的row模式,对于用户相对于更加灵活一些.
	可以阅读相关文档,了解细节信息.
	1 http://www.postgresql.org/docs/9.4/static/sql-altertable.html#SQL-CREATETABLE-REPLICA-IDENTITY
	2 http://www.postgresql.org/docs/9.4/static/logicaldecoding.html
	
## 四:架构解析

	PG的增量方案架构上分为两个大的部分
	1 服务器端
	在被订阅的服务器端,嵌入一个用于 decode 的插件--ali_decoding
	该插件实现了流复制的自定义数据流格式,用户使用pg_create_logical_replication_slot函数创建 logical replication 时指定插件 ali_decoding 和逻辑 slot 名.
	在客户端使用 START_REPLICATION 命令开启对应的流复制传输后,服务器端会有一个backend 进程加载该插件(ali_decoding)开始向用户传输增量数据.
	
	2 客户端
	START_REPLICATION 命令需要指定对应的逻辑 slot 名和位点.
	所谓的位点是PG WAL日志的日志名和日志文件的偏移的组合.
	PG的位点概念对于物理流复制和逻辑流复制是统一的.
	1) PG对应逻辑 slot 的位点信息保存在数据库端,具有断点续传特性.
	2) 除非需要跳过某部分数据,则使用 0/0 去请求拉取对应 slot 的逻辑日志.
	3) PG的位点不对应时间点.
	
	对于客户端,在建立连接后,读到的增量信息以消息的形式出现,可以分为三类信息
	1) 事务开始,begin
	2) 事务结束,commit
	3) 事务中单个表的变化信息,分为
		a) insert
		b) update
		c) delete
		根据表 REPLICA 的状态不同,各类DML收到的信息略有变化.
		
## 五:编译和使用

### 1 编译

	1 编译机上下载安装 PG94 或更高版本的二进制,或用源码安装.
	2 使用软链接或别的方式,把对应版本的 pg_config 链接到公共目录
	例: ln -s /u01/pgsql_20150924/bin/pg_config /usr/bin/ 或 
	export PATH=/u01/pgsql_20150924/bin
	3 下载服务器端和客户端代码,make;make install;
	
### 2 使用

	1 使用SQL或 demo 中的API创建 logical slot
	例: SELECT * FROM pg_create_logical_replication_slot('regression_slot', 'ali_decoding');
	
	2 可以用相应的 SQL 语句查看创建好的 slot
	例: SELECT * FROM pg_replication_slots;
	
	3 在客户端目录下,编辑 demo.cpp 填入需要拉取增量的服务器的连接参数,并重新编译成新的demo.
	执行demo.
	对应的增量信息会输出到客户端.
	可以参考 out_put_decode_message 解析和读取增量消息中的数据.

## 六:限制
    1 以 ctid 为条件的更新语句,在表处于 REPLICA FULL 时,update 语句无法完整还原.
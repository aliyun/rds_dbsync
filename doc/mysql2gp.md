# mysql2gp 使用和部署说明

## 一 mysql2gp 介绍
mysql2gp 实现了从 MySQL 中迁移增量数据到 PostgreSQL 或 Greenplum

其中增量数据来自于 MySQL 的 binlog, 结合全量数据迁移工具 mysql2pgsql 可以把 MySQL 中的数据完整的迁移到  PostgreSQL Greenplum 中，且保持准实时同步。

### 1.1 支持特性和限制
1 支持拉取 MySQL 5.1 5.5 5.6 5.7 版本的 binlog，需要 binlog 相关参数

	binlog_format = ROW
	binlog_row_image = FULL

2 支持同步指定表的各类数据变化到目标DB中，包括对应行的 insert update delete。

3 数据同步的表需要有单列主键。

4 支持对主键进行修改。 

5 暂时不支持异构数据库的 DDL 同步。

6 支持指定表镜像方式同步到 PostgreSQL 或 Greenplum（配置文件方式）。

7 支持指定模式的表同步。

### 1.2 mysql2gp 实现架构
简单的说，mysql2gp 的实现方式是：

1 在客户端主机（也可以部署在其他主机）上启动一个临时 PG 数据库，用于临时存放从 MySQL 拉去到的 binlog 数据。

2 binlog_miner 从源 MySQL 的一个 binlog 文件开始，拉取和解析 binlog 并存放到临时 PG 中。

3 binlog_loader 从临时 PG 中读取增量数据，并做适当的处理，最终批量写入到目标 PostgreSQL 或 Greenplum 中去。

### 1.3 mysql2gp 模块介绍

mysql2gp 分为5个部分
1 binlog_miner 用于拉取目标库中的 binlog, 并保存到临时 DB 中。

2 binlog_loader 用于读取临时 DB 中的 binlog 数据并加载到目标库中。

3 my.cfg 配置文件，设置需要同步数据的源和目标数据库的链接信息和相关参数。

4 loader_table_list.txt 配置文件，设置需要同步的表名列表，用回车符隔开。

5 临时 DB，用户保存增量数据的临时数据库。建议和 binlog_miner binlog_loader 部署在同一个主机。

## 二 mysql2gp 部署
建议临时 DB 和客户端二进制部署在同主机

部署步骤：

### 2.1 部署临时 PG DB
在目标主机部署一个临时 PG DB 用户存放临时数据，主机需要为临时数据预留足够的保存增量数据的空间。部署完成后获得一个连接临时 PG DB 的连接串，如 “dbname=test port=5432 user=test password=pgsql”

### 2.2 配置文件
#### 2.2.1 MySQL 相关

my.cnf

```
[src.mysql]
host = "192.168.1.1"
port = "3301"
user = "test"
password = "123456"
db = "test"
encodingdir = "share"
encoding = "utf8"
binlogfile = "mysql-bin.000001"
```

注意：

1 MySQL 的连接信息需要有 select 权限和拉取 binlog 的权限。

2 binlogfile 为读取 binlog 的启始文件，必须设置。该配置和全量数据同步工具配合使用。
通常在开始同步全量 MySQL 数据时记录当前正在写的 binlog 文件名，并配置到 my.cnf 中。

#### 2.2.2 临时数据库

my.cnf

```
[local.pgsql]
connect_string = "dbname=test port=5432 user=test password=pgsql"

```

注意：

1 连接本地数据库可以不指定 host 信息，这样的链接模式效率较高。


#### 2.2.3 目的数据库

my.cnf
 
```
[desc.pgsql]
connect_string = "host=192.167.1.2 dbname=postgres port=5432 user=test password=pgsql"
target_schema = "test"

```

注意: 

1 target_schema 用于指定目标表存在的 schema，也可以不指定，不指时默认 schema 为 public。


#### 2.2.4 设置需要同步的表

1 my.cnf

```
[binlogloader]
loader_table_list = "loader_table_list.txt"

```

2 loader_table_list.txt

```
a
b
```

### 2.3 启动同步进程

#### 2.3.1 启动 binlog 拉取进程

推荐命令行：

	nohup ./binlog_miner 1>minner.log 2>&1 &

#### 2.3.2 启动 binlog 写入进程

推荐命令行：

	nohup ./binlog_loader 1>loader.log 2>&1 &

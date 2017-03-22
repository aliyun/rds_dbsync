# dbsync 项目

dbsync 项目目标是围绕 PostgreSQL Greenplum ,实现易用的数据的互迁功能。

## 支持的功能

1. PostgreSQL -> PostgreSQL pgsql2pgsql

	功能 pg->pg 全量+增量数据同步

	状态：已开源 [文档](https://github.com/aliyun/rds_dbsync/blob/master/doc/mysql2pgsql.md)

2. MySQL -> PostgreSQL/Greenplum（binlog_minner binlog_loader）

	功能：基于 MySQL binlog 解析的增量数据同步

	状态：已开放二进制 [文档](https://github.com/aliyun/rds_dbsync/blob/master/doc/mysql2gp.md)

3. PostgreSQL -> PostgreSQL/Greenplum pgsql2gp

	功能：基于 PostgreSQL 逻辑日志的增量数据同步

	状态：未开发完成

4. MySQL -> PostgreSQL/Greenplum mysql2pgsql

	功能：以表为单位的多线程全量数据迁移

	状态：已开源 [文档](https://github.com/aliyun/rds_dbsync/blob/master/doc/mysql2pgsql.md)


## 项目成员
该项目由阿里云 PostgreSQL 小组开发，为 PostgreSQL 世界贡献一份力量

1. PM & 架构设计 曾文旌（义从）
2. PD 萧少聪（铁庵）
3. TESTER & 技术支持 周正中(德歌)
4. DEV 张广舟（明虚）曾文旌（义从）

## 使用方法
1. 修改配置文件 my.cfg 中相关的项，例如需求 MySQL -> PostgreSQL 全量迁移，不需要增量，则只需要配置 src.mysql 和 desc.pgsql ，其他的项不用管。
2. 执行对应二进制，在二进制所在目录执行 ./mysql2pgsql 

## 编译步骤
1. 下载代码

  git clone git@github.com:aliyun/rds_dbsync.git

2. 下载安装mysql的开发包

  下载repo的rpm：wget  http://dev.mysql.com/get/mysql57-community-release-el6-9.noarch.rpm

  安装repo：rpm -Uvh mysql57-community-release-el6-9.noarch.rpm

  编辑 /etc/yum.repos.d/mysql-community.repo，把除mysql 57外的其他repo的enable设为0

  查看可安装的mysql报：yum list mysql-community-*

  安装mysql的开发包： yum install mysql-community-devel.x86_64

3. 下载安装pg的安装包

  下载repo的rpm：wget https://download.postgresql.org/pub/repos/yum/9.6/redhat/rhel-6-x86_64/pgdg-centos96-9.6-3.noarch.rpm

  安装repo：rpm -ivh pgdg-centos96-9.6-3.noarch.rpm

  编辑/etc/yum.repos.d/pgdg-96-centos.repo，可能需要把https改成http

  查看可安装的pg包：yum list postgresql96*

  安装pg的server和开发包：yum install postgresql96-devel.x86_64 postgresql96-server.x86_64

4. 执行make

5. 打包二进制 make package 将生成一个install目录，里面有二进制和lib

6. 执行dbsync：cd install; bin/mysql2pgsql ; bin/pgsql2pgsql ; bin/demo

## 问题反馈
有任何问题，请反馈到 https://github.com/aliyun/rds_dbsync issues 或联系 158306855@qq.com

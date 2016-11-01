# dbsync 项目

dbsync 项目目标是围绕 PostgreSQL Greenplum ,实现易用的数据的互迁功能。

## 支持的功能

1. PostgreSQL -> PostgreSQL 全量加增量迁移(秒级同步) pgsql2pgsql
2. PostgreSQL -> Greenplum 全量加增量迁移（批量，分钟级同步）pgsql2gp
3. MySQL -> PostgreSQL 全量加增量迁移(秒级同步) mysql2pgsql
4. MySQL -> Greenplum 全量加增量迁移（批量，分钟级同步）mysql2gp

## 项目成员
该项目由阿里云 PostgreSQL 小组开发，为 PostgreSQL 世界贡献一份力量

1. PM & 架构设计 曾文旌（义从）
2. PD 萧少聪（铁庵）
3. TESTER & 技术支持 周正中(德歌)
4. DEV 张广舟（明虚）曾文旌（义从）

## 使用方法
1. 修改配置文件 my.cfg 中相关的项，例如需求 MySQL -> PostgreSQL 全量迁移，不需要增量，则只需要配置 src.mysql 和 desc.pgsql ，其他的项不用管。
2. 执行对应二进制，在二进制所在目录执行 ./mysql2pgsql 

## 问题反馈
有任何问题，请反馈到 https://github.com/aliyun/rds_dbsync issues 或联系 wenjing.zwj@alibaba-inc.com

# pgsql2pgsql Import data from PostgreSQL

The pgsql2pgsql tool supports migrating tables in HybridDB for PostgreSQL, Greenplum Database, PostgreSQL, or PPAS to HybridDB for PostgreSQL, Greenplum Database, PostgreSQL, or PPAS without storing the data separately.

# Features

pgsql2pgsql supports the following features:

* 1 Full-database migration from PostgreSQL, PPAS, Greenplum Database, or HybridDB for PostgreSQL to PostgreSQL, PPAS, Greenplum Database, or HybridDB for PostgreSQL.

* 2 Full-database migration and incremental data migration from PostgreSQL or PPAS (9.4 or later versions) to PostgreSQL, or PPAS.

# Parameters configuration
Modify the “my.cfg” configuration file, and configure the source and target database connection information.

* The connection information of the source PostgreSQL database is shown as follows:

	**Note:** The user is preferably the corresponding database owner in the source PostgreSQL database connection information.

```
[src.pgsql]
connect_string = "host=192.168.1.1 dbname=test port=5888  user=test password=pgsql"
```

* The connection information of the local temporary PostgreSQL database is shown as follows:

```
[local.pgsql]
connect_string = "host=192.168.1.1 dbname=test port=5888  user=test2 password=pgsql"
```

* The connection information of the target PostgreSQL database is shown as follows:

	**Note:** You need to have the write permission on the target table of the target PostgreSQL database.


```
[desc.pgsql]
connect_string = "host=192.168.1.1 dbname=test port=5888  user=test3 password=pgsql"
```

#Note:

* If you want to perform incremental data synchronization, the connected source database must have the permission to create replication slots.

* PostgreSQL 9.4 and later versions support logic flow replication, so it supports the incremental migration if PostgreSQL serves as the data source. The kernel only supports logic flow replication after you enable the following kernel parameters.


```
wal_level = logical
max_wal_senders = 6
max_replication_slots = 6
```


# Use pgsql2pgsql

## Full-database migration
	
Run the following command to perform a full-database migration:

```
./pgsql2pgsql
```


By default, the migration program migrates the table data of all the users in the corresponding PostgreSQL database to PostgreSQL.

## Status information query

Connect to the local temporary database, and you can view the status information in a single migration process. The information is stored in the db_sync_status table, including the start and end time of the full-database migration, the start time of the incremental data migration, and the data situation of incremental synchronization.


# Download and instructions

## binary download link

[Download the binary installer of pgsql2pgsql](https://github.com/aliyun/rds_dbsync/releases)

## rds_dbsync project

[View the mysql2pgsql source code compilation instructions](https://github.com/aliyun/rds_dbsync/blob/master/README.md)



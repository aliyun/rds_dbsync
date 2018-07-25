## mysql2pgsql Import data from MySQL

The mysql2pgsql tool supports migrating tables in MySQL to HybridDB for PostgreSQL, Greenplum Database, PostgreSQL, or PPAS without storing the data separately. This tool connects to the source MySQL database and the target database at the same time, querries and retrieves the data to be exported in the MySQL database, and then imports the data to the target database by using the COPY command. It supports multithread import (every worker thread is in charge of importing a part of database tables).



## Parameters configuration

Modify the “my.cfg” configuration file, and configure the source and target database connection information.

- The connection information of the source MySQL database is as follows:

	**Note:** You need to have the read permission on all user tables in the source MySQL database connection information.

```
[src.mysql]
host = "192.168.1.1"
port = "3306"
user = "test"
password = "test"
db = "test"
encodingdir = "share"
encoding = "utf8"
```

- The connection information of the target PostgreSQL database (including PostgreSQL, PPAS and HybridDB for PostgreSQL) is as follows:

	**Note:** You need to have the write permission on the target table in the target PostgreSQL database.


```
[desc.pgsql]
connect_string = "host=192.168.1.1 dbname=test port=5888  user=test password=pgsql"
```

## mysql2pgsql Usage discription

The usage of mysql2pgsql is described as follows:


```
./mysql2pgsql -l <tables_list_file> -d -n -j <number of threads> -s <schema of target able> 

```

Parameter descriptions:

- -l: Optional parameter, used to specify a text file that contains tables to be synchronized. If this parameter is not specified, all the tables in the database specified in the configuration file are synchronized. <tables_list_file>is a file name. The file contains tables set to be synchronized and query conditions on the tables. An example of the content format is shown as follows:


```
table1 : select * from table_big where column1 < '2016-08-05'
table2 : 
table3
table4 : select column1, column2 from tableX where column1 != 10
table5 : select * from table_big where column1 >= '2016-08-05'
```

- -d: Optional parameter, indicating to only generate the tabulation DDL statement of the target table without performing actual data synchronization.

- -j: Optional parameter, specifying the number of threads used for data synchronization. If this parameter is not specified, five threads are used concurrently.


### Typical usage

#### Full-database migration

The procedure is as follows:

1\. Run the following command to get the DDL statements of the corresponding table on the target end:


```
./mysql2pgsql -d
```

2\. Create a table on the target based on these DDL statements with the distribution key information added.

3\. Run the following command to synchronize all tables:


```
./mysql2pgsql
```

This command migrates the data from all MySQL tables in the database specified in the configuration file to the target. Five threads are used during the process (the default thread number is five) to read and import the data from all tables involved.

#### Partial table migration

The procedure is as follows:

1\. Create a new file (tab_list.txt) and insert the following content:


```
t1 
t2 : select * from t2 where c1 > 138888
```

2\. Run the following command to synchronize the specified t1 and t2 tables:


```
./mysql2pgsql -l tab_list.txt
```

**Note:** For the t2 table, only the data that meets the c1 > 138888 condition is migrated.

## Download and instructions

[Download the binary installer of mysql2pgsql](https://github.com/aliyun/rds_dbsync/releases)

## rds_dbsync project

[View the mysql2pgsql source code compilation instructions](https://github.com/aliyun/rds_dbsync/blob/master/README.md)

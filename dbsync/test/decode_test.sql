
create schema test_case;
set search_path=test_case;

-- no index
create table a(a int ,b text, c timestamptz);

insert into a values(1,'test','1999-01-08 04:05:06');
insert into a values(2,'test','1999-01-08 04:05:06');
insert into a values(3,'test','1999-01-08 04:05:06');

update a set b = 'test1';
update a set b = 'test2' where a = 3;

delete from a where a = 2;
delete from a;

-- primary key
create table b(a int primary key ,b text, c timestamptz);

insert into b values(1,'test','1999-01-08 04:05:06');
insert into b values(2,'test','1999-01-08 04:05:06');
insert into b values(3,'test','1999-01-08 04:05:06');

update b set b = 'test1';
update b set a = 5 where a = 1;
update b set b = 'test2' where a = 3;
update b set c = '1999-01-08 04:05:06' where a = 5;
update b set a = 6, c = '1999-01-08 04:05:06' where a = 5;
update b set a = 5, b = 't',c = '1999-01-08 04:05:06' where a = 6;
 
delete from b where a = 2;
delete from b;

-- mprimary key
create table c(a int ,b text, c timestamptz, d bigint, primary key(a,d));

insert into c values(1,'test','1999-01-08 04:05:06',3);
insert into c values(2,'test','1999-01-08 04:05:06',2);
insert into c values(3,'test','1999-01-08 04:05:06',1);

update c set b = 'test1';
update c set a = 5 where a = 1;
update c set b = null where a = 3;
delete from c where a = 2;

-- REPLICA index
create table d(a int ,b text, c timestamptz, d bigint);
CREATE  UNIQUE  INDEX idx_d_a_d ON d(a,d); 
alter table d   ALTER COLUMN a  set not null;
alter table d   ALTER COLUMN d  set not null;
alter table d REPLICA IDENTITY  USING INDEX idx_d_a_d;
 
insert into d values(1,'test','1999-01-08 04:05:06',3);
insert into d values(2,'test','1999-01-08 04:05:06',2);
insert into d values(3,'test','1999-01-08 04:05:06',1);

update d set b = 'test1';
update d set a = 5 where a = 1;
update d set b = 'test2' where a = 3;
update d set a = 5, b = 't',c = '1999-01-08 04:05:06' where a = 3;
delete from d;

-- full data
create table e(a int ,b text, c timestamptz, d bigint);
alter table e REPLICA IDENTITY FULL;
 
insert into e values(1,'test','1999-01-08 04:05:06',3);
insert into e values(2,'test','1999-01-08 04:05:06',2);
insert into e values(3,'test','1999-01-08 04:05:06',1);

update e set b = 'test1';
update e set a = 5 where a = 1;
update e set b = 'test2' where a = 3;
update e set a = 5, b = 't',c = '1999-01-08 04:05:06' where a = 3;

delete from e;


-- full data and primary key
create table f(a int primary key,b text, c timestamptz, d bigint);
alter table f REPLICA IDENTITY FULL;
 
insert into f values(1,'test','1999-01-08 04:05:06',3);
insert into f values(2,'test','1999-01-08 04:05:06',2);
insert into f values(3,'test','1999-01-08 04:05:06',1);

update f set b = 'test1';
update f set a = 5 where a = 1;

alter table f REPLICA IDENTITY DEFAULT;
update f set a = 7 where a = 2;

update f set b = 'test2' where a = 3;
update f set a = 6, b = 't',c = '1999-01-08 04:05:06' where a = 3;

delete from f;

-- data type
create table test_data_type_1(a smallint,b integer,c bigint,d decimal,e numeric);
insert into test_data_type_1 values(-32768, 2147483647, 9223372036854775807, 111.111, 111.111);

create table test_data_type_2(a real,b double precision,c smallserial,d serial,e bigserial);
insert into test_data_type_2 values(111.111, 111.111, 32767, 2147483647, 9223372036854775807);

create table test_data_type_3(a money,b character varying(20),c character(20),d text,e char(20));
insert into test_data_type_3 values('12.34', '12.34', '12.34', '12.34', '12.34');

create table test_data_type_4(a bytea,b bytea,c bytea,d bytea,e bytea);
insert into test_data_type_4 values('\\xDEADBEEF', '\\000', '0', '\\134', '\\176');

create table test_data_type_5(a timestamp without time zone ,b timestamp with time zone,c timestamp,d time,e time with time zone);
insert into test_data_type_5 values('1999-01-08 04:05:06', '1999-01-08 04:05:06 +8:00', '1999-01-08 04:05:06 -8:00', '1999-01-08 04:05:06 -8:00', '1999-01-08 04:05:06 -8:00');

CREATE TYPE mood AS ENUM ('sad', 'ok', 'happy');
create table test_data_type_6(a boolean,b mood,c point,d line,e lseg);
insert into test_data_type_6 values(TRUE, 'happy', '(1,1)', '{1,2,1}', '[(1,2),(2,1)]');

create table test_data_type_7(a path,b path,c polygon,d circle,e circle);
insert into test_data_type_7 values('[(1,3),(2,2)]', '((1,3),(2,2))', '((1,3),(2,2))', '<(2,2),2>', '((2,3),1)');

create table test_data_type_8(a cidr,b cidr,c inet,d macaddr,e macaddr);
insert into test_data_type_8 values('192.168.100.128/25', '2001:4f8:3:ba:2e0:81ff:fe22:d1f1/128', '1.2.3.4', '08-00-2b-01-02-03', '08:00:2b:01:02:03');

CREATE TABLE test_9 (a BIT(3), b BIT VARYING(5));
INSERT INTO test_9 VALUES (B'101', B'00');
INSERT INTO test_9 VALUES (B'10'::bit(3), B'101');

CREATE TABLE test_10 (a tsvector, b tsvector,c tsquery, d tsquery);
INSERT INTO test_10 VALUES ('a fat cat sat on a mat and ate a fat rat', 'a:1 fat:2 cat:3 sat:4 on:5 a:6 mat:7 and:8 ate:9 a:10 fat:11 rat:12','fat & rat','Fat:ab & Cats');

create extension "uuid-ossp";
CREATE TABLE test_11 (a uuid, b uuid,c uuid, d uuid);
INSERT INTO test_11 VALUES ('25285134-7314-11e5-8e45-d89d672b3560', 'c12a3d5f-53bb-4223-9fca-0af78b4d269f', 'cf16fe52-3365-3a1f-8572-288d8d2aaa46', '252852d8-7314-11e5-8e45-2f1f0837ccca');

CREATE TABLE test_12 (a xml, b xml,c xml);
INSERT INTO test_12 VALUES (xml '<foo>bar</foo>', XMLPARSE (DOCUMENT '<?xml version="1.0"?><book><title>Manual</title><chapter>...</chapter></book>'), XMLPARSE (CONTENT 'abc<foo>bar</foo><bar>foo</bar>'));

CREATE TABLE test_13 (a xml, b xml,c xml);
INSERT INTO test_13 VALUES ('{"reading": 1.230e-5}',
	'[1, 2, "foo", null]',
	'{"bar": "baz", "balance": 7.77, "active":false}');

CREATE TABLE sal_emp_14 (
    name            text,
    pay_by_quarter  integer[],
    schedule        text[][]
);

INSERT INTO sal_emp_14
    VALUES ('Bill',
    '{10000, 10000, 10000, 10000}',
    '{{"meeting", "lunch"}, {"training", "presentation"}}');

INSERT INTO sal_emp_14
    VALUES ('Carol',
    '{20000, 25000, 25000, 25000}',
    '{{"breakfast", "consulting"}, {"meeting", "lunch"}}');

INSERT INTO sal_emp_14
    VALUES ('Carol',
    ARRAY[20000, 25000, 25000, 25000],
    ARRAY[['breakfast', 'consulting'], ['meeting', 'lunch']]);

CREATE TYPE complex AS (
    r       double precision,
    i       double precision
);

CREATE TYPE inventory_item AS (
    name            text,
    supplier_id     integer,
    price           numeric
);

CREATE TABLE on_hand_15 (
    item      inventory_item,
    count     integer
);

INSERT INTO on_hand_15 VALUES (ROW('fuzzy dice', 42, 1.99), 1000);

CREATE TABLE reservation_16 (room int, during tsrange);
INSERT INTO reservation_16 VALUES
    (1108, '[2010-01-01 14:30, 2010-01-01 15:30)');

CREATE TYPE floatrange AS RANGE (
    subtype = float8,
    subtype_diff = float8mi
);

create table t_range_16(a floatrange);
insert into t_range_16 values('[1.234, 5.678]');

create extension hstore;
create table hstore_test_17(item_id serial, data hstore);
INSERT INTO hstore_test_17 (data) VALUES ('"key1"=>"value1", "key2"=>"value2", "key3"=>"value3"');
UPDATE hstore_test_17 SET data = delete(data, 'key2');
UPDATE hstore_test_17 SET data = data || '"key4"=>"some value"'::hstore;

CREATE EXTENSION postgis;
CREATE EXTENSION postgis_topology;
CREATE EXTENSION fuzzystrmatch;
CREATE EXTENSION postgis_tiger_geocoder;

create table test_18 (myID int4, pt geometry, myName varchar );
insert into test_18 values (1, 'POINT(0 0)', 'beijing' );
insert into test_18 values (2, 'MULTIPOINT(1 1, 3 4, -1 3)', 'shanghai' );
insert into test_18 values (3, 'LINESTRING(1 1, 2 2, 3 4)', 'tianjin' );
insert into test_18 values (3, 'POLYGON((0 0, 0 1, 1 1, 1 0, 0 0))', 'tianjin' );
insert into test_18 values (3, 'MULTIPOLYGON(((0 0,4 0,4 4,0 4,0 0),(1 1,2 1,2 2,1 2,1 1)), ((-1 -1,-1 -2,-2 -2,-2 -1,-1 -1)))', 'tianjin' );
insert into test_18 values (3, 'MULTILINESTRING((1 1, 2 2, 3 4),(2 2, 3 3, 4 5))', 'tianjin' );

insert into test_18 values (3, '01060000000200000001030000000200000005000000000000000000000000000000000000000000000000001040000000000000000000000000000010400000000000001040000000000000000000000000000010400000000000000000000000000000000005000000000000000000F03F000000000000F03F0000000000000040000000000000F03F00000000000000400000000000000040000000000000F03F0000000000000040000000000000F03F000000000000F03F01030000000100000005000000000000000000F0BF000000000000F0BF000000000000F0BF00000000000000C000000000000000C000000000000000C000000000000000C0000000000000F0BF000000000000F0BF000000000000F0BF', 'tianjin' );

-- m sql in a tran
create table msql(a int primary key,b text, c timestamptz);

begin;
insert into msql values(1,'test','1999-01-08 04:05:06');
insert into msql values(2,'test','1999-01-08 04:05:06');
insert into msql values(3,'test','1999-01-08 04:05:06');
update msql set b = 'test' where a = 1;
delete from msql where a = 3;
commit;

-- alter table 
create table msql_1(a int primary key,b text, c timestamptz);

insert into msql_1 values(1,'test','1999-01-08 04:05:06');
alter table msql_1 add COLUMN d int;
insert into msql_1 values(2,'test','1999-01-08 04:05:06',1);
alter table msql_1 drop COLUMN b;
insert into msql_1 values(3,'1999-01-08 04:05:06',2);

update msql_1 set c = '1999-01-08 04:05:07';
delete from msql_1;

-- alter table in a tran
create table msql_2(a int primary key,b text, c timestamptz);
begin;
insert into msql_2 values(1,'test','1999-01-08 04:05:06');
alter table msql_2 add COLUMN d int;
insert into msql_2 values(2,'test','1999-01-08 04:05:06',1);
alter table msql_2 drop COLUMN b;
insert into msql_2 values(3,'1999-01-08 04:05:06',2);
update msql_2 set c = '1999-01-08 04:05:07';
commit;


-- alter table drop pk
create table msql_3(a int primary key,b text, c timestamptz);
begin;
insert into msql_3 values(1,'test','1999-01-08 04:05:06');
insert into msql_3 values(5,'test','1999-01-08 04:05:06');
alter table msql_3 add COLUMN d int;
insert into msql_3 values(2,'test','1999-01-08 04:05:06',1);
alter table msql_3 drop COLUMN a;
insert into msql_3 values('test','1999-01-08 04:05:06',2);
delete from  msql_3;
commit;

-- SERIAL 
CREATE TABLE seq_test  
(  
    id SERIAL primary key ,  
    name text
) ; 

insert into seq_test (name) values('test');

-- toast
-- create table t_kenyon(id int,vname varchar(48),remark text);
-- select oid,relname,reltoastrelid from pg_class where relname = 't_kenyon';
-- insert into t_kenyon select generate_series(1,2000),repeat('kenyon here'||'^_^',2),repeat('^_^ Kenyon is not God',500);
-- insert into t_kenyon select generate_series(1,2),repeat('kenyon here'||'^_^',2),repeat('^_^ Kenyon is not God,Remark here!!',2000);
-- insert into t_kenyon select generate_series(3,4),repeat('kenyon here'||'^_^',2),repeat('^_^ Kenyon is not God,Remark here!!',4000);
-- insert into t_kenyon select generate_series(5,6),repeat('kenyon here'||'^_^',2),repeat('^_^ Kenyon is not God,Remark here!!',5500);
-- insert into t_kenyon select generate_series(1,2),repeat('kenyon here'||'^_^',2),repeat('^_^ Kenyon is not God,Remark here!!',10000);
-- insert into t_kenyon select generate_series(7,8),repeat('kenyon here'||'^_^',2),repeat('^_^ Kenyon is not God,Remark here!!',20000);

-- utf8
create table chinese_text(t text);
insert into chinese_text values('微软 Surface Pro 4 中国开放预售 价格公布');
insert into chinese_text values('\');
insert into chinese_text values('\\');
insert into chinese_text values('\\\');
insert into chinese_text values('///');
insert into chinese_text values('//');
insert into chinese_text values('/');
insert into chinese_text values('''');
insert into chinese_text values('"''"');

-- bug extra_float_digits default 3
create table tf(c1 float4, c2 float8 ,c3 numeric);
insert into tf values (1.5555555555555555555555,1.5555555555555555555555,1.5555555555555555555555);

drop schema test_case cascade;

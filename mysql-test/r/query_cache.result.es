set GLOBAL query_cache_size=1355776;
flush query cache;
flush query cache;
reset query cache;
flush status;
drop table if exists t1,t2,t3,t4,t11,t21;
drop database if exists mysqltest;
create table t1 (a int not null);
insert into t1 values (1),(2),(3);
select * from t1;
a
1
2
3
select * from t1;
a
1
2
3
select sql_no_cache * from t1;
a
1
2
3
select length(now()) from t1;
length(now())
19
19
19
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
show status like "Qcache_inserts";
Variable_name	Value
Qcache_inserts	1
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	1
drop table t1;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
create table t1 (a int not null);
insert into t1 values (1),(2),(3);
create table t2 (a int not null);
insert into t2 values (4),(5),(6);
create table t3 (a int not null) engine=MERGE UNION=(t1,t2) INSERT_METHOD=FIRST;
select * from t3;
a
1
2
3
4
5
6
select * from t3;
a
1
2
3
4
5
6
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	2
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
insert into t2  values (7);
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
select * from t1;
a
1
2
3
select * from t1;
a
1
2
3
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	3
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
insert into t3 values (8);
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
select * from t3;
a
1
2
3
8
4
5
6
7
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
update t2 set a=9 where a=7;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
select * from t1;
a
1
2
3
8
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
update t3 set a=10 where a=1;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
select * from t3;
a
10
2
3
8
4
5
6
9
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
delete from t2 where a=9;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
select * from t1;
a
10
2
3
8
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
delete from t3 where a=10;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
drop table t1, t2, t3;
create table t1 (a int not null);
insert into t1 values (1),(2),(3);
create table t2 (a int not null);
insert into t2 values (1),(2),(3);
select * from t1;
a
1
2
3
select * from t2;
a
1
2
3
insert into t1 values (4);
show status like "Qcache_free_blocks";
Variable_name	Value
Qcache_free_blocks	2
flush query cache;
show status like "Qcache_free_blocks";
Variable_name	Value
Qcache_free_blocks	1
drop table t1, t2;
create table t1 (a text not null);
create table t11 (a text not null);
create table t2 (a text not null);
create table t21 (a text not null);
create table t3 (a text not null);
insert into t1 values("1111111111111111111111111111111111111111111111111111");
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t11 select * from t1;
insert into t21 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t3 select * from t1;
insert into t3 select * from t2;
insert into t3 select * from t1;
select * from t11;
select * from t21;
show status like "Qcache_total_blocks";
Variable_name	Value
Qcache_total_blocks	7
show status like "Qcache_free_blocks";
Variable_name	Value
Qcache_free_blocks	1
insert into t11 values("");
select * from t3;
show status like "Qcache_total_blocks";
Variable_name	Value
Qcache_total_blocks	8
show status like "Qcache_free_blocks";
Variable_name	Value
Qcache_free_blocks	2
flush query cache;
show status like "Qcache_total_blocks";
Variable_name	Value
Qcache_total_blocks	7
show status like "Qcache_free_blocks";
Variable_name	Value
Qcache_free_blocks	1
drop table t1, t2, t3, t11, t21;
set query_cache_type=demand;
create table t1 (a int not null);
insert into t1 values (1),(2),(3);
select * from t1;
a
1
2
3
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
select sql_cache * from t1 union select * from t1;
a
1
2
3
set query_cache_type=2;
select sql_cache * from t1 union select * from t1;
a
1
2
3
select * from t1 union select sql_cache * from t1;
a
1
2
3
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	4
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
set query_cache_type=on;
reset query cache;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
select sql_no_cache * from t1;
a
1
2
3
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
drop table t1;
create table t1 (a text not null);
select CONNECTION_ID() from t1;
CONNECTION_ID()
select FOUND_ROWS();
FOUND_ROWS()
0
select NOW() from t1;
NOW()
select CURDATE() from t1;
CURDATE()
select CURTIME() from t1;
CURTIME()
select DATABASE() from t1;
DATABASE()
select ENCRYPT("test") from t1;
ENCRYPT("test")
select LAST_INSERT_ID() from t1;
LAST_INSERT_ID()
select RAND() from t1;
RAND()
select UNIX_TIMESTAMP() from t1;
UNIX_TIMESTAMP()
select USER() from t1;
USER()
select benchmark(1,1) from t1;
benchmark(1,1)
explain extended select benchmark(1,1) from t1;
id	select_type	table	type	possible_keys	key	key_len	ref	rows	Extra
1	SIMPLE	t1	system	NULL	NULL	NULL	NULL	0	const row not found
Warnings:
Note	1003	select sql_no_cache benchmark(1,1) AS `benchmark(1,1)` from test.t1
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
create table t2 (a text not null);
insert into t1 values("1111111111111111111111111111111111111111111111111111");
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	4
show status like "Qcache_lowmem_prunes";
Variable_name	Value
Qcache_lowmem_prunes	0
select a as a1, a as a2 from t1;
select a as a2, a as a3 from t1;
select a as a3, a as a4 from t1;
select a as a1, a as a2 from t1;
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	4
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
show status like "Qcache_lowmem_prunes";
Variable_name	Value
Qcache_lowmem_prunes	2
reset query cache;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
insert into t2 select * from t1;
insert into t1 select * from t2;
select * from t1;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
drop table t1,t2;
create database mysqltest;
create table mysqltest.t1 (i int not null auto_increment, a int, primary key (i));
insert into mysqltest.t1 (a) values (1);
select * from mysqltest.t1 where i is null;
i	a
1	1
create table t1(a int);
select * from t1;
a
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
select * from mysqltest.t1;
i	a
1	1
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	3
drop database mysqltest;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
drop table t1;
create table t1 (a char(1) not null collate koi8r_general_ci);
insert into t1 values(_koi8r"�");
set CHARACTER SET koi8r;
select * from t1;
a
�
set CHARACTER SET cp1251_koi8;
select * from t1;
a
�
set CHARACTER SET DEFAULT;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	4
drop table t1;
create database if not exists mysqltest;
create table mysqltest.t1 (i int not null);
create table t1 (i int not null);
insert into mysqltest.t1 (i) values (1);
insert into t1 (i) values (2);
select * from t1;
i
2
use mysqltest;
select * from t1;
i
1
select * from t1;
i
1
use test;
select * from t1;
i
2
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	6
drop database mysqltest;
drop table t1;
create table t1 (i int not null);
insert into t1 (i) values (1),(2),(3),(4);
select SQL_CALC_FOUND_ROWS * from t1 limit 2;
i
1
2
select FOUND_ROWS();
FOUND_ROWS()
4
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	6
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
select * from t1 where i=1;
i
1
select FOUND_ROWS();
FOUND_ROWS()
1
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	6
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
select SQL_CALC_FOUND_ROWS * from t1 limit 2;
i
1
2
select FOUND_ROWS();
FOUND_ROWS()
4
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	7
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
select * from t1 where i=1;
i
1
select FOUND_ROWS();
FOUND_ROWS()
1
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	8
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
drop table t1;
flush query cache;
reset query cache;
create table t1 (a int not null);
insert into t1 values (1),(2),(3);
select * from t1;
a
1
2
3
select * from t1;
a
1
2
3
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
insert delayed into t1 values (4);
select a from t1;
a
1
2
3
4
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
drop table t1;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
show global variables like "query_cache_min_res_unit";
Variable_name	Value
query_cache_min_res_unit	4096
set GLOBAL query_cache_min_res_unit=1001;
show global variables like "query_cache_min_res_unit";
Variable_name	Value
query_cache_min_res_unit	1008
create table t1 (a int not null);
insert into t1 values (1),(2),(3);
create table t2 (a int not null);
insert into t2 values (1),(2),(3);
select * from t1;
a
1
2
3
select * from t1;
a
1
2
3
select * from t2;
a
1
2
3
select * from t2;
a
1
2
3
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	11
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
drop table t1;
select a from t2;
a
1
2
3
select a from t2;
a
1
2
3
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	12
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
drop table t2;
set GLOBAL query_cache_min_res_unit=default;
show global variables like "query_cache_min_res_unit";
Variable_name	Value
query_cache_min_res_unit	4096
create table t1 (a int not null);
insert into t1 values (1);
select "aaa" from t1;
aaa
aaa
select "AAA" from t1;
AAA
AAA
drop table t1;
create table t1 (a int);
set GLOBAL query_cache_size=1000;
show global variables like "query_cache_size";
Variable_name	Value
query_cache_size	0
select * from t1;
a
set GLOBAL query_cache_size=1024;
Warnings:
Warning	1282	Query cache failed to set size 1024; new query cache size is 0
show global variables like "query_cache_size";
Variable_name	Value
query_cache_size	0
select * from t1;
a
set GLOBAL query_cache_size=10240;
Warnings:
Warning	1282	Query cache failed to set size 10240; new query cache size is 0
show global variables like "query_cache_size";
Variable_name	Value
query_cache_size	0
select * from t1;
a
set GLOBAL query_cache_size=20480;
Warnings:
Warning	1282	Query cache failed to set size 20480; new query cache size is 0
show global variables like "query_cache_size";
Variable_name	Value
query_cache_size	0
select * from t1;
a
set GLOBAL query_cache_size=40960;
Warnings:
Warning	1282	Query cache failed to set size 40960; new query cache size is 0
show global variables like "query_cache_size";
Variable_name	Value
query_cache_size	0
select * from t1;
a
set GLOBAL query_cache_size=51200;
show global variables like "query_cache_size";
Variable_name	Value
query_cache_size	51200
select * from t1;
a
set GLOBAL query_cache_size=61440;
show global variables like "query_cache_size";
Variable_name	Value
query_cache_size	61440
select * from t1;
a
set GLOBAL query_cache_size=81920;
show global variables like "query_cache_size";
Variable_name	Value
query_cache_size	81920
select * from t1;
a
set GLOBAL query_cache_size=102400;
show global variables like "query_cache_size";
Variable_name	Value
query_cache_size	102400
select * from t1;
a
drop table t1;
set GLOBAL query_cache_size=1048576;
create table t1 (i int not null);
create table t2 (i int not null);
select * from t1;
i
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
create temporary table t3 (i int not null);
select * from t2;
i
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
select * from t3;
i
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
update t1 set i=(select distinct 1 from (select * from t2) a);
drop table t1, t2, t3;
use mysql;
select * from db;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
use test;
select * from mysql.db;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
create table t1(id int auto_increment primary key);
insert into t1 values (NULL), (NULL), (NULL);
select * from t1 where id=2;
id
2
alter table t1 rename to t2;
select * from t1 where id=2;
ERROR 42S02: Table 'test.t1' doesn't exist
drop table t2;
select * from t1 where id=2;
ERROR 42S02: Table 'test.t1' doesn't exist
create table t1 (word char(20) not null);
select * from t1;
word
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
load data infile 'TEST_DIR/std_data/words.dat' into table t1;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
select count(*) from t1;
count(*)
70
drop table t1;
create table t1 (a int);
insert into t1 values (1),(2),(3);
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
select * from t1 into outfile "query_cache.out.file";
select * from t1 into outfile "query_cache.out.file";
ERROR HY000: File 'query_cache.out.file' already exists
select * from t1 limit 1 into dumpfile "query_cache.dump.file";
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
drop table t1;
create table t1 (a int);
insert into t1 values (1),(2);
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	0
select * from t1;
a
1
2
SET OPTION SQL_SELECT_LIMIT=1;
select * from t1;
a
1
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
SET OPTION SQL_SELECT_LIMIT=DEFAULT;
drop table t1;
flush query cache;
reset query cache;
flush status;
set GLOBAL query_cache_size=1048576;
create table t1 (a int not null);
insert into t1 values (1),(2),(3);
create table t2 (a text not null);
create table t3 (a text not null);
insert into t3 values("1111111111111111111111111111111111111111111111111111");
insert into t2 select * from t3;
insert into t3 select * from t2;
insert into t2 select * from t3;
insert into t3 select * from t2;
insert into t2 select * from t3;
insert into t3 select * from t2;
insert into t2 select * from t3;
insert into t3 select * from t2;
insert into t2 select * from t3;
insert into t3 select * from t2;
drop table t2;
create table t2 (a int not null);
insert into t2 values (1),(2),(3);
create table t4 (a int not null);
insert into t4 values (1),(2),(3);
select * from t4;
select * from t2;
select * from t1 as tt, t1 as ttt  where tt.a=1 and ttt.a=2;
select * from t2;
select * from t4;
select * from t1 as tt, t1 as ttt  where tt.a=1 and ttt.a=2;
select * from t2;
select * from t4;
select * from t1 as tt, t1 as ttt  where tt.a=1 and ttt.a=2;
delete from t2 where a=1;
flush query cache;
select * from t3;
delete from t4 where a=1;
flush query cache;
drop table t1,t2,t3,t4;
set query_cache_wlock_invalidate=1;
create table t1 (a int not null);
create table t2 (a int not null);
select * from t1;
a
select * from t2;
a
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
lock table t1 write, t2 read;
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
unlock table;
drop table t1,t2;
set query_cache_wlock_invalidate=default;
CREATE TABLE t1 (id INT PRIMARY KEY);
insert into t1 values (1),(2),(3);
select * from t1;
id
1
2
3
create temporary table t1 (a int not null auto_increment
primary key);
select * from t1;
a
drop table t1;
drop table t1;
SET NAMES koi8r;
CREATE TABLE t1 (a char(1) character set koi8r);
INSERT INTO t1 VALUES (_koi8r'�'),(_koi8r'�');
SELECT a,'�','�'='�' FROM t1;
a	б	'Б'='б'
�	�	1
�	�	1
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	6
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	1
set collation_connection=koi8r_bin;
SELECT a,'�','�'='�' FROM t1;
a	б	'Б'='б'
�	�	0
�	�	0
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	6
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	2
set character_set_client=cp1251;
SELECT a,'�','�'='�' FROM t1;
a	В	'в'='В'
�	�	0
�	�	0
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	6
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	3
set character_set_results=cp1251;
SELECT a,'�','�'='�' FROM t1;
a	В	'в'='В'
�	�	0
�	�	0
show status like "Qcache_hits";
Variable_name	Value
Qcache_hits	6
show status like "Qcache_queries_in_cache";
Variable_name	Value
Qcache_queries_in_cache	4
DROP TABLE t1;
CREATE TABLE t1 (a int(1));
CREATE DATABASE mysqltest;
USE mysqltest;
DROP DATABASE mysqltest;
SELECT * FROM test.t1;
a
USE test;
DROP TABLE t1;
set character_set_results=null;
select @@character_set_results;
@@character_set_results
NULL
set character_set_results=default;
set GLOBAL query_cache_size=1355776;
create table t1 (id int auto_increment primary key, c char(25));
insert into t1 set c = repeat('x',24);
insert into t1 set c = concat(repeat('x',24),'x');
insert into t1 set c = concat(repeat('x',24),'w');
insert into t1 set c = concat(repeat('x',24),'y');
set max_sort_length=200;
select c from t1 order by c, id;
c
xxxxxxxxxxxxxxxxxxxxxxxx
xxxxxxxxxxxxxxxxxxxxxxxxw
xxxxxxxxxxxxxxxxxxxxxxxxx
xxxxxxxxxxxxxxxxxxxxxxxxy
reset query cache;
set max_sort_length=20;
select c from t1 order by c, id;
c
xxxxxxxxxxxxxxxxxxxxxxxx
xxxxxxxxxxxxxxxxxxxxxxxxx
xxxxxxxxxxxxxxxxxxxxxxxxw
xxxxxxxxxxxxxxxxxxxxxxxxy
set max_sort_length=200;
select c from t1 order by c, id;
c
xxxxxxxxxxxxxxxxxxxxxxxx
xxxxxxxxxxxxxxxxxxxxxxxxw
xxxxxxxxxxxxxxxxxxxxxxxxx
xxxxxxxxxxxxxxxxxxxxxxxxy
set max_sort_length=default;
select '1' || '3' from t1;
'1' || '3'
1
1
1
1
set SQL_MODE=oracle;
select '1' || '3' from t1;
'1' || '3'
13
13
13
13
set SQL_MODE=default;
drop table t1;
create table t1 (a varchar(20), b int);
insert into t1 values ('12345678901234567890', 1);
set group_concat_max_len=10;
select group_concat(a) FROM t1 group by b;
group_concat(a)
1234567890
Warnings:
Warning	1260	1 line(s) were cut by GROUP_CONCAT()
set group_concat_max_len=1024;
select group_concat(a) FROM t1 group by b;
group_concat(a)
12345678901234567890
set group_concat_max_len=default;
drop table t1;
SET GLOBAL query_cache_size=0;

drop table if exists t1;
SET @test_character_set= 'big5';
SET @test_collation= 'big5_chinese_ci';
SET @safe_character_set_server= @@character_set_server;
SET @safe_collation_server= @@collation_server;
SET character_set_server= @test_character_set;
SET collation_server= @test_collation;
CREATE DATABASE d1;
USE d1;
CREATE TABLE t1 (c CHAR(10), KEY(c));
SHOW FULL COLUMNS FROM t1;
Field	Type	Collation	Null	Key	Default	Extra	Privileges	Comment
c	char(10)	big5_chinese_ci	YES	MUL	NULL			
INSERT INTO t1 VALUES ('aaa'),('aaaa'),('aaaaa');
SELECT c as want3results FROM t1 WHERE c LIKE 'aaa%';
want3results
aaa
aaaa
aaaaa
DROP TABLE t1;
CREATE TABLE t1 (c1 varchar(15), KEY c1 (c1(2)));
SHOW FULL COLUMNS FROM t1;
Field	Type	Collation	Null	Key	Default	Extra	Privileges	Comment
c1	varchar(15)	big5_chinese_ci	YES	MUL	NULL			
INSERT INTO t1 VALUES ('location'),('loberge'),('lotre'),('boabab');
SELECT c1 as want3results from t1 where c1 like 'l%';
want3results
location
loberge
lotre
SELECT c1 as want3results from t1 where c1 like 'lo%';
want3results
location
loberge
lotre
SELECT c1 as want1result  from t1 where c1 like 'loc%';
want1result
location
SELECT c1 as want1result  from t1 where c1 like 'loca%';
want1result
location
SELECT c1 as want1result  from t1 where c1 like 'locat%';
want1result
location
SELECT c1 as want1result  from t1 where c1 like 'locati%';
want1result
location
SELECT c1 as want1result  from t1 where c1 like 'locatio%';
want1result
location
SELECT c1 as want1result  from t1 where c1 like 'location%';
want1result
location
DROP TABLE t1;
DROP DATABASE d1;
USE test;
SET character_set_server= @safe_character_set_server;
SET collation_server= @safe_collation_server;

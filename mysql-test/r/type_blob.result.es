drop table if exists t1,t2,t3,t4,t5,t6,t7;
CREATE TABLE t1 (a blob, b text, c blob(250), d text(70000), e text(70000000));
show columns from t1;
Field	Type	Null	Key	Default	Extra
a	blob	YES		NULL	
b	text	YES		NULL	
c	blob	YES		NULL	
d	mediumtext	YES		NULL	
e	longtext	YES		NULL	
CREATE TABLE t2 (a char(257), b varbinary(70000), c varchar(70000000));
Warnings:
Warning	1246	Converting column 'a' from CHAR to TEXT
Warning	1246	Converting column 'b' from CHAR to BLOB
Warning	1246	Converting column 'c' from CHAR to TEXT
show columns from t2;
Field	Type	Null	Key	Default	Extra
a	text	YES		NULL	
b	mediumblob	YES		NULL	
c	longtext	YES		NULL	
create table t3 (a long, b long byte);
show create TABLE t3;
Table	Create Table
t3	CREATE TABLE `t3` (
  `a` mediumtext,
  `b` mediumblob
) ENGINE=MyISAM DEFAULT CHARSET=latin1
drop table t1,t2,t3
#;
CREATE TABLE t1 (a char(257) default "hello");
ERROR 42000: Column length too big for column 'a' (max = 255); use BLOB instead
CREATE TABLE t2 (a blob default "hello");
ERROR 42000: BLOB/TEXT column 'a' can't have a default value
drop table if exists t1,t2;
create table t1 (nr int(5) not null auto_increment,b blob,str char(10), primary key (nr));
insert into t1 values (null,"a","A");
insert into t1 values (null,"bbb","BBB");
insert into t1 values (null,"ccc","CCC");
select last_insert_id();
last_insert_id()
3
select * from t1,t1 as t2;
nr	b	str	nr	b	str
1	a	A	1	a	A
2	bbb	BBB	1	a	A
3	ccc	CCC	1	a	A
1	a	A	2	bbb	BBB
2	bbb	BBB	2	bbb	BBB
3	ccc	CCC	2	bbb	BBB
1	a	A	3	ccc	CCC
2	bbb	BBB	3	ccc	CCC
3	ccc	CCC	3	ccc	CCC
drop table t1;
create table t1 (a text);
insert into t1 values ('where');
update t1 set a='Where';
select * from t1;
a
Where
drop table t1;
create table t1 (t text,c char(10),b blob, d binary(10));
insert into t1 values (NULL,NULL,NULL,NULL);
insert into t1 values ("","","","");
insert into t1 values ("hello","hello","hello","hello");
insert into t1 values ("HELLO","HELLO","HELLO","HELLO");
insert into t1 values ("HELLO MY","HELLO MY","HELLO MY","HELLO MY");
insert into t1 values ("a","a","a","a");
insert into t1 values (1,1,1,1);
insert into t1 values (NULL,NULL,NULL,NULL);
update t1 set c="",b=null where c="1";
lock tables t1 READ;
show full fields from t1;
Field	Type	Collation	Null	Key	Default	Extra	Privileges	Comment
t	text	latin1_swedish_ci	YES		NULL			
c	varchar(10)	latin1_swedish_ci	YES		NULL			
b	blob	NULL	YES		NULL			
d	varbinary(10)	NULL	YES		NULL			
lock tables t1 WRITE;
show full fields from t1;
Field	Type	Collation	Null	Key	Default	Extra	Privileges	Comment
t	text	latin1_swedish_ci	YES		NULL			
c	varchar(10)	latin1_swedish_ci	YES		NULL			
b	blob	NULL	YES		NULL			
d	varbinary(10)	NULL	YES		NULL			
unlock tables;
select t from t1 where t like "hello";
t
hello
HELLO
select c from t1 where c like "hello";
c
hello
HELLO
select b from t1 where b like "hello";
b
hello
select d from t1 where d like "hello";
d
hello
select c from t1 having c like "hello";
c
hello
HELLO
select d from t1 having d like "hello";
d
hello
select t from t1 where t like "%HELLO%";
t
hello
HELLO
HELLO MY
select c from t1 where c like "%HELLO%";
c
hello
HELLO
HELLO MY
select b from t1 where b like "%HELLO%";
b
HELLO
HELLO MY
select d from t1 where d like "%HELLO%";
d
HELLO
HELLO MY
select c from t1 having c like "%HELLO%";
c
hello
HELLO
HELLO MY
select d from t1 having d like "%HELLO%";
d
HELLO
HELLO MY
select d from t1 having d like "%HE%LLO%";
d
HELLO
HELLO MY
select t from t1 order by t;
t
NULL
NULL

1
a
hello
HELLO
HELLO MY
select c from t1 order by c;
c
NULL
NULL


a
hello
HELLO
HELLO MY
select b from t1 order by b;
b
NULL
NULL
NULL

HELLO
HELLO MY
a
hello
select d from t1 order by d;
d
NULL
NULL

1
HELLO
HELLO MY
a
hello
select distinct t from t1;
t
NULL

hello
HELLO MY
a
1
select distinct b from t1;
b
NULL

hello
HELLO
HELLO MY
a
select distinct t from t1 order by t;
t
NULL

1
a
hello
HELLO MY
select distinct b from t1 order by b;
b
NULL

HELLO
HELLO MY
a
hello
select t from t1 group by t;
t
NULL

1
a
hello
HELLO MY
select b from t1 group by b;
b
NULL

HELLO
HELLO MY
a
hello
set option sql_big_tables=1;
select distinct t from t1;
t
NULL

hello
HELLO MY
a
1
select distinct b from t1;
b
NULL

hello
HELLO
HELLO MY
a
select distinct t from t1 order by t;
t
NULL

1
a
hello
HELLO MY
select distinct b from t1 order by b;
b
NULL

HELLO
HELLO MY
a
hello
select distinct c from t1;
c
NULL

hello
HELLO MY
a
select distinct d from t1;
d
NULL

hello
HELLO
HELLO MY
a
1
select distinct c from t1 order by c;
c
NULL

a
hello
HELLO MY
select distinct d from t1 order by d;
d
NULL

1
HELLO
HELLO MY
a
hello
select c from t1 group by c;
c
NULL

a
hello
HELLO MY
select d from t1 group by d;
d
NULL

1
HELLO
HELLO MY
a
hello
set option sql_big_tables=0;
select distinct * from t1;
t	c	b	d
NULL	NULL	NULL	NULL
			
hello	hello	hello	hello
HELLO	HELLO	HELLO	HELLO
HELLO MY	HELLO MY	HELLO MY	HELLO MY
a	a	a	a
1		NULL	1
select t,count(*) from t1 group by t;
t	count(*)
NULL	2
	1
1	1
a	1
hello	2
HELLO MY	1
select b,count(*) from t1 group by b;
b	count(*)
NULL	3
	1
HELLO	1
HELLO MY	1
a	1
hello	1
select c,count(*) from t1 group by c;
c	count(*)
NULL	2
	2
a	1
hello	2
HELLO MY	1
select d,count(*) from t1 group by d;
d	count(*)
NULL	2
	1
1	1
HELLO	1
HELLO MY	1
a	1
hello	1
drop table t1;
create table t1 (a text, unique (a(2100)));
ERROR 42000: Specified key was too long; max key length is 1000 bytes
create table t1 (a text, key (a(2100)));
Warnings:
Warning	1071	Specified key was too long; max key length is 1000 bytes
show create table t1;
Table	Create Table
t1	CREATE TABLE `t1` (
  `a` text,
  KEY `a` (`a`(1000))
) ENGINE=MyISAM DEFAULT CHARSET=latin1
drop table t1;
CREATE TABLE t1 (
t1_id bigint(21) NOT NULL auto_increment,
_field_72 varchar(128) DEFAULT '' NOT NULL,
_field_95 varchar(32),
_field_115 tinyint(4) DEFAULT '0' NOT NULL,
_field_122 tinyint(4) DEFAULT '0' NOT NULL,
_field_126 tinyint(4),
_field_134 tinyint(4),
PRIMARY KEY (t1_id),
UNIQUE _field_72 (_field_72),
KEY _field_115 (_field_115),
KEY _field_122 (_field_122)
);
INSERT INTO t1 VALUES (1,'admin','21232f297a57a5a743894a0e4a801fc3',0,1,NULL,NULL);
INSERT INTO t1 VALUES (2,'hroberts','7415275a8c95952901e42b13a6b78566',0,1,NULL,NULL);
INSERT INTO t1 VALUES (3,'guest','d41d8cd98f00b204e9800998ecf8427e',1,0,NULL,NULL);
CREATE TABLE t2 (
seq_0_id bigint(21) DEFAULT '0' NOT NULL,
seq_1_id bigint(21) DEFAULT '0' NOT NULL,
PRIMARY KEY (seq_0_id,seq_1_id)
);
INSERT INTO t2 VALUES (1,1);
INSERT INTO t2 VALUES (2,1);
INSERT INTO t2 VALUES (2,2);
CREATE TABLE t3 (
t3_id bigint(21) NOT NULL auto_increment,
_field_131 varchar(128),
_field_133 tinyint(4) DEFAULT '0' NOT NULL,
_field_135 datetime DEFAULT '0000-00-00 00:00:00' NOT NULL,
_field_137 tinyint(4),
_field_139 datetime DEFAULT '0000-00-00 00:00:00' NOT NULL,
_field_140 blob,
_field_142 tinyint(4) DEFAULT '0' NOT NULL,
_field_145 tinyint(4) DEFAULT '0' NOT NULL,
_field_148 tinyint(4) DEFAULT '0' NOT NULL,
PRIMARY KEY (t3_id),
KEY _field_133 (_field_133),
KEY _field_135 (_field_135),
KEY _field_139 (_field_139),
KEY _field_142 (_field_142),
KEY _field_145 (_field_145),
KEY _field_148 (_field_148)
);
INSERT INTO t3 VALUES (1,'test job 1',0,'0000-00-00 00:00:00',0,'1999-02-25 22:43:32','test\r\njob\r\n1',0,0,0);
INSERT INTO t3 VALUES (2,'test job 2',0,'0000-00-00 00:00:00',0,'1999-02-26 21:08:04','',0,0,0);
CREATE TABLE t4 (
seq_0_id bigint(21) DEFAULT '0' NOT NULL,
seq_1_id bigint(21) DEFAULT '0' NOT NULL,
PRIMARY KEY (seq_0_id,seq_1_id)
);
INSERT INTO t4 VALUES (1,1);
INSERT INTO t4 VALUES (2,1);
CREATE TABLE t5 (
t5_id bigint(21) NOT NULL auto_increment,
_field_149 tinyint(4),
_field_156 varchar(128) DEFAULT '' NOT NULL,
_field_157 varchar(128) DEFAULT '' NOT NULL,
_field_158 varchar(128) DEFAULT '' NOT NULL,
_field_159 varchar(128) DEFAULT '' NOT NULL,
_field_160 varchar(128) DEFAULT '' NOT NULL,
_field_161 varchar(128) DEFAULT '' NOT NULL,
PRIMARY KEY (t5_id),
KEY _field_156 (_field_156),
KEY _field_157 (_field_157),
KEY _field_158 (_field_158),
KEY _field_159 (_field_159),
KEY _field_160 (_field_160),
KEY _field_161 (_field_161)
);
INSERT INTO t5 VALUES (1,0,'tomato','','','','','');
INSERT INTO t5 VALUES (2,0,'cilantro','','','','','');
CREATE TABLE t6 (
seq_0_id bigint(21) DEFAULT '0' NOT NULL,
seq_1_id bigint(21) DEFAULT '0' NOT NULL,
PRIMARY KEY (seq_0_id,seq_1_id)
);
INSERT INTO t6 VALUES (1,1);
INSERT INTO t6 VALUES (1,2);
INSERT INTO t6 VALUES (2,2);
CREATE TABLE t7 (
t7_id bigint(21) NOT NULL auto_increment,
_field_143 tinyint(4),
_field_165 varchar(32),
_field_166 smallint(6) DEFAULT '0' NOT NULL,
PRIMARY KEY (t7_id),
KEY _field_166 (_field_166)
);
INSERT INTO t7 VALUES (1,0,'High',1);
INSERT INTO t7 VALUES (2,0,'Medium',2);
INSERT INTO t7 VALUES (3,0,'Low',3);
select replace(t3._field_140, "\r","^M"),t3_id,min(t3._field_131), min(t3._field_135), min(t3._field_139), min(t3._field_137), min(link_alias_142._field_165), min(link_alias_133._field_72), min(t3._field_145), min(link_alias_148._field_156), replace(min(t3._field_140), "\r","^M"),t3.t3_id from t3 left join t4 on t4.seq_0_id = t3.t3_id left join t7 link_alias_142 on t4.seq_1_id = link_alias_142.t7_id left join t6 on t6.seq_0_id = t3.t3_id left join t1 link_alias_133 on t6.seq_1_id = link_alias_133.t1_id left join t2 on t2.seq_0_id = t3.t3_id left join t5 link_alias_148 on t2.seq_1_id = link_alias_148.t5_id where t3.t3_id in (1) group by t3.t3_id order by link_alias_142._field_166, _field_139, link_alias_133._field_72, _field_135, link_alias_148._field_156;
replace(t3._field_140, "\r","^M")	t3_id	min(t3._field_131)	min(t3._field_135)	min(t3._field_139)	min(t3._field_137)	min(link_alias_142._field_165)	min(link_alias_133._field_72)	min(t3._field_145)	min(link_alias_148._field_156)	replace(min(t3._field_140), "\r","^M")	t3_id
test^M
job^M
1	1	test job 1	0000-00-00 00:00:00	1999-02-25 22:43:32	0	High	admin	0	tomato	test^M
job^M
1	1
drop table t1,t2,t3,t4,t5,t6,t7;
create table t1 (a blob);
insert into t1 values ("empty"),("");
select a,reverse(a) from t1;
a	reverse(a)
empty	ytpme
	
drop table t1;
create table t1 (a blob, key (a(10)));
insert into t1 values ("bye"),("hello"),("hello"),("hello word");
select * from t1 where a like "hello%";
a
hello
hello
hello word
drop table t1;
CREATE TABLE t1 (
f1 int(11) DEFAULT '0' NOT NULL,
f2 varchar(16) DEFAULT '' NOT NULL,
f5 text,
KEY index_name (f1,f2,f5(16))
);
INSERT INTO t1 VALUES (0,'traktor','1111111111111');
INSERT INTO t1 VALUES (1,'traktor','1111111111111111111111111');
select count(*) from t1 where f2='traktor';
count(*)
2
drop table t1;
create table t1 (foobar tinyblob not null, boggle smallint not null, key (foobar(32), boggle));
insert into t1 values ('fish', 10),('bear', 20);
select foobar, boggle from t1 where foobar = 'fish';
foobar	boggle
fish	10
select foobar, boggle from t1 where foobar = 'fish' and boggle = 10;
foobar	boggle
fish	10
drop table t1;
create table t1 (id integer auto_increment unique,imagem LONGBLOB not null);
insert into t1 (id) values (1);
select 
charset(load_file('../../std_data/words.dat')),
collation(load_file('../../std_data/words.dat')),
coercibility(load_file('../../std_data/words.dat'));
charset(load_file('../../std_data/words.dat'))	collation(load_file('../../std_data/words.dat'))	coercibility(load_file('../../std_data/words.dat'))
NULL	NULL	3
explain extended select 
charset(load_file('../../std_data/words.dat')),
collation(load_file('../../std_data/words.dat')),
coercibility(load_file('../../std_data/words.dat'));
id	select_type	table	type	possible_keys	key	key_len	ref	rows	Extra
1	SIMPLE	NULL	NULL	NULL	NULL	NULL	NULL	NULL	No tables used
Warnings:
Note	1003	select sql_no_cache charset(load_file(_latin1'../../std_data/words.dat')) AS `charset(load_file('../../std_data/words.dat'))`,collation(load_file(_latin1'../../std_data/words.dat')) AS `collation(load_file('../../std_data/words.dat'))`,coercibility(load_file(_latin1'../../std_data/words.dat')) AS `coercibility(load_file('../../std_data/words.dat'))`
update t1 set imagem=load_file('../../std_data/words.dat') where id=1;
Warnings:
Warning	1263	Data truncated; NULL supplied to NOT NULL column 'imagem' at row 1
select if(imagem is null, "ERROR", "OK"),length(imagem) from t1 where id = 1;
if(imagem is null, "ERROR", "OK")	length(imagem)
OK	0
drop table t1;
create table t1 select load_file('../../std_data/words.dat');
show full fields from t1;
Field	Type	Collation	Null	Key	Default	Extra	Privileges	Comment
load_file('../../std_data/words.dat')	longblob	NULL	YES		NULL			
drop table t1;
create table t1 (id integer primary key auto_increment, txt text not null, unique index txt_index (txt (20)));
insert into t1 (txt) values ('Chevy'), ('Chevy ');
select * from t1 where txt='Chevy';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt='Chevy ';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt='Chevy ' or txt='Chevy';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt='Chevy' or txt='Chevy ';
id	txt
1	Chevy
2	Chevy 
select * from t1 where id='1' or id='2';
id	txt
1	Chevy
2	Chevy 
insert into t1 (txt) values('Ford');
select * from t1 where txt='Chevy' or txt='Chevy ' or txt='Ford';
id	txt
1	Chevy
2	Chevy 
3	Ford
select * from t1 where txt='Chevy' or txt='Chevy ';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt='Chevy' or txt='Chevy ' or txt=' Chevy';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt in ('Chevy ','Chevy');
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt in ('Chevy');
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt between 'Chevy' and 'Chevy';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt between 'Chevy' and 'Chevy' or txt='Chevy ';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt between 'Chevy' and 'Chevy ';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt < 'Chevy ';
id	txt
select * from t1 where txt <= 'Chevy';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt > 'Chevy';
id	txt
3	Ford
select * from t1 where txt >= 'Chevy';
id	txt
1	Chevy
2	Chevy 
3	Ford
drop table t1;
create table t1 (id integer primary key auto_increment, txt text, unique index txt_index (txt (20)));
insert into t1 (txt) values ('Chevy'), ('Chevy '), (NULL);
select * from t1 where txt='Chevy' or txt is NULL;
id	txt
3	NULL
1	Chevy
2	Chevy 
explain select * from t1 where txt='Chevy' or txt is NULL;
id	select_type	table	type	possible_keys	key	key_len	ref	rows	Extra
1	SIMPLE	t1	range	txt_index	txt_index	23	NULL	2	Using where
select * from t1 where txt='Chevy ';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt='Chevy ' or txt='Chevy';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt='Chevy' or txt='Chevy ';
id	txt
1	Chevy
2	Chevy 
select * from t1 where id='1' or id='2';
id	txt
1	Chevy
2	Chevy 
insert into t1 (txt) values('Ford');
select * from t1 where txt='Chevy' or txt='Chevy ' or txt='Ford';
id	txt
1	Chevy
2	Chevy 
4	Ford
select * from t1 where txt='Chevy' or txt='Chevy ';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt='Chevy' or txt='Chevy ' or txt=' Chevy';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt in ('Chevy ','Chevy');
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt in ('Chevy');
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt between 'Chevy' and 'Chevy';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt between 'Chevy' and 'Chevy' or txt='Chevy ';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt between 'Chevy' and 'Chevy ';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt < 'Chevy ';
id	txt
select * from t1 where txt < 'Chevy ' or txt is NULL;
id	txt
3	NULL
select * from t1 where txt <= 'Chevy';
id	txt
1	Chevy
2	Chevy 
select * from t1 where txt > 'Chevy';
id	txt
4	Ford
select * from t1 where txt >= 'Chevy';
id	txt
1	Chevy
2	Chevy 
4	Ford
alter table t1 modify column txt blob;
explain select * from t1 where txt='Chevy' or txt is NULL;
id	select_type	table	type	possible_keys	key	key_len	ref	rows	Extra
1	SIMPLE	t1	ref_or_null	txt_index	txt_index	23	const	2	Using where
select * from t1 where txt='Chevy' or txt is NULL;
id	txt
1	Chevy
3	NULL
explain select * from t1 where txt='Chevy' or txt is NULL order by txt;
id	select_type	table	type	possible_keys	key	key_len	ref	rows	Extra
1	SIMPLE	t1	ref_or_null	txt_index	txt_index	23	const	2	Using where; Using filesort
select * from t1 where txt='Chevy' or txt is NULL order by txt;
id	txt
3	NULL
1	Chevy
drop table t1;
CREATE TABLE t1 ( i int(11) NOT NULL default '0',    c text NOT NULL, d varchar(1) NOT NULL DEFAULT ' ', PRIMARY KEY  (i), KEY (c(1),d));
INSERT t1 (i, c) VALUES (1,''),(2,''),(3,'asdfh'),(4,'');
select max(i) from t1 where c = '';
max(i)
4
drop table t1;

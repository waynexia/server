/* Copyright (C) 2000 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* Definefile for error messagenumbers */

#define ER_HASHCHK 1000
#define ER_NISAMCHK 1001
#define ER_NO 1002
#define ER_YES 1003
#define ER_CANT_CREATE_FILE 1004
#define ER_CANT_CREATE_TABLE 1005
#define ER_CANT_CREATE_DB 1006
#define ER_DB_CREATE_EXISTS 1007
#define ER_DB_DROP_EXISTS 1008
#define ER_DB_DROP_DELETE 1009
#define ER_DB_DROP_RMDIR 1010
#define ER_CANT_DELETE_FILE 1011
#define ER_CANT_FIND_SYSTEM_REC 1012
#define ER_CANT_GET_STAT 1013
#define ER_CANT_GET_WD 1014
#define ER_CANT_LOCK 1015
#define ER_CANT_OPEN_FILE 1016
#define ER_FILE_NOT_FOUND 1017
#define ER_CANT_READ_DIR 1018
#define ER_CANT_SET_WD 1019
#define ER_CHECKREAD 1020
#define ER_DISK_FULL 1021
#define ER_DUP_KEY 1022
#define ER_ERROR_ON_CLOSE 1023
#define ER_ERROR_ON_READ 1024
#define ER_ERROR_ON_RENAME 1025
#define ER_ERROR_ON_WRITE 1026
#define ER_FILE_USED 1027
#define ER_FILSORT_ABORT 1028
#define ER_FORM_NOT_FOUND 1029
#define ER_GET_ERRNO 1030
#define ER_ILLEGAL_HA 1031
#define ER_KEY_NOT_FOUND 1032
#define ER_NOT_FORM_FILE 1033
#define ER_NOT_KEYFILE 1034
#define ER_OLD_KEYFILE 1035
#define ER_OPEN_AS_READONLY 1036
#define ER_OUTOFMEMORY 1037
#define ER_OUT_OF_SORTMEMORY 1038
#define ER_UNEXPECTED_EOF 1039
#define ER_CON_COUNT_ERROR 1040
#define ER_OUT_OF_RESOURCES 1041
#define ER_BAD_HOST_ERROR 1042
#define ER_HANDSHAKE_ERROR 1043
#define ER_DBACCESS_DENIED_ERROR 1044
#define ER_ACCESS_DENIED_ERROR 1045
#define ER_NO_DB_ERROR 1046
#define ER_UNKNOWN_COM_ERROR 1047
#define ER_BAD_NULL_ERROR 1048
#define ER_BAD_DB_ERROR 1049
#define ER_TABLE_EXISTS_ERROR 1050
#define ER_BAD_TABLE_ERROR 1051
#define ER_NON_UNIQ_ERROR 1052
#define ER_SERVER_SHUTDOWN 1053
#define ER_BAD_FIELD_ERROR 1054
#define ER_WRONG_FIELD_WITH_GROUP 1055
#define ER_WRONG_GROUP_FIELD 1056
#define ER_WRONG_SUM_SELECT 1057
#define ER_WRONG_VALUE_COUNT 1058
#define ER_TOO_LONG_IDENT 1059
#define ER_DUP_FIELDNAME 1060
#define ER_DUP_KEYNAME 1061
#define ER_DUP_ENTRY 1062
#define ER_WRONG_FIELD_SPEC 1063
#define ER_PARSE_ERROR 1064
#define ER_EMPTY_QUERY 1065
#define ER_NONUNIQ_TABLE 1066
#define ER_INVALID_DEFAULT 1067
#define ER_MULTIPLE_PRI_KEY 1068
#define ER_TOO_MANY_KEYS 1069
#define ER_TOO_MANY_KEY_PARTS 1070
#define ER_TOO_LONG_KEY 1071
#define ER_KEY_COLUMN_DOES_NOT_EXITS 1072
#define ER_BLOB_USED_AS_KEY 1073
#define ER_TOO_BIG_FIELDLENGTH 1074
#define ER_WRONG_AUTO_KEY 1075
#define ER_READY 1076
#define ER_NORMAL_SHUTDOWN 1077
#define ER_GOT_SIGNAL 1078
#define ER_SHUTDOWN_COMPLETE 1079
#define ER_FORCING_CLOSE 1080
#define ER_IPSOCK_ERROR 1081
#define ER_NO_SUCH_INDEX 1082
#define ER_WRONG_FIELD_TERMINATORS 1083
#define ER_BLOBS_AND_NO_TERMINATED 1084
#define ER_TEXTFILE_NOT_READABLE 1085
#define ER_FILE_EXISTS_ERROR 1086
#define ER_LOAD_INFO 1087
#define ER_ALTER_INFO 1088
#define ER_WRONG_SUB_KEY 1089
#define ER_CANT_REMOVE_ALL_FIELDS 1090
#define ER_CANT_DROP_FIELD_OR_KEY 1091
#define ER_INSERT_INFO 1092
#define ER_UPDATE_TABLE_USED 1093
#define ER_NO_SUCH_THREAD 1094
#define ER_KILL_DENIED_ERROR 1095
#define ER_NO_TABLES_USED 1096
#define ER_TOO_BIG_SET 1097
#define ER_NO_UNIQUE_LOGFILE 1098
#define ER_TABLE_NOT_LOCKED_FOR_WRITE 1099
#define ER_TABLE_NOT_LOCKED 1100
#define ER_BLOB_CANT_HAVE_DEFAULT 1101
#define ER_WRONG_DB_NAME 1102
#define ER_WRONG_TABLE_NAME 1103
#define ER_TOO_BIG_SELECT 1104
#define ER_UNKNOWN_ERROR 1105
#define ER_UNKNOWN_PROCEDURE 1106
#define ER_WRONG_PARAMCOUNT_TO_PROCEDURE 1107
#define ER_WRONG_PARAMETERS_TO_PROCEDURE 1108
#define ER_UNKNOWN_TABLE 1109
#define ER_FIELD_SPECIFIED_TWICE 1110
#define ER_INVALID_GROUP_FUNC_USE 1111
#define ER_UNSUPPORTED_EXTENSION 1112
#define ER_TABLE_MUST_HAVE_COLUMNS 1113
#define ER_RECORD_FILE_FULL 1114
#define ER_UNKNOWN_CHARACTER_SET 1115
#define ER_TOO_MANY_TABLES 1116
#define ER_TOO_MANY_FIELDS 1117
#define ER_TOO_BIG_ROWSIZE 1118
#define ER_STACK_OVERRUN 1119
#define ER_WRONG_OUTER_JOIN 1120
#define ER_NULL_COLUMN_IN_INDEX 1121
#define ER_CANT_FIND_UDF 1122
#define ER_CANT_INITIALIZE_UDF 1123
#define ER_UDF_NO_PATHS 1124
#define ER_UDF_EXISTS 1125
#define ER_CANT_OPEN_LIBRARY 1126
#define ER_CANT_FIND_DL_ENTRY 1127
#define ER_FUNCTION_NOT_DEFINED 1128
#define ER_HOST_IS_BLOCKED 1129
#define ER_HOST_NOT_PRIVILEGED 1130
#define ER_PASSWORD_ANONYMOUS_USER 1131
#define ER_PASSWORD_NOT_ALLOWED 1132
#define ER_PASSWORD_NO_MATCH 1133
#define ER_UPDATE_INFO 1134
#define ER_CANT_CREATE_THREAD 1135
#define ER_WRONG_VALUE_COUNT_ON_ROW 1136
#define ER_CANT_REOPEN_TABLE 1137
#define ER_INVALID_USE_OF_NULL 1138
#define ER_REGEXP_ERROR 1139
#define ER_MIX_OF_GROUP_FUNC_AND_FIELDS 1140
#define ER_NONEXISTING_GRANT 1141
#define ER_TABLEACCESS_DENIED_ERROR 1142
#define ER_COLUMNACCESS_DENIED_ERROR 1143
#define ER_ILLEGAL_GRANT_FOR_TABLE 1144
#define ER_GRANT_WRONG_HOST_OR_USER 1145
#define ER_NO_SUCH_TABLE 1146
#define ER_NONEXISTING_TABLE_GRANT 1147
#define ER_NOT_ALLOWED_COMMAND 1148
#define ER_SYNTAX_ERROR 1149
#define ER_DELAYED_CANT_CHANGE_LOCK 1150
#define ER_TOO_MANY_DELAYED_THREADS 1151
#define ER_ABORTING_CONNECTION 1152
#define ER_NET_PACKET_TOO_LARGE 1153
#define ER_NET_READ_ERROR_FROM_PIPE 1154
#define ER_NET_FCNTL_ERROR 1155
#define ER_NET_PACKETS_OUT_OF_ORDER 1156
#define ER_NET_UNCOMPRESS_ERROR 1157
#define ER_NET_READ_ERROR 1158
#define ER_NET_READ_INTERRUPTED 1159
#define ER_NET_ERROR_ON_WRITE 1160
#define ER_NET_WRITE_INTERRUPTED 1161
#define ER_TOO_LONG_STRING 1162
#define ER_TABLE_CANT_HANDLE_BLOB 1163
#define ER_TABLE_CANT_HANDLE_AUTO_INCREMENT 1164
#define ER_DELAYED_INSERT_TABLE_LOCKED 1165
#define ER_WRONG_COLUMN_NAME 1166
#define ER_WRONG_KEY_COLUMN 1167
#define ER_WRONG_MRG_TABLE 1168
#define ER_DUP_UNIQUE 1169
#define ER_BLOB_KEY_WITHOUT_LENGTH 1170
#define ER_PRIMARY_CANT_HAVE_NULL 1171
#define ER_TOO_MANY_ROWS 1172
#define ER_REQUIRES_PRIMARY_KEY 1173
#define ER_NO_RAID_COMPILED 1174
#define ER_UPDATE_WITHOUT_KEY_IN_SAFE_MODE 1175
#define ER_KEY_DOES_NOT_EXITS 1176
#define ER_CHECK_NO_SUCH_TABLE 1177
#define ER_CHECK_NOT_IMPLEMENTED 1178
#define ER_CANT_DO_THIS_DURING_AN_TRANSACTION 1179
#define ER_ERROR_DURING_COMMIT 1180
#define ER_ERROR_DURING_ROLLBACK 1181
#define ER_ERROR_DURING_FLUSH_LOGS 1182
#define ER_ERROR_DURING_CHECKPOINT 1183
#define ER_NEW_ABORTING_CONNECTION 1184
#define ER_DUMP_NOT_IMPLEMENTED    1185
#define ER_FLUSH_MASTER_BINLOG_CLOSED 1186
#define ER_INDEX_REBUILD  1187
#define ER_MASTER 1188
#define ER_MASTER_NET_READ 1189
#define ER_MASTER_NET_WRITE 1190
#define ER_FT_MATCHING_KEY_NOT_FOUND 1191
#define ER_LOCK_OR_ACTIVE_TRANSACTION 1192
#define ER_UNKNOWN_SYSTEM_VARIABLE 1193
#define ER_CRASHED_ON_USAGE 1194
#define ER_CRASHED_ON_REPAIR 1195
#define ER_WARNING_NOT_COMPLETE_ROLLBACK 1196
#define ER_TRANS_CACHE_FULL 1197
#define ER_SLAVE_MUST_STOP 1198
#define ER_SLAVE_NOT_RUNNING 1199
#define ER_BAD_SLAVE 1200
#define ER_MASTER_INFO 1201
#define ER_SLAVE_THREAD 1202
#define ER_TOO_MANY_USER_CONNECTIONS 1203
#define ER_SET_CONSTANTS_ONLY 1204
#define ER_LOCK_WAIT_TIMEOUT 1205
#define ER_LOCK_TABLE_FULL 1206
#define ER_READ_ONLY_TRANSACTION 1207
#define ER_DROP_DB_WITH_READ_LOCK 1208
#define ER_CREATE_DB_WITH_READ_LOCK 1209
#define ER_WRONG_ARGUMENTS 1210
#define ER_NO_PERMISSION_TO_CREATE_USER 1211
#define ER_UNION_TABLES_IN_DIFFERENT_DIR 1212
#define ER_LOCK_DEADLOCK 1213
#define ER_TABLE_CANT_HANDLE_FULLTEXT 1214
#define ER_CANNOT_ADD_FOREIGN 1215
#define ER_NO_REFERENCED_ROW 1216
#define ER_ROW_IS_REFERENCED 1217
#define ER_CONNECT_TO_MASTER 1218
#define ER_QUERY_ON_MASTER 1219
#define ER_ERROR_WHEN_EXECUTING_COMMAND 1220
#define ER_WRONG_USAGE 1221
#define ER_WRONG_NUMBER_OF_COLUMNS_IN_SELECT 1222
#define ER_CANT_UPDATE_WITH_READLOCK 1223
#define ER_MIXING_NOT_ALLOWED 1224
#define ER_DUP_ARGUMENT 1225
#define ER_USER_LIMIT_REACHED 1226
#define ER_SPECIFIC_ACCESS_DENIED_ERROR 1227
#define ER_LOCAL_VARIABLE 1228
#define ER_GLOBAL_VARIABLE 1229
#define ER_NO_DEFAULT 1230
#define ER_WRONG_VALUE_FOR_VAR 1231
#define ER_WRONG_TYPE_FOR_VAR 1232
#define ER_VAR_CANT_BE_READ 1233
#define ER_CANT_USE_OPTION_HERE 1234
#define ER_NOT_SUPPORTED_YET	1235
#define ER_MASTER_FATAL_ERROR_READING_BINLOG 1236
#define ER_SLAVE_IGNORED_TABLE 1237
#define ER_WRONG_FK_DEF 1238
#define ER_KEY_REF_DO_NOT_MATCH_TABLE_REF 1239
#define ER_OPERAND_COLUMNS 1240
#define ER_SUBQUERY_NO_1_ROW 1241
#define ER_UNKNOWN_STMT_HANDLER 1242
#define ER_CORRUPT_HELP_DB 1243
#define ER_CYCLIC_REFERENCE 1244
#define ER_AUTO_CONVERT 1245
#define ER_ILLEGAL_REFERENCE 1246
#define ER_DERIVED_MUST_HAVE_ALIAS 1247
#define ER_SELECT_REDUCED 1248
#define ER_TABLENAME_NOT_ALLOWED_HERE 1249
#define ER_NOT_SUPPORTED_AUTH_MODE 1250
#define ER_SPATIAL_CANT_HAVE_NULL 1251
#define ER_COLLATION_CHARSET_MISMATCH 1252
#define ER_SLAVE_WAS_RUNNING 1253
#define ER_SLAVE_WAS_NOT_RUNNING 1254
#define ER_TOO_BIG_FOR_UNCOMPRESS 1255
#define ER_ZLIB_Z_MEM_ERROR 1256
#define ER_ZLIB_Z_BUF_ERROR 1257
#define ER_ZLIB_Z_DATA_ERROR 1258
#define ER_CUT_VALUE_GROUP_CONCAT 1259
#define ER_WARN_TOO_FEW_RECORDS 1260
#define ER_WARN_TOO_MANY_RECORDS 1261
#define ER_WARN_NULL_TO_NOTNULL 1262
#define ER_WARN_DATA_OUT_OF_RANGE 1263
#define ER_WARN_DATA_TRUNCATED 1264
#define ER_WARN_USING_OTHER_HANDLER 1265
#define ER_CANT_AGGREGATE_2COLLATIONS 1266
#define ER_DROP_USER 1267
#define ER_REVOKE_GRANTS 1268
#define ER_CANT_AGGREGATE_3COLLATIONS 1269
#define ER_CANT_AGGREGATE_NCOLLATIONS 1270
#define ER_VARIABLE_IS_NOT_STRUCT 1271
#define ER_UNKNOWN_COLLATION 1272
#define ER_SLAVE_IGNORED_SSL_PARAMS 1273
#define ER_SERVER_IS_IN_SECURE_AUTH_MODE 1274
#define ER_WARN_FIELD_RESOLVED 1275
#define ER_BAD_SLAVE_UNTIL_COND 1276
#define ER_MISSING_SKIP_SLAVE 1277
#define ER_UNTIL_COND_IGNORED 1278
#define ER_WRONG_INDEX_NAME 1279
#define ER_ERROR_MESSAGES 280

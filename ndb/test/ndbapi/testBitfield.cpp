
#include <ndb_global.h>
#include <ndb_opts.h>
#include <NDBT.hpp>
#include <NdbApi.hpp>

static const char* opt_connect_str= 0;
static const char* _dbname = "TEST_DB";
static int g_loops = 5;
static struct my_option my_long_options[] =
{
  NDB_STD_OPTS("ndb_desc"),
  { "database", 'd', "Name of database table is in",
    (gptr*) &_dbname, (gptr*) &_dbname, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};

static void print_version()
{
  printf("MySQL distrib %s, for %s (%s)\n",
	 MYSQL_SERVER_VERSION,SYSTEM_TYPE,MACHINE_TYPE);
}
static void usage()
{
  char desc[] = 
    "tabname\n"\
    "This program list all properties of table(s) in NDB Cluster.\n"\
    "  ex: desc T1 T2 T4\n";  
  print_version();
  my_print_help(my_long_options);
  my_print_variables(my_long_options);
}
static my_bool
get_one_option(int optid, const struct my_option *opt __attribute__((unused)),
	       char *argument)
{
  switch (optid) {
  case '#':
    DBUG_PUSH(argument ? argument : "d:t:O,/tmp/ndb_desc.trace");
    break;
  case 'V':
    print_version();
    exit(0);
  case '?':
    usage();
    exit(0);
  }
  return 0;
}

static const NdbDictionary::Table* create_random_table(Ndb*);
static int transactions(Ndb*, const NdbDictionary::Table* tab);
static int unique_indexes(Ndb*, const NdbDictionary::Table* tab);
static int ordered_indexes(Ndb*, const NdbDictionary::Table* tab);
static int node_restart(Ndb*, const NdbDictionary::Table* tab);
static int system_restart(Ndb*, const NdbDictionary::Table* tab);

int 
main(int argc, char** argv){
  NDB_INIT(argv[0]);
  const char *load_default_groups[]= { "mysql_cluster",0 };
  load_defaults("my",load_default_groups,&argc,&argv);
  int ho_error;
  if ((ho_error=handle_options(&argc, &argv, my_long_options, get_one_option)))
    return NDBT_ProgramExit(NDBT_WRONGARGS);

  Ndb::setConnectString(opt_connect_str);

  Ndb* pNdb;
  pNdb = new Ndb(_dbname);  
  pNdb->init();
  while (pNdb->waitUntilReady() != 0);
  int res = NDBT_FAILED;

  NdbDictionary::Dictionary * dict = pNdb->getDictionary();

  const NdbDictionary::Table* pTab = 0;
  for (int i = 0; i < (argc ? argc : g_loops) ; i++)
  {
    res = NDBT_FAILED;
    if(argc == 0)
    {
      pTab = create_random_table(pNdb);
    }
    else
    {
      dict->dropTable(argv[i]);
      NDBT_Tables::createTable(pNdb, argv[i]);
      pTab = dict->getTable(argv[i]);
    }
    
    if (pTab == 0)
    {
      ndbout << "Failed to create table" << endl;
      ndbout << dict->getNdbError() << endl;
      break;
    }
    
    if(transactions(pNdb, pTab))
      break;

    if(unique_indexes(pNdb, pTab))
      break;

    if(ordered_indexes(pNdb, pTab))
      break;
    
    if(node_restart(pNdb, pTab))
      break;
    
    if(system_restart(pNdb, pTab))
      break;

    dict->dropTable(pTab->getName());
    res = NDBT_OK;
  }

  if(res != NDBT_OK && pTab)
  {
    dict->dropTable(pTab->getName());
  }
  
  delete pNdb;
  return NDBT_ProgramExit(res);
}

static 
const NdbDictionary::Table* 
create_random_table(Ndb* pNdb)
{
  return 0;
}
static 
int
transactions(Ndb* pNdb, const NdbDictionary::Table* tab)
{
  return 0;
}

static 
int 
unique_indexes(Ndb* pNdb, const NdbDictionary::Table* tab)
{
  return 0;
}

static 
int 
ordered_indexes(Ndb* pNdb, const NdbDictionary::Table* tab)
{
  return 0;
}

static 
int 
node_restart(Ndb* pNdb, const NdbDictionary::Table* tab)
{
  return 0;
}

static 
int 
system_restart(Ndb* pNdb, const NdbDictionary::Table* tab)
{
  return 0;
}

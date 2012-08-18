
/*
  This file is a "bridge" interface between cassandra+Thrift and MariaDB.

  It is #included by both sides, so it must itself include neither (including
  both together causes compile errors due to conflicts).
*/


/* 
  Storage for (name,value) pairs. name==NULL means 'non-object'.

  This should be used for 
  - shipping data from sql to cassandra for INSERTs 
  - shipping data from cassandra to SQL for record reads.

*/
class NameAndValue
{
public:
  char *name;
  char *value;
  size_t value_len;
};

/*
  Interface to one cassandra column family, i.e. one 'table'
*/
class Cassandra_se_interface
{
public:
  Cassandra_se_interface() { err_buffer[0]=0; }
  
  virtual ~Cassandra_se_interface(){};
  /* Init */
  virtual bool connect(const char *host, const char *port)=0;
  virtual void set_column_family(const char *cfname) = 0;
   
  /* Check underlying DDL */
  virtual bool setup_ddl_checks()=0;
  virtual void first_ddl_column()=0;
  virtual bool next_ddl_column(char **name, int *name_len, char **value, 
                               int *value_len)=0;

  /* Writes */
  virtual void start_prepare_insert(const char *key, int key_len)=0;
  virtual void add_insert_column(const char *name, const char *value, 
                                 int value_len)=0;
  virtual bool do_insert()=0;

  /* Reads */
  virtual bool get_slice(char *key, size_t key_len, bool *found)=0 ;
  virtual bool get_next_read_column(char **name, char **value, int *value_len)=0;

  /* Passing error messages up to ha_cassandra */
  char err_buffer[512];
  const char *error_str() { return err_buffer; }
  void print_error(const char *format, ...);
};

Cassandra_se_interface *get_cassandra_se();

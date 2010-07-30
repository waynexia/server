
#include "mysql_priv.h"
#include "sql_select.h"

/*
  Expression cache is used only for caching subqueries now, so its statistic
  variables we call subquery_cache*.
*/
ulonglong subquery_cache_miss, subquery_cache_hit;

Expression_cache_tmptable::Expression_cache_tmptable(THD *thd,
                                                 List<Item*> &dependants,
                                                 Item *value)
  :cache_table(NULL), table_thd(thd), list(&dependants), val(value),
   equalities(NULL), inited (0)
{
  DBUG_ENTER("Expression_cache_tmptable::Expression_cache_tmptable");
  DBUG_VOID_RETURN;
};


/**
  Build and of equalities for the expression's parameters of certain types

  @details
  If the temporary table used as an expression cache contains fields of
  certain types then it's not enough to perform a lookup into the table to
  verify that there is no row in the table for a given set of parameters.
  Additionally for those fields we have to check equalities of the form
  fld=val, where val is the value of the parameter stored in the column
  fld.
  The function generates a conjunction of all such equality predicates
  and saves a pointer to it in the field 'equalities'.

  @retval FALSE OK
  @retval TRUE  Error
*/

bool Expression_cache_tmptable::make_equalities()
{
  List<Item> args;
  List_iterator_fast<Item*> li(*list);
  Item **ref;
  DBUG_ENTER("Expression_cache_tmptable::make_equalities");

  for (uint i= 1 /* skip result filed */; (ref= li++); i++)
  {
    Field *fld= cache_table->field[i];
    /* Only some field types should be checked after lookup */
    if (fld->type() == MYSQL_TYPE_VARCHAR ||
        fld->type() == MYSQL_TYPE_TINY_BLOB ||
        fld->type() == MYSQL_TYPE_MEDIUM_BLOB ||
        fld->type() == MYSQL_TYPE_LONG_BLOB ||
        fld->type() == MYSQL_TYPE_BLOB ||
        fld->type() == MYSQL_TYPE_VAR_STRING ||
        fld->type() == MYSQL_TYPE_STRING ||
        fld->type() == MYSQL_TYPE_NEWDECIMAL ||
        fld->type() == MYSQL_TYPE_DECIMAL)
    {
      args.push_front(new Item_func_eq(*ref, new Item_field(fld)));
    }
  }
  if (args.elements == 1)
    equalities= args.head();
  else
    equalities= new Item_cond_and(args);

  DBUG_RETURN(equalities->fix_fields(table_thd, &equalities));
}


/**
  Field enumerator for TABLE::add_tmp_key

  @param arg             reference variable with current field number

  @return field number
*/

static uint field_enumerator(uchar *arg)
{
  return ((uint*)arg)[0]++;
}


/**
  Initialize temporary table and auxiliary structures for the expression
  cache

  @details
  The function creates a temporary table for the expression cache, defines
  the search index and initializes auxiliary search structures used to check
  whether a given set of of values of the expression parameters is in some
  cache entry.
*/

void Expression_cache_tmptable::init()
{
  List_iterator_fast<Item*> li(*list);
  Item_iterator_ref_list it(li);
  Item **item;
  uint field_counter;
  DBUG_ENTER("Expression_cache_tmptable::init");
  DBUG_ASSERT(!inited);
  inited= TRUE;

  if (!(ULONGLONG_MAX >> (list->elements + 1)))
  {
    DBUG_PRINT("info", ("Too many dependencies"));
    DBUG_VOID_RETURN;
  }

  cache_table= NULL;

  cache_table_param.init();
  /* dependent items and result */
  cache_table_param.field_count= list->elements + 1;
  /* postpone table creation to index description */
  cache_table_param.skip_create_table= 1;
  cache_table= NULL;

  while ((item= li++))
  {
    DBUG_ASSERT(item);
    DBUG_ASSERT(*item);
    DBUG_ASSERT((*item)->fixed);
    items.push_back((*item));
  }
  items.push_front(val);

  if (!(cache_table= create_tmp_table(table_thd, &cache_table_param,
                                      items, (ORDER*) NULL,
                                      FALSE, FALSE,
                                      ((table_thd->options |
                                        TMP_TABLE_ALL_COLUMNS) &
                                       ~(OPTION_BIG_TABLES |
                                         TMP_TABLE_FORCE_MYISAM)),
                                      HA_POS_ERROR,
                                      (char *)"subquery-cache-table")))
  {
    DBUG_PRINT("error", ("create_tmp_table failed, caching switched off"));
    DBUG_VOID_RETURN;
  }

  if (cache_table->s->db_type() != heap_hton)
  {
    DBUG_PRINT("error", ("we need only heap table"));
    goto error;
  }

  /* This list do not contain result field */
  it.open();

  field_counter=1;

  if (cache_table->alloc_keys(1) ||
      (cache_table->add_tmp_key(0, items.elements - 1,
                                &field_enumerator,
                                (uchar*)&field_counter) < 0) ||
      ref.tmp_table_index_lookup_init(table_thd, cache_table->key_info, it,
                                      TRUE))
  {
    DBUG_PRINT("error", ("creating index failed"));
    goto error;
  }
  cache_table->s->keys= 1;
  cache_table->s->uniques= 1;
  ref.null_rejecting= 1;
  ref.disable_cache= FALSE;
  ref.has_record= 0;
  ref.use_count= 0;


  if (open_tmp_table(cache_table))
  {
    DBUG_PRINT("error", ("Opening (creating) temporary table failed"));
    goto error;
  }

  if (!(cached_result= new Item_field(cache_table->field[0])))
  {
    DBUG_PRINT("error", ("Creating Item_field failed"));
    goto error;
  }

  if (make_equalities())
  {
    DBUG_PRINT("error", ("Creating equalities failed"));
    goto error;
  }

  DBUG_VOID_RETURN;

error:
  /* switch off cache */
  free_tmp_table(table_thd, cache_table);
  cache_table= NULL;
  DBUG_VOID_RETURN;
}


Expression_cache_tmptable::~Expression_cache_tmptable()
{
  if (cache_table)
    free_tmp_table(table_thd, cache_table);
}


/**
  Check if a given set of parameters of the expression is in the cache

  @param [out] value     the expression value found in the cache if any

  @details
  For a given set of the parameters of the expression the function
  checks whether it can be found in some entry of the cache. If so
  the function returns the result of the expression extracted from
  the cache.

  @retval Expression_cache::HIT if the set of parameters is in the cache
  @retval Expression_cache::MISS - otherwise
*/

Expression_cache::result Expression_cache_tmptable::check_value(Item **value)
{
  int res;
  DBUG_ENTER("Expression_cache_tmptable::check_value");

  /*
    We defer cache initialization to get item references that are
    used at the execution phase.
  */
  if (!inited)
    init();

  if (cache_table)
  {
    DBUG_PRINT("info", ("status: %u  has_record %u",
                        (uint)cache_table->status, (uint)ref.has_record));
    if ((res= join_read_key2(table_thd, NULL, cache_table, &ref)) == 1)
      DBUG_RETURN(ERROR);
    if (res || (equalities && !equalities->val_int()))
    {
      subquery_cache_miss++;
      DBUG_RETURN(MISS);
    }

    subquery_cache_hit++;
    *value= cached_result;
    DBUG_RETURN(Expression_cache::HIT);
  }
  DBUG_RETURN(Expression_cache::MISS);
}


/**
  Put a new entry into the expression cache

  @param value     the result of the expression to be put into the cache

  @details
  The function evaluates 'value' and puts the result into the cache as the
  result of the expression for the current set of parameters.

  @retval FALSE OK
  @retval TRUE  Error
*/

my_bool Expression_cache_tmptable::put_value(Item *value)
{
  int error;
  DBUG_ENTER("Expression_cache_tmptable::put_value");
  DBUG_ASSERT(inited);

  if (!cache_table)
  {
    DBUG_PRINT("info", ("No table so behave as we successfully put value"));
    DBUG_RETURN(FALSE);
  }

  *(items.head_ref())= value;
  fill_record(table_thd, cache_table->field, items, TRUE, TRUE);
  if (table_thd->is_error())
    goto err;;

  if ((error= cache_table->file->ha_write_row(cache_table->record[0])))
  {
    /* create_myisam_from_heap will generate error if needed */
    if (cache_table->file->is_fatal_error(error, HA_CHECK_DUP) &&
        create_internal_tmp_table_from_heap(table_thd, cache_table,
                                            cache_table_param.start_recinfo,
                                            &cache_table_param.recinfo,
                                            error, 1))
      goto err;
  }
  cache_table->status= 0; /* cache_table->record contains an existed record */
  ref.has_record= TRUE; /* the same as above */
  DBUG_PRINT("info", ("has_record: TRUE  status: 0"));

  DBUG_RETURN(FALSE);

err:
  free_tmp_table(table_thd, cache_table);
  cache_table= NULL;
  DBUG_RETURN(TRUE);
}

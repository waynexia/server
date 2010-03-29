/* Copyright (C) 2000 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/**
  @file

  @brief
  subselect Item

  @todo
    - add function from mysql_select that use JOIN* as parameter to JOIN
    methods (sql_select.h/sql_select.cc)
*/

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation				// gcc: Class implementation
#endif

#include "mysql_priv.h"
#include "sql_select.h"


Item_subselect::Item_subselect():
  Item_result_field(), value_assigned(0), thd(0), substitution(0),
  engine(0), old_engine(0), used_tables_cache(0), have_to_be_excluded(0),
  const_item_cache(1), 
  inside_first_fix_fields(0), done_first_fix_fields(FALSE),
  eliminated(FALSE), 
  engine_changed(0), changed(0), is_correlated(FALSE)
{
  with_subselect= 1;
  reset();
  /*
    Item value is NULL if select_result_interceptor didn't change this value
    (i.e. some rows will be found returned)
  */
  null_value= 1;
}


void Item_subselect::init(st_select_lex *select_lex,
			  select_result_interceptor *result)
{
  /*
    Please see Item_singlerow_subselect::invalidate_and_restore_select_lex(),
    which depends on alterations to the parse tree implemented here.
  */

  DBUG_ENTER("Item_subselect::init");
  DBUG_PRINT("enter", ("select_lex: 0x%lx", (long) select_lex));
  unit= select_lex->master_unit();

  if (unit->item)
  {
    /*
      Item can be changed in JOIN::prepare while engine in JOIN::optimize
      => we do not copy old_engine here
    */
    engine= unit->item->engine;
    parsing_place= unit->item->parsing_place;
    unit->item->engine= 0;
    unit->item= this;
    engine->change_result(this, result);
  }
  else
  {
    SELECT_LEX *outer_select= unit->outer_select();
    /*
      do not take into account expression inside aggregate functions because
      they can access original table fields
    */
    parsing_place= (outer_select->in_sum_expr ?
                    NO_MATTER :
                    outer_select->parsing_place);
    if (unit->is_union())
      engine= new subselect_union_engine(unit, result, this);
    else
      engine= new subselect_single_select_engine(select_lex, result, this);
  }
  {
    SELECT_LEX *upper= unit->outer_select();
    if (upper->parsing_place == IN_HAVING)
      upper->subquery_in_having= 1;
  }
  DBUG_VOID_RETURN;
}

st_select_lex *
Item_subselect::get_select_lex()
{
  return unit->first_select();
}

void Item_subselect::cleanup()
{
  DBUG_ENTER("Item_subselect::cleanup");
  Item_result_field::cleanup();
  if (old_engine)
  {
    if (engine)
      engine->cleanup();
    engine= old_engine;
    old_engine= 0;
  }
  if (engine)
    engine->cleanup();
  reset();
  value_assigned= 0;
  DBUG_VOID_RETURN;
}

void Item_singlerow_subselect::cleanup()
{
  DBUG_ENTER("Item_singlerow_subselect::cleanup");
  value= 0; row= 0;
  Item_subselect::cleanup();
  DBUG_VOID_RETURN;
}


void Item_in_subselect::cleanup()
{
  DBUG_ENTER("Item_in_subselect::cleanup");
  if (left_expr_cache)
  {
    left_expr_cache->delete_elements();
    delete left_expr_cache;
    left_expr_cache= NULL;
  }
  first_execution= TRUE;
  is_constant= FALSE;
  Item_subselect::cleanup();
  DBUG_VOID_RETURN;
}

Item_subselect::~Item_subselect()
{
  delete engine;
}

Item_subselect::trans_res
Item_subselect::select_transformer(JOIN *join)
{
  DBUG_ENTER("Item_subselect::select_transformer");
  DBUG_RETURN(RES_OK);
}


bool Item_subselect::fix_fields(THD *thd_param, Item **ref)
{
  char const *save_where= thd_param->where;
  uint8 uncacheable;
  bool res;

  DBUG_ASSERT(fixed == 0);
  engine->set_thd((thd= thd_param));
  if (!done_first_fix_fields)
  {
    done_first_fix_fields= TRUE;
    inside_first_fix_fields= TRUE;
    upper_refs.empty();
    /*
      psergey-todo: remove _first_fix_fields calls, we need changes on every
      execution
    */
  }

  eliminated= FALSE;
  parent_select= thd_param->lex->current_select;

  if (check_stack_overrun(thd, STACK_MIN_SIZE, (uchar*)&res))
    return TRUE;
  

  if (!(res= engine->prepare()))
  {
    // all transformation is done (used by prepared statements)
    changed= 1;
  inside_first_fix_fields= FALSE;


    // all transformation is done (used by prepared statements)
    changed= 1;

    /*
      Substitute the current item with an Item_in_optimizer that was
      created by Item_in_subselect::select_in_like_transformer and
      call fix_fields for the substituted item which in turn calls
      engine->prepare for the subquery predicate.
    */
    if (substitution)
    {
      // did we changed top item of WHERE condition
      if (unit->outer_select()->where == (*ref))
	unit->outer_select()->where= substitution; // correct WHERE for PS
      else if (unit->outer_select()->having == (*ref))
	unit->outer_select()->having= substitution; // correct HAVING for PS

      (*ref)= substitution;
      substitution->name= name;
      if (have_to_be_excluded)
	engine->exclude();
      substitution= 0;
      thd->where= "checking transformed subquery";
      if (!(*ref)->fixed)
	res= (*ref)->fix_fields(thd, ref);
      goto end;
//psergey-merge:  done_first_fix_fields= FALSE;
    }
    // Is it one field subselect?
    if (engine->cols() > max_columns)
    {
      my_error(ER_OPERAND_COLUMNS, MYF(0), 1);
//psergey-merge:  done_first_fix_fields= FALSE;
      goto end;
    }
    fix_length_and_dec();
  }
  else
    goto end;
  
  if ((uncacheable= engine->uncacheable()))
  {
    const_item_cache= 0;
    if (uncacheable & UNCACHEABLE_RAND)
      used_tables_cache|= RAND_TABLE_BIT;
  }
  fixed= 1;

end:
  done_first_fix_fields= FALSE;
  thd->where= save_where;
  return res;
}


bool Item_subselect::enumerate_field_refs_processor(uchar *arg)
{
  List_iterator<Ref_to_outside> it(upper_refs);
  Ref_to_outside *upper;
  
  while ((upper= it++))
  {
    if (upper->item->walk(&Item::enumerate_field_refs_processor, FALSE, arg))
      return TRUE;
  }
  return FALSE;
}

bool Item_subselect::mark_as_eliminated_processor(uchar *arg)
{
  eliminated= TRUE;
  return FALSE;
}


bool Item_subselect::mark_as_dependent(THD *thd, st_select_lex *select, 
                                       Item *item)
{
  if (inside_first_fix_fields)
  {
    is_correlated= TRUE;
    Ref_to_outside *upper;
    if (!(upper= new (thd->stmt_arena->mem_root) Ref_to_outside()))
      return TRUE;
    upper->select= select;
    upper->item= item;
    if (upper_refs.push_back(upper, thd->stmt_arena->mem_root))
      return TRUE;
  }
  return FALSE;
}


/*
  Adjust attributes after our parent select has been merged into grandparent

  DESCRIPTION
    Subquery is a composite object which may be correlated, that is, it may
    have
    1. references to tables of the parent select (i.e. one that has the clause
      with the subquery predicate)
    2. references to tables of the grandparent select
    3. references to tables of further ancestors.
    
    Before the pullout, this item indicates:
    - #1 with table bits in used_tables()
    - #2 and #3 with OUTER_REF_TABLE_BIT.

    After parent has been merged with grandparent:
    - references to parent and grandparent tables should be indicated with 
      table bits.
    - references to greatgrandparent and further ancestors - with
      OUTER_REF_TABLE_BIT.
*/

void Item_subselect::fix_after_pullout(st_select_lex *new_parent, Item **ref)
{
  recalc_used_tables(new_parent, TRUE);
  parent_select= new_parent;
}


class Field_fixer: public Field_enumerator
{
public:
  table_map used_tables; /* Collect used_tables here */
  st_select_lex *new_parent; /* Select we're in */
  virtual void visit_field(Item_field *item)
  {
    //for (TABLE_LIST *tbl= new_parent->leaf_tables; tbl; tbl= tbl->next_local)
    //{
    //  if (tbl->table == field->table)
    //  {
        used_tables|= item->field->table->map;
    //    return;
    //  }
    //}
    //used_tables |= OUTER_REF_TABLE_BIT;
  }
};


/*
  Recalculate used_tables_cache 
*/

void Item_subselect::recalc_used_tables(st_select_lex *new_parent, 
                                        bool after_pullout)
{
  List_iterator<Ref_to_outside> it(upper_refs);
  Ref_to_outside *upper;
  
  used_tables_cache= 0;
  while ((upper= it++))
  {
    bool found= FALSE;
    /*
      Check if
        1. the upper reference refers to the new immediate parent select, or
        2. one of the further ancestors.

      We rely on the fact that the tree of selects is modified by some kind of
      'flattening', i.e. a process where child selects are merged into their
      parents.
      The merged selects are removed from the select tree but keep pointers to
      their parents.
    */
    for (st_select_lex *sel= upper->select; sel; sel= sel->outer_select())
    {
      /* 
        If we've reached the new parent select by walking upwards from
        reference's original select, this means that the reference is now 
        referring to the direct parent:
      */
      if (sel == new_parent)
      {
        found= TRUE;
        /* 
          upper->item may be NULL when we've referred to a grouping function,
          in which case we don't care about what it's table_map really is,
          because item->with_sum_func==1 will ensure correct placement of the
          item.
        */
        if (upper->item)
        {
          // Now, iterate over fields and collect used_tables() attribute:
          Field_fixer fixer;
          fixer.used_tables= 0;
          fixer.new_parent= new_parent;
          upper->item->walk(&Item::enumerate_field_refs_processor, FALSE,
                            (uchar*)&fixer);
          used_tables_cache |= fixer.used_tables;
          /*
          if (after_pullout)
            upper->item->fix_after_pullout(new_parent, &(upper->item));
          upper->item->update_used_tables();
          used_tables_cache |= upper->item->used_tables();
          */
        }
      }
    }
    if (!found)
      used_tables_cache|= OUTER_REF_TABLE_BIT;
  }
  /* 
    Don't update const_tables_cache yet as we don't yet know which of the
    parent's tables are constant. Parent will call update_used_tables() after
    he has done const table detection, and that will be our chance to update
    const_tables_cache.
  */
}

bool Item_subselect::walk(Item_processor processor, bool walk_subquery,
                          uchar *argument)
{

  if (walk_subquery)
  {
    for (SELECT_LEX *lex= unit->first_select(); lex; lex= lex->next_select())
    {
      List_iterator<Item> li(lex->item_list);
      Item *item;
      ORDER *order;

      if (lex->where && (lex->where)->walk(processor, walk_subquery, argument))
        return 1;
      if (lex->having && (lex->having)->walk(processor, walk_subquery,
                                             argument))
        return 1;
      /* TODO: why does this walk WHERE/HAVING but not ON expressions of outer joins? */

      while ((item=li++))
      {
        if (item->walk(processor, walk_subquery, argument))
          return 1;
      }
      for (order= (ORDER*) lex->order_list.first ; order; order= order->next)
      {
        if ((*order->item)->walk(processor, walk_subquery, argument))
          return 1;
      }
      for (order= (ORDER*) lex->group_list.first ; order; order= order->next)
      {
        if ((*order->item)->walk(processor, walk_subquery, argument))
          return 1;
      }
    }
  }
  return (this->*processor)(argument);
}


bool Item_subselect::exec()
{
  int res;

  if (thd->is_error())
  {
    /* Do not execute subselect in case of a fatal error */
    return 1;
  }
  /*
    Simulate a failure in sub-query execution. Used to test e.g.
    out of memory or query being killed conditions.
  */
  DBUG_EXECUTE_IF("subselect_exec_fail", return 1;);

  res= engine->exec();

  if (engine_changed)
  {
    engine_changed= 0;
    return exec();
  }
  return (res);
}


/*
  Compute the IN predicate if the left operand's cache changed.
*/

bool Item_in_subselect::exec()
{
  DBUG_ENTER("Item_in_subselect::exec");
  /*
    Initialize the cache of the left predicate operand. This has to be done as
    late as now, because Cached_item directly contains a resolved field (not
    an item, and in some cases (when temp tables are created), these fields
    end up pointing to the wrong field. One solution is to change Cached_item
    to not resolve its field upon creation, but to resolve it dynamically
    from a given Item_ref object.
    TODO: the cache should be applied conditionally based on:
    - rules - e.g. only if the left operand is known to be ordered, and/or
    - on a cost-based basis, that takes into account the cost of a cache
      lookup, the cache hit rate, and the savings per cache hit.
  */
  if (!left_expr_cache && exec_method == MATERIALIZATION)
    init_left_expr_cache();

  /*
    If the new left operand is already in the cache, reuse the old result.
    Use the cached result only if this is not the first execution of IN
    because the cache is not valid for the first execution.
  */
  if (!first_execution && left_expr_cache &&
      test_if_item_cache_changed(*left_expr_cache) < 0)
    DBUG_RETURN(FALSE);

  /*
    The exec() method below updates item::value, and item::null_value, thus if
    we don't call it, the next call to item::val_int() will return whatever
    result was computed by its previous call.
  */
  DBUG_RETURN(Item_subselect::exec());
}


Item::Type Item_subselect::type() const
{
  return SUBSELECT_ITEM;
}


void Item_subselect::fix_length_and_dec()
{
  engine->fix_length_and_dec(0);
}


table_map Item_subselect::used_tables() const
{
  return (table_map) (engine->uncacheable() ? used_tables_cache : 0L);
}


bool Item_subselect::const_item() const
{
  return const_item_cache;
}

Item *Item_subselect::get_tmp_table_item(THD *thd_arg)
{
  if (!with_sum_func && !const_item())
    return new Item_field(result_field);
  return copy_or_same(thd_arg);
}

void Item_subselect::update_used_tables()
{
  recalc_used_tables(parent_select, FALSE);
  if (!engine->uncacheable())
  {
    // did all used tables become static?
    if (!(used_tables_cache & ~engine->upper_select_const_tables()))
      const_item_cache= 1;
  }
}


void Item_subselect::print(String *str, enum_query_type query_type)
{
  if (engine)
  {
    str->append('(');
    engine->print(str, query_type);
    str->append(')');
  }
  else
    str->append("(...)");
}


Item_singlerow_subselect::Item_singlerow_subselect(st_select_lex *select_lex)
  :Item_subselect(), value(0)
{
  DBUG_ENTER("Item_singlerow_subselect::Item_singlerow_subselect");
  init(select_lex, new select_singlerow_subselect(this));
  maybe_null= 1;
  max_columns= UINT_MAX;
  DBUG_VOID_RETURN;
}

st_select_lex *
Item_singlerow_subselect::invalidate_and_restore_select_lex()
{
  DBUG_ENTER("Item_singlerow_subselect::invalidate_and_restore_select_lex");
  st_select_lex *result= get_select_lex();

  DBUG_ASSERT(result);

  /*
    This code restore the parse tree in it's state before the execution of
    Item_singlerow_subselect::Item_singlerow_subselect(),
    and in particular decouples this object from the SELECT_LEX,
    so that the SELECT_LEX can be used with a different flavor
    or Item_subselect instead, as part of query rewriting.
  */
  unit->item= NULL;

  DBUG_RETURN(result);
}

Item_maxmin_subselect::Item_maxmin_subselect(THD *thd_param,
                                             Item_subselect *parent,
					     st_select_lex *select_lex,
					     bool max_arg)
  :Item_singlerow_subselect(), was_values(TRUE)
{
  DBUG_ENTER("Item_maxmin_subselect::Item_maxmin_subselect");
  max= max_arg;
  init(select_lex, new select_max_min_finder_subselect(this, max_arg));
  max_columns= 1;
  maybe_null= 1;
  max_columns= 1;

  /*
    Following information was collected during performing fix_fields()
    of Items belonged to subquery, which will be not repeated
  */
  used_tables_cache= parent->get_used_tables_cache();
  const_item_cache= parent->get_const_item_cache();

  /*
    this subquery always creates during preparation, so we can assign
    thd here
  */
  thd= thd_param;

  DBUG_VOID_RETURN;
}

void Item_maxmin_subselect::cleanup()
{
  DBUG_ENTER("Item_maxmin_subselect::cleanup");
  Item_singlerow_subselect::cleanup();

  /*
    By default it is TRUE to avoid TRUE reporting by
    Item_func_not_all/Item_func_nop_all if this item was never called.

    Engine exec() set it to FALSE by reset_value_registration() call.
    select_max_min_finder_subselect::send_data() set it back to TRUE if some
    value will be found.
  */
  was_values= TRUE;
  DBUG_VOID_RETURN;
}


void Item_maxmin_subselect::print(String *str, enum_query_type query_type)
{
  str->append(max?"<max>":"<min>", 5);
  Item_singlerow_subselect::print(str, query_type);
}


void Item_singlerow_subselect::reset()
{
  eliminated= FALSE;
  null_value= 1;
  if (value)
    value->null_value= 1;
}


/**
  @todo
  - We cant change name of Item_field or Item_ref, because it will
  prevent it's correct resolving, but we should save name of
  removed item => we do not make optimization if top item of
  list is field or reference.
  - switch off this optimization for prepare statement,
  because we do not rollback this changes.
  Make rollback for it, or special name resolving mode in 5.0.
*/
Item_subselect::trans_res
Item_singlerow_subselect::select_transformer(JOIN *join)
{
  DBUG_ENTER("Item_singlerow_subselect::select_transformer");
  if (changed)
    DBUG_RETURN(RES_OK);

  SELECT_LEX *select_lex= join->select_lex;
  Query_arena *arena= thd->stmt_arena;
 
  if (!select_lex->master_unit()->is_union() &&
      !select_lex->table_list.elements &&
      select_lex->item_list.elements == 1 &&
      !select_lex->item_list.head()->with_sum_func &&
      /*
	We cant change name of Item_field or Item_ref, because it will
	prevent it's correct resolving, but we should save name of
	removed item => we do not make optimization if top item of
	list is field or reference.
	TODO: solve above problem
      */
      !(select_lex->item_list.head()->type() == FIELD_ITEM ||
	select_lex->item_list.head()->type() == REF_ITEM) &&
      !join->conds && !join->having &&
      /*
        switch off this optimization for prepare statement,
        because we do not rollback this changes
        TODO: make rollback for it, or special name resolving mode in 5.0.
      */
      !arena->is_stmt_prepare_or_first_sp_execute()
      )
  {

    have_to_be_excluded= 1;
    if (thd->lex->describe)
    {
      char warn_buff[MYSQL_ERRMSG_SIZE];
      sprintf(warn_buff, ER(ER_SELECT_REDUCED), select_lex->select_number);
      push_warning(thd, MYSQL_ERROR::WARN_LEVEL_NOTE,
		   ER_SELECT_REDUCED, warn_buff);
    }
    substitution= select_lex->item_list.head();
    /*
      as far as we moved content to upper level, field which depend of
      'upper' select is not really dependent => we remove this dependence
    */
    substitution->walk(&Item::remove_dependence_processor, 0,
		       (uchar *) select_lex->outer_select());
    DBUG_RETURN(RES_REDUCE);
  }
  DBUG_RETURN(RES_OK);
}


void Item_singlerow_subselect::store(uint i, Item *item)
{
  row[i]->store(item);
  //psergey-merge: can do without that: row[i]->cache_value();
  //psergey-backport-timours: ^ really, without that ^ 
  //psergey-try-merge-again:
  row[i]->cache_value();
}

enum Item_result Item_singlerow_subselect::result_type() const
{
  return engine->type();
}

/* 
 Don't rely on the result type to calculate field type. 
 Ask the engine instead.
*/
enum_field_types Item_singlerow_subselect::field_type() const
{
  return engine->field_type();
}

void Item_singlerow_subselect::fix_length_and_dec()
{
  if ((max_columns= engine->cols()) == 1)
  {
    engine->fix_length_and_dec(row= &value);
  }
  else
  {
    if (!(row= (Item_cache**) sql_alloc(sizeof(Item_cache*)*max_columns)))
      return;
    engine->fix_length_and_dec(row);
    value= *row;
  }
  unsigned_flag= value->unsigned_flag;
  /*
    If there are not tables in subquery then ability to have NULL value
    depends on SELECT list (if single row subquery have tables then it
    always can be NULL if there are not records fetched).
  */
  if (engine->no_tables())
    maybe_null= engine->may_be_null();
}

uint Item_singlerow_subselect::cols()
{
  return engine->cols();
}

bool Item_singlerow_subselect::check_cols(uint c)
{
  if (c != engine->cols())
  {
    my_error(ER_OPERAND_COLUMNS, MYF(0), c);
    return 1;
  }
  return 0;
}

bool Item_singlerow_subselect::null_inside()
{
  for (uint i= 0; i < max_columns ; i++)
  {
    if (row[i]->null_value)
      return 1;
  }
  return 0;
}

void Item_singlerow_subselect::bring_value()
{
  exec();
}

double Item_singlerow_subselect::val_real()
{
  DBUG_ASSERT(fixed == 1);
  if (!exec() && !value->null_value)
  {
    null_value= 0;
    return value->val_real();
  }
  else
  {
    reset();
    return 0;
  }
}

longlong Item_singlerow_subselect::val_int()
{
  DBUG_ASSERT(fixed == 1);
  if (!exec() && !value->null_value)
  {
    null_value= 0;
    return value->val_int();
  }
  else
  {
    reset();
    return 0;
  }
}

String *Item_singlerow_subselect::val_str(String *str)
{
  if (!exec() && !value->null_value)
  {
    null_value= 0;
    return value->val_str(str);
  }
  else
  {
    reset();
    return 0;
  }
}


my_decimal *Item_singlerow_subselect::val_decimal(my_decimal *decimal_value)
{
  if (!exec() && !value->null_value)
  {
    null_value= 0;
    return value->val_decimal(decimal_value);
  }
  else
  {
    reset();
    return 0;
  }
}


bool Item_singlerow_subselect::val_bool()
{
  if (!exec() && !value->null_value)
  {
    null_value= 0;
    return value->val_bool();
  }
  else
  {
    reset();
    return 0;
  }
}


Item_exists_subselect::Item_exists_subselect(st_select_lex *select_lex):
  Item_subselect()
{
  DBUG_ENTER("Item_exists_subselect::Item_exists_subselect");
  bool val_bool();
  init(select_lex, new select_exists_subselect(this));
  max_columns= UINT_MAX;
  null_value= 0; //can't be NULL
  maybe_null= 0; //can't be NULL
  value= 0;
  DBUG_VOID_RETURN;
}


void Item_exists_subselect::print(String *str, enum_query_type query_type)
{
  str->append(STRING_WITH_LEN("exists"));
  Item_subselect::print(str, query_type);
}


bool Item_in_subselect::test_limit(st_select_lex_unit *unit_arg)
{
  if (unit_arg->fake_select_lex &&
      unit_arg->fake_select_lex->test_limit())
    return(1);

  SELECT_LEX *sl= unit_arg->first_select();
  for (; sl; sl= sl->next_select())
  {
    if (sl->test_limit())
      return(1);
  }
  return(0);
}

Item_in_subselect::Item_in_subselect(Item * left_exp,
				     st_select_lex *select_lex):
  Item_exists_subselect(), left_expr_cache(0), first_execution(TRUE),
  is_constant(FALSE), optimizer(0), pushed_cond_guards(NULL),
  exec_method(NOT_TRANSFORMED), upper_item(0)
{
  DBUG_ENTER("Item_in_subselect::Item_in_subselect");
  left_expr= left_exp;
  init(select_lex, new select_exists_subselect(this));
  max_columns= UINT_MAX;
  maybe_null= 1;
  abort_on_null= 0;
  reset();
  //if test_limit will fail then error will be reported to client
  test_limit(select_lex->master_unit());
  DBUG_VOID_RETURN;
}

int Item_in_subselect::get_identifier()
{
  return engine->get_identifier();
}

Item_allany_subselect::Item_allany_subselect(Item * left_exp,
                                             chooser_compare_func_creator fc,
					     st_select_lex *select_lex,
					     bool all_arg)
  :Item_in_subselect(), func_creator(fc), all(all_arg)
{
  DBUG_ENTER("Item_allany_subselect::Item_allany_subselect");
  left_expr= left_exp;
  func= func_creator(all_arg);
  init(select_lex, new select_exists_subselect(this));
  max_columns= 1;
  abort_on_null= 0;
  reset();
  //if test_limit will fail then error will be reported to client
  test_limit(select_lex->master_unit());
  DBUG_VOID_RETURN;
}


void Item_exists_subselect::fix_length_and_dec()
{
   decimals= 0;
   max_length= 1;
   max_columns= engine->cols();
  /* We need only 1 row to determine existence */
  unit->global_parameters->select_limit= new Item_int((int32) 1);
}

double Item_exists_subselect::val_real()
{
  DBUG_ASSERT(fixed == 1);
  if (exec())
  {
    reset();
    return 0;
  }
  return (double) value;
}

longlong Item_exists_subselect::val_int()
{
  DBUG_ASSERT(fixed == 1);
  if (exec())
  {
    reset();
    return 0;
  }
  return value;
}


/**
  Return the result of EXISTS as a string value

  Converts the true/false result into a string value.
  Note that currently this cannot be NULL, so if the query exection fails
  it will return 0.

  @param decimal_value[out]    buffer to hold the resulting string value
  @retval                      Pointer to the converted string.
                               Can't be a NULL pointer, as currently
                               EXISTS cannot return NULL.
*/

String *Item_exists_subselect::val_str(String *str)
{
  DBUG_ASSERT(fixed == 1);
  if (exec())
    reset();
  str->set((ulonglong)value,&my_charset_bin);
  return str;
}


/**
  Return the result of EXISTS as a decimal value

  Converts the true/false result into a decimal value.
  Note that currently this cannot be NULL, so if the query exection fails
  it will return 0.

  @param decimal_value[out]    Buffer to hold the resulting decimal value
  @retval                      Pointer to the converted decimal.
                               Can't be a NULL pointer, as currently
                               EXISTS cannot return NULL.
*/

my_decimal *Item_exists_subselect::val_decimal(my_decimal *decimal_value)
{
  DBUG_ASSERT(fixed == 1);
  if (exec())
    reset();
  int2my_decimal(E_DEC_FATAL_ERROR, value, 0, decimal_value);
  return decimal_value;
}


bool Item_exists_subselect::val_bool()
{
  DBUG_ASSERT(fixed == 1);
  if (exec())
  {
    reset();
    return 0;
  }
  return value != 0;
}


double Item_in_subselect::val_real()
{
  /*
    As far as Item_in_subselect called only from Item_in_optimizer this
    method should not be used
  */
  DBUG_ASSERT(0);
  DBUG_ASSERT(fixed == 1);
  null_value= 0;
  if (exec())
  {
    reset();
    null_value= 1;
    return 0;
  }
  if (was_null && !value)
    null_value= 1;
  return (double) value;
}


longlong Item_in_subselect::val_int()
{
  /*
    As far as Item_in_subselect called only from Item_in_optimizer this
    method should not be used
  */
  DBUG_ASSERT(0);
  DBUG_ASSERT(fixed == 1);
  null_value= 0;
  if (exec())
  {
    reset();
    null_value= 1;
    return 0;
  }
  if (was_null && !value)
    null_value= 1;
  return value;
}


String *Item_in_subselect::val_str(String *str)
{
  /*
    As far as Item_in_subselect called only from Item_in_optimizer this
    method should not be used
  */
  DBUG_ASSERT(0);
  DBUG_ASSERT(fixed == 1);
  null_value= 0;
  if (exec())
  {
    reset();
    null_value= 1;
    return 0;
  }
  if (was_null && !value)
  {
    null_value= 1;
    return 0;
  }
  str->set((ulonglong)value, &my_charset_bin);
  return str;
}


bool Item_in_subselect::val_bool()
{
  DBUG_ASSERT(fixed == 1);
  null_value= 0;
  if (is_constant)
    return value;
  if (exec())
  {
    reset();
    /* 
      Must mark the IN predicate as NULL so as to make sure an enclosing NOT
      predicate will return FALSE. See the comments in 
      subselect_uniquesubquery_engine::copy_ref_key for further details.
    */
    null_value= 1;
    return 0;
  }
  if (was_null && !value)
    null_value= 1;
  return value;
}

my_decimal *Item_in_subselect::val_decimal(my_decimal *decimal_value)
{
  /*
    As far as Item_in_subselect called only from Item_in_optimizer this
    method should not be used
  */
  DBUG_ASSERT(0);
  null_value= 0;
  DBUG_ASSERT(fixed == 1);
  if (exec())
  {
    reset();
    null_value= 1;
    return 0;
  }
  if (was_null && !value)
    null_value= 1;
  int2my_decimal(E_DEC_FATAL_ERROR, value, 0, decimal_value);
  return decimal_value;
}


/* 
  Rewrite a single-column IN/ALL/ANY subselect

  SYNOPSIS
    Item_in_subselect::single_value_transformer()
      join  Join object of the subquery (i.e. 'child' join).
      func  Subquery comparison creator

  DESCRIPTION
    Rewrite a single-column subquery using rule-based approach. The subquery
    
       oe $cmp$ (SELECT ie FROM ... WHERE subq_where ... HAVING subq_having)
    
    First, try to convert the subquery to scalar-result subquery in one of
    the forms:
    
       - oe $cmp$ (SELECT MAX(...) )  // handled by Item_singlerow_subselect
       - oe $cmp$ <max>(SELECT ...)   // handled by Item_maxmin_subselect
   
    If that fails, the subquery will be handled with class Item_in_optimizer, 
    Inject the predicates into subquery, i.e. convert it to:

    - If the subquery has aggregates, GROUP BY, or HAVING, convert to

       SELECT ie FROM ...  HAVING subq_having AND 
                                   trigcond(oe $cmp$ ref_or_null_helper<ie>)
                                   
      the addition is wrapped into trigger only when we want to distinguish
      between NULL and FALSE results.

    - Otherwise (no aggregates/GROUP BY/HAVING) convert it to one of the
      following:

      = If we don't need to distinguish between NULL and FALSE subquery:
        
        SELECT 1 FROM ... WHERE (oe $cmp$ ie) AND subq_where

      = If we need to distinguish between those:

        SELECT 1 FROM ...
          WHERE  subq_where AND trigcond((oe $cmp$ ie) OR (ie IS NULL))
          HAVING trigcond(<is_not_null_test>(ie))

  RETURN
    RES_OK     Either subquery was transformed, or appopriate
                       predicates where injected into it.
    RES_REDUCE The subquery was reduced to non-subquery
    RES_ERROR  Error
*/

Item_subselect::trans_res
Item_in_subselect::single_value_transformer(JOIN *join,
					    Comp_creator *func)
{
  SELECT_LEX *select_lex= join->select_lex;
  DBUG_ENTER("Item_in_subselect::single_value_transformer");

  /*
    Check that the right part of the subselect contains no more than one
    column. E.g. in SELECT 1 IN (SELECT * ..) the right part is (SELECT * ...)
  */
  // psergey: duplicated_subselect_card_check
  if (select_lex->item_list.elements > 1)
  {
    my_error(ER_OPERAND_COLUMNS, MYF(0), 1);
    DBUG_RETURN(RES_ERROR);
  }

  /*
    If this is an ALL/ANY single-value subselect, try to rewrite it with
    a MIN/MAX subselect. We can do that if a possible NULL result of the
    subselect can be ignored.
    E.g. SELECT * FROM t1 WHERE b > ANY (SELECT a FROM t2) can be rewritten
    with SELECT * FROM t1 WHERE b > (SELECT MAX(a) FROM t2).
    We can't check that this optimization is safe if it's not a top-level
    item of the WHERE clause (e.g. because the WHERE clause can contain IS
    NULL/IS NOT NULL functions). If so, we rewrite ALL/ANY with NOT EXISTS
    later in this method.
  */
  if ((abort_on_null || (upper_item && upper_item->top_level())) &&
      !select_lex->master_unit()->uncacheable && !func->eqne_op())
  {
    if (substitution)
    {
      // It is second (third, ...) SELECT of UNION => All is done
      DBUG_RETURN(RES_OK);
    }

    Item *subs;
    if (!select_lex->group_list.elements &&
        !select_lex->having &&
	!select_lex->with_sum_func &&
	!(select_lex->next_select()) &&
        select_lex->table_list.elements)
    {
      Item_sum_hybrid *item;
      nesting_map save_allow_sum_func;
      if (func->l_op())
      {
	/*
	  (ALL && (> || =>)) || (ANY && (< || =<))
	  for ALL condition is inverted
	*/
	item= new Item_sum_max(*select_lex->ref_pointer_array);
      }
      else
      {
	/*
	  (ALL && (< || =<)) || (ANY && (> || =>))
	  for ALL condition is inverted
	*/
	item= new Item_sum_min(*select_lex->ref_pointer_array);
      }
      if (upper_item)
        upper_item->set_sum_test(item);
      *select_lex->ref_pointer_array= item;
      {
	List_iterator<Item> it(select_lex->item_list);
	it++;
	it.replace(item);
      }

      save_allow_sum_func= thd->lex->allow_sum_func;
      thd->lex->allow_sum_func|= 1 << thd->lex->current_select->nest_level;
      /*
	Item_sum_(max|min) can't substitute other item => we can use 0 as
        reference, also Item_sum_(max|min) can't be fixed after creation, so
        we do not check item->fixed
      */
      if (item->fix_fields(thd, 0))
	DBUG_RETURN(RES_ERROR);
      thd->lex->allow_sum_func= save_allow_sum_func; 
      /* we added aggregate function => we have to change statistic */
      count_field_types(select_lex, &join->tmp_table_param, join->all_fields, 
                        0);

      subs= new Item_singlerow_subselect(select_lex);
    }
    else
    {
      Item_maxmin_subselect *item;
      subs= item= new Item_maxmin_subselect(thd, this, select_lex, func->l_op());
      if (upper_item)
        upper_item->set_sub_test(item);
    }
    /* fix fields is already called for  left expression */
    substitution= func->create(left_expr, subs);
    DBUG_RETURN(RES_OK);
  }

  if (!substitution)
  {
    /* We're invoked for the 1st (or the only) SELECT in the subquery UNION */
    SELECT_LEX_UNIT *master_unit= select_lex->master_unit();
    substitution= optimizer;

    SELECT_LEX *current= thd->lex->current_select;

    thd->lex->current_select= current->return_after_parsing();
    //optimizer never use Item **ref => we can pass 0 as parameter
    if (!optimizer || optimizer->fix_left(thd, 0))
    {
      thd->lex->current_select= current;
      DBUG_RETURN(RES_ERROR);
    }
    thd->lex->current_select= current;

    /* We will refer to upper level cache array => we have to save it for SP */
    optimizer->keep_top_level_cache();

    /*
      As far as  Item_ref_in_optimizer do not substitute itself on fix_fields
      we can use same item for all selects.
    */
    expr= new Item_direct_ref(&select_lex->context,
                              (Item**)optimizer->get_cache(),
			      (char *)"<no matter>",
			      (char *)in_left_expr_name);

    master_unit->uncacheable|= UNCACHEABLE_DEPENDENT;
    //psergey: placed then removed: select_lex->uncacheable|= UNCACHEABLE_DEPENDENT;
  }

  if (!abort_on_null && left_expr->maybe_null && !pushed_cond_guards)
  {
    if (!(pushed_cond_guards= (bool*)join->thd->alloc(sizeof(bool))))
      DBUG_RETURN(RES_ERROR);
    pushed_cond_guards[0]= TRUE;
  }

  /*
    If this IN predicate can be computed via materialization, do not
    perform the IN -> EXISTS transformation.
  */
  if (exec_method == MATERIALIZATION)
    DBUG_RETURN(RES_OK);

  /* Perform the IN=>EXISTS transformation. */
  DBUG_RETURN(single_value_in_to_exists_transformer(join, func));
}


/**
  Transofrm an IN predicate into EXISTS via predicate injection.

  @details The transformation injects additional predicates into the subquery
  (and makes the subquery correlated) as follows.

  - If the subquery has aggregates, GROUP BY, or HAVING, convert to

    SELECT ie FROM ...  HAVING subq_having AND 
                               trigcond(oe $cmp$ ref_or_null_helper<ie>)
                                   
    the addition is wrapped into trigger only when we want to distinguish
    between NULL and FALSE results.

  - Otherwise (no aggregates/GROUP BY/HAVING) convert it to one of the
    following:

    = If we don't need to distinguish between NULL and FALSE subquery:
        
      SELECT 1 FROM ... WHERE (oe $cmp$ ie) AND subq_where

    = If we need to distinguish between those:

      SELECT 1 FROM ...
        WHERE  subq_where AND trigcond((oe $cmp$ ie) OR (ie IS NULL))
        HAVING trigcond(<is_not_null_test>(ie))

    @param join  Join object of the subquery (i.e. 'child' join).
    @param func  Subquery comparison creator

    @retval RES_OK     Either subquery was transformed, or appopriate
                       predicates where injected into it.
    @retval RES_REDUCE The subquery was reduced to non-subquery
    @retval RES_ERROR  Error
*/

Item_subselect::trans_res
Item_in_subselect::single_value_in_to_exists_transformer(JOIN * join, Comp_creator *func)
{
  SELECT_LEX *select_lex= join->select_lex;
  DBUG_ENTER("Item_in_subselect::single_value_in_to_exists_transformer");

  select_lex->uncacheable|= UNCACHEABLE_DEPENDENT;
  if (join->having || select_lex->with_sum_func ||
      select_lex->group_list.elements)
  {
    bool tmp;
    Item *item= func->create(expr,
                             new Item_ref_null_helper(&select_lex->context,
                                                      this,
                                                      select_lex->
                                                      ref_pointer_array,
                                                      (char *)"<ref>",
                                                      this->full_name()));
    if (!abort_on_null && left_expr->maybe_null)
    {
      /* 
        We can encounter "NULL IN (SELECT ...)". Wrap the added condition
        within a trig_cond.
      */
      item= new Item_func_trig_cond(item, get_cond_guard(0));
    }
    
    /*
      AND and comparison functions can't be changed during fix_fields()
      we can assign select_lex->having here, and pass 0 as last
      argument (reference) to fix_fields()
    */
    select_lex->having= join->having= and_items(join->having, item);
    if (join->having == item)
      item->name= (char*)in_having_cond;
    select_lex->having_fix_field= 1;
    /*
      we do not check join->having->fixed, because Item_and (from and_items)
      or comparison function (from func->create) can't be fixed after creation
    */
    tmp= join->having->fix_fields(thd, 0);
    select_lex->having_fix_field= 0;
    if (tmp)
      DBUG_RETURN(RES_ERROR);
  }
  else
  {
    Item *item= (Item*) select_lex->item_list.head();

    if (select_lex->table_list.elements)
    {
      bool tmp;
      Item *having= item, *orig_item= item;
      select_lex->item_list.empty();
      select_lex->item_list.push_back(new Item_int("Not_used",
                                                   (longlong) 1,
                                                   MY_INT64_NUM_DECIMAL_DIGITS));
      select_lex->ref_pointer_array[0]= select_lex->item_list.head();
       
      item= func->create(expr, item);
      if (!abort_on_null && orig_item->maybe_null)
      {
	having= new Item_is_not_null_test(this, having);
        if (left_expr->maybe_null)
        {
          if (!(having= new Item_func_trig_cond(having,
                                                get_cond_guard(0))))
            DBUG_RETURN(RES_ERROR);
        }
	/*
	  Item_is_not_null_test can't be changed during fix_fields()
	  we can assign select_lex->having here, and pass 0 as last
	  argument (reference) to fix_fields()
	*/
        having->name= (char*)in_having_cond;
	select_lex->having= join->having= having;
	select_lex->having_fix_field= 1;
        /*
          we do not check join->having->fixed, because Item_and (from
          and_items) or comparison function (from func->create) can't be
          fixed after creation
        */
	tmp= join->having->fix_fields(thd, 0);
        select_lex->having_fix_field= 0;
        if (tmp)
	  DBUG_RETURN(RES_ERROR);
	item= new Item_cond_or(item,
			       new Item_func_isnull(orig_item));
      }
      /* 
        If we may encounter NULL IN (SELECT ...) and care whether subquery
        result is NULL or FALSE, wrap condition in a trig_cond.
      */
      if (!abort_on_null && left_expr->maybe_null)
      {
        if (!(item= new Item_func_trig_cond(item, get_cond_guard(0))))
          DBUG_RETURN(RES_ERROR);
      }
      /*
        TODO: figure out why the following is done here in 
        single_value_transformer but there is no corresponding action in
        row_value_transformer?
      */
      item->name= (char *)in_additional_cond;

      /*
	AND can't be changed during fix_fields()
	we can assign select_lex->having here, and pass 0 as last
	argument (reference) to fix_fields()
      */
      select_lex->where= join->conds= and_items(join->conds, item);
      select_lex->where->top_level_item();
      /*
        we do not check join->conds->fixed, because Item_and can't be fixed
        after creation
      */
      if (join->conds->fix_fields(thd, 0))
	DBUG_RETURN(RES_ERROR);
    }
    else
    {
      bool tmp;
      if (select_lex->master_unit()->is_union())
      {
	/*
	  comparison functions can't be changed during fix_fields()
	  we can assign select_lex->having here, and pass 0 as last
	  argument (reference) to fix_fields()
	*/
        Item *new_having=
          func->create(expr,
                       new Item_ref_null_helper(&select_lex->context, this,
                                            select_lex->ref_pointer_array,
                                            (char *)"<no matter>",
                                            (char *)"<result>"));
        if (!abort_on_null && left_expr->maybe_null)
        {
          if (!(new_having= new Item_func_trig_cond(new_having,
                                                    get_cond_guard(0))))
            DBUG_RETURN(RES_ERROR);
        }
        new_having->name= (char*)in_having_cond;
	select_lex->having= join->having= new_having;
	select_lex->having_fix_field= 1;
        
        /*
          we do not check join->having->fixed, because comparison function
          (from func->create) can't be fixed after creation
        */
	tmp= join->having->fix_fields(thd, 0);
        select_lex->having_fix_field= 0;
        if (tmp)
	  DBUG_RETURN(RES_ERROR);
      }
      else
      {
	// it is single select without tables => possible optimization
        // remove the dependence mark since the item is moved to upper
        // select and is not outer anymore.
        item->walk(&Item::remove_dependence_processor, 0,
                           (uchar *) select_lex->outer_select());
	item= func->create(left_expr, item);
	// fix_field of item will be done in time of substituting
	substitution= item;
	have_to_be_excluded= 1;
	if (thd->lex->describe)
	{
	  char warn_buff[MYSQL_ERRMSG_SIZE];
	  sprintf(warn_buff, ER(ER_SELECT_REDUCED), select_lex->select_number);
	  push_warning(thd, MYSQL_ERROR::WARN_LEVEL_NOTE,
		       ER_SELECT_REDUCED, warn_buff);
	}
	DBUG_RETURN(RES_REDUCE);
      }
    }
  }

  DBUG_RETURN(RES_OK);
}


Item_subselect::trans_res
Item_in_subselect::row_value_transformer(JOIN *join)
{
  SELECT_LEX *select_lex= join->select_lex;
  uint cols_num= left_expr->cols();

  DBUG_ENTER("Item_in_subselect::row_value_transformer");

  // psergey: duplicated_subselect_card_check
  if (select_lex->item_list.elements != cols_num)
  {
    my_error(ER_OPERAND_COLUMNS, MYF(0), cols_num);
    DBUG_RETURN(RES_ERROR);
  }

  /*
    Wrap the current IN predicate in an Item_in_optimizer. The actual
    substitution in the Item tree takes place in Item_subselect::fix_fields.
  */
  if (!substitution)
  {
    //first call for this unit
    SELECT_LEX_UNIT *master_unit= select_lex->master_unit();
    substitution= optimizer;

    SELECT_LEX *current= thd->lex->current_select;
    thd->lex->current_select= current->return_after_parsing();
    //optimizer never use Item **ref => we can pass 0 as parameter
    if (!optimizer || optimizer->fix_left(thd, 0))
    {
      thd->lex->current_select= current;
      DBUG_RETURN(RES_ERROR);
    }

    // we will refer to upper level cache array => we have to save it in PS
    optimizer->keep_top_level_cache();

    thd->lex->current_select= current;
    master_unit->uncacheable|= UNCACHEABLE_DEPENDENT;

    if (!abort_on_null && left_expr->maybe_null && !pushed_cond_guards)
    {
      if (!(pushed_cond_guards= (bool*)join->thd->alloc(sizeof(bool) *
                                                        left_expr->cols())))
        DBUG_RETURN(RES_ERROR);
      for (uint i= 0; i < cols_num; i++)
        pushed_cond_guards[i]= TRUE;
    }
  }

  /*
    If this IN predicate can be computed via materialization, do not
    perform the IN -> EXISTS transformation.
  */
  if (exec_method == MATERIALIZATION)
    DBUG_RETURN(RES_OK);

  /* Perform the IN=>EXISTS transformation. */
  DBUG_RETURN(row_value_in_to_exists_transformer(join));
}


/**
  Tranform a (possibly non-correlated) IN subquery into a correlated EXISTS.

  @todo
  The IF-ELSE below can be refactored so that there is no duplication of the
  statements that create the new conditions. For this we have to invert the IF
  and the FOR statements as this:
  for (each left operand)
    create the equi-join condition
    if (is_having_used || !abort_on_null)
      create the "is null" and is_not_null_test items
    if (is_having_used)
      add the equi-join and the null tests to HAVING
    else
      add the equi-join and the "is null" to WHERE
      add the is_not_null_test to HAVING
*/

Item_subselect::trans_res
Item_in_subselect::row_value_in_to_exists_transformer(JOIN * join)
{
  SELECT_LEX *select_lex= join->select_lex;
  Item *having_item= 0;
  uint cols_num= left_expr->cols();
  bool is_having_used= (join->having || select_lex->with_sum_func ||
                        select_lex->group_list.first ||
                        !select_lex->table_list.elements);

  DBUG_ENTER("Item_in_subselect::row_value_in_to_exists_transformer");

  select_lex->uncacheable|= UNCACHEABLE_DEPENDENT;
  if (is_having_used)
  {
    /*
      (l1, l2, l3) IN (SELECT v1, v2, v3 ... HAVING having) =>
      EXISTS (SELECT ... HAVING having and
                                (l1 = v1 or is null v1) and
                                (l2 = v2 or is null v2) and
                                (l3 = v3 or is null v3) and
                                is_not_null_test(v1) and
                                is_not_null_test(v2) and
                                is_not_null_test(v3))
      where is_not_null_test used to register nulls in case if we have
      not found matching to return correct NULL value
      TODO: say here explicitly if the order of AND parts matters or not.
    */
    Item *item_having_part2= 0;
    for (uint i= 0; i < cols_num; i++)
    {
      DBUG_ASSERT((left_expr->fixed &&
                  select_lex->ref_pointer_array[i]->fixed) ||
                  (select_lex->ref_pointer_array[i]->type() == REF_ITEM &&
                   ((Item_ref*)(select_lex->ref_pointer_array[i]))->ref_type() ==
                    Item_ref::OUTER_REF));
      if (select_lex->ref_pointer_array[i]->
          check_cols(left_expr->element_index(i)->cols()))
        DBUG_RETURN(RES_ERROR);
      Item *item_eq=
        new Item_func_eq(new
                         Item_ref(&select_lex->context,
                                  (*optimizer->get_cache())->
                                  addr(i),
                                  (char *)"<no matter>",
                                  (char *)in_left_expr_name),
                         new
                         Item_ref(&select_lex->context,
                                  select_lex->ref_pointer_array + i,
                                  (char *)"<no matter>",
                                  (char *)"<list ref>")
                        );
      Item *item_isnull=
        new Item_func_isnull(new
                             Item_ref(&select_lex->context,
                                      select_lex->ref_pointer_array+i,
                                      (char *)"<no matter>",
                                      (char *)"<list ref>")
                            );
      Item *col_item= new Item_cond_or(item_eq, item_isnull);
      if (!abort_on_null && left_expr->element_index(i)->maybe_null)
      {
        if (!(col_item= new Item_func_trig_cond(col_item, get_cond_guard(i))))
          DBUG_RETURN(RES_ERROR);
      }
      having_item= and_items(having_item, col_item);
      
      Item *item_nnull_test= 
         new Item_is_not_null_test(this,
                                   new Item_ref(&select_lex->context,
                                                select_lex->
                                                ref_pointer_array + i,
                                                (char *)"<no matter>",
                                                (char *)"<list ref>"));
      if (!abort_on_null && left_expr->element_index(i)->maybe_null)
      {
        if (!(item_nnull_test= 
              new Item_func_trig_cond(item_nnull_test, get_cond_guard(i))))
          DBUG_RETURN(RES_ERROR);
      }
      item_having_part2= and_items(item_having_part2, item_nnull_test);
      item_having_part2->top_level_item();
    }
    having_item= and_items(having_item, item_having_part2);
    having_item->top_level_item();
  }
  else
  {
    /*
      (l1, l2, l3) IN (SELECT v1, v2, v3 ... WHERE where) =>
      EXISTS (SELECT ... WHERE where and
                               (l1 = v1 or is null v1) and
                               (l2 = v2 or is null v2) and
                               (l3 = v3 or is null v3)
                         HAVING is_not_null_test(v1) and
                                is_not_null_test(v2) and
                                is_not_null_test(v3))
      where is_not_null_test register NULLs values but reject rows

      in case when we do not need correct NULL, we have simplier construction:
      EXISTS (SELECT ... WHERE where and
                               (l1 = v1) and
                               (l2 = v2) and
                               (l3 = v3)
    */
    Item *where_item= 0;
    for (uint i= 0; i < cols_num; i++)
    {
      Item *item, *item_isnull;
      DBUG_ASSERT((left_expr->fixed &&
                  select_lex->ref_pointer_array[i]->fixed) ||
                  (select_lex->ref_pointer_array[i]->type() == REF_ITEM &&
                   ((Item_ref*)(select_lex->ref_pointer_array[i]))->ref_type() ==
                    Item_ref::OUTER_REF));
      if (select_lex->ref_pointer_array[i]->
          check_cols(left_expr->element_index(i)->cols()))
        DBUG_RETURN(RES_ERROR);
      item=
        new Item_func_eq(new
                         Item_direct_ref(&select_lex->context,
                                         (*optimizer->get_cache())->
                                         addr(i),
                                         (char *)"<no matter>",
                                         (char *)in_left_expr_name),
                         new
                         Item_direct_ref(&select_lex->context,
                                         select_lex->
                                         ref_pointer_array+i,
                                         (char *)"<no matter>",
                                         (char *)"<list ref>")
                        );
      if (!abort_on_null)
      {
        Item *having_col_item=
          new Item_is_not_null_test(this,
                                    new
                                    Item_ref(&select_lex->context, 
                                             select_lex->ref_pointer_array + i,
                                             (char *)"<no matter>",
                                             (char *)"<list ref>"));
        
        
        item_isnull= new
          Item_func_isnull(new
                           Item_direct_ref(&select_lex->context,
                                           select_lex->
                                           ref_pointer_array+i,
                                           (char *)"<no matter>",
                                           (char *)"<list ref>")
                          );
        item= new Item_cond_or(item, item_isnull);
        /* 
          TODO: why we create the above for cases where the right part
                cant be NULL?
        */
        if (left_expr->element_index(i)->maybe_null)
        {
          if (!(item= new Item_func_trig_cond(item, get_cond_guard(i))))
            DBUG_RETURN(RES_ERROR);
          if (!(having_col_item= 
                  new Item_func_trig_cond(having_col_item, get_cond_guard(i))))
            DBUG_RETURN(RES_ERROR);
        }
        having_item= and_items(having_item, having_col_item);
      }
      where_item= and_items(where_item, item);
    }
    /*
      AND can't be changed during fix_fields()
      we can assign select_lex->where here, and pass 0 as last
      argument (reference) to fix_fields()
    */
    select_lex->where= join->conds= and_items(join->conds, where_item);
    select_lex->where->top_level_item();
    if (join->conds->fix_fields(thd, 0))
      DBUG_RETURN(RES_ERROR);
  }
  if (having_item)
  {
    bool res;
    select_lex->having= join->having= and_items(join->having, having_item);
    if (having_item == select_lex->having)
      having_item->name= (char*)in_having_cond;
    select_lex->having->top_level_item();
    /*
      AND can't be changed during fix_fields()
      we can assign select_lex->having here, and pass 0 as last
      argument (reference) to fix_fields()
    */
    select_lex->having_fix_field= 1;
    res= join->having->fix_fields(thd, 0);
    select_lex->having_fix_field= 0;
    if (res)
    {
      DBUG_RETURN(RES_ERROR);
    }
  }

  DBUG_RETURN(RES_OK);
}


Item_subselect::trans_res
Item_in_subselect::select_transformer(JOIN *join)
{
  return select_in_like_transformer(join, &eq_creator);
}


/**
  Prepare IN/ALL/ANY/SOME subquery transformation and call appropriate
  transformation function.

    To decide which transformation procedure (scalar or row) applicable here
    we have to call fix_fields() for left expression to be able to call
    cols() method on it. Also this method make arena management for
    underlying transformation methods.

  @param join    JOIN object of transforming subquery
  @param func    creator of condition function of subquery

  @retval
    RES_OK      OK
  @retval
    RES_REDUCE  OK, and current subquery was reduced during
    transformation
  @retval
    RES_ERROR   Error
*/

Item_subselect::trans_res
Item_in_subselect::select_in_like_transformer(JOIN *join, Comp_creator *func)
{
  Query_arena *arena, backup;
  SELECT_LEX *current= thd->lex->current_select;
  const char *save_where= thd->where;
  Item_subselect::trans_res res= RES_ERROR;
  bool result;

  DBUG_ENTER("Item_in_subselect::select_in_like_transformer");

  {
    /*
      IN/SOME/ALL/ANY subqueries aren't support LIMIT clause. Without it
      ORDER BY clause becomes meaningless thus we drop it here.
    */
    SELECT_LEX *sl= current->master_unit()->first_select();
    for (; sl; sl= sl->next_select())
    {
      if (sl->join)
        sl->join->order= 0;
    }
  }

  if (changed)
    DBUG_RETURN(RES_OK);

  thd->where= "IN/ALL/ANY subquery";

  /*
    In some optimisation cases we will not need this Item_in_optimizer
    object, but we can't know it here, but here we need address correct
    reference on left expresion.

    //psergey: he means degenerate cases like "... IN (SELECT 1)"
  */
  if (!optimizer)
  {
    arena= thd->activate_stmt_arena_if_needed(&backup);
    result= (!(optimizer= new Item_in_optimizer(left_expr, this)));
    if (arena)
      thd->restore_active_arena(arena, &backup);
    if (result)
      goto err;
  }

  thd->lex->current_select= current->return_after_parsing();
  result= (!left_expr->fixed &&
           left_expr->fix_fields(thd, optimizer->arguments()));
  /* fix_fields can change reference to left_expr, we need reassign it */
  left_expr= optimizer->arguments()[0];

  thd->lex->current_select= current;
  if (result)
    goto err;

  /*
    If we didn't choose an execution method up to this point, we choose
    the IN=>EXISTS transformation.
  */
  if (exec_method == NOT_TRANSFORMED)
    exec_method= IN_TO_EXISTS;
  arena= thd->activate_stmt_arena_if_needed(&backup);

  /*
    Both transformers call fix_fields() only for Items created inside them,
    and all that items do not make permanent changes in current item arena
    which allow to us call them with changed arena (if we do not know nature
    of Item, we have to call fix_fields() for it only with original arena to
    avoid memory leack)
  */
  if (left_expr->cols() == 1)
    res= single_value_transformer(join, func);
  else
  {
    /* we do not support row operation for ALL/ANY/SOME */
    if (func != &eq_creator)
    {
      if (arena)
        thd->restore_active_arena(arena, &backup);
      my_error(ER_OPERAND_COLUMNS, MYF(0), 1);
      DBUG_RETURN(RES_ERROR);
    }
    res= row_value_transformer(join);
  }
  if (arena)
    thd->restore_active_arena(arena, &backup);
err:
  thd->where= save_where;
  DBUG_RETURN(res);
}


void Item_in_subselect::print(String *str, enum_query_type query_type)
{
  if (exec_method == IN_TO_EXISTS)
    str->append(STRING_WITH_LEN("<exists>"));
  else
  {
    left_expr->print(str, query_type);
    str->append(STRING_WITH_LEN(" in "));
  }
  Item_subselect::print(str, query_type);
}


bool Item_in_subselect::fix_fields(THD *thd_arg, Item **ref)
{
  uint outer_cols_num;
  List<Item> *inner_cols;

  if (exec_method == SEMI_JOIN)
    return !( (*ref)= new Item_int(1));

  /*
    Check if the outer and inner IN operands match in those cases when we
    will not perform IN=>EXISTS transformation. Currently this is when we
    use subquery materialization.

    The condition below is true when this method was called recursively from
    inside JOIN::prepare for the JOIN object created by the call chain
    Item_subselect::fix_fields -> subselect_single_select_engine::prepare,
    which creates a JOIN object for the subquery and calls JOIN::prepare for
    the JOIN of the subquery.
    Notice that in some cases, this doesn't happen, and the check_cols()
    test for each Item happens later in
    Item_in_subselect::row_value_in_to_exists_transformer.
    The reason for this mess is that our JOIN::prepare phase works top-down
    instead of bottom-up, so we first do name resoluton and semantic checks
    for the outer selects, then for the inner.
  */
  if (engine &&
      engine->engine_type() == subselect_engine::SINGLE_SELECT_ENGINE &&
      ((subselect_single_select_engine*)engine)->join)
  {
    outer_cols_num= left_expr->cols();

    if (unit->is_union())
      inner_cols= &(unit->types);
    else
      inner_cols= &(unit->first_select()->item_list);
    if (outer_cols_num != inner_cols->elements)
    {
      my_error(ER_OPERAND_COLUMNS, MYF(0), outer_cols_num);
      return TRUE;
    }
    if (outer_cols_num > 1)
    {
      List_iterator<Item> inner_col_it(*inner_cols);
      Item *inner_col;
      for (uint i= 0; i < outer_cols_num; i++)
      {
        inner_col= inner_col_it++;
        if (inner_col->check_cols(left_expr->element_index(i)->cols()))
          return TRUE;
      }
    }
  }

  if (thd_arg->lex->view_prepare_mode && left_expr && !left_expr->fixed &&
      left_expr->fix_fields(thd_arg, &left_expr))
    return TRUE;
  if (Item_subselect::fix_fields(thd_arg, ref))
    return TRUE;

  fixed= TRUE;

  return FALSE;
}


void Item_in_subselect::fix_after_pullout(st_select_lex *new_parent, Item **ref)
{
  left_expr->fix_after_pullout(new_parent, &left_expr);
  Item_subselect::fix_after_pullout(new_parent, ref);
}

void Item_in_subselect::update_used_tables()
{
  Item_subselect::update_used_tables();
  left_expr->update_used_tables();
  used_tables_cache |= left_expr->used_tables();
}

/**
  Try to create an engine to compute the subselect via materialization,
  and if this fails, revert to execution via the IN=>EXISTS transformation.

  @details
    The purpose of this method is to hide the implementation details
    of this Item's execution. The method creates a new engine for
    materialized execution, and initializes the engine.

    If this initialization fails
    - either because it wasn't possible to create the needed temporary table
      and its index,
    - or because of a memory allocation error,
    then we revert back to execution via the IN=>EXISTS tranformation.

    The initialization of the new engine is divided in two parts - a permanent
    one that lives across prepared statements, and one that is repeated for each
    execution.

  @returns
    @retval TRUE  memory allocation error occurred
    @retval FALSE an execution method was chosen successfully
*/

bool Item_in_subselect::setup_engine()
{
  subselect_hash_sj_engine *new_engine= NULL;
  bool res= FALSE;

  DBUG_ENTER("Item_in_subselect::setup_engine");

  if (engine->engine_type() == subselect_engine::SINGLE_SELECT_ENGINE)
  {
    /* Create/initialize objects in permanent memory. */
    subselect_single_select_engine *old_engine;
    Query_arena *arena= thd->stmt_arena, backup;

    old_engine= (subselect_single_select_engine*) engine;

    if (arena->is_conventional())
      arena= 0;
    else
      thd->set_n_backup_active_arena(arena, &backup);

    if (!(new_engine= new subselect_hash_sj_engine(thd, this,
                                                   old_engine)) ||
        new_engine->init_permanent(unit->get_unit_column_types()))
    {
      Item_subselect::trans_res trans_res;
      /*
        If for some reason we cannot use materialization for this IN predicate,
        delete all materialization-related objects, and apply the IN=>EXISTS
        transformation.
      */
      delete new_engine;
      new_engine= NULL;
      exec_method= NOT_TRANSFORMED;
      if (left_expr->cols() == 1)
        trans_res= single_value_in_to_exists_transformer(old_engine->join,
                                                         &eq_creator);
      else
        trans_res= row_value_in_to_exists_transformer(old_engine->join);
      res= (trans_res != Item_subselect::RES_OK);
    }
    if (new_engine)
      engine= new_engine;

    if (arena)
      thd->restore_active_arena(arena, &backup);
  }
  else
  {
    DBUG_ASSERT(engine->engine_type() == subselect_engine::HASH_SJ_ENGINE);
    new_engine= (subselect_hash_sj_engine*) engine;
  }

  /* Initilizations done in runtime memory, repeated for each execution. */
  if (new_engine)
  {
    /*
      Reset the LIMIT 1 set in Item_exists_subselect::fix_length_and_dec.
      TODO:
      Currently we set the subquery LIMIT to infinity, and this is correct
      because we forbid at parse time LIMIT inside IN subqueries (see
      Item_in_subselect::test_limit). However, once we allow this, here
      we should set the correct limit if given in the query.
    */
    unit->global_parameters->select_limit= NULL;
    if ((res= new_engine->init_runtime()))
      DBUG_RETURN(res);
  }

  DBUG_RETURN(res);
}


/**
  Initialize the cache of the left operand of the IN predicate.

  @note This method has the same purpose as alloc_group_fields(),
  but it takes a different kind of collection of items, and the
  list we push to is dynamically allocated.

  @retval TRUE  if a memory allocation error occurred or the cache is
                not applicable to the current query
  @retval FALSE if success
*/

bool Item_in_subselect::init_left_expr_cache()
{
  JOIN *outer_join;
  Next_select_func end_select;
  bool use_result_field= FALSE;

  outer_join= unit->outer_select()->join;
  /*
    An IN predicate might be evaluated in a query for which all tables have
    been optimzied away.
  */ 
  if (!outer_join || !outer_join->tables || !outer_join->tables_list)
    return TRUE;
  /*
    If we use end_[send | write]_group to handle complete rows of the outer
    query, make the cache of the left IN operand use Item_field::result_field
    instead of Item_field::field.  We need this because normally
    Cached_item_field uses Item::field to fetch field data, while
    copy_ref_key() that copies the left IN operand into a lookup key uses
    Item::result_field. In the case end_[send | write]_group result_field is
    one row behind field.
  */
  end_select= outer_join->join_tab[outer_join->tables-1].next_select;
  if (end_select == end_send_group || end_select == end_write_group)
    use_result_field= TRUE;

  if (!(left_expr_cache= new List<Cached_item>))
    return TRUE;

  for (uint i= 0; i < left_expr->cols(); i++)
  {
    Cached_item *cur_item_cache= new_Cached_item(thd,
                                                 left_expr->element_index(i),
                                                 use_result_field);
    if (!cur_item_cache || left_expr_cache->push_front(cur_item_cache))
      return TRUE;
  }
  return FALSE;
}


/*
  Callback to test if an IN predicate is expensive.

  @details
    IN predicates are considered expensive only if they will be executed via
    materialization. The return value affects the behavior of
    make_cond_for_table() in such a way that it is unchanged when we use
    the IN=>EXISTS transformation to compute IN.

  @retval TRUE  if the predicate is expensive
  @retval FALSE otherwise
*/

bool Item_in_subselect::is_expensive_processor(uchar *arg)
{
  return exec_method == MATERIALIZATION;
}


Item_subselect::trans_res
Item_allany_subselect::select_transformer(JOIN *join)
{
  DBUG_ENTER("Item_allany_subselect::select_transformer");
  exec_method= IN_TO_EXISTS;
  if (upper_item)
    upper_item->show= 1;
  DBUG_RETURN(select_in_like_transformer(join, func));
}


void Item_allany_subselect::print(String *str, enum_query_type query_type)
{
  if (exec_method == IN_TO_EXISTS)
    str->append(STRING_WITH_LEN("<exists>"));
  else
  {
    left_expr->print(str, query_type);
    str->append(' ');
    str->append(func->symbol(all));
    str->append(all ? " all " : " any ", 5);
  }
  Item_subselect::print(str, query_type);
}


void subselect_engine::set_thd(THD *thd_arg)
{
  thd= thd_arg;
  if (result)
    result->set_thd(thd_arg);
}


subselect_single_select_engine::
subselect_single_select_engine(st_select_lex *select,
			       select_result_interceptor *result_arg,
			       Item_subselect *item_arg)
  :subselect_engine(item_arg, result_arg),
   prepared(0), executed(0), select_lex(select), join(0)
{
  select_lex->master_unit()->item= item_arg;
}

int subselect_single_select_engine::get_identifier()
{
  return select_lex->select_number; 
}

void subselect_single_select_engine::cleanup()
{
  DBUG_ENTER("subselect_single_select_engine::cleanup");
  prepared= executed= 0;
  join= 0;
  result->cleanup();
  DBUG_VOID_RETURN;
}


void subselect_union_engine::cleanup()
{
  DBUG_ENTER("subselect_union_engine::cleanup");
  unit->reinit_exec_mechanism();
  result->cleanup();
  DBUG_VOID_RETURN;
}


bool subselect_union_engine::is_executed() const
{
  return unit->executed;
}


/*
  Check if last execution of the subquery engine produced any rows

  SYNOPSIS
    subselect_union_engine::no_rows()

  DESCRIPTION
    Check if last execution of the subquery engine produced any rows. The
    return value is undefined if last execution ended in an error.

  RETURN
    TRUE  - Last subselect execution has produced no rows
    FALSE - Otherwise
*/

bool subselect_union_engine::no_rows()
{
  /* Check if we got any rows when reading UNION result from temp. table: */
  return test(!unit->fake_select_lex->join->send_records);
}


void subselect_uniquesubquery_engine::cleanup()
{
  DBUG_ENTER("subselect_uniquesubquery_engine::cleanup");
  /* Tell handler we don't need the index anymore */
  if (tab->table->file->inited)
    tab->table->file->ha_index_end();
  DBUG_VOID_RETURN;
}


subselect_union_engine::subselect_union_engine(st_select_lex_unit *u,
					       select_result_interceptor *result_arg,
					       Item_subselect *item_arg)
  :subselect_engine(item_arg, result_arg)
{
  unit= u;
  if (!result_arg)				//out of memory
    current_thd->fatal_error();
  unit->item= item_arg;
}


/**
  Create and prepare the JOIN object that represents the query execution
  plan for the subquery.

  @details
  This method is called from Item_subselect::fix_fields. For prepared
  statements it is called both during the PREPARE and EXECUTE phases in the
  following ways:
  - During PREPARE the optimizer needs some properties
    (join->fields_list.elements) of the JOIN to proceed with preparation of
    the remaining query (namely to complete ::fix_fields for the subselect
    related classes. In the end of PREPARE the JOIN is deleted.
  - When we EXECUTE the query, Item_subselect::fix_fields is called again, and
    the JOIN object is re-created again, prepared and executed. In the end of
    execution it is deleted.
  In all cases the JOIN is created in runtime memory (not in the permanent
  memory root).

  @todo
  Re-check what properties of 'join' are needed during prepare, and see if
  we can avoid creating a JOIN during JOIN::prepare of the outer join.

  @retval 0  if success
  @retval 1  if error
*/

int subselect_single_select_engine::prepare()
{
  if (prepared)
    return 0;
  if (select_lex->join)
  {
    select_lex->cleanup();
  }
  join= new JOIN(thd, select_lex->item_list,
		 select_lex->options | SELECT_NO_UNLOCK, result);
  if (!join || !result)
  {
    thd->fatal_error();				//out of memory
    return 1;
  }
  prepared= 1;
  SELECT_LEX *save_select= thd->lex->current_select;
  thd->lex->current_select= select_lex;
  if (join->prepare(&select_lex->ref_pointer_array,
		    (TABLE_LIST*) select_lex->table_list.first,
		    select_lex->with_wild,
		    select_lex->where,
		    select_lex->order_list.elements +
		    select_lex->group_list.elements,
		    (ORDER*) select_lex->order_list.first,
		    (ORDER*) select_lex->group_list.first,
		    select_lex->having,
		    (ORDER*) 0, select_lex,
		    select_lex->master_unit()))
    return 1;
  thd->lex->current_select= save_select;
  return 0;
}

int subselect_union_engine::prepare()
{
  return unit->prepare(thd, result, SELECT_NO_UNLOCK);
}

int subselect_uniquesubquery_engine::prepare()
{
  /* Should never be called. */
  DBUG_ASSERT(FALSE);
  return 1;
}


/*
  Check if last execution of the subquery engine produced any rows

  SYNOPSIS
    subselect_single_select_engine::no_rows()

  DESCRIPTION
    Check if last execution of the subquery engine produced any rows. The
    return value is undefined if last execution ended in an error.

  RETURN
    TRUE  - Last subselect execution has produced no rows
    FALSE - Otherwise
*/

bool subselect_single_select_engine::no_rows()
{ 
  return !item->assigned();
}


/* 
 makes storage for the output values for the subquery and calcuates 
 their data and column types and their nullability.
*/ 
void subselect_engine::set_row(List<Item> &item_list, Item_cache **row)
{
  Item *sel_item;
  List_iterator_fast<Item> li(item_list);
  res_type= STRING_RESULT;
  res_field_type= MYSQL_TYPE_VAR_STRING;
  for (uint i= 0; (sel_item= li++); i++)
  {
    item->max_length= sel_item->max_length;
    res_type= sel_item->result_type();
    res_field_type= sel_item->field_type();
    item->decimals= sel_item->decimals;
    item->unsigned_flag= sel_item->unsigned_flag;
    maybe_null= sel_item->maybe_null;
    if (!(row[i]= Item_cache::get_cache(sel_item)))
      return;
    row[i]->setup(sel_item);
 //psergey-backport-timours:   row[i]->store(sel_item);
  }
  if (item_list.elements > 1)
    res_type= ROW_RESULT;
}

void subselect_single_select_engine::fix_length_and_dec(Item_cache **row)
{
  DBUG_ASSERT(row || select_lex->item_list.elements==1);
  set_row(select_lex->item_list, row);
  item->collation.set(row[0]->collation);
  if (cols() != 1)
    maybe_null= 0;
}

void subselect_union_engine::fix_length_and_dec(Item_cache **row)
{
  DBUG_ASSERT(row || unit->first_select()->item_list.elements==1);

  if (unit->first_select()->item_list.elements == 1)
  {
    set_row(unit->types, row);
    item->collation.set(row[0]->collation);
  }
  else
  {
    bool maybe_null_saved= maybe_null;
    set_row(unit->types, row);
    maybe_null= maybe_null_saved;
  }
}

void subselect_uniquesubquery_engine::fix_length_and_dec(Item_cache **row)
{
  //this never should be called
  DBUG_ASSERT(0);
}

int  init_read_record_seq(JOIN_TAB *tab);
int join_read_always_key_or_null(JOIN_TAB *tab);
int join_read_next_same_or_null(READ_RECORD *info);

int subselect_single_select_engine::exec()
{
  DBUG_ENTER("subselect_single_select_engine::exec");
  char const *save_where= thd->where;
  SELECT_LEX *save_select= thd->lex->current_select;
  thd->lex->current_select= select_lex;
  if (!join->optimized)
  {
    SELECT_LEX_UNIT *unit= select_lex->master_unit();

    unit->set_limit(unit->global_parameters);
    if (join->optimize())
    {
      thd->where= save_where;
      executed= 1;
      thd->lex->current_select= save_select;
      DBUG_RETURN(join->error ? join->error : 1);
    }
    if (!select_lex->uncacheable && thd->lex->describe && 
        !(join->select_options & SELECT_DESCRIBE) && 
        join->need_tmp && item->const_item())
    {
      /*
        Force join->join_tmp creation, because this subquery will be replaced
        by a simple select from the materialization temp table by optimize()
        called by EXPLAIN and we need to preserve the initial query structure
        so we can display it.
       */
      select_lex->uncacheable|= UNCACHEABLE_EXPLAIN;
      select_lex->master_unit()->uncacheable|= UNCACHEABLE_EXPLAIN;
      if (join->init_save_join_tab())
        DBUG_RETURN(1);                        /* purecov: inspected */
    }
    if (item->engine_changed)
    {
      DBUG_RETURN(1);
    }
  }
  if (select_lex->uncacheable &&
      select_lex->uncacheable != UNCACHEABLE_EXPLAIN
      && executed)
  {
    if (join->reinit())
    {
      thd->where= save_where;
      thd->lex->current_select= save_select;
      DBUG_RETURN(1);
    }
    item->reset();
    item->assigned((executed= 0));
  }
  if (!executed)
  {
    item->reset_value_registration();
    JOIN_TAB *changed_tabs[MAX_TABLES];
    JOIN_TAB **last_changed_tab= changed_tabs;
    if (item->have_guarded_conds())
    {
      /*
        For at least one of the pushed predicates the following is true:
        We should not apply optimizations based on the condition that was
        pushed down into the subquery. Those optimizations are ref[_or_null]
        acceses. Change them to be full table scans.
      */
      for (uint i=join->const_tables ; i < join->tables ; i++)
      {
        JOIN_TAB *tab=join->join_tab+i;
        if (tab && tab->keyuse)
        {
          for (uint i= 0; i < tab->ref.key_parts; i++)
          {
            bool *cond_guard= tab->ref.cond_guards[i];
            if (cond_guard && !*cond_guard)
            {
              /* Change the access method to full table scan */
              tab->save_read_first_record= tab->read_first_record;
              tab->save_read_record= tab->read_record.read_record;
              tab->read_first_record= init_read_record_seq;
              tab->read_record.record= tab->table->record[0];
              tab->read_record.thd= join->thd;
              tab->read_record.ref_length= tab->table->file->ref_length;
              tab->read_record.unlock_row= rr_unlock_row;
              *(last_changed_tab++)= tab;
              break;
            }
          }
        }
      }
    }
    
    join->exec();

    /* Enable the optimizations back */
    for (JOIN_TAB **ptab= changed_tabs; ptab != last_changed_tab; ptab++)
    {
      JOIN_TAB *tab= *ptab;
      tab->read_record.record= 0;
      tab->read_record.ref_length= 0;
      tab->read_first_record= tab->save_read_first_record; 
      tab->read_record.read_record= tab->save_read_record;
    }
    executed= 1;
    thd->where= save_where;
    thd->lex->current_select= save_select;
    DBUG_RETURN(join->error||thd->is_fatal_error);
  }
  thd->where= save_where;
  thd->lex->current_select= save_select;
  DBUG_RETURN(0);
}

int subselect_union_engine::exec()
{
  char const *save_where= thd->where;
  int res= unit->exec();
  thd->where= save_where;
  return res;
}


/*
  Search for at least one row satisfying select condition
 
  SYNOPSIS
    subselect_uniquesubquery_engine::scan_table()

  DESCRIPTION
    Scan the table using sequential access until we find at least one row
    satisfying select condition.
    
    The caller must set this->empty_result_set=FALSE before calling this
    function. This function will set it to TRUE if it finds a matching row.

  RETURN
    FALSE - OK
    TRUE  - Error
*/

int subselect_uniquesubquery_engine::scan_table()
{
  int error;
  TABLE *table= tab->table;
  DBUG_ENTER("subselect_uniquesubquery_engine::scan_table");

  if (table->file->inited)
    table->file->ha_index_end();
 
  table->file->ha_rnd_init(1);
  table->file->extra_opt(HA_EXTRA_CACHE,
                         current_thd->variables.read_buff_size);
  table->null_row= 0;
  for (;;)
  {
    error=table->file->ha_rnd_next(table->record[0]);
    if (error) {
      if (error == HA_ERR_RECORD_DELETED)
      {
        error= 0;
        continue;
      }
      if (error == HA_ERR_END_OF_FILE)
      {
        error= 0;
        break;
      }
      else
      {
        error= report_error(table, error);
        break;
      }
    }

    if (!cond || cond->val_int())
    {
      empty_result_set= FALSE;
      break;
    }
  }

  table->file->ha_rnd_end();
  DBUG_RETURN(error != 0);
}


/*
  Copy ref key and check for null parts in it

  SYNOPSIS
    subselect_uniquesubquery_engine::copy_ref_key()

  DESCRIPTION
    Copy ref key and check for null parts in it.
    Depending on the nullability and conversion problems this function
    recognizes and processes the following states :
      1. Partial match on top level. This means IN has a value of FALSE
         regardless of the data in the subquery table.
         Detected by finding a NULL in the left IN operand of a top level
         expression.
         We may actually skip reading the subquery, so return TRUE to skip
         the table scan in subselect_uniquesubquery_engine::exec and make
         the value of the IN predicate a NULL (that is equal to FALSE on
         top level).
      2. No exact match when IN is nested inside another predicate.
         Detected by finding a NULL in the left IN operand when IN is not
         a top level predicate.
         We cannot have an exact match. But we must proceed further with a
         table scan to find out if it's a partial match (and IN has a value
         of NULL) or no match (and IN has a value of FALSE).
         So we return FALSE to continue with the scan and see if there are
         any record that would constitute a partial match (as we cannot
         determine that from the index).
      3. Error converting the left IN operand to the column type of the
         right IN operand. This counts as no match (and IN has the value of
         FALSE). We mark the subquery table cursor as having no more rows
         (to ensure that the processing that follows will not find a match)
         and return FALSE, so IN is not treated as returning NULL.


  RETURN
    FALSE - The value of the IN predicate is not known. Proceed to find the
            value of the IN predicate using the determined values of
            null_keypart and table->status.
    TRUE  - IN predicate has a value of NULL. Stop the processing right there
            and return NULL to the outer predicates.
*/

bool subselect_uniquesubquery_engine::copy_ref_key()
{
  DBUG_ENTER("subselect_uniquesubquery_engine::copy_ref_key");

  for (store_key **copy= tab->ref.key_copy ; *copy ; copy++)
  {
    tab->ref.key_err= (*copy)->copy();

    /*
      When there is a NULL part in the key we don't need to make index
      lookup for such key thus we don't need to copy whole key.
      If we later should do a sequential scan return OK. Fail otherwise.

      See also the comment for the subselect_uniquesubquery_engine::exec()
      function.
    */
    null_keypart= (*copy)->null_key;
    if (null_keypart)
    {
      bool top_level= ((Item_in_subselect *) item)->is_top_level_item();
      if (top_level)
      {
        /* Partial match on top level */
        DBUG_RETURN(1);
      }
      else
      {
        /* No exact match when IN is nested inside another predicate */
        break;
      }
    }

    /*
      Check if the error is equal to STORE_KEY_FATAL. This is not expressed 
      using the store_key::store_key_result enum because ref.key_err is a 
      boolean and we want to detect both TRUE and STORE_KEY_FATAL from the 
      space of the union of the values of [TRUE, FALSE] and 
      store_key::store_key_result.  
      TODO: fix the variable an return types.
    */
    if (tab->ref.key_err & 1)
    {
      /*
       Error converting the left IN operand to the column type of the right
       IN operand. 
      */
      tab->table->status= STATUS_NOT_FOUND;
      break;
    }
  }
  DBUG_RETURN(0);
}


/*
  @retval  1  A NULL was found in the outer reference, index lookup is
              not applicable, the outer ref is unsusable as a lookup key,
              use some other method to find a match.
  @retval  0  The outer ref was copied into an index lookup key.
  @retval -1  The outer ref cannot possibly match any row, IN is FALSE.
*/
/* TIMOUR: this method is a variant of copy_ref_key(), needs refactoring. */

int subselect_uniquesubquery_engine::copy_ref_key_simple()
{
  for (store_key **copy= tab->ref.key_copy ; *copy ; copy++)
  {
    enum store_key::store_key_result store_res;
    store_res= (*copy)->copy();
    tab->ref.key_err= store_res;

    /*
      When there is a NULL part in the key we don't need to make index
      lookup for such key thus we don't need to copy whole key.
      If we later should do a sequential scan return OK. Fail otherwise.

      See also the comment for the subselect_uniquesubquery_engine::exec()
      function.
    */
    null_keypart= (*copy)->null_key;
    if (null_keypart)
      return 1;

    /*
      Check if the error is equal to STORE_KEY_FATAL. This is not expressed 
      using the store_key::store_key_result enum because ref.key_err is a 
      boolean and we want to detect both TRUE and STORE_KEY_FATAL from the 
      space of the union of the values of [TRUE, FALSE] and 
      store_key::store_key_result.  
      TODO: fix the variable an return types.
    */
    if (store_res == store_key::STORE_KEY_FATAL)
    {
      /*
       Error converting the left IN operand to the column type of the right
       IN operand. 
      */
      return -1;
    }
  }
  return 0;
}


/*
  Execute subselect

  SYNOPSIS
    subselect_uniquesubquery_engine::exec()

  DESCRIPTION
    Find rows corresponding to the ref key using index access.
    If some part of the lookup key is NULL, then we're evaluating
      NULL IN (SELECT ... )
    This is a special case, we don't need to search for NULL in the table,
    instead, the result value is 
      - NULL  if select produces empty row set
      - FALSE otherwise.

    In some cases (IN subselect is a top level item, i.e. abort_on_null==TRUE)
    the caller doesn't distinguish between NULL and FALSE result and we just
    return FALSE. 
    Otherwise we make a full table scan to see if there is at least one 
    matching row.
    
    The result of this function (info about whether a row was found) is
    stored in this->empty_result_set.
  NOTE
    
  RETURN
    FALSE - ok
    TRUE  - an error occured while scanning
*/

int subselect_uniquesubquery_engine::exec()
{
  DBUG_ENTER("subselect_uniquesubquery_engine::exec");
  int error;
  TABLE *table= tab->table;
  empty_result_set= TRUE;
  table->status= 0;
 
  /* TODO: change to use of 'full_scan' here? */
  if (copy_ref_key())
  {
    /*
      TIMOUR: copy_ref_key() == 1 means NULL result, not error, why return 1?
      Check who reiles on this result.
    */
    DBUG_RETURN(1);
  }
  if (table->status)
  {
    /* 
      We know that there will be no rows even if we scan. 
      Can be set in copy_ref_key.
    */
    ((Item_in_subselect *) item)->value= 0;
    DBUG_RETURN(0);
  }

  if (null_keypart)
    DBUG_RETURN(scan_table());
 
  if (!table->file->inited)
    table->file->ha_index_init(tab->ref.key, 0);
  error= table->file->ha_index_read_map(table->record[0],
                                        tab->ref.key_buff,
                                        make_prev_keypart_map(tab->
                                                              ref.key_parts),
                                        HA_READ_KEY_EXACT);
  if (error &&
      error != HA_ERR_KEY_NOT_FOUND && error != HA_ERR_END_OF_FILE)
    error= report_error(table, error);
  else
  {
    error= 0;
    table->null_row= 0;
    if (!table->status && (!cond || cond->val_int()))
    {
      ((Item_in_subselect *) item)->value= 1;
      empty_result_set= FALSE;
    }
    else
      ((Item_in_subselect *) item)->value= 0;
  }

  DBUG_RETURN(error != 0);
}


/*
  TIMOUR: write comment
*/

int subselect_uniquesubquery_engine::index_lookup()
{
  DBUG_ENTER("subselect_uniquesubquery_engine::index_lookup");
  int error;
  TABLE *table= tab->table;
 
  if (!table->file->inited)
    table->file->ha_index_init(tab->ref.key, 0);
  error= table->file->ha_index_read_map(table->record[0],
                                        tab->ref.key_buff,
                                        make_prev_keypart_map(tab->
                                                              ref.key_parts),
                                        HA_READ_KEY_EXACT);
  DBUG_PRINT("info", ("lookup result: %i", error));

  if (error && error != HA_ERR_KEY_NOT_FOUND && error != HA_ERR_END_OF_FILE)
  {
    /*
      TIMOUR: I don't understand at all when do we need to call report_error.
      In most places where we access an index, we don't do this. Why here?
    */
    error= report_error(table, error);
    DBUG_RETURN(error);
  }

  table->null_row= 0;
  if (!error && (!cond || cond->val_int()))
    ((Item_in_subselect *) item)->value= 1;
  else
    ((Item_in_subselect *) item)->value= 0;

  DBUG_RETURN(0);
}



subselect_uniquesubquery_engine::~subselect_uniquesubquery_engine()
{
  /* Tell handler we don't need the index anymore */
  //psergey-merge-todo: the following was gone in 6.0:
 //psergey-merge: don't need this after all: tab->table->file->ha_index_end();
}


/*
  Index-lookup subselect 'engine' - run the subquery

  SYNOPSIS
    subselect_indexsubquery_engine:exec()
      full_scan 

  DESCRIPTION
    The engine is used to resolve subqueries in form

      oe IN (SELECT key FROM tbl WHERE subq_where) 

    The value of the predicate is calculated as follows: 
    1. If oe IS NULL, this is a special case, do a full table scan on
       table tbl and search for row that satisfies subq_where. If such 
       row is found, return NULL, otherwise return FALSE.
    2. Make an index lookup via key=oe, search for a row that satisfies
       subq_where. If found, return TRUE.
    3. If check_null==TRUE, make another lookup via key=NULL, search for a 
       row that satisfies subq_where. If found, return NULL, otherwise
       return FALSE.

  TODO
    The step #1 can be optimized further when the index has several key
    parts. Consider a subquery:
    
      (oe1, oe2) IN (SELECT keypart1, keypart2 FROM tbl WHERE subq_where)

    and suppose we need to evaluate it for {oe1, oe2}=={const1, NULL}.
    Current code will do a full table scan and obtain correct result. There
    is a better option: instead of evaluating

      SELECT keypart1, keypart2 FROM tbl WHERE subq_where            (1)

    and checking if it has produced any matching rows, evaluate
    
      SELECT keypart2 FROM tbl WHERE subq_where AND keypart1=const1  (2)

    If this query produces a row, the result is NULL (as we're evaluating 
    "(const1, NULL) IN { (const1, X), ... }", which has a value of UNKNOWN,
    i.e. NULL).  If the query produces no rows, the result is FALSE.

    We currently evaluate (1) by doing a full table scan. (2) can be
    evaluated by doing a "ref" scan on "keypart1=const1", which can be much
    cheaper. We can use index statistics to quickly check whether "ref" scan
    will be cheaper than full table scan.

  RETURN
    0
    1
*/

int subselect_indexsubquery_engine::exec()
{
  DBUG_ENTER("subselect_indexsubquery_engine::exec");
  int error;
  bool null_finding= 0;
  TABLE *table= tab->table;

  ((Item_in_subselect *) item)->value= 0;
  empty_result_set= TRUE;
  null_keypart= 0;
  table->status= 0;

  if (check_null)
  {
    /* We need to check for NULL if there wasn't a matching value */
    *tab->ref.null_ref_key= 0;			// Search first for not null
    ((Item_in_subselect *) item)->was_null= 0;
  }

  /* Copy the ref key and check for nulls... */
  if (copy_ref_key())
    DBUG_RETURN(1);

  if (table->status)
  {
    /* 
      We know that there will be no rows even if we scan. 
      Can be set in copy_ref_key.
    */
    ((Item_in_subselect *) item)->value= 0;
    DBUG_RETURN(0);
  }

  if (null_keypart)
    DBUG_RETURN(scan_table());

  if (!table->file->inited)
    table->file->ha_index_init(tab->ref.key, 1);
  error= table->file->ha_index_read_map(table->record[0],
                                        tab->ref.key_buff,
                                        make_prev_keypart_map(tab->
                                                              ref.key_parts),
                                        HA_READ_KEY_EXACT);
  if (error &&
      error != HA_ERR_KEY_NOT_FOUND && error != HA_ERR_END_OF_FILE)
    error= report_error(table, error);
  else
  {
    for (;;)
    {
      error= 0;
      table->null_row= 0;
      if (!table->status)
      {
        if ((!cond || cond->val_int()) && (!having || having->val_int()))
        {
          empty_result_set= FALSE;
          if (null_finding)
            ((Item_in_subselect *) item)->was_null= 1;
          else
            ((Item_in_subselect *) item)->value= 1;
          break;
        }
        error= table->file->ha_index_next_same(table->record[0],
                                               tab->ref.key_buff,
                                               tab->ref.key_length);
        if (error && error != HA_ERR_END_OF_FILE)
        {
          error= report_error(table, error);
          break;
        }
      }
      else
      {
        if (!check_null || null_finding)
          break;			/* We don't need to check nulls */
        *tab->ref.null_ref_key= 1;
        null_finding= 1;
        /* Check if there exists a row with a null value in the index */
        if ((error= (safe_index_read(tab) == 1)))
          break;
      }
    }
  }
  DBUG_RETURN(error != 0);
}


uint subselect_single_select_engine::cols()
{
  //psergey-sj-backport: the following assert was gone in 6.0:
  //DBUG_ASSERT(select_lex->join != 0); // should be called after fix_fields()
  //return select_lex->join->fields_list.elements;
  return select_lex->item_list.elements;
}


uint subselect_union_engine::cols()
{
  DBUG_ASSERT(unit->is_prepared());  // should be called after fix_fields()
  return unit->types.elements;
}


uint8 subselect_single_select_engine::uncacheable()
{
  return select_lex->uncacheable;
}


uint8 subselect_union_engine::uncacheable()
{
  return unit->uncacheable;
}


void subselect_single_select_engine::exclude()
{
  select_lex->master_unit()->exclude_level();
}

void subselect_union_engine::exclude()
{
  unit->exclude_level();
}


void subselect_uniquesubquery_engine::exclude()
{
  //this never should be called
  DBUG_ASSERT(0);
}


table_map subselect_engine::calc_const_tables(TABLE_LIST *table)
{
  table_map map= 0;
  for (; table; table= table->next_leaf)
  {
    TABLE *tbl= table->table;
    if (tbl && tbl->const_table)
      map|= tbl->map;
  }
  return map;
}


table_map subselect_single_select_engine::upper_select_const_tables()
{
  return calc_const_tables((TABLE_LIST *) select_lex->outer_select()->
			   leaf_tables);
}


table_map subselect_union_engine::upper_select_const_tables()
{
  return calc_const_tables((TABLE_LIST *) unit->outer_select()->leaf_tables);
}


void subselect_single_select_engine::print(String *str,
                                           enum_query_type query_type)
{
  select_lex->print(thd, str, query_type);
}


void subselect_union_engine::print(String *str, enum_query_type query_type)
{
  unit->print(str, query_type);
}


void subselect_uniquesubquery_engine::print(String *str,
                                            enum_query_type query_type)
{
  char *table_name= tab->table->s->table_name.str;
  str->append(STRING_WITH_LEN("<primary_index_lookup>("));
  tab->ref.items[0]->print(str, query_type);
  str->append(STRING_WITH_LEN(" in "));
  if (tab->table->s->table_category == TABLE_CATEGORY_TEMPORARY)
  {
    /*
      Temporary tables' names change across runs, so they can't be used for
      EXPLAIN EXTENDED.
    */
    str->append(STRING_WITH_LEN("<temporary table>"));
  }
  else
    str->append(table_name, tab->table->s->table_name.length);
  KEY *key_info= tab->table->key_info+ tab->ref.key;
  str->append(STRING_WITH_LEN(" on "));
  str->append(key_info->name);
  if (cond)
  {
    str->append(STRING_WITH_LEN(" where "));
    cond->print(str, query_type);
  }
  str->append(')');
}

/*
TODO:
The above ::print method should be changed as below. Do it after
all other tests pass.

void subselect_uniquesubquery_engine::print(String *str)
{
  KEY *key_info= tab->table->key_info + tab->ref.key;
  str->append(STRING_WITH_LEN("<primary_index_lookup>("));
  for (uint i= 0; i < key_info->key_parts; i++)
    tab->ref.items[i]->print(str);
  str->append(STRING_WITH_LEN(" in "));
  str->append(tab->table->s->table_name.str, tab->table->s->table_name.length);
  str->append(STRING_WITH_LEN(" on "));
  str->append(key_info->name);
  if (cond)
  {
    str->append(STRING_WITH_LEN(" where "));
    cond->print(str);
  }
  str->append(')');
}
*/

void subselect_indexsubquery_engine::print(String *str,
                                           enum_query_type query_type)
{
  str->append(STRING_WITH_LEN("<index_lookup>("));
  tab->ref.items[0]->print(str, query_type);
  str->append(STRING_WITH_LEN(" in "));
  str->append(tab->table->s->table_name.str, tab->table->s->table_name.length);
  KEY *key_info= tab->table->key_info+ tab->ref.key;
  str->append(STRING_WITH_LEN(" on "));
  str->append(key_info->name);
  if (check_null)
    str->append(STRING_WITH_LEN(" checking NULL"));
  if (cond)
  {
    str->append(STRING_WITH_LEN(" where "));
    cond->print(str, query_type);
  }
  if (having)
  {
    str->append(STRING_WITH_LEN(" having "));
    having->print(str, query_type);
  }
  str->append(')');
}

/**
  change select_result object of engine.

  @param si		new subselect Item
  @param res		new select_result object

  @retval
    FALSE OK
  @retval
    TRUE  error
*/

bool subselect_single_select_engine::change_result(Item_subselect *si,
                                                 select_result_interceptor *res)
{
  item= si;
  result= res;
  return select_lex->join->change_result(result);
}


/**
  change select_result object of engine.

  @param si		new subselect Item
  @param res		new select_result object

  @retval
    FALSE OK
  @retval
    TRUE  error
*/

bool subselect_union_engine::change_result(Item_subselect *si,
                                           select_result_interceptor *res)
{
  item= si;
  int rc= unit->change_result(res, result);
  result= res;
  return rc;
}


/**
  change select_result emulation, never should be called.

  @param si		new subselect Item
  @param res		new select_result object

  @retval
    FALSE OK
  @retval
    TRUE  error
*/

bool subselect_uniquesubquery_engine::change_result(Item_subselect *si,
                                                    select_result_interceptor *res)
{
  DBUG_ASSERT(0);
  return TRUE;
}


/**
  Report about presence of tables in subquery.

  @retval
    TRUE  there are not tables used in subquery
  @retval
    FALSE there are some tables in subquery
*/
bool subselect_single_select_engine::no_tables()
{
  return(select_lex->table_list.elements == 0);
}


/*
  Check statically whether the subquery can return NULL

  SINOPSYS
    subselect_single_select_engine::may_be_null()

  RETURN
    FALSE  can guarantee that the subquery never return NULL
    TRUE   otherwise
*/
bool subselect_single_select_engine::may_be_null()
{
  return ((no_tables() && !join->conds && !join->having) ? maybe_null : 1);
}


/**
  Report about presence of tables in subquery.

  @retval
    TRUE  there are not tables used in subquery
  @retval
    FALSE there are some tables in subquery
*/
bool subselect_union_engine::no_tables()
{
  for (SELECT_LEX *sl= unit->first_select(); sl; sl= sl->next_select())
  {
    if (sl->table_list.elements)
      return FALSE;
  }
  return TRUE;
}


/**
  Report about presence of tables in subquery.

  @retval
    TRUE  there are not tables used in subquery
  @retval
    FALSE there are some tables in subquery
*/

bool subselect_uniquesubquery_engine::no_tables()
{
  /* returning value is correct, but this method should never be called */
  DBUG_ASSERT(FALSE);
  return 0;
}


/******************************************************************************
  WL#1110 - Implementation of class subselect_hash_sj_engine
******************************************************************************/


/**
  Check if an IN predicate should be executed via partial matching using
  only schema information.

  @details
  This test essentially has three results:
  - partial matching is applicable, but cannot be executed due to a
    limitation in the total number of indexes, as a result we can't
    use subquery materialization at all.
  - partial matching is either applicable or not, and this can be
    determined by looking at 'this->max_keys'.
  If max_keys > 1, then we need partial matching because there are
  more indexes than just the one we use during materialization to
  remove duplicates.

  @note
  TIMOUR: The schema-based analysis for partial matching can be done once for
  prepared statement and remembered. It is done here to remove the need to
  save/restore all related variables between each re-execution, thus making
  the code simpler.

  @retval PARTIAL_MATCH  if a partial match should be used
  @retval COMPLETE_MATCH if a complete match (index lookup) should be used
*/

subselect_hash_sj_engine::exec_strategy
subselect_hash_sj_engine::get_strategy_using_schema()
{
  Item_in_subselect *item_in= (Item_in_subselect *) item;

  if (item_in->is_top_level_item())
    return COMPLETE_MATCH;
  else
  {
    List_iterator<Item> inner_col_it(*item_in->unit->get_unit_column_types());
    Item *outer_col, *inner_col;

    for (uint i= 0; i < item_in->left_expr->cols(); i++)
    {
      outer_col= item_in->left_expr->element_index(i);
      inner_col= inner_col_it++;

      if (!inner_col->maybe_null && !outer_col->maybe_null)
        bitmap_set_bit(&non_null_key_parts, i);
      else
      {
        bitmap_set_bit(&partial_match_key_parts, i);
        ++count_partial_match_columns;
      }
    }
  }

  /* If no column contains NULLs use regular hash index lookups. */
  if (count_partial_match_columns)
    return PARTIAL_MATCH;
  return COMPLETE_MATCH;
}


/**
  Test whether an IN predicate must be computed via partial matching
  based on the NULL statistics for each column of a materialized subquery.

  @details The procedure analyzes column NULL statistics, updates the
  matching type of columns that cannot be NULL or that contain only NULLs.
  Based on this, the procedure determines the final execution strategy for
  the [NOT] IN predicate.

  @retval PARTIAL_MATCH  if a partial match should be used
  @retval COMPLETE_MATCH if a complete match (index lookup) should be used
*/

subselect_hash_sj_engine::exec_strategy
subselect_hash_sj_engine::get_strategy_using_data()
{
  Item_in_subselect *item_in= (Item_in_subselect *) item;
  select_materialize_with_stats *result_sink=
    (select_materialize_with_stats *) result;
  Item *outer_col;

  /*
    If we already determined that a complete match is enough based on schema
    information, nothing can be better.
  */
  if (strategy == COMPLETE_MATCH)
    return COMPLETE_MATCH;

  for (uint i= 0; i < item_in->left_expr->cols(); i++)
  {
    if (!bitmap_is_set(&partial_match_key_parts, i))
      continue;
    outer_col= item_in->left_expr->element_index(i);
    /*
      If column 'i' doesn't contain NULLs, and the corresponding outer reference
      cannot have a NULL value, then 'i' is a non-nullable column.
    */
    if (result_sink->get_null_count_of_col(i) == 0 && !outer_col->maybe_null)
    {
      bitmap_clear_bit(&partial_match_key_parts, i);
      bitmap_set_bit(&non_null_key_parts, i);
      --count_partial_match_columns;
    }
    if (result_sink->get_null_count_of_col(i) ==
               tmp_table->file->stats.records)
      ++count_null_only_columns;
  }

  /* If no column contains NULLs use regular hash index lookups. */
  if (!count_partial_match_columns)
    return COMPLETE_MATCH;
  return PARTIAL_MATCH;
}


void
subselect_hash_sj_engine::choose_partial_match_strategy(
  bool has_non_null_key, bool has_covering_null_row,
  MY_BITMAP *partial_match_key_parts)
{
  size_t pm_buff_size;

  DBUG_ASSERT(strategy == PARTIAL_MATCH);
  /*
    Choose according to global optimizer switch. If only one of the switches is
    'ON', then the remaining strategy is the only possible one. The only cases
    when this will be overriden is when the total size of all buffers for the
    merge strategy is bigger than the 'rowid_merge_buff_size' system variable,
    or if there isn't enough physical memory to allocate the buffers.
  */
  if (!optimizer_flag(thd, OPTIMIZER_SWITCH_PARTIAL_MATCH_ROWID_MERGE) &&
       optimizer_flag(thd, OPTIMIZER_SWITCH_PARTIAL_MATCH_TABLE_SCAN))
    strategy= PARTIAL_MATCH_SCAN;
  else if
     ( optimizer_flag(thd, OPTIMIZER_SWITCH_PARTIAL_MATCH_ROWID_MERGE) &&
      !optimizer_flag(thd, OPTIMIZER_SWITCH_PARTIAL_MATCH_TABLE_SCAN))
    strategy= PARTIAL_MATCH_MERGE;

  /*
    If both switches are ON, or both are OFF, we interpret that as "let the
    optimizer decide". Perform a cost based choice between the two partial
    matching strategies.
  */
  /*
    TIMOUR: the above interpretation of the switch values could be changed to:
    - if both are ON - let the optimizer decide,
    - if both are OFF - do not use partial matching, therefore do not use
      materialization in non-top-level predicates.
    The problem with this is that we know for sure if we need partial matching
    only after the subquery is materialized, and this is too late to revert to
    the IN=>EXISTS strategy.
  */
  if (strategy == PARTIAL_MATCH)
  {
    /*
      TIMOUR: Currently we use a super simplistic measure. This will be
      addressed in a separate task.
    */
    if (tmp_table->file->stats.records < 100)
      strategy= PARTIAL_MATCH_SCAN;
    else
      strategy= PARTIAL_MATCH_MERGE;
  }

  /* Check if there is enough memory for the rowid merge strategy. */
  if (strategy == PARTIAL_MATCH_MERGE)
  {
    pm_buff_size= rowid_merge_buff_size(has_non_null_key,
                                        has_covering_null_row,
                                        partial_match_key_parts);
    if (pm_buff_size > thd->variables.rowid_merge_buff_size)
      strategy= PARTIAL_MATCH_SCAN;
  }
}


/*
  Compute the memory size of all buffers proportional to the number of rows
  in tmp_table.

  @details
  If the result is bigger than thd->variables.rowid_merge_buff_size, partial
  matching via merging is not applicable.
*/

size_t subselect_hash_sj_engine::rowid_merge_buff_size(
  bool has_non_null_key, bool has_covering_null_row,
  MY_BITMAP *partial_match_key_parts)
{
  size_t buff_size; /* Total size of all buffers used by partial matching. */
  ha_rows row_count= tmp_table->file->stats.records;
  uint rowid_length= tmp_table->file->ref_length;
  select_materialize_with_stats *result_sink=
    (select_materialize_with_stats *) result;

  /* Size of the subselect_rowid_merge_engine::row_num_to_rowid buffer. */
  buff_size= row_count * rowid_length * sizeof(uchar);

  if (has_non_null_key)
  {
    /* Add the size of Ordered_key::key_buff of the only non-NULL key. */
    buff_size+= row_count * sizeof(rownum_t);
  }

  if (!has_covering_null_row)
  {
    for (uint i= 0; i < partial_match_key_parts->n_bits; i++)
    {
      if (!bitmap_is_set(partial_match_key_parts, i) ||
          result_sink->get_null_count_of_col(i) == row_count)
        continue; /* In these cases we wouldn't construct Ordered keys. */

      /* Add the size of Ordered_key::key_buff */
      buff_size+= (row_count - result_sink->get_null_count_of_col(i)) *
                         sizeof(rownum_t);
      /* Add the size of Ordered_key::null_key */
      buff_size+= bitmap_buffer_size(result_sink->get_max_null_of_col(i));
    }
  }

  return buff_size;
}


/*
  Initialize a MY_BITMAP with a buffer allocated on the current
  memory root.
  TIMOUR: move to bitmap C file?
*/

static my_bool
bitmap_init_memroot(MY_BITMAP *map, uint n_bits, MEM_ROOT *mem_root)
{
  my_bitmap_map *bitmap_buf;

  if (!(bitmap_buf= (my_bitmap_map*) alloc_root(mem_root,
                                                bitmap_buffer_size(n_bits))) ||
      bitmap_init(map, bitmap_buf, n_bits, FALSE))
    return TRUE;
  bitmap_clear_all(map);
  return FALSE;
}


/**
  Create all structures needed for IN execution that can live between PS
  reexecution.

  @param tmp_columns the items that produce the data for the temp table

  @details
  - Create a temporary table to store the result of the IN subquery. The
    temporary table has one hash index on all its columns.
  - Create a new result sink that sends the result stream of the subquery to
    the temporary table,

  @notice:
    Currently Item_subselect::init() already chooses and creates at parse
    time an engine with a corresponding JOIN to execute the subquery.

  @retval TRUE  if error
  @retval FALSE otherwise
*/

bool subselect_hash_sj_engine::init_permanent(List<Item> *tmp_columns)
{
  /* Options to create_tmp_table. */
  ulonglong tmp_create_options= thd->options | TMP_TABLE_ALL_COLUMNS;
                             /* | TMP_TABLE_FORCE_MYISAM; TIMOUR: force MYISAM */

  DBUG_ENTER("subselect_hash_sj_engine::init_permanent");

  if (bitmap_init_memroot(&non_null_key_parts, tmp_columns->elements,
                            thd->mem_root) ||
      bitmap_init_memroot(&partial_match_key_parts, tmp_columns->elements,
                            thd->mem_root))
    DBUG_RETURN(TRUE);

  /*
    Create and initialize a select result interceptor that stores the
    result stream in a temporary table. The temporary table itself is
    managed (created/filled/etc) internally by the interceptor.
  */
/*
  TIMOUR:
  Select a more efficient result sink when we know there is no need to collect
  data statistics.

  if (strategy == COMPLETE_MATCH)
  {
    if (!(result= new select_union))
      DBUG_RETURN(TRUE);
  }
  else if (strategy == PARTIAL_MATCH)
  {
  if (!(result= new select_materialize_with_stats))
    DBUG_RETURN(TRUE);
  }
*/
  if (!(result= new select_materialize_with_stats))
    DBUG_RETURN(TRUE);

  if (((select_union*) result)->create_result_table(
                         thd, tmp_columns, TRUE, tmp_create_options,
                         "materialized subselect", TRUE))
    DBUG_RETURN(TRUE);

  tmp_table= ((select_union*) result)->table;

  /*
    If the subquery has blobs, or the total key lenght is bigger than
    some length, or the total number of key parts is more than the
    allowed maximum (currently MAX_REF_PARTS == 16), then the created
    index cannot be used for lookups and we can't use hash semi
    join. If this is the case, delete the temporary table since it
    will not be used, and tell the caller we failed to initialize the
    engine.
  */
  if (tmp_table->s->keys == 0)
  {
    DBUG_ASSERT(
      tmp_table->s->uniques ||
      tmp_table->key_info->key_length >= tmp_table->file->max_key_length() ||
      tmp_table->key_info->key_parts > tmp_table->file->max_key_parts());
    free_tmp_table(thd, tmp_table);
    tmp_table= NULL;
    delete result;
    result= NULL;
    DBUG_RETURN(TRUE);
  }

  /*
    Make sure there is only one index on the temp table, and it doesn't have
    the extra key part created when s->uniques > 0.
  */
  DBUG_ASSERT(tmp_table->s->keys == 1 &&
              ((Item_in_subselect *) item)->left_expr->cols() ==
              tmp_table->key_info->key_parts);

  if (make_semi_join_conds() ||
      /* A unique_engine is used both for complete and partial matching. */
      !(lookup_engine= make_unique_engine()))
    DBUG_RETURN(TRUE);

  DBUG_RETURN(FALSE);
}


/*
  Create an artificial condition to post-filter those rows matched by index
  lookups that cannot be distinguished by the index lookup procedure.

  @notes
  The need for post-filtering may occur e.g. because of
  truncation. Prepared statements execution requires that fix_fields is
  called for every execution. In order to call fix_fields we need to
  create a Name_resolution_context and a corresponding TABLE_LIST for
  the temporary table for the subquery, so that all column references
  to the materialized subquery table can be resolved correctly.

  @returns
    @retval TRUE  memory allocation error occurred
    @retval FALSE the conditions were created and resolved (fixed)
*/

bool subselect_hash_sj_engine::make_semi_join_conds()
{
  /*
    Table reference for tmp_table that is used to resolve column references
    (Item_fields) to columns in tmp_table.
  */
  TABLE_LIST *tmp_table_ref;
  /* Name resolution context for all tmp_table columns created below. */
  Name_resolution_context *context;
  Item_in_subselect *item_in= (Item_in_subselect *) item;

  DBUG_ENTER("subselect_hash_sj_engine::make_semi_join_conds");
  DBUG_ASSERT(semi_join_conds == NULL);

  if (!(semi_join_conds= new Item_cond_and))
    DBUG_RETURN(TRUE);

  if (!(tmp_table_ref= (TABLE_LIST*) thd->alloc(sizeof(TABLE_LIST))))
    DBUG_RETURN(TRUE);

  tmp_table_ref->init_one_table("", "materialized subselect", TL_READ);
  tmp_table_ref->table= tmp_table;

  context= new Name_resolution_context;
  context->init();
  context->first_name_resolution_table=
    context->last_name_resolution_table= tmp_table_ref;
  
  for (uint i= 0; i < item_in->left_expr->cols(); i++)
  {
    Item_func_eq *eq_cond; /* New equi-join condition for the current column. */
    /* Item for the corresponding field from the materialized temp table. */
    Item_field *right_col_item;

    if (!(right_col_item= new Item_field(thd, context, tmp_table->field[i])) ||
        !(eq_cond= new Item_func_eq(item_in->left_expr->element_index(i),
                                    right_col_item)) ||
        (((Item_cond_and*)semi_join_conds)->add(eq_cond)))
    {
      delete semi_join_conds;
      semi_join_conds= NULL;
      DBUG_RETURN(TRUE);
    }
  }
  if (semi_join_conds->fix_fields(thd, (Item**)&semi_join_conds))
    DBUG_RETURN(TRUE);

  DBUG_RETURN(FALSE);
}


/**
  Create a new uniquesubquery engine for the execution of an IN predicate.

  @details
  Create and initialize a new JOIN_TAB, and Table_ref objects to perform
  lookups into the indexed temporary table.

  @retval A new subselect_hash_sj_engine object
  @retval NULL if a memory allocation error occurs
*/

subselect_uniquesubquery_engine*
subselect_hash_sj_engine::make_unique_engine()
{
  Item_in_subselect *item_in= (Item_in_subselect *) item;
  /* The only index on the temporary table. */
  KEY *tmp_key= tmp_table->key_info;
  /* Number of keyparts in tmp_key. */
  uint tmp_key_parts= tmp_key->key_parts;
  JOIN_TAB *tab;

  DBUG_ENTER("subselect_hash_sj_engine::make_unique_engine");

  /*
    Create and initialize the JOIN_TAB that represents an index lookup
    plan operator into the materialized subquery result. Notice that:
    - this JOIN_TAB has no corresponding JOIN (and doesn't need one), and
    - here we initialize only those members that are used by
      subselect_uniquesubquery_engine, so these objects are incomplete.
  */
  if (!(tab= (JOIN_TAB*) thd->alloc(sizeof(JOIN_TAB))))
    DBUG_RETURN(NULL);
  tab->table= tmp_table;
  tab->ref.key= 0; /* The only temp table index. */
  tab->ref.key_length= tmp_key->key_length;
  if (!(tab->ref.key_buff=
        (uchar*) thd->calloc(ALIGN_SIZE(tmp_key->key_length) * 2)) ||
      !(tab->ref.key_copy=
        (store_key**) thd->alloc((sizeof(store_key*) *
                                  (tmp_key_parts + 1)))) ||
      !(tab->ref.items=
        (Item**) thd->alloc(sizeof(Item*) * tmp_key_parts)))
    DBUG_RETURN(NULL);

  KEY_PART_INFO *cur_key_part= tmp_key->key_part;
  store_key **ref_key= tab->ref.key_copy;
  uchar *cur_ref_buff= tab->ref.key_buff;
  
  for (uint i= 0; i < tmp_key_parts; i++, cur_key_part++, ref_key++)
  {
    tab->ref.items[i]= item_in->left_expr->element_index(i);
    int null_count= test(cur_key_part->field->real_maybe_null());
    *ref_key= new store_key_item(thd, cur_key_part->field,
                                 /* TIMOUR:
                                    the NULL byte is taken into account in
                                    cur_key_part->store_length, so instead of
                                    cur_ref_buff + test(maybe_null), we could
                                    use that information instead.
                                 */
                                 cur_ref_buff + null_count,
                                 null_count ? tab->ref.key_buff : 0,
                                 cur_key_part->length, tab->ref.items[i]);
    cur_ref_buff+= cur_key_part->store_length;
  }
  *ref_key= NULL; /* End marker. */
  tab->ref.key_err= 1;
  tab->ref.key_parts= tmp_key_parts;

  DBUG_RETURN(new subselect_uniquesubquery_engine(thd, tab, item,
                                                  semi_join_conds));
}


/**
  Initialize members of the engine that need to be re-initilized at each
  execution.

  @retval TRUE  if a memory allocation error occurred
  @retval FALSE if success
*/

bool subselect_hash_sj_engine::init_runtime()
{
  /*
    Create and optimize the JOIN that will be used to materialize
    the subquery if not yet created.
  */
  materialize_engine->prepare();
  /*
    Repeat name resolution for 'cond' since cond is not part of any
    clause of the query, and it is not 'fixed' during JOIN::prepare.
  */
  if (semi_join_conds && !semi_join_conds->fixed &&
      semi_join_conds->fix_fields(thd, (Item**)&semi_join_conds))
    return TRUE;
  /* Let our engine reuse this query plan for materialization. */
  materialize_join= materialize_engine->join;
  materialize_join->change_result(result);
  return FALSE;
}


subselect_hash_sj_engine::~subselect_hash_sj_engine()
{
  delete lookup_engine;
  delete result;
  if (tmp_table)
    free_tmp_table(thd, tmp_table);
}


/**
  Cleanup performed after each PS execution.

  @details
  Called in the end of JOIN::prepare for PS from Item_subselect::cleanup.
*/

void subselect_hash_sj_engine::cleanup()
{
  enum_engine_type lookup_engine_type= lookup_engine->engine_type();
  is_materialized= FALSE;
  bitmap_clear_all(&non_null_key_parts);
  bitmap_clear_all(&partial_match_key_parts);
  count_partial_match_columns= 0;
  count_null_only_columns= 0;
  strategy= UNDEFINED;
  materialize_engine->cleanup();
  if (lookup_engine_type == TABLE_SCAN_ENGINE ||
      lookup_engine_type == ROWID_MERGE_ENGINE)
  {
    subselect_engine *inner_lookup_engine;
    inner_lookup_engine=
      ((subselect_partial_match_engine*) lookup_engine)->lookup_engine;
    /*
      Partial match engines are recreated for each PS execution inside
      subselect_hash_sj_engine::exec().
    */
    delete lookup_engine;
    lookup_engine= inner_lookup_engine;
  }
  DBUG_ASSERT(lookup_engine->engine_type() == UNIQUESUBQUERY_ENGINE);
  lookup_engine->cleanup();
  result->cleanup(); /* Resets the temp table as well. */
}


/**
  Execute a subquery IN predicate via materialization.

  @details
  If needed materialize the subquery into a temporary table, then
  copmpute the predicate via a lookup into this table.

  @retval TRUE  if error
  @retval FALSE otherwise
*/

int subselect_hash_sj_engine::exec()
{
  Item_in_subselect *item_in= (Item_in_subselect *) item;
  SELECT_LEX *save_select= thd->lex->current_select;
  subselect_partial_match_engine *pm_engine= NULL;
  int res= 0;

  DBUG_ENTER("subselect_hash_sj_engine::exec");

  /*
    Optimize and materialize the subquery during the first execution of
    the subquery predicate.
  */
  thd->lex->current_select= materialize_engine->select_lex;
  if ((res= materialize_join->optimize()))
    goto err; /* purecov: inspected */
  DBUG_ASSERT(!is_materialized); /* We should materialize only once. */
  materialize_join->exec();
  if ((res= test(materialize_join->error || thd->is_fatal_error)))
    goto err;

  /*
    TODO:
    - Unlock all subquery tables as we don't need them. To implement this
      we need to add new functionality to JOIN::join_free that can unlock
      all tables in a subquery (and all its subqueries).
    - The temp table used for grouping in the subquery can be freed
      immediately after materialization (yet it's done together with
      unlocking).
  */
  is_materialized= TRUE;
  /*
    If the subquery returned no rows, the temporary table is empty, so we know
    directly that the result of IN is FALSE. We first update the table
    statistics, then we test if the temporary table for the query result is
    empty.
  */
  tmp_table->file->info(HA_STATUS_VARIABLE);
  if (!tmp_table->file->stats.records)
  {
    item_in->value= FALSE;
    /* The value of IN will not change during this execution. */
    item_in->is_constant= TRUE;
    item_in->set_first_execution();
    /* TIMOUR: check if we need this: item_in->null_value= FALSE; */
    DBUG_RETURN(FALSE);
  }

  /*
    TIMOUR: The schema-based analysis for partial matching can be done once for
    prepared statement and remembered. It is done here to remove the need to
    save/restore all related variables between each re-execution, thus making
    the code simpler.
  */
  strategy= get_strategy_using_schema();
  /* This call may discover that we don't need partial matching at all. */
  strategy= get_strategy_using_data();
  if (strategy == PARTIAL_MATCH)
  {
    uint count_pm_keys; /* Total number of keys needed for partial matching. */
    MY_BITMAP *nn_key_parts; /* The key parts of the only non-NULL index. */
    uint covering_null_row_width;
    select_materialize_with_stats *result_sink=
      (select_materialize_with_stats *) result;

    nn_key_parts= (count_partial_match_columns < tmp_table->s->fields) ?
                  &non_null_key_parts : NULL;

    if (result_sink->get_max_nulls_in_row() ==
        tmp_table->s->fields -
        (nn_key_parts ? bitmap_bits_set(nn_key_parts) : 0))
      covering_null_row_width= result_sink->get_max_nulls_in_row();
    else
      covering_null_row_width= 0;

    if (covering_null_row_width)
      count_pm_keys= nn_key_parts ? 1 : 0;
    else
      count_pm_keys= count_partial_match_columns - count_null_only_columns +
        (nn_key_parts ? 1 : 0);

    choose_partial_match_strategy(test(nn_key_parts),
                                  test(covering_null_row_width),
                                  &partial_match_key_parts);
    DBUG_ASSERT(strategy == PARTIAL_MATCH_MERGE ||
                strategy == PARTIAL_MATCH_SCAN);
    if (strategy == PARTIAL_MATCH_MERGE)
    {
      pm_engine=
        new subselect_rowid_merge_engine((subselect_uniquesubquery_engine*)
                                         lookup_engine, tmp_table,
                                         count_pm_keys,
                                         covering_null_row_width,
                                         item, result,
                                         semi_join_conds->argument_list());
      if (!pm_engine ||
          ((subselect_rowid_merge_engine*) pm_engine)->
            init(nn_key_parts, &partial_match_key_parts))
      {
        /*
          The call to init() would fail if there was not enough memory to allocate
          all buffers for the rowid merge strategy. In this case revert to table
          scanning which doesn't need any big buffers.
        */
        delete pm_engine;
        pm_engine= NULL;
        strategy= PARTIAL_MATCH_SCAN;
      }
    }

    if (strategy == PARTIAL_MATCH_SCAN)
    {
      if (!(pm_engine=
            new subselect_table_scan_engine((subselect_uniquesubquery_engine*)
                                            lookup_engine, tmp_table,
                                            item, result,
                                            semi_join_conds->argument_list(),
                                            covering_null_row_width)))
      {
        /* This is an irrecoverable error. */
        res= 1;
        goto err;
      }
    }
  }

  if (pm_engine)
    lookup_engine= pm_engine;
  item_in->change_engine(lookup_engine);

err:
  thd->lex->current_select= save_select;
  DBUG_RETURN(res);
}


/**
  Print the state of this engine into a string for debugging and views.
*/

void subselect_hash_sj_engine::print(String *str, enum_query_type query_type)
{
  str->append(STRING_WITH_LEN(" <materialize> ("));
  materialize_engine->print(str, query_type);
  str->append(STRING_WITH_LEN(" ), "));

  if (lookup_engine)
    lookup_engine->print(str, query_type);
  else
    str->append(STRING_WITH_LEN(
           "<engine selected at execution time>"
         ));
}

void subselect_hash_sj_engine::fix_length_and_dec(Item_cache** row)
{
  DBUG_ASSERT(FALSE);
}

void subselect_hash_sj_engine::exclude()
{
  DBUG_ASSERT(FALSE);
}

bool subselect_hash_sj_engine::no_tables()
{
  DBUG_ASSERT(FALSE);
  return FALSE;
}

bool subselect_hash_sj_engine::change_result(Item_subselect *si,
                                             select_result_interceptor *res)
{
  DBUG_ASSERT(FALSE);
  return TRUE;
}


Ordered_key::Ordered_key(uint keyid_arg, TABLE *tbl_arg, Item *search_key_arg,
                         ha_rows null_count_arg, ha_rows min_null_row_arg,
                         ha_rows max_null_row_arg, uchar *row_num_to_rowid_arg)
  : keyid(keyid_arg), tbl(tbl_arg), search_key(search_key_arg),
    row_num_to_rowid(row_num_to_rowid_arg), null_count(null_count_arg)
{
  DBUG_ASSERT(tbl->file->stats.records > null_count);
  key_buff_elements= tbl->file->stats.records - null_count;
  cur_key_idx= HA_POS_ERROR;

  DBUG_ASSERT((null_count && min_null_row_arg && max_null_row_arg) ||
              (!null_count && !min_null_row_arg && !max_null_row_arg));
  if (null_count)
  {
    /* The counters are 1-based, for key access we need 0-based indexes. */
    min_null_row= min_null_row_arg - 1;
    max_null_row= max_null_row_arg - 1;
  }
  else
    min_null_row= max_null_row= 0;
}


Ordered_key::~Ordered_key()
{
  my_free((char*) key_buff, MYF(0));
  bitmap_free(&null_key);
}


/*
  Cleanup that needs to be done for each PS (re)execution.
*/

void Ordered_key::cleanup()
{
  /*
    Currently these keys are recreated for each PS re-execution, thus
    there is nothing to cleanup, the whole object goes away after execution
    is over. All handler related initialization/deinitialization is done by
    the parent subselect_rowid_merge_engine object.
  */
}


/*
  Initialize a multi-column index.
*/

bool Ordered_key::init(MY_BITMAP *columns_to_index)
{
  THD *thd= tbl->in_use;
  uint cur_key_col= 0;
  Item_field *cur_tmp_field;
  Item_func_lt *fn_less_than;

  key_column_count= bitmap_bits_set(columns_to_index);

  // TIMOUR: check for mem allocation err, revert to scan

  key_columns= (Item_field**) thd->alloc(key_column_count *
                                         sizeof(Item_field*));
  compare_pred= (Item_func_lt**) thd->alloc(key_column_count *
                                            sizeof(Item_func_lt*));

  for (uint i= 0; i < columns_to_index->n_bits; i++)
  {
    if (!bitmap_is_set(columns_to_index, i))
      continue;
    cur_tmp_field= new Item_field(tbl->field[i]);
    /* Create the predicate (tmp_column[i] < outer_ref[i]). */
    fn_less_than= new Item_func_lt(cur_tmp_field,
                                   search_key->element_index(i));
    fn_less_than->fix_fields(thd, (Item**) &fn_less_than);
    key_columns[cur_key_col]= cur_tmp_field;
    compare_pred[cur_key_col]= fn_less_than;
    ++cur_key_col;
  }

  if (alloc_keys_buffers())
  {
    /* TIMOUR revert to partial match via table scan. */
    return TRUE;
  }
  return FALSE;
}


/*
  Initialize a single-column index.
*/

bool Ordered_key::init(int col_idx)
{
  THD *thd= tbl->in_use;

  key_column_count= 1;

  // TIMOUR: check for mem allocation err, revert to scan

  key_columns= (Item_field**) thd->alloc(sizeof(Item_field*));
  compare_pred= (Item_func_lt**) thd->alloc(sizeof(Item_func_lt*));

  key_columns[0]= new Item_field(tbl->field[col_idx]);
  /* Create the predicate (tmp_column[i] < outer_ref[i]). */
  compare_pred[0]= new Item_func_lt(key_columns[0],
                                    search_key->element_index(col_idx));
  compare_pred[0]->fix_fields(thd, (Item**)&compare_pred[0]);

  if (alloc_keys_buffers())
  {
    /* TIMOUR revert to partial match via table scan. */
    return TRUE;
  }
  return FALSE;
}


/*
  Allocate the buffers for both the row number, and the NULL-bitmap indexes.
*/

bool Ordered_key::alloc_keys_buffers()
{
  DBUG_ASSERT(key_buff_elements > 0);

  if (!(key_buff= (rownum_t*) my_malloc(key_buff_elements * sizeof(rownum_t),
                                        MYF(MY_WME))))
    return TRUE;

  /*
    TIMOUR: it is enough to create bitmaps with size
    (max_null_row - min_null_row), and then use min_null_row as
    lookup offset.
  */
  /* Notice that max_null_row is max array index, we need count, so +1. */
  if (bitmap_init(&null_key, NULL, max_null_row + 1, FALSE))
    return TRUE;

  cur_key_idx= HA_POS_ERROR;

  return FALSE;
}


/*
  Quick sort comparison function that compares two rows of the same table
  indentfied with their row numbers.

  @retval -1
  @retval  0
  @retval +1
*/

int
Ordered_key::cmp_keys_by_row_data(ha_rows a, ha_rows b)
{
  uchar *rowid_a, *rowid_b;
  int error, cmp_res;
  /* The length in bytes of the rowids (positions) of tmp_table. */
  uint rowid_length= tbl->file->ref_length;

  if (a == b)
    return 0;
  /* Get the corresponding rowids. */
  rowid_a= row_num_to_rowid + a * rowid_length;
  rowid_b= row_num_to_rowid + b * rowid_length;
  /* Fetch the rows for comparison. */
  error= tbl->file->ha_rnd_pos(tbl->record[0], rowid_a);
  DBUG_ASSERT(!error);
  error= tbl->file->ha_rnd_pos(tbl->record[1], rowid_b);
  DBUG_ASSERT(!error);
  /*
    Compare the two rows by the corresponding values of the indexed
    columns.
  */
  for (uint i= 0; i < key_column_count; i++)
  {
    Field *cur_field= key_columns[i]->field;
    if ((cmp_res= cur_field->cmp_offset(tbl->s->rec_buff_length)))
      return (cmp_res > 0 ? 1 : -1);
  }
  return 0;
}


int
Ordered_key::cmp_keys_by_row_data_and_rownum(Ordered_key *key,
                                             rownum_t* a, rownum_t* b)
{
  /* The result of comparing the two keys according to their row data. */
  int cmp_row_res= key->cmp_keys_by_row_data(*a, *b);
  if (cmp_row_res)
    return cmp_row_res;
  return (*a < *b) ? -1 : (*a > *b) ? 1 : 0;
}


void Ordered_key::sort_keys()
{
  my_qsort2(key_buff, key_buff_elements, sizeof(rownum_t),
            (qsort2_cmp) &cmp_keys_by_row_data_and_rownum, (void*) this);
  /* Invalidate the current row position. */
  cur_key_idx= HA_POS_ERROR;
}


/*
  The fraction of rows that do not contain NULL in the columns indexed by
  this key.

  @retval  1  if there are no NULLs
  @retval  0  if only NULLs
*/

double Ordered_key::null_selectivity()
{
  /* We should not be processing empty tables. */
  DBUG_ASSERT(tbl->file->stats.records);
  return (1 - (double) null_count / (double) tbl->file->stats.records);
}


/*
  Compare the value(s) of the current key in 'search_key' with the
  data of the current table record.

  @notes The comparison result follows from the way compare_pred
  is created in Ordered_key::init. Currently compare_pred compares
  a field in of the current row with the corresponding Item that
  contains the search key.

  @param row_num  Number of the row (not index in the key_buff array)

  @retval -1  if (current row  < search_key)
  @retval  0  if (current row == search_key)
  @retval +1  if (current row  > search_key)
*/

int Ordered_key::cmp_key_with_search_key(rownum_t row_num)
{
  /* The length in bytes of the rowids (positions) of tmp_table. */
  uint rowid_length= tbl->file->ref_length;
  uchar *cur_rowid= row_num_to_rowid + row_num * rowid_length;
  int error, cmp_res;

  error= tbl->file->ha_rnd_pos(tbl->record[0], cur_rowid);
  DBUG_ASSERT(!error);

  for (uint i= 0; i < key_column_count; i++)
  {
    cmp_res= compare_pred[i]->get_comparator()->compare();
    /* Unlike Arg_comparator::compare_row() here there should be no NULLs. */
    DBUG_ASSERT(!compare_pred[i]->null_value);
    if (cmp_res)
      return (cmp_res > 0 ? 1 : -1);
  }
  return 0;
}


/*
  Find a key in a sorted array of keys via binary search.

  see create_subq_in_equalities()
*/

bool Ordered_key::lookup()
{
  DBUG_ASSERT(key_buff_elements);

  ha_rows lo= 0;
  ha_rows hi= key_buff_elements - 1;
  ha_rows mid;
  int cmp_res;

  while (lo <= hi)
  {
    mid= lo + (hi - lo) / 2;
    cmp_res= cmp_key_with_search_key(key_buff[mid]);
    /*
      In order to find the minimum match, check if the pevious element is
      equal or smaller than the found one. If equal, we need to search further
      to the left.
    */
    if (!cmp_res && mid > 0)
      cmp_res= !cmp_key_with_search_key(key_buff[mid - 1]) ? 1 : 0;

    if (cmp_res == -1)
    {
      /* row[mid] < search_key */
      lo= mid + 1;
    }
    else if (cmp_res == 1)
    {
      /* row[mid] > search_key */
      if (!mid)
        goto not_found;
      hi= mid - 1;
    }
    else
    {
      /* row[mid] == search_key */
      cur_key_idx= mid;
      return TRUE;
    }
  }
not_found:
  cur_key_idx= HA_POS_ERROR;
  return FALSE;
}


/*
  Move the current index pointer to the next key with the same column
  values as the current key. Since the index is sorted, all such keys
  are contiguous.
*/

bool Ordered_key::next_same()
{
  DBUG_ASSERT(key_buff_elements);

  if (cur_key_idx < key_buff_elements - 1)
  {
    /*
      TIMOUR:
      The below is quite inefficient, since as a result we will fetch every
      row (except the last one) twice. There must be a more efficient way,
      e.g. swapping record[0] and record[1], and reading only the new record.
    */
    if (!cmp_keys_by_row_data(key_buff[cur_key_idx], key_buff[cur_key_idx + 1]))
    {
      ++cur_key_idx;
      return TRUE;
    }
  }
  return FALSE;
}


void Ordered_key::print(String *str)
{
  uint i;
  str->append("{idx=");
  str->qs_append(keyid);
  str->append(", (");
  for (i= 0; i < key_column_count - 1; i++)
  {
    str->append(key_columns[i]->field->field_name);
    str->append(", ");
  }
  str->append(key_columns[i]->field->field_name);
  str->append("), ");

  str->append("null_bitmap: (bits=");
  str->qs_append(null_key.n_bits);
  str->append(", nulls= ");
  str->qs_append((double)null_count);
  str->append(", min_null= ");
  str->qs_append((double)min_null_row);
  str->append(", max_null= ");
  str->qs_append((double)max_null_row);
  str->append("), ");

  str->append('}');
}


subselect_partial_match_engine::subselect_partial_match_engine(
  subselect_uniquesubquery_engine *engine_arg,
  TABLE *tmp_table_arg, Item_subselect *item_arg,
  select_result_interceptor *result_arg,
  List<Item> *equi_join_conds_arg,
  uint covering_null_row_width_arg)
  :subselect_engine(item_arg, result_arg),
   tmp_table(tmp_table_arg), lookup_engine(engine_arg),
   equi_join_conds(equi_join_conds_arg),
   covering_null_row_width(covering_null_row_width_arg)
{}


int subselect_partial_match_engine::exec()
{
  Item_in_subselect *item_in= (Item_in_subselect *) item;
  int res;

  /* Try to find a matching row by index lookup. */
  res= lookup_engine->copy_ref_key_simple();
  if (res == -1)
  {
    /* The result is FALSE based on the outer reference. */
    item_in->value= 0;
    item_in->null_value= 0;
    return 0;
  }
  else if (res == 0)
  {
    /* Search for a complete match. */
    if ((res= lookup_engine->index_lookup()))
    {
      /* An error occured during lookup(). */
      item_in->value= 0;
      item_in->null_value= 0;
      return res;
    }
    else if (item_in->value)
    {
      /*
        A complete match was found, the result of IN is TRUE.
        Notice: (this->item == lookup_engine->item)
      */
      return 0;
    }
  }

  if (covering_null_row_width == tmp_table->s->fields)
  {
    /*
      If there is a NULL-only row that coveres all columns the result of IN
      is UNKNOWN. 
    */
    item_in->value= 0;
    /*
      TIMOUR: which one is the right way to propagate an UNKNOWN result?
      Should we also set empty_result_set= FALSE; ???
    */
    //item_in->was_null= 1;
    item_in->null_value= 1;
    return 0;
  }

  /*
    There is no complete match. Look for a partial match (UNKNOWN result), or
    no match (FALSE).
  */
  if (tmp_table->file->inited)
    tmp_table->file->ha_index_end();

  if (partial_match())
  {
    /* The result of IN is UNKNOWN. */
    item_in->value= 0;
    /*
      TIMOUR: which one is the right way to propagate an UNKNOWN result?
      Should we also set empty_result_set= FALSE; ???
    */
    //item_in->was_null= 1;
    item_in->null_value= 1;
  }
  else
  {
    /* The result of IN is FALSE. */
    item_in->value= 0;
    /*
      TIMOUR: which one is the right way to propagate an UNKNOWN result?
      Should we also set empty_result_set= FALSE; ???
    */
    //item_in->was_null= 0;
    item_in->null_value= 0;
  }

  return 0;
}


void subselect_partial_match_engine::print(String *str,
                                           enum_query_type query_type)
{
  /*
    Should never be called as the actual engine cannot be known at query
    optimization time.
  */
  DBUG_ASSERT(FALSE);
}


/*
  @param non_null_key_parts  
  @param partial_match_key_parts  A union of all single-column NULL key parts.
  @param count_partial_match_columns Number of NULL keyparts (set bits above).

  @retval FALSE  the engine was initialized successfully
  @retval TRUE   there was some (memory allocation) error during initialization,
                 such errors should be interpreted as revert to other strategy
*/

bool
subselect_rowid_merge_engine::init(MY_BITMAP *non_null_key_parts,
                                   MY_BITMAP *partial_match_key_parts)
{
  /* The length in bytes of the rowids (positions) of tmp_table. */
  uint rowid_length= tmp_table->file->ref_length;
  ha_rows row_count= tmp_table->file->stats.records;
  rownum_t cur_rownum= 0;
  select_materialize_with_stats *result_sink=
    (select_materialize_with_stats *) result;
  uint cur_keyid= 0;
  Item_in_subselect *item_in= (Item_in_subselect*) item;
  int error;

  if (keys_count == 0)
  {
    /* There is nothing to initialize, we will only do regular lookups. */
    return FALSE;
  }

  DBUG_ASSERT(!covering_null_row_width || (covering_null_row_width &&
                                           keys_count == 1 &&
                                           non_null_key_parts));
  /*
    Allocate buffers to hold the merged keys and the mapping between rowids and
    row numbers.
  */
  if (!(merge_keys= (Ordered_key**) thd->alloc(keys_count *
                                               sizeof(Ordered_key*))) ||
      !(row_num_to_rowid= (uchar*) my_malloc(row_count * rowid_length *
                                             sizeof(uchar), MYF(MY_WME))))
    return TRUE;

  /* Create the only non-NULL key if there is any. */
  if (non_null_key_parts)
  {
    non_null_key= new Ordered_key(cur_keyid, tmp_table, item_in->left_expr,
                                  0, 0, 0, row_num_to_rowid);
    if (non_null_key->init(non_null_key_parts))
      return TRUE;
    merge_keys[cur_keyid]= non_null_key;
    merge_keys[cur_keyid]->first();
    ++cur_keyid;
  }

  /*
    If there is a covering NULL row, the only key that is needed is the
    only non-NULL key that is already created above. We create keys on
    NULL-able columns only if there is no covering NULL row.
  */
  if (!covering_null_row_width)
  {
    if (bitmap_init_memroot(&matching_keys, keys_count, thd->mem_root) ||
        bitmap_init_memroot(&matching_outer_cols, keys_count, thd->mem_root) ||
        bitmap_init_memroot(&null_only_columns, keys_count, thd->mem_root))
      return TRUE;

    /*
      Create one single-column NULL-key for each column in
      partial_match_key_parts.
    */
    for (uint i= 0; i < partial_match_key_parts->n_bits; i++)
    {
      if (!bitmap_is_set(partial_match_key_parts, i))
        continue;

      if (result_sink->get_null_count_of_col(i) == row_count)
        bitmap_set_bit(&null_only_columns, cur_keyid);
      else
      {
        merge_keys[cur_keyid]= new Ordered_key(
                                     cur_keyid, tmp_table,
                                     item_in->left_expr->element_index(i),
                                     result_sink->get_null_count_of_col(i),
                                     result_sink->get_min_null_of_col(i),
                                     result_sink->get_max_null_of_col(i),
                                     row_num_to_rowid);
        if (merge_keys[cur_keyid]->init(i))
          return TRUE;
        merge_keys[cur_keyid]->first();
      }
      ++cur_keyid;
    }
  }

  /* Populate the indexes with data from the temporary table. */
  tmp_table->file->ha_rnd_init(1);
  tmp_table->file->extra_opt(HA_EXTRA_CACHE,
                             current_thd->variables.read_buff_size);
  tmp_table->null_row= 0;
  while (TRUE)
  {
    error= tmp_table->file->ha_rnd_next(tmp_table->record[0]);
    if (error == HA_ERR_RECORD_DELETED)
    {
      /* We get this for duplicate records that should not be in tmp_table. */
      continue;
    }
    /*
      This is a temp table that we fully own, there should be no other
      cause to stop the iteration than EOF.
    */
    DBUG_ASSERT(!error || error == HA_ERR_END_OF_FILE);
    if (error == HA_ERR_END_OF_FILE)
    {
      DBUG_ASSERT(cur_rownum == tmp_table->file->stats.records);
      break;
    }

    /*
      Save the position of this record in the row_num -> rowid mapping.
    */
    tmp_table->file->position(tmp_table->record[0]);
    memcpy(row_num_to_rowid + cur_rownum * rowid_length,
           tmp_table->file->ref, rowid_length);

    /* Add the current row number to the corresponding keys. */
    if (non_null_key)
    {
      /* By definition there are no NULLs in the non-NULL key. */
      non_null_key->add_key(cur_rownum);
    }

    for (uint i= (non_null_key ? 1 : 0); i < keys_count; i++)
    {
      /*
        Check if the first and only indexed column contains NULL in the curent
        row, and add the row number to the corresponding key.
      */
      if (tmp_table->field[merge_keys[i]->get_field_idx(0)]->is_null())
        merge_keys[i]->set_null(cur_rownum);
      else
        merge_keys[i]->add_key(cur_rownum);
    }
    ++cur_rownum;
  }

  tmp_table->file->ha_rnd_end();

  /* Sort all the keys by their NULL selectivity. */
  my_qsort(merge_keys, keys_count, sizeof(Ordered_key*),
           (qsort_cmp) cmp_keys_by_null_selectivity);

  /* Sort the keys in each of the indexes. */
  for (uint i= 0; i < keys_count; i++)
    merge_keys[i]->sort_keys();

  if (init_queue(&pq, keys_count, 0, FALSE,
                 subselect_rowid_merge_engine::cmp_keys_by_cur_rownum, NULL))
    return TRUE;

  return FALSE;
}


subselect_rowid_merge_engine::~subselect_rowid_merge_engine()
{
  /* None of the resources below is allocated if there are no ordered keys. */
  if (keys_count)
  {
    my_free((char*) row_num_to_rowid, MYF(0));
    for (uint i= 0; i < keys_count; i++)
      delete merge_keys[i];
    delete_queue(&pq);
    if (tmp_table->file->inited == handler::RND)
      tmp_table->file->ha_rnd_end();
  }
}


void subselect_rowid_merge_engine::cleanup()
{
}


/*
  Quick sort comparison function to compare keys in order of decreasing bitmap
  selectivity, so that the most selective keys come first.

  @param  k1 first key to compare
  @param  k2 second key to compare

  @retval  1  if k1 is less selective than k2
  @retval  0  if k1 is equally selective as k2
  @retval -1  if k1 is more selective than k2
*/

int
subselect_rowid_merge_engine::cmp_keys_by_null_selectivity(Ordered_key **k1,
                                                           Ordered_key **k2)
{
  double k1_sel= (*k1)->null_selectivity();
  double k2_sel= (*k2)->null_selectivity();
  if (k1_sel < k2_sel)
    return 1;
  if (k1_sel > k2_sel)
    return -1;
  return 0;
}


/*
*/

int
subselect_rowid_merge_engine::cmp_keys_by_cur_rownum(void *arg,
                                                     uchar *k1, uchar *k2)
{
  rownum_t r1= ((Ordered_key*) k1)->current();
  rownum_t r2= ((Ordered_key*) k2)->current();

  return (r1 < r2) ? -1 : (r1 > r2) ? 1 : 0;
}


/*
  Check if certain table row contains a NULL in all columns for which there is
  no match in the corresponding value index.

  @retval TRUE if a NULL row exists
  @retval FALSE otherwise
*/

bool subselect_rowid_merge_engine::test_null_row(rownum_t row_num)
{
  Ordered_key *cur_key;
  uint cur_id;
  for (uint i = 0; i < keys_count; i++)
  {
    cur_key= merge_keys[i];
    cur_id= cur_key->get_keyid();
    if (bitmap_is_set(&matching_keys, cur_id))
    {
      /*
        The key 'i' (with id 'cur_keyid') already matches a value in row 'row_num',
        thus we skip it as it can't possibly match a NULL.
      */
      continue;
    }
    if (!cur_key->is_null(row_num))
      return FALSE;
  }
  return TRUE;
}


/*
  @retval TRUE  there is a partial match (UNKNOWN)
  @retval FALSE  there is no match at all (FALSE)
*/

bool subselect_rowid_merge_engine::partial_match()
{
  Ordered_key *min_key; /* Key that contains the current minimum position. */
  rownum_t min_row_num; /* Current row number of min_key. */
  Ordered_key *cur_key;
  rownum_t cur_row_num;
  uint count_nulls_in_search_key= 0;
  bool res= FALSE;

  /* If there is a non-NULL key, it must be the first key in the keys array. */
  DBUG_ASSERT(!non_null_key || (non_null_key && merge_keys[0] == non_null_key));

  /* All data accesses during execution are via handler::ha_rnd_pos() */
  tmp_table->file->ha_rnd_init(0);

  /* Check if there is a match for the columns of the only non-NULL key. */
  if (non_null_key && !non_null_key->lookup())
  {
    res= FALSE;
    goto end;
  }

  /*
    If there is a NULL (sub)row that covers all NULL-able columns,
    then there is a guranteed partial match, and we don't need to search
    for the matching row.
   */
  if (covering_null_row_width)
  {
    res= TRUE;
    goto end;
  }

  if (non_null_key)
    queue_insert(&pq, (uchar *) non_null_key);
  /*
    Do not add the non_null_key, since it was already processed above.
  */
  bitmap_clear_all(&matching_outer_cols);
  for (uint i= test(non_null_key); i < keys_count; i++)
  {
    DBUG_ASSERT(merge_keys[i]->get_column_count() == 1);
    if (merge_keys[i]->get_search_key(0)->is_null())
    {
      ++count_nulls_in_search_key;
      bitmap_set_bit(&matching_outer_cols, merge_keys[i]->get_keyid());
    }
    else if (merge_keys[i]->lookup())
      queue_insert(&pq, (uchar *) merge_keys[i]);
  }

  /*
    If the outer reference consists of only NULLs, or if it has NULLs in all
    nullable columns, the result is UNKNOWN.
  */
  if (count_nulls_in_search_key ==
      ((Item_in_subselect *) item)->left_expr->cols() -
      (non_null_key ? non_null_key->get_column_count() : 0))
  {
    res= TRUE;
    goto end;
  }

  /*
    If there is no NULL (sub)row that covers all NULL columns, and there is no
    single match for any of the NULL columns, the result is FALSE.
  */
  if (pq.elements - test(non_null_key) == 0)
  {
    res= FALSE;
    goto end;
  }

  DBUG_ASSERT(pq.elements);

  min_key= (Ordered_key*) queue_remove(&pq, 0);
  min_row_num= min_key->current();
  bitmap_copy(&matching_keys, &null_only_columns);
  bitmap_set_bit(&matching_keys, min_key->get_keyid());
  bitmap_union(&matching_keys, &matching_outer_cols);
  if (min_key->next_same())
    queue_insert(&pq, (uchar *) min_key);

  if (pq.elements == 0)
  {
    /*
      Check the only matching row of the only key min_key for NULL matches
      in the other columns.
    */
    res= test_null_row(min_row_num);
    goto end;
  }

  while (TRUE)
  {
    cur_key= (Ordered_key*) queue_remove(&pq, 0);
    cur_row_num= cur_key->current();

    if (cur_row_num == min_row_num)
      bitmap_set_bit(&matching_keys, cur_key->get_keyid());
    else
    {
      /* Follows from the correct use of priority queue. */
      DBUG_ASSERT(cur_row_num > min_row_num);
      if (test_null_row(min_row_num))
      {
        res= TRUE;
        goto end;
      }
      else
      {
        min_key= cur_key;
        min_row_num= cur_row_num;
        bitmap_copy(&matching_keys, &null_only_columns);
        bitmap_set_bit(&matching_keys, min_key->get_keyid());
        bitmap_union(&matching_keys, &matching_outer_cols);
      }
    }

    if (cur_key->next_same())
      queue_insert(&pq, (uchar *) cur_key);

    if (pq.elements == 0)
    {
      /* Check the last row of the last column in PQ for NULL matches. */
      res= test_null_row(min_row_num);
      goto end;
    }
  }

  /* We should never get here - all branches must be handled explicitly above. */
  DBUG_ASSERT(FALSE);

end:
  tmp_table->file->ha_rnd_end();
  return res;
}


subselect_table_scan_engine::subselect_table_scan_engine(
  subselect_uniquesubquery_engine *engine_arg,
  TABLE *tmp_table_arg,
  Item_subselect *item_arg,
  select_result_interceptor *result_arg,
  List<Item> *equi_join_conds_arg,
  uint covering_null_row_width_arg)
  :subselect_partial_match_engine(engine_arg, tmp_table_arg, item_arg,
                                  result_arg, equi_join_conds_arg,
                                  covering_null_row_width_arg)
{}


/*
  TIMOUR:
  This method is based on subselect_uniquesubquery_engine::scan_table().
  Consider refactoring somehow, 80% of the code is the same.

  for each row_i in tmp_table
  {
    count_matches= 0;
    for each row element row_i[j]
    {
      if (outer_ref[j] is NULL || row_i[j] is NULL || outer_ref[j] == row_i[j])
        ++count_matches;
    }
    if (count_matches == outer_ref.elements)
      return TRUE
  }
  return FALSE
*/

bool subselect_table_scan_engine::partial_match()
{
  List_iterator_fast<Item> equality_it(*equi_join_conds);
  Item *cur_eq;
  uint count_matches;
  int error;
  bool res;

  tmp_table->file->ha_rnd_init(1);
  tmp_table->file->extra_opt(HA_EXTRA_CACHE,
                             current_thd->variables.read_buff_size);
  /*
  TIMOUR:
  scan_table() also calls "table->null_row= 0;", why, do we need it?
  */
  for (;;)
  {
    error= tmp_table->file->ha_rnd_next(tmp_table->record[0]);
    if (error) {
      if (error == HA_ERR_RECORD_DELETED)
      {
        error= 0;
        continue;
      }
      if (error == HA_ERR_END_OF_FILE)
      {
        error= 0;
        break;
      }
      else
      {
        error= report_error(tmp_table, error);
        break;
      }
    }

    equality_it.rewind();
    count_matches= 0;
    while ((cur_eq= equality_it++))
    {
      DBUG_ASSERT(cur_eq->type() == Item::FUNC_ITEM &&
                  ((Item_func*)cur_eq)->functype() == Item_func::EQ_FUNC);
      if (!cur_eq->val_int() && !cur_eq->null_value)
        break;
      ++count_matches;
    }
    if (count_matches == tmp_table->s->fields)
    {
      res= TRUE; /* Found a matching row. */
      goto end;
    }
  }

  res= FALSE;
end:
  tmp_table->file->ha_rnd_end();
  return res;
}


void subselect_table_scan_engine::cleanup()
{
}

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

/* 
   subselect Item

SUBSELECT TODO:
   - add function from mysql_select that use JOIN* as parameter to JOIN methods
     (sql_select.h/sql_select.cc)
   - remove double 'having' & 'having_list' from JOIN
     (sql_select.h/sql_select.cc)

*/

#ifdef __GNUC__
#pragma implementation				// gcc: Class implementation
#endif

#include "mysql_priv.h"
#include "sql_select.h"

Item_subselect::Item_subselect():
  Item_result_field(), engine_owner(1), value_assigned(0), substitution(0),
  have_to_be_excluded(0)
{
  reset();
  /*
    item value is NULL if select_subselect not changed this value 
    (i.e. some rows will be found returned)
  */
  null_value= 1;
}

void Item_subselect::init(THD *thd, st_select_lex *select_lex,
			  select_subselect *result)
{

  DBUG_ENTER("Item_subselect::init");
  DBUG_PRINT("subs", ("select_lex 0x%xl", (ulong) select_lex));

  select_transformer(select_lex);
  if (select_lex->next_select())
    engine= new subselect_union_engine(thd, select_lex->master_unit(), result,
				       this);
  else
    engine= new subselect_single_select_engine(thd, select_lex, result,
					       this);
  DBUG_VOID_RETURN;
}

Item_subselect::~Item_subselect()
{
  if (engine_owner)
    delete engine;
}

void Item_subselect::select_transformer(st_select_lex *select_lex) 
{
  DBUG_ENTER("Item_subselect::select_transformer");
  DBUG_VOID_RETURN;
}


bool Item_subselect::fix_fields(THD *thd, TABLE_LIST *tables, Item **ref)
{
  if (substitution)
  {
    (*ref)= substitution;
    substitution->name= name;
    if (have_to_be_excluded)
      engine->exclude();
    substitution= 0;
    return (*ref)->fix_fields(thd, tables, ref);
  }

  char const *save_where= thd->where;
  int res= engine->prepare();
  if (!res)
  {
    // Is it one field subselect?
    if (engine->cols() > max_columns)
    {  
      my_error(ER_CARDINALITY_COL, MYF(0), 1);
      return 1;
    }
    fix_length_and_dec();
  }
  fixed= 1;
  thd->where= save_where;
  return res;
}

bool Item_subselect::check_loop(uint id)
{
  DBUG_ENTER("Item_subselect::check_loop");
  if (Item_result_field::check_loop(id))
    DBUG_RETURN(1);
  
  DBUG_RETURN(engine->check_loop(id));
}

Item::Type Item_subselect::type() const 
{
  return SUBSELECT_ITEM;
}

void Item_subselect::fix_length_and_dec()
{
  engine->fix_length_and_dec(0);
}

inline table_map Item_subselect::used_tables() const
{
  return (table_map) engine->depended() ? 1L : 0L; 
}

Item_singlerow_subselect::Item_singlerow_subselect(THD *thd,
						   st_select_lex *select_lex):
  Item_subselect(), value(0)
{
  DBUG_ENTER("Item_singlerow_subselect::Item_singlerow_subselect");
  init(thd, select_lex, new select_singlerow_subselect(this));
  max_columns= 1;
  maybe_null= 1;
  max_columns= UINT_MAX;
  DBUG_VOID_RETURN;
}

void Item_singlerow_subselect::reset()
{
  null_value= 1;
  if (value)
    value->null_value= 1;
}

void Item_singlerow_subselect::store(uint i, Item *item)
{
  row[i]->store(item);
}

enum Item_result Item_singlerow_subselect::result_type() const
{
  return engine->type();
}

void Item_singlerow_subselect::fix_length_and_dec()
{
  if ((max_columns= engine->cols()) == 1)
  {
    engine->fix_length_and_dec(row= &value);
    if (!(value= Item_cache::get_cache(engine->type())))
    {
      my_message(ER_OUT_OF_RESOURCES, ER(ER_OUT_OF_RESOURCES), MYF(0));
      current_thd->fatal_error= 1;
      return;
    }  
  }
  else
  {
    THD *thd= current_thd;
    if (!(row= (Item_cache**)thd->alloc(sizeof(Item_cache*)*max_columns)))
    {
      my_message(ER_OUT_OF_RESOURCES, ER(ER_OUT_OF_RESOURCES), MYF(0));
      thd->fatal_error= 1;
      return;
    }
    engine->fix_length_and_dec(row);
    value= *row;
  }
}

uint Item_singlerow_subselect::cols()
{
  return engine->cols();
}

bool Item_singlerow_subselect::check_cols(uint c)
{
  if (c != engine->cols())
  {
    my_error(ER_CARDINALITY_COL, MYF(0), c);
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
  engine->exec();
}

double Item_singlerow_subselect::val () 
{
  if (!engine->exec() && !value->null_value)
  {
    null_value= 0;
    return value->val();
  }
  else
  {
    reset();
    return 0;
  }
}

longlong Item_singlerow_subselect::val_int () 
{
  if (!engine->exec() && !value->null_value)
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

String *Item_singlerow_subselect::val_str (String *str) 
{
  if (!engine->exec() && !value->null_value)
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

Item_exists_subselect::Item_exists_subselect(THD *thd,
					     st_select_lex *select_lex):
  Item_subselect()
{
  DBUG_ENTER("Item_exists_subselect::Item_exists_subselect");
  init(thd, select_lex, new select_exists_subselect(this));
  max_columns= UINT_MAX;
  null_value= 0; //can't be NULL
  maybe_null= 0; //can't be NULL
  value= 0;
  // We need only 1 row to determinate existence
  select_lex->master_unit()->global_parameters->select_limit= 1;
  DBUG_VOID_RETURN;
}

Item_in_subselect::Item_in_subselect(THD *thd, Item * left_exp,
				     st_select_lex *select_lex):
  Item_exists_subselect()
{
  DBUG_ENTER("Item_in_subselect::Item_in_subselect");
  left_expr= left_exp;
  init(thd, select_lex, new select_exists_subselect(this));
  max_columns= UINT_MAX;
  maybe_null= 1;
  reset();
  // We need only 1 row to determinate existence
  select_lex->master_unit()->global_parameters->select_limit= 1;
  DBUG_VOID_RETURN;
}

Item_allany_subselect::Item_allany_subselect(THD *thd, Item * left_exp,
					     compare_func_creator f,
					     st_select_lex *select_lex):
  Item_in_subselect()
{
  DBUG_ENTER("Item_in_subselect::Item_in_subselect");
  left_expr= left_exp;
  func= f;
  init(thd, select_lex, new select_exists_subselect(this));
  max_columns= 1;
  reset();
  // We need only 1 row to determinate existence
  select_lex->master_unit()->global_parameters->select_limit= 1;
  DBUG_VOID_RETURN;
}


void Item_exists_subselect::fix_length_and_dec()
{
   decimals= 0;
   max_length= 1;
   max_columns= engine->cols();
}

double Item_exists_subselect::val () 
{
  if (engine->exec())
  {
    reset();
    return 0;
  }
  return (double) value;
}

longlong Item_exists_subselect::val_int () 
{
  if (engine->exec())
  {
    reset();
    return 0;
  }
  return value;
}

String *Item_exists_subselect::val_str(String *str)
{
  if (engine->exec())
  {
    reset();
    return 0;
  }
  str->set(value,thd_charset());
  return str;
}

double Item_in_subselect::val () 
{
  if (engine->exec())
  {
    reset();
    null_value= 1;
    return 0;
  }
  if (was_null && !value)
    null_value= 1;
  return (double) value;
}

longlong Item_in_subselect::val_int () 
{
  if (engine->exec())
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
  if (engine->exec())
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
  str->set(value,thd_charset());
  return str;
}

Item_in_subselect::Item_in_subselect(Item_in_subselect *item):
  Item_exists_subselect(item)
{
  left_expr= item->left_expr;
}

Item_allany_subselect::Item_allany_subselect(Item_allany_subselect *item):
  Item_in_subselect(item)
{
  func= item->func;
}

void Item_in_subselect::single_value_transformer(st_select_lex *select_lex,
						 Item *left_expr,
						 compare_func_creator func)
{
  DBUG_ENTER("Item_in_subselect::single_value_transformer");
  if (select_lex->master_unit()->global_parameters->select_limit != 
      HA_POS_ERROR)
  {
    my_error(ER_NOT_SUPPORTED_YET, MYF(0),
	     "LIMIT & IN/ALL/ANY/SOME subquery");
    DBUG_VOID_RETURN;
  }
  Item_in_optimizer *optimizer;
  substitution= optimizer= new Item_in_optimizer(left_expr, this);
  if (!optimizer)
  {
    current_thd->fatal_error= 1;
    DBUG_VOID_RETURN;
  }
  /*
    As far as  Item_ref_in_optimizer do not substitude itself on fix_fields
    we can use same item for all selects.
  */
  Item *expr= new Item_ref((Item**)optimizer->get_cache(), 
			   (char *)"<no matter>",
			   (char*)"<left expr>");
  select_lex->master_unit()->dependent= 1;
  for (SELECT_LEX * sl= select_lex; sl; sl= sl->next_select())
  {
    if (select_lex->select_limit != HA_POS_ERROR)
    {
      my_error(ER_NOT_SUPPORTED_YET, MYF(0),
	       "LIMIT & IN/ALL/ANY/SOME subquery");
      DBUG_VOID_RETURN;
    }

    select_lex->dependent= 1;
    Item *item;
    if (sl->item_list.elements > 1)
    {
      my_error(ER_CARDINALITY_COL, MYF(0), 1);
      DBUG_VOID_RETURN;
    }
    else
      item= (Item*) sl->item_list.pop();

    if (sl->having || sl->with_sum_func || sl->group_list.first ||
	sl->order_list.first)
    {
      sl->item_list.push_back(item);
      item= (*func)(expr, new Item_ref_null_helper(this,
						   sl->item_list.head_ref(),
						   (char *)"<no matter>",
						   (char*)"<result>"));
      if (sl->having || sl->with_sum_func || sl->group_list.first)
	if (sl->having)
	  sl->having= new Item_cond_and(sl->having, item);
	else
	  sl->having= item;
      else
	if (sl->where)
	  sl->where= new Item_cond_and(sl->where, item);
	else
	  sl->where= item;
    }
    else
    {
      sl->item_list.empty();
      sl->item_list.push_back(new Item_int(1));
      if (sl->table_list.elements)
      {
	item= (*func)(expr, new Item_asterisk_remover(this, item,
						      (char *)"<no matter>",
						      (char*)"<result>"));
	if (sl->where)
	  sl->where= new Item_cond_and(sl->where, item);
	else
	  sl->where= item;
      }
      else
      {
	if (item->type() == Item::FIELD_ITEM &&
	    ((Item_field*) item)->field_name[0] == '*')
	{
	  my_error(ER_NO_TABLES_USED, MYF(0));
	  DBUG_VOID_RETURN;
	}
	if (select_lex->next_select())
	{
	  /* 
	     It is in union => we should perform it.
	     Item_asterisk_remover used only as wrapper to receine NULL value
	  */
	  sl->having= (*func)(expr, 
			      new Item_asterisk_remover(this, item,
							(char *)"<no matter>",
							(char*)"<result>"));
	}
	else
	{
	  // it is single select without tables => possible optimization
	  item= (*func)(left_expr, item);
	  substitution= item;
	  have_to_be_excluded= 1;
	  THD *thd= current_thd;
	  if (thd->lex.describe)
	  {
	    char warn_buff[MYSQL_ERRMSG_SIZE];
	    sprintf(warn_buff, ER(ER_SELECT_REDUCED), sl->select_number);
	    push_warning(thd, MYSQL_ERROR::WARN_LEVEL_NOTE,
			 ER_SELECT_REDUCED, warn_buff);
	  }
	}
      }
    }
  }
  DBUG_VOID_RETURN;
}

void Item_in_subselect::row_value_transformer(st_select_lex *select_lex,
					      Item *left_expr)
{
  DBUG_ENTER("Item_in_subselect::row_value_transformer");
  Item_in_optimizer *optimizer;
  substitution= optimizer= new Item_in_optimizer(left_expr, this);
  if (!optimizer)
  {
    current_thd->fatal_error= 1;
    DBUG_VOID_RETURN;
  }
  select_lex->master_unit()->dependent= 1;
  uint n= left_expr->cols();
  if (optimizer->preallocate_row() || (*optimizer->get_cache())->allocate(n))
    DBUG_VOID_RETURN;
  for (SELECT_LEX * sl= select_lex; sl; sl= sl->next_select())
  {
    select_lex->dependent= 1;

    Item *item= 0;
    List_iterator_fast<Item> li(sl->item_list);
    for (uint i= 0; i < n; i++)
    {
      Item *func=
	new Item_ref_on_list_position(this, sl->item_list, i,
					       (char *) "<no matter>",
					       (char *) "<list ref>");
      func=
	Item_bool_func2::eq_creator(new Item_ref((*optimizer->get_cache())->
						 addr(i), 
						 (char *)"<no matter>",
						 (char *)"<left expr>"),
				    func);
      if (!item)
	item= func;
      else
	item= new Item_cond_and(item, func);
    }

    if (sl->having || sl->with_sum_func || sl->group_list.first ||
	!sl->table_list.elements)
      if (sl->having)
	sl->having= new Item_cond_and(sl->having, item);
      else
	sl->having= item;
    else
      if (sl->where)
	sl->where= new Item_cond_and(sl->where, item);
      else
	sl->where= item;
  }
  DBUG_VOID_RETURN;
}


void Item_in_subselect::select_transformer(st_select_lex *select_lex)
{
  if (left_expr->cols() == 1)
    single_value_transformer(select_lex, left_expr,
			     &Item_bool_func2::eq_creator);
  else
    row_value_transformer(select_lex, left_expr);
}

void Item_allany_subselect::select_transformer(st_select_lex *select_lex)
{
  single_value_transformer(select_lex, left_expr, func);
}

subselect_single_select_engine::subselect_single_select_engine(THD *thd, 
							       st_select_lex *select,
							       select_subselect *result,
							       Item_subselect *item):
  subselect_engine(thd, item, result),
    prepared(0), optimized(0), executed(0)
{
  select_lex= select;
  SELECT_LEX_UNIT *unit= select_lex->master_unit();
  unit->offset_limit_cnt= unit->global_parameters->offset_limit;
  unit->select_limit_cnt= unit->global_parameters->select_limit+
    unit->global_parameters ->offset_limit;
  if (unit->select_limit_cnt < unit->global_parameters->select_limit)
    unit->select_limit_cnt= HA_POS_ERROR;		// no limit
  if (unit->select_limit_cnt == HA_POS_ERROR)
    select_lex->options&= ~OPTION_FOUND_ROWS;
  join= new JOIN(thd, select_lex->item_list, select_lex->options, result);
  if (!join || !result)
  {
    //out of memory
    thd->fatal_error= 1;
    my_message(ER_OUT_OF_RESOURCES, ER(ER_OUT_OF_RESOURCES), MYF(0));
  } 
  unit->item= item;
  this->select_lex= select_lex;
}

subselect_union_engine::subselect_union_engine(THD *thd,
					       st_select_lex_unit *u,
					       select_subselect *result,
					       Item_subselect *item):
  subselect_engine(thd, item, result)
{
  unit= u;
  if( !result)
  {
    //out of memory
    thd->fatal_error= 1;
    my_message(ER_OUT_OF_RESOURCES, ER(ER_OUT_OF_RESOURCES), MYF(0));
  }
  unit->item= item;
}

int subselect_single_select_engine::prepare()
{
  if (prepared)
    return 0;
  prepared= 1;
  SELECT_LEX_NODE *save_select= thd->lex.current_select;
  thd->lex.current_select= select_lex;
  if(join->prepare((TABLE_LIST*) select_lex->table_list.first,
		   select_lex->where,
		   (ORDER*) select_lex->order_list.first,
		   (ORDER*) select_lex->group_list.first,
		   select_lex->having,
		   (ORDER*) 0, select_lex, 
		   select_lex->master_unit(), 0))
    return 1;
  thd->lex.current_select= save_select;
  return 0;
}

int subselect_union_engine::prepare()
{
  return unit->prepare(thd, result);
}

static Item_result set_row(SELECT_LEX *select_lex, Item * item,
			   Item_cache **row)
{
  Item_result res_type= STRING_RESULT;
  Item *sel_item;
  List_iterator_fast<Item> li(select_lex->item_list);
  for (uint i= 0; (sel_item= li++); i++)
  {
    item->max_length= sel_item->max_length;
    res_type= sel_item->result_type();
    item->decimals= sel_item->decimals;
    if (row)
    {
      if (!(row[i]= Item_cache::get_cache(res_type)))
      {
	my_message(ER_OUT_OF_RESOURCES, ER(ER_OUT_OF_RESOURCES), MYF(0));
	current_thd->fatal_error= 1;
	return STRING_RESULT; // we should return something
      }
      row[i]->set_len_n_dec(sel_item->max_length, sel_item->decimals);
    }
  }
  if (select_lex->item_list.elements > 1)
    res_type= ROW_RESULT;
  return res_type;
}

void subselect_single_select_engine::fix_length_and_dec(Item_cache **row)
{
  DBUG_ASSERT(row || select_lex->item_list.elements==1);
  res_type= set_row(select_lex, item, row);
}

void subselect_union_engine::fix_length_and_dec(Item_cache **row)
{
  DBUG_ASSERT(row || unit->first_select()->item_list.elements==1);

  if (unit->first_select()->item_list.elements == 1)
  {
    uint32 mlen= 0, len;
    Item *sel_item= 0;
    for (SELECT_LEX *sl= unit->first_select(); sl; sl= sl->next_select())
    {
      List_iterator_fast<Item> li(sl->item_list);
      Item *s_item= li++;
      if ((len= s_item->max_length) > mlen)
	mlen= len;
      if (!sel_item)
	sel_item= s_item;
    }
    item->max_length= mlen;
    res_type= sel_item->result_type();
    item->decimals= sel_item->decimals;
    if (row)
    {
      if (!(row[0]= Item_cache::get_cache(res_type)))
      {
	my_message(ER_OUT_OF_RESOURCES, ER(ER_OUT_OF_RESOURCES), MYF(0));
	current_thd->fatal_error= 1;
	return;
      }
      row[0]->set_len_n_dec(mlen, sel_item->decimals);
    }
  }
  else
  {
    SELECT_LEX *sl= unit->first_select();
    res_type= set_row(sl, item, row);
    for(sl= sl->next_select(); sl; sl->next_select())
    {
      List_iterator_fast<Item> li(sl->item_list);
      Item *sel_item;
      for (uint i= 0; (sel_item= li++); i++)
      {
	if (sel_item->max_length > row[i]->max_length)
	  row[i]->max_length= sel_item->max_length;
      }
    }
  }
}

int subselect_single_select_engine::exec()
{
  DBUG_ENTER("subselect_single_select_engine::exec");
  char const *save_where= join->thd->where;
  if (!optimized)
  {
    optimized=1;
    if (join->optimize())
    {
      join->thd->where= save_where;
      executed= 1;
      DBUG_RETURN(join->error?join->error:1);
    }
  }
  if (select_lex->dependent && executed)
  {
    if (join->reinit())
    {
      join->thd->where= save_where;
      DBUG_RETURN(1);
    }
    item->reset();
    item->assigned((executed= 0));
  }
  if (!executed)
  {
    SELECT_LEX_NODE *save_select= join->thd->lex.current_select;
    join->thd->lex.current_select= select_lex;
    join->exec();
    join->thd->lex.current_select= save_select;
    executed= 1;
    join->thd->where= save_where;
    DBUG_RETURN(join->error||thd->fatal_error);
  }
  join->thd->where= save_where;
  DBUG_RETURN(0);
}

int subselect_union_engine::exec()
{
  char const *save_where= unit->thd->where;
  int res= unit->exec();
  unit->thd->where= save_where;
  return res;
}

uint subselect_single_select_engine::cols()
{
  return select_lex->item_list.elements;
}

uint subselect_union_engine::cols()
{
  return unit->first_select()->item_list.elements;
}

bool subselect_single_select_engine::depended()
{
  return select_lex->dependent;
}

bool subselect_union_engine::depended()
{
  return unit->dependent;
}

bool subselect_single_select_engine::check_loop(uint id)
{
  DBUG_ENTER("subselect_single_select_engine::check_loop");
  DBUG_RETURN(join->check_loop(id));
}

bool subselect_union_engine::check_loop(uint id)
{
  DBUG_ENTER("subselect_union_engine::check_loop");
  for (SELECT_LEX *sl= unit->first_select(); sl; sl= sl->next_select())
    if (sl->join && sl->join->check_loop(id))
      DBUG_RETURN(1);
  DBUG_RETURN(0);
}

void subselect_single_select_engine::exclude()
{
  select_lex->master_unit()->exclude_level();
  //if (current_thd->lex->describe)
}

void subselect_union_engine::exclude()
{
  unit->exclude_level();
  // for (SELECT_LEX *sl= unit->first_select(); sl; sl= sl->next_select())
  //  if (sl->join && sl->join->check_loop(id))
  //    DBUG_RETURN(1);
}

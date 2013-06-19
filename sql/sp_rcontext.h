/* -*- C++ -*- */
/* Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef _SP_RCONTEXT_H_
#define _SP_RCONTEXT_H_

#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

#include "sql_class.h"                    // select_result_interceptor
#include "sp_pcontext.h"                  // sp_condition_value

///////////////////////////////////////////////////////////////////////////
// sp_rcontext declaration.
///////////////////////////////////////////////////////////////////////////

class sp_cursor;
class sp_lex_keeper;
class sp_instr_cpush;
class Query_arena;
class sp_head;
class Item_cache;


/*
  This class is a runtime context of a Stored Routine. It is used in an
  execution and is intended to contain all dynamic objects (i.e.  objects, which
  can be changed during execution), such as:
    - stored routine variables;
    - cursors;
    - handlers;

  Runtime context is used with sp_head class. sp_head class is intended to
  contain all static things, related to the stored routines (code, for example).
  sp_head instance creates runtime context for the execution of a stored
  routine.

  There is a parsing context (an instance of sp_pcontext class), which is used
  on parsing stage. However, now it contains some necessary for an execution
  things, such as definition of used stored routine variables. That's why
  runtime context needs a reference to the parsing context.
*/

class sp_rcontext : public Sql_alloc
{
public:
  /// Construct and properly initialize a new sp_rcontext instance. The static
  /// create-function is needed because we need a way to return an error from
  /// the constructor.
  ///
  /// @param thd              Thread handle.
  /// @param root_parsing_ctx Top-level parsing context for this stored program.
  /// @param return_value_fld Field object to store the return value
  ///                         (for stored functions only).
  ///
  /// @return valid sp_rcontext object or NULL in case of OOM-error.
  static sp_rcontext *create(THD *thd,
                             const sp_pcontext *root_parsing_ctx,
                             Field *return_value_fld);

  ~sp_rcontext();

private:
  sp_rcontext(const sp_pcontext *root_parsing_ctx,
              Field *return_value_fld,
              bool in_sub_stmt);

  // Prevent use of copying constructor and operator.
  sp_rcontext(const sp_rcontext &);
  void operator=(sp_rcontext &);

private:
  /// This is an auxillary class to store entering instruction pointer for an
  /// SQL-handler.
  class sp_handler_entry : public Sql_alloc
  {
  public:
    /// Handler definition (from parsing context).
    const sp_handler *handler;

    /// Instruction pointer to the first instruction.
    uint first_ip;

    /// The constructor.
    ///
    /// @param _handler   sp_handler object.
    /// @param _first_ip  first instruction pointer.
    sp_handler_entry(const sp_handler *_handler, uint _first_ip)
     :handler(_handler), first_ip(_first_ip)
    { }
  };

public:
  /// This class stores basic information about SQL-condition, such as:
  ///   - SQL error code;
  ///   - error level;
  ///   - SQLSTATE;
  ///   - text message.
  ///
  /// It's used to organize runtime SQL-handler call stack.
  ///
  /// Standard Sql_condition class can not be used, because we don't always have
  /// an Sql_condition object for an SQL-condition in Diagnostics_area.
  ///
  /// Eventually, this class should be moved to sql_error.h, and be a part of
  /// standard SQL-condition processing (Diagnostics_area should contain an
  /// object for active SQL-condition, not just information stored in DA's
  /// fields).
  class Sql_condition_info : public Sql_alloc
  {
  public:
    /// SQL error code.
    uint sql_errno;

    /// Error level.
    Sql_condition::enum_warning_level level;

    /// SQLSTATE.
    char sql_state[SQLSTATE_LENGTH + 1];

    /// Text message.
    char *message;

    /// The constructor.
    ///
    /// @param _sql_condition  The SQL condition.
    /// @param arena           Query arena for SP
    Sql_condition_info(const Sql_condition *_sql_condition,
                       Query_arena *arena)
      :sql_errno(_sql_condition->get_sql_errno()),
       level(_sql_condition->get_level())
    {
      memcpy(sql_state, _sql_condition->get_sqlstate(), SQLSTATE_LENGTH);
      sql_state[SQLSTATE_LENGTH]= '\0';

      message= strdup_root(arena->mem_root, _sql_condition->get_message_text());
    }
  };

private:
  /// This class represents a call frame of SQL-handler (one invocation of a
  /// handler). Basically, it's needed to store continue instruction pointer for
  /// CONTINUE SQL-handlers.
  class Handler_call_frame : public Sql_alloc
  {
  public:
    /// SQL-condition, triggered handler activation.
    const Sql_condition_info *sql_condition;

    /// Continue-instruction-pointer for CONTINUE-handlers.
    /// The attribute contains 0 for EXIT-handlers.
    uint continue_ip;

    /// The constructor.
    ///
    /// @param _sql_condition SQL-condition, triggered handler activation.
    /// @param _continue_ip   Continue instruction pointer.
    Handler_call_frame(const Sql_condition_info *_sql_condition,
                       uint _continue_ip)
     :sql_condition(_sql_condition),
      continue_ip(_continue_ip)
    { }
 };

public:
  /// Arena used to (re) allocate items on. E.g. reallocate INOUT/OUT
  /// SP-variables when they don't fit into prealloced items. This is common
  /// situation with String items. It is used mainly in sp_eval_func_item().
  Query_arena *callers_arena;

  /// Flag to end an open result set before start executing an SQL-handler
  /// (if one is found). Otherwise the client will hang due to a violation
  /// of the client/server protocol.
  bool end_partial_result_set;

#ifndef DBUG_OFF
  /// The stored program for which this runtime context is created. Used for
  /// checking if correct runtime context is used for variable handling.
  sp_head *sp;
#endif

  /////////////////////////////////////////////////////////////////////////
  // SP-variables.
  /////////////////////////////////////////////////////////////////////////

  int set_variable(THD *thd, uint var_idx, Item **value)
  { return set_variable(thd, m_var_table->field[var_idx], value); }

  Item *get_item(uint var_idx) const
  { return m_var_items[var_idx]; }

  Item **get_item_addr(uint var_idx) const
  { return m_var_items.array() + var_idx; }

  bool set_return_value(THD *thd, Item **return_value_item);

  bool is_return_value_set() const
  { return m_return_value_set; }

  /////////////////////////////////////////////////////////////////////////
  // SQL-handlers.
  /////////////////////////////////////////////////////////////////////////

  /// Create a new sp_handler_entry instance and push it to the handler call
  /// stack.
  ///
  /// @param handler  SQL-handler object.
  /// @param first_ip First instruction pointer of the handler.
  ///
  /// @return error flag.
  /// @retval false on success.
  /// @retval true on error.
  bool push_handler(sp_handler *handler, uint first_ip);

  /// Pop and delete given number of sp_handler_entry instances from the handler
  /// call stack.
  ///
  /// @param count Number of handler entries to pop & delete.
  void pop_handlers(int count);

  const Sql_condition_info *raised_condition() const
  {
    return m_handler_call_stack.elements() ?
      (*m_handler_call_stack.back())->sql_condition : NULL;
  }

  /// Handle current SQL condition (if any).
  ///
  /// This is the public-interface function to handle SQL conditions in
  /// stored routines.
  ///
  /// @param thd            Thread handle.
  /// @param ip[out]        Instruction pointer to the first handler
  ///                       instruction.
  /// @param cur_spi        Current SP instruction.
  ///
  /// @retval true if an SQL-handler has been activated. That means, all of
  /// the following conditions are satisfied:
  ///   - the SP-instruction raised SQL-condition(s),
  ///   - and there is an SQL-handler to process at least one of those
  ///     SQL-conditions,
  ///   - and that SQL-handler has been activated.
  /// Note, that the return value has nothing to do with "error flag"
  /// semantics.
  ///
  /// @retval false otherwise.
  bool handle_sql_condition(THD *thd,
                            uint *ip,
                            const sp_instr *cur_spi);

  /// Remove latest call frame from the handler call stack.
  ///
  /// @param da Diagnostics area containing handled conditions.
  ///
  /// @return continue instruction pointer of the removed handler.
  uint exit_handler(Diagnostics_area *da);

  /////////////////////////////////////////////////////////////////////////
  // Cursors.
  /////////////////////////////////////////////////////////////////////////

  /// Create a new sp_cursor instance and push it to the cursor stack.
  ///
  /// @param lex_keeper SP-instruction execution helper.
  /// @param i          Cursor-push instruction.
  ///
  /// @return error flag.
  /// @retval false on success.
  /// @retval true on error.
  bool push_cursor(sp_lex_keeper *lex_keeper, sp_instr_cpush *i);

  /// Pop and delete given number of sp_cursor instance from the cursor stack.
  ///
  /// @param count Number of cursors to pop & delete.
  void pop_cursors(uint count);

  void pop_all_cursors()
  { pop_cursors(m_ccount); }

  sp_cursor *get_cursor(uint i) const
  { return m_cstack[i]; }

  /////////////////////////////////////////////////////////////////////////
  // CASE expressions.
  /////////////////////////////////////////////////////////////////////////

  /// Set CASE expression to the specified value.
  ///
  /// @param thd             Thread handler.
  /// @param case_expr_id    The CASE expression identifier.
  /// @param case_expr_item  The CASE expression value
  ///
  /// @return error flag.
  /// @retval false on success.
  /// @retval true on error.
  ///
  /// @note The idea is to reuse Item_cache for the expression of the one
  /// CASE statement. This optimization takes place when there is CASE
  /// statement inside of a loop. So, in other words, we will use the same
  /// object on each iteration instead of creating a new one for each
  /// iteration.
  ///
  /// TODO
  ///   Hypothetically, a type of CASE expression can be different for each
  ///   iteration. For instance, this can happen if the expression contains
  ///   a session variable (something like @@VAR) and its type is changed
  ///   from one iteration to another.
  ///
  ///   In order to cope with this problem, we check type each time, when we
  ///   use already created object. If the type does not match, we re-create
  ///   Item.  This also can (should?) be optimized.
  bool set_case_expr(THD *thd, int case_expr_id, Item **case_expr_item_ptr);

  Item *get_case_expr(int case_expr_id) const
  { return m_case_expr_holders[case_expr_id]; }

  Item ** get_case_expr_addr(int case_expr_id) const
  { return (Item**) m_case_expr_holders.array() + case_expr_id; }

private:
  /// Internal function to allocate memory for arrays.
  ///
  /// @param thd Thread handle.
  ///
  /// @return error flag: false on success, true in case of failure.
  bool alloc_arrays(THD *thd);

  /// Create and initialize a table to store SP-variables.
  ///
  /// param thd Thread handle.
  ///
  /// @return error flag.
  /// @retval false on success.
  /// @retval true on error.
  bool init_var_table(THD *thd);

  /// Create and initialize an Item-adapter (Item_field) for each SP-var field.
  ///
  /// param thd Thread handle.
  ///
  /// @return error flag.
  /// @retval false on success.
  /// @retval true on error.
  bool init_var_items(THD *thd);

  /// Create an instance of appropriate Item_cache class depending on the
  /// specified type in the callers arena.
  ///
  /// @note We should create cache items in the callers arena, as they are
  /// used between in several instructions.
  ///
  /// @param thd   Thread handler.
  /// @param item  Item to get the expression type.
  ///
  /// @return Pointer to valid object on success, or NULL in case of error.
  Item_cache *create_case_expr_holder(THD *thd, const Item *item) const;

  int set_variable(THD *thd, Field *field, Item **value);

private:
  /// Top-level (root) parsing context for this runtime context.
  const sp_pcontext *m_root_parsing_ctx;

  /// Virtual table for storing SP-variables.
  TABLE *m_var_table;

  /// Collection of Item_field proxies, each of them points to the
  /// corresponding field in m_var_table.
  Bounds_checked_array<Item *> m_var_items;

  /// This is a pointer to a field, which should contain return value for
  /// stored functions (only). For stored procedures, this pointer is NULL.
  Field *m_return_value_fld;

  /// Indicates whether the return value (in m_return_value_fld) has been
  /// set during execution.
  bool m_return_value_set;

  /// Flag to tell if the runtime context is created for a sub-statement.
  bool m_in_sub_stmt;

  /// Stack of visible handlers.
  Dynamic_array<sp_handler_entry *> m_handlers;

  /// Stack of caught SQL conditions.
  Dynamic_array<Handler_call_frame *> m_handler_call_stack;

  /// Stack of cursors.
  Bounds_checked_array<sp_cursor *> m_cstack;

  /// Current number of cursors in m_cstack.
  uint m_ccount;

  /// Array of CASE expression holders.
  Bounds_checked_array<Item_cache *> m_case_expr_holders;
}; // class sp_rcontext : public Sql_alloc

///////////////////////////////////////////////////////////////////////////
// sp_cursor declaration.
///////////////////////////////////////////////////////////////////////////

class Server_side_cursor;
typedef class st_select_lex_unit SELECT_LEX_UNIT;

/* A mediator between stored procedures and server side cursors */

class sp_cursor : public Sql_alloc
{
private:
  /// An interceptor of cursor result set used to implement
  /// FETCH <cname> INTO <varlist>.
  class Select_fetch_into_spvars: public select_result_interceptor
  {
    List<sp_variable> *spvar_list;
    uint field_count;
  public:
    Select_fetch_into_spvars() {}               /* Remove gcc warning */
    uint get_field_count() { return field_count; }
    void set_spvar_list(List<sp_variable> *vars) { spvar_list= vars; }

    virtual bool send_eof() { return FALSE; }
    virtual bool send_data(List<Item> &items);
    virtual int prepare(List<Item> &list, SELECT_LEX_UNIT *u);
};

public:
  sp_cursor(sp_lex_keeper *lex_keeper, sp_instr_cpush *i);

  virtual ~sp_cursor()
  { destroy(); }

  sp_lex_keeper *get_lex_keeper() { return m_lex_keeper; }

  int open(THD *thd);

  int close(THD *thd);

  my_bool is_open()
  { return test(server_side_cursor); }

  int fetch(THD *, List<sp_variable> *vars);

  sp_instr_cpush *get_instr()
  { return m_i; }

private:
  Select_fetch_into_spvars result;
  sp_lex_keeper *m_lex_keeper;
  Server_side_cursor *server_side_cursor;
  sp_instr_cpush *m_i;		// My push instruction
  void destroy();

}; // class sp_cursor : public Sql_alloc

#endif /* _SP_RCONTEXT_H_ */

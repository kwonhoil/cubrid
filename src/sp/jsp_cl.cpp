/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * jsp_cl.cpp - Java Stored Procedure Client Module Source
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#if !defined(WINDOWS)
#include <sys/socket.h>
#else /* not WINDOWS */
#include <winsock2.h>
#endif /* not WINDOWS */

#include <vector>
#include <functional>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "authenticate.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "dbtype.h"
#include "parser.h"
#include "parser_message.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "db.h"
#include "object_accessor.h"
#include "set_object.h"
#include "locator_cl.h"
#include "transaction_cl.h"
#include "schema_manager.h"
#include "numeric_opfunc.h"
#include "jsp_cl.h"
#include "system_parameter.h"
#include "network_interface_cl.h"
#include "unicode_support.h"
#include "dbtype.h"
#include "jsp_comm.h"
#include "method_compile_def.hpp"
#include "sp_catalog.hpp"
#include "authenticate_access_auth.hpp"

#define PT_NODE_SP_NAME(node) \
  (((node)->info.sp.name == NULL) ? "" : \
   (node)->info.sp.name->info.name.original)

#define PT_NODE_SP_TYPE(node) \
  ((node)->info.sp.type)

#define PT_NODE_SP_RETURN_TYPE(node) \
  ((node)->info.sp.ret_type->info.name.original)

#define PT_NODE_SP_BODY(node) \
  ((node)->info.sp.body)

#define PT_NODE_SP_LANG(node) \
  ((node)->info.sp.body->info.sp_body.lang)

#define PT_NODE_SP_ARGS(node) \
  ((node)->info.sp.param_list)

#define PT_NODE_SP_DIRECT(node) \
  ((node)->info.sp.body->info.sp_body.direct)

#define PT_NODE_SP_IMPL(node) \
  ((node)->info.sp.body->info.sp_body.impl->info.value.data_value.str->bytes)

#define PT_NODE_SP_JAVA_METHOD(node) \
  ((node)->info.sp.body->info.sp_body.decl->info.value.data_value.str->bytes)

#define PT_NODE_SP_AUTHID(node) \
  ((node)->info.sp.auth_id)

#define PT_NODE_SP_COMMENT(node) \
  (((node)->info.sp.comment == NULL) ? "" : \
   (char *) (node)->info.sp.comment->info.value.data_value.str->bytes)

#define PT_NODE_SP_ARG_NAME(node) \
  (((node)->info.sp_param.name == NULL) ? "" : \
   (node)->info.sp_param.name->info.name.original)

#define PT_NODE_SP_ARG_COMMENT(node) \
  (((node)->info.sp_param.comment == NULL) ? "" : \
   (char *) (node)->info.sp_param.comment->info.value.data_value.str->bytes)

#define MAX_CALL_COUNT  16

#define MAX_ARG_COUNT 64

static int server_port = -1;
static int call_cnt = 0;
static bool is_prepare_call[MAX_CALL_COUNT] = { false, };

static SP_TYPE_ENUM jsp_map_pt_misc_to_sp_type (PT_MISC_TYPE pt_enum);
static SP_MODE_ENUM jsp_map_pt_misc_to_sp_mode (PT_MISC_TYPE pt_enum);
static PT_MISC_TYPE jsp_map_sp_type_to_pt_misc (SP_TYPE_ENUM sp_type);
static SP_DIRECTIVE_ENUM jsp_map_pt_to_sp_authid (PT_MISC_TYPE pt_authid);

static char *jsp_check_stored_procedure_name (const char *str);
static int drop_stored_procedure (const char *name, SP_TYPE_ENUM expected_type);
static int drop_stored_procedure_code (const char *name);
static int alter_stored_procedure_code (PARSER_CONTEXT *parser, MOP sp_mop, const char *name, const char *owner_str,
					int sp_recompile);

static int jsp_make_method_sig_list (PARSER_CONTEXT *parser, PT_NODE *node_list, method_sig_list &sig_list);
static int *jsp_make_method_arglist (PARSER_CONTEXT *parser, PT_NODE *node_list);

static int check_execute_authorization (const MOP sp_obj, const DB_AUTH au_type);

extern bool ssl_client;

/*
 * jsp_find_stored_procedure
 *   return: MOP
 *   name(in): find java stored procedure name
 *
 * Note:
 */

MOP
jsp_find_stored_procedure (const char *name, DB_AUTH purpose)
{
  MOP mop = NULL;
  DB_VALUE value;
  int save, err = NO_ERROR;
  char *checked_name;

  if (!name)
    {
      return NULL;
    }

  AU_DISABLE (save);

  checked_name = jsp_check_stored_procedure_name (name);
  db_make_string (&value, checked_name);
  mop = db_find_unique (db_find_class (SP_CLASS_NAME), SP_ATTR_UNIQUE_NAME, &value);

  if (er_errid () == ER_OBJ_OBJECT_NOT_FOUND)
    {
      er_clear ();
      err = ER_SP_NOT_EXIST;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, err, 1, checked_name);
    }

  if (mop)
    {
      err = check_execute_authorization (mop, purpose);
    }

  if (err != NO_ERROR)
    {
      mop = NULL;
    }

  free_and_init (checked_name);
  AU_ENABLE (save);

  return mop;
}

/*
 * jsp_is_exist_stored_procedure
 *   return: name is exist then return true
 *                         else return false
 *   name(in): find java stored procedure name
 *
 * Note:
 */


MOP
jsp_find_stored_procedure_code (const char *name)
{
  MOP mop = NULL;
  DB_VALUE value;
  int save;

  if (!name)
    {
      return NULL;
    }

  AU_DISABLE (save);

  db_make_string (&value, name);
  mop = db_find_unique (db_find_class (SP_CODE_CLASS_NAME), SP_ATTR_CLS_NAME, &value);

  if (er_errid () == ER_OBJ_OBJECT_NOT_FOUND)
    {
      er_clear ();

      // TODO: error
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_EXIST, 1, name);
    }

  AU_ENABLE (save);

  return mop;
}

int
jsp_is_exist_stored_procedure (const char *name)
{
  MOP mop = NULL;

  mop = jsp_find_stored_procedure (name, DB_AUTH_NONE);
  er_clear ();

  return mop != NULL;
}

int
jsp_check_out_param_in_query (PARSER_CONTEXT *parser, PT_NODE *node, int arg_mode)
{
  int error = NO_ERROR;

  assert ((node) && (node)->node_type == PT_METHOD_CALL);

  if (node->info.method_call.call_or_expr != PT_IS_CALL_STMT)
    {
      // check out parameters
      if (arg_mode != SP_MODE_IN)
	{
	  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_SP_OUT_ARGS_EXISTS_IN_QUERY,
		      node->info.method_call.method_name->info.name.original);
	  error = ER_PT_SEMANTIC;
	}
    }

  return error;
}

/*
 * jsp_check_param_type_supported
 *
 * Note:
 */

int
jsp_check_param_type_supported (PT_NODE *node)
{
  assert (node && node->node_type == PT_SP_PARAMETERS);

  PT_TYPE_ENUM pt_type = node->type_enum;
  DB_TYPE domain_type = pt_type_enum_to_db (pt_type);

  switch (domain_type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_STRING:
    case DB_TYPE_OBJECT:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_CHAR:
    case DB_TYPE_BIGINT:
    case DB_TYPE_DATETIME:
      return NO_ERROR;
      break;

    case DB_TYPE_RESULTSET:
      if (node->info.sp_param.mode != PT_OUTPUT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_INPUT_RESULTSET, 0);
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_SUPPORTED_ARG_TYPE, 1, pr_type_name (domain_type));
      break;
    }

  return er_errid ();
}

/*
 * jsp_check_return_type_supported
 *
 * Note:
 */

int
jsp_check_return_type_supported (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_NULL:
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_STRING:
    case DB_TYPE_OBJECT:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_CHAR:
    case DB_TYPE_BIGINT:
    case DB_TYPE_DATETIME:
    case DB_TYPE_RESULTSET:
      return NO_ERROR;
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_SUPPORTED_RETURN_TYPE, 1, pr_type_name (type));
      break;
    }

  return er_errid ();
}

/*
 * jsp_get_return_type - Return Java Stored Procedure Type
 *   return: if fail return error code
 *           else return Java Stored Procedure Type
 *   name(in): java stored procedure name
 *
 * Note:
 */

int
jsp_get_return_type (const char *name)
{
  DB_OBJECT *mop_p;
  DB_VALUE return_type;
  int err;
  int save;

  AU_DISABLE (save);

  mop_p = jsp_find_stored_procedure (name, DB_AUTH_NONE);
  if (mop_p == NULL)
    {
      AU_ENABLE (save);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  err = db_get (mop_p, SP_ATTR_RETURN_TYPE, &return_type);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return err;
    }

  AU_ENABLE (save);
  return db_get_int (&return_type);
}

/*
 * jsp_get_sp_type - Return Java Stored Procedure Type
 *   return: if fail return error code
 *           else return Java Stored Procedure Type
 *   name(in): java stored procedure name
 *
 * Note:
 */

int
jsp_get_sp_type (const char *name)
{
  DB_OBJECT *mop_p;
  DB_VALUE sp_type_val;
  int err;
  int save;

  AU_DISABLE (save);

  mop_p = jsp_find_stored_procedure (name, DB_AUTH_NONE);
  if (mop_p == NULL)
    {
      AU_ENABLE (save);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* check type */
  err = db_get (mop_p, SP_ATTR_SP_TYPE, &sp_type_val);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return err;
    }

  AU_ENABLE (save);
  return jsp_map_sp_type_to_pt_misc ((SP_TYPE_ENUM) db_get_int (&sp_type_val));
}

MOP
jsp_get_owner (MOP mop_p)
{
  int save;
  DB_VALUE value;

  AU_DISABLE (save);

  /* check type */
  int err = db_get (mop_p, SP_ATTR_OWNER, &value);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return NULL;
    }

  MOP owner = db_get_object (&value);

  AU_ENABLE (save);
  return owner;
}

char *
jsp_get_name (MOP mop_p)
{
  int save;
  DB_VALUE value;
  char *res = NULL;

  AU_DISABLE (save);

  /* check type */
  int err = db_get (mop_p, SP_ATTR_NAME, &value);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return NULL;
    }

  res = ws_copy_string (db_get_string (&value));
  pr_clear_value (&value);

  AU_ENABLE (save);
  return res;
}

char *
jsp_get_unique_name (MOP mop_p, char *buf, int buf_size)
{
  int save;
  DB_VALUE value;
  int err = NO_ERROR;

  assert (buf != NULL);
  assert (buf_size > 0);

  if (mop_p == NULL)
    {
      ERROR_SET_WARNING (err, ER_SM_INVALID_ARGUMENTS);
      buf[0] = '\0';
      return NULL;
    }

  AU_DISABLE (save);

  /* check type */
  err = db_get (mop_p, SP_ATTR_UNIQUE_NAME, &value);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return NULL;
    }

  strncpy (buf, db_get_string (&value), buf_size);
  pr_clear_value (&value);

  AU_ENABLE (save);
  return buf;
}

/*
 * jsp_get_owner_name - Return Java Stored Procedure'S Owner nmae
 *   return: if fail return MULL
 *           else return Java Stored Procedure Type
 *   name(in): java stored procedure name
 *
 * Note:
 */

char *
jsp_get_owner_name (const char *name, char *buf, int buf_size)
{
  DB_OBJECT *mop_p;
  DB_VALUE value;
  int err;
  int save;

  assert (buf != NULL);
  assert (buf_size > 0);

  if (name == NULL || name[0] == '\0')
    {
      ERROR_SET_WARNING (err, ER_SM_INVALID_ARGUMENTS);
      buf[0] = '\0';
      return NULL;
    }

  AU_DISABLE (save);

  mop_p = jsp_find_stored_procedure (name, DB_AUTH_NONE);
  if (mop_p == NULL)
    {
      AU_ENABLE (save);

      assert (er_errid () != NO_ERROR);
      return NULL;
    }

  /* check type */
  err = db_get (mop_p, SP_ATTR_OWNER, &value);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return NULL;
    }

  MOP owner = db_get_object (&value);
  if (owner != NULL)
    {
      DB_VALUE value2;
      err = db_get (owner, "name", &value2);
      if (err == NO_ERROR)
	{
	  strncpy (buf, db_get_string (&value2), buf_size);
	}
      pr_clear_value (&value2);
    }
  pr_clear_value (&value);

  AU_ENABLE (save);
  return buf;
}



static PT_MISC_TYPE
jsp_map_sp_type_to_pt_misc (SP_TYPE_ENUM sp_type)
{
  if (sp_type == SP_TYPE_PROCEDURE)
    {
      return PT_SP_PROCEDURE;
    }
  else
    {
      return PT_SP_FUNCTION;
    }
}

/*
 * jsp_call_stored_procedure - call java stored procedure in constant folding
 *   return: call jsp failed return error code
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

int
jsp_call_stored_procedure (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  int error = NO_ERROR;
  PT_NODE *method;
  const char *method_name;
  if (!statement || ! (method = statement->info.method_call.method_name) || method->node_type != PT_NAME
      || ! (method_name = method->info.name.original))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return er_errid ();
    }

  DB_VALUE ret_value;
  db_make_null (&ret_value);

  // *INDENT-OFF*
  std::vector <std::reference_wrapper <DB_VALUE>> args;
  // *INDENT-ON*

  PT_NODE *vc = statement->info.method_call.arg_list;
  while (vc)
    {
      DB_VALUE *db_value;
      bool to_break = false;

      /*
       * Don't clone host vars; they may actually be acting as output variables (e.g., a character array that is
       * intended to receive bytes from the method), and cloning will ensure that the results never make it to the
       * expected area.  Since pt_evaluate_tree() always clones its db_values we must not use pt_evaluate_tree() to
       * extract the db_value from a host variable; instead extract it ourselves. */
      if (PT_IS_CONST (vc))
	{
	  db_value = pt_value_to_db (parser, vc);
	}
      else
	{
	  db_value = (DB_VALUE *) malloc (sizeof (DB_VALUE));
	  if (db_value == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE));
	      return er_errid ();
	    }

	  /* must call pt_evaluate_tree */
	  pt_evaluate_tree (parser, vc, db_value, 1);
	  if (pt_has_error (parser))
	    {
	      /* to maintain the list to free all the allocated */
	      to_break = true;
	    }
	}

      if (to_break)
	{
	  break;
	}

      args.push_back (std::ref (*db_value));
      vc = vc->next;
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      error = er_errid ();
    }
  else
    {
      /* call sp */
      method_sig_list sig_list;

      sig_list.method_sig = nullptr;
      sig_list.num_methods = 0;

      error = jsp_make_method_sig_list (parser, statement, sig_list);
      if (error == NO_ERROR && locator_get_sig_interrupt () == 0)
	{
	  error = method_invoke_fold_constants (sig_list, args, ret_value);
	}
      sig_list.freemem ();
    }

  if (error == NO_ERROR)
    {
      vc = statement->info.method_call.arg_list;
      for (int i = 0; i < (int) args.size () && vc; i++)
	{
	  if (!PT_IS_CONST (vc))
	    {
	      DB_VALUE &arg = args[i];
	      db_value_clear (&arg);
	      free (&arg);
	    }
	  vc = vc->next;
	}
    }

  if (error == NO_ERROR)
    {
      /* Save the method result and its domain */
      statement->etc = (void *) db_value_copy (&ret_value);
      statement = pt_bind_type_from_dbval (parser, statement, &ret_value);

      PT_NODE *into = statement->info.method_call.to_return_var;

      const char *into_label;
      if (into != NULL && into->node_type == PT_NAME && (into_label = into->info.name.original) != NULL)
	{
	  /* create another DB_VALUE of the new instance for the label_table */
	  DB_VALUE *ins_value = db_value_copy (&ret_value);
	  error = pt_associate_label_with_value_check_reference (into_label, ins_value);
	}
    }

#if defined (CS_MODE)
  db_value_clear (&ret_value);
#endif

  return error;
}

/*
 * jsp_drop_stored_procedure - drop java stored procedure
 *   return: Error code
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

int
jsp_drop_stored_procedure (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  const char *name;
  PT_MISC_TYPE type;
  PT_NODE *name_list, *p;
  int i;
  int err = NO_ERROR;

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BLOCK_DDL_STMT, 0);
      return ER_BLOCK_DDL_STMT;
    }

  name_list = statement->info.sp.name;
  type = PT_NODE_SP_TYPE (statement);

  for (p = name_list, i = 0; p != NULL; p = p->next)
    {
      name = (char *) p->info.name.original;
      if (name == NULL || name[0] == '\0')
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_NAME, 0);
	  return er_errid ();
	}

      err = drop_stored_procedure (name, jsp_map_pt_misc_to_sp_type (type));
      if (err != NO_ERROR)
	{
	  break;
	}
    }

  return err;
}

/*
 * jsp_create_stored_procedure
 *   return: if failed return error code else execute jsp_add_stored_procedure
 *           function
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

int
jsp_create_stored_procedure (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  const char *decl = NULL, *comment = NULL;
  char owner_name[DB_MAX_USER_LENGTH];
  owner_name[0] = '\0';

  PT_NODE *param_list, *p;
  PT_TYPE_ENUM ret_type = PT_TYPE_NONE;
  int lang;
  int err = NO_ERROR;
  bool has_savepoint = false;

  PLCSQL_COMPILE_REQUEST compile_request;
  PLCSQL_COMPILE_RESPONSE compile_response;

  SP_INFO sp_info;
  char *temp;

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BLOCK_DDL_STMT, 0);
      return ER_BLOCK_DDL_STMT;
    }

  // check PL/CSQL's AUTHID with CURRENT_USER
  sp_info.directive = jsp_map_pt_to_sp_authid (PT_NODE_SP_AUTHID (statement));
  sp_info.lang = (SP_LANG_ENUM) PT_NODE_SP_LANG (statement);
  if (sp_info.directive == SP_DIRECTIVE_RIGHTS_CALLER && sp_info.lang == SP_LANG_PLCSQL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVOKERS_RIGHTS_NOT_SUPPORTED, 0);
      return er_errid ();
    }

  temp = jsp_check_stored_procedure_name (PT_NODE_SP_NAME (statement));
  sp_info.unique_name = temp;
  free (temp);
  if (sp_info.unique_name.empty ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_NAME, 0);
      return er_errid ();
    }

  sp_info.sp_name = sm_remove_qualifier_name (sp_info.unique_name.data ());
  if (sp_info.sp_name.empty ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_NAME, 0);
      return er_errid ();
    }

  sp_info.sp_type = jsp_map_pt_misc_to_sp_type (PT_NODE_SP_TYPE (statement));
  if (sp_info.sp_type == SP_TYPE_FUNCTION)
    {
      sp_info.return_type = pt_type_enum_to_db (statement->info.sp.ret_type);
    }
  else
    {
      sp_info.return_type = DB_TYPE_NULL;
    }

  // set rows for _db_stored_procedure_args
  int param_count = 0;
  param_list = PT_NODE_SP_ARGS (statement);
  for (p = param_list; p != NULL; p = p->next)
    {
      SP_ARG_INFO arg_info (sp_info.unique_name, sp_info.pkg_name);

      arg_info.index_of = param_count++;
      arg_info.arg_name = PT_NODE_SP_ARG_NAME (p);
      arg_info.data_type = pt_type_enum_to_db (p->type_enum);
      arg_info.mode = jsp_map_pt_misc_to_sp_mode (p->info.sp_param.mode);

      // default value
      // coerciable is already checked in semantic_check
      PT_NODE *default_value = p->info.sp_param.default_value;
      if (default_value)
	{
	  pt_evaluate_tree (parser, default_value->info.data_default.default_value, &arg_info.default_value, 1);
	  arg_info.is_optional = true;
	}
      else
	{
	  db_make_null (&arg_info.default_value);
	}

      arg_info.comment = (char *) PT_NODE_SP_ARG_COMMENT (p);

      // check # of args constraint
      if (param_count > MAX_ARG_COUNT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TOO_MANY_ARG_COUNT, 1, sp_info.unique_name.data ());
	  goto error_exit;
	}

      sp_info.args.push_back (arg_info);
    }

  if (sm_qualifier_name (sp_info.unique_name.data (), owner_name, DB_MAX_USER_LENGTH) == NULL)
    {
      ASSERT_ERROR ();
      goto error_exit;
    }

  sp_info.owner = owner_name[0] == '\0' ? Au_user : db_find_user (owner_name);
  if (sp_info.owner == NULL)
    {
      // for safeguard: it is already checked in pt_check_create_stored_procedure ()
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER_NAME, 1, owner_name);
      goto error_exit;
    }

  if (sp_info.lang == SP_LANG_PLCSQL)
    {
      assert (statement->sql_user_text && statement->sql_user_text_len);
      compile_request.code.assign (statement->sql_user_text, statement->sql_user_text_len);
      compile_request.owner.assign ((owner_name[0] == '\0') ? au_get_current_user_name () : owner_name);

      // TODO: Only the owner's rights is supported for PL/CSQL
      au_perform_push_user (sp_info.owner);
      err = plcsql_transfer_file (compile_request, compile_response);
      au_perform_pop_user ();

      if (err == NO_ERROR && compile_response.err_code == NO_ERROR)
	{
	  decl = compile_response.java_signature.c_str ();
	}
      else
	{
	  // TODO: error handling needs to be improved
	  err = ER_SP_COMPILE_ERROR;

	  std::string err_msg;
	  if (compile_response.err_msg.empty ())
	    {
	      err_msg = "unknown";
	    }
	  else
	    {
	      err_msg.assign (compile_response.err_msg);
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_COMPILE_ERROR, 3, compile_response.err_line,
		  compile_response.err_column, err_msg.c_str ());
	  pt_record_error (parser, parser->statement_number, compile_response.err_line, compile_response.err_column, er_msg (),
			   NULL);
	  goto error_exit;
	}
    }
  else				/* SP_LANG_JAVA */
    {
      bool is_direct = PT_NODE_SP_DIRECT (statement);
      if (is_direct)
	{
	  // TODO: CBRD-24641
	  assert (false);
	}
      else
	{
	  decl = (const char *) PT_NODE_SP_JAVA_METHOD (statement);
	}
    }

  if (decl)
    {
      std::string target = decl;
      sp_split_target_signature (target, sp_info.target_class, sp_info.target_method);
    }

  sp_info.comment = (char *) PT_NODE_SP_COMMENT (statement);

  if (err != NO_ERROR)
    {
      goto error_exit;
    }

  /* check already exists */
  if (jsp_is_exist_stored_procedure (sp_info.unique_name.data ()))
    {
      if (statement->info.sp.or_replace)
	{
	  /* drop existing stored procedure */
	  err = tran_system_savepoint (SAVEPOINT_CREATE_STORED_PROC);
	  if (err != NO_ERROR)
	    {
	      return err;
	    }
	  has_savepoint = true;

	  err = drop_stored_procedure (sp_info.unique_name.data (), sp_info.sp_type);
	  if (err != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_ALREADY_EXIST, 1, sp_info.unique_name.data ());
	  goto error_exit;
	}
    }

  err = sp_add_stored_procedure (sp_info);
  if (err != NO_ERROR)
    {
      goto error_exit;
    }

  if (!compile_request.code.empty ())
    {
      SP_CODE_INFO code_info;

      auto now = std::chrono::system_clock::now();
      auto converted_timep = std::chrono::system_clock::to_time_t (now);
      std::stringstream stm;
      stm << std::put_time (localtime (&converted_timep), "%Y%m%d%H%M%S");

      code_info.name = sp_info.target_class;
      code_info.created_time = stm.str ();
      code_info.stype = (sp_info.lang == SP_LANG_PLCSQL) ? SPSC_PLCSQL : SPSC_JAVA;
      code_info.scode = compile_request.code;
      code_info.otype = compile_response.compiled_type;
      code_info.ocode = compile_response.compiled_code;
      code_info.owner = sp_info.owner;
      code_info.itype = 0; // dummy;
      code_info.icode = compile_response.translated_code;

      err = sp_add_stored_procedure_code (code_info);
      if (err != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  return NO_ERROR;

error_exit:
  if (has_savepoint)
    {
      tran_abort_upto_system_savepoint (SAVEPOINT_CREATE_STORED_PROC);
    }

  return (err == NO_ERROR) ? er_errid () : err;
}

/*
 * jsp_alter_stored_procedure
 *   return: if failed return error code else NO_ERROR
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

int
jsp_alter_stored_procedure (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  int err = NO_ERROR, sp_recompile, save, lang;
  PT_NODE *sp_name = NULL, *sp_owner = NULL, *sp_comment = NULL;
  const char *name_str = NULL, *owner_str = NULL, *comment_str = NULL, *target_cls = NULL;
  char new_name_str[DB_MAX_IDENTIFIER_LENGTH];
  new_name_str[0] = '\0';
  char downcase_owner_name[DB_MAX_USER_LENGTH];
  downcase_owner_name[0] = '\0';
  PT_MISC_TYPE type;
  SP_TYPE_ENUM real_type;
  MOP sp_mop = NULL, new_owner = NULL;
  DB_VALUE user_val, sp_type_val, sp_lang_val, target_cls_val;

  assert (statement != NULL);

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BLOCK_DDL_STMT, 0);
      return ER_BLOCK_DDL_STMT;
    }

  db_make_null (&user_val);
  db_make_null (&sp_type_val);
  db_make_null (&sp_lang_val);
  db_make_null (&target_cls_val);

  type = PT_NODE_SP_TYPE (statement);
  sp_name = statement->info.sp.name;
  assert (sp_name != NULL);

  sp_owner = statement->info.sp.owner;
  sp_recompile = statement->info.sp.recompile;
  sp_comment = statement->info.sp.comment;

  assert (sp_owner != NULL || sp_comment != NULL || sp_recompile);

  name_str = sp_name->info.name.original;
  assert (name_str != NULL);

  if (sp_owner != NULL)
    {
      owner_str = sp_owner->info.name.original;
      assert (owner_str != NULL);
    }

  comment_str = (char *) PT_NODE_SP_COMMENT (statement);

  AU_DISABLE (save);

  /* authentication */
  if (!au_is_dba_group_member (Au_user))
    {
      err = ER_AU_DBA_ONLY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, "change stored procedure owner");
      goto error;
    }

  /* existence of sp */
  sp_mop = jsp_find_stored_procedure (name_str, DB_AUTH_SELECT);
  if (sp_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  /* when changing the owner, all privileges are revoked */
  err = au_object_revoke_all_privileges (NULL, sp_mop);
  if (err != NO_ERROR)
    {
      goto error;
    }

  /* existence of new owner */
  if (sp_owner != NULL)
    {
      new_owner = db_find_user (owner_str);
      if (new_owner == NULL)
	{
	  err = ER_OBJ_OBJECT_NOT_FOUND;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, owner_str);
	  goto error;
	}
    }

  /* check type */
  err = db_get (sp_mop, SP_ATTR_SP_TYPE, &sp_type_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  real_type = (SP_TYPE_ENUM) db_get_int (&sp_type_val);
  if (real_type != jsp_map_pt_misc_to_sp_type (type))
    {
      err = ER_SP_INVALID_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2, name_str,
	      real_type == SP_TYPE_FUNCTION ? "FUNCTION" : "PROCEDURE");
      goto error;
    }

  /* change _db_stored_procedure */
  if (sp_owner != NULL)
    {
      /* change the unique_name */
      sm_downcase_name (owner_str, downcase_owner_name, DB_MAX_USER_LENGTH);
      sprintf (new_name_str, "%s.%s", downcase_owner_name, sm_remove_qualifier_name (name_str));

      db_make_string (&user_val, new_name_str);
      err = obj_set (sp_mop, SP_ATTR_UNIQUE_NAME, &user_val);
      if (err < 0)
	{
	  goto error;
	}
      pr_clear_value (&user_val);

      /* change the owner */
      db_make_object (&user_val, new_owner);
      err = obj_set (sp_mop, SP_ATTR_OWNER, &user_val);
      if (err < 0)
	{
	  goto error;
	}
      pr_clear_value (&user_val);
    }

  /* check lang */
  err = db_get (sp_mop, SP_ATTR_LANG, &sp_lang_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  lang = db_get_int (&sp_lang_val);
  if (lang == SP_LANG_PLCSQL)
    {
      if (sp_owner != NULL || sp_recompile == 1)
	{
	  err = db_get (sp_mop, SP_ATTR_TARGET_CLASS, &target_cls_val);
	  if (err != NO_ERROR)
	    {
	      goto error;
	    }
	  target_cls = db_get_string (&target_cls_val);

	  if (sp_recompile == 1)
	    {
	      owner_str = sm_qualifier_name (name_str, downcase_owner_name, DB_MAX_USER_LENGTH);
	    }

	  err = alter_stored_procedure_code (parser, sp_mop, target_cls, owner_str, sp_recompile);
	  if (err != NO_ERROR)
	    {
	      goto error;
	    }
	  pr_clear_value (&target_cls_val);
	}
      pr_clear_value (&sp_lang_val);
    }

  /* change the comment */
  if (sp_comment != NULL)
    {
      db_make_string (&user_val, comment_str);
      err = obj_set (sp_mop, SP_ATTR_COMMENT, &user_val);
      if (err < 0)
	{
	  goto error;
	}
      pr_clear_value (&user_val);
    }

error:

  pr_clear_value (&user_val);
  pr_clear_value (&sp_type_val);
  pr_clear_value (&sp_lang_val);
  pr_clear_value (&target_cls_val);
  AU_ENABLE (save);

  return err;
}

/*
 * jsp_map_pt_misc_to_sp_type
 *   return : stored procedure type ( Procedure or Function )
 *   pt_enum(in): Misc Types
 *
 * Note:
 */

static SP_TYPE_ENUM
jsp_map_pt_misc_to_sp_type (PT_MISC_TYPE pt_enum)
{
  if (pt_enum == PT_SP_PROCEDURE)
    {
      return SP_TYPE_PROCEDURE;
    }
  else
    {
      return SP_TYPE_FUNCTION;
    }
}

/*
 * jsp_map_pt_misc_to_sp_mode
 *   return : stored procedure mode ( input or output or inout )
 *   pt_enum(in) : Misc Types
 *
 * Note:
 */

static SP_MODE_ENUM
jsp_map_pt_misc_to_sp_mode (PT_MISC_TYPE pt_enum)
{
  if (pt_enum == PT_INPUT || pt_enum == PT_NOPUT)
    {
      return SP_MODE_IN;
    }
  else if (pt_enum == PT_OUTPUT)
    {
      return SP_MODE_OUT;
    }
  else
    {
      return SP_MODE_INOUT;
    }
}

static SP_DIRECTIVE_ENUM
jsp_map_pt_to_sp_authid (PT_MISC_TYPE pt_authid)
{
  assert (pt_authid == PT_AUTHID_OWNER || pt_authid == PT_AUTHID_CALLER);
  return (pt_authid == PT_AUTHID_OWNER ? SP_DIRECTIVE_ENUM::SP_DIRECTIVE_RIGHTS_OWNER :
	  SP_DIRECTIVE_ENUM::SP_DIRECTIVE_RIGHTS_CALLER);
}

/*
 * jsp_check_stored_procedure_name -
 *   return: java stored procedure name
 *   str(in) :
 *
 * Note: convert lowercase
 */

static char *
jsp_check_stored_procedure_name (const char *str)
{
  char buffer[SM_MAX_IDENTIFIER_LENGTH + 2];
  char tmp[SM_MAX_IDENTIFIER_LENGTH + 2];
  char *name = NULL;
  static int dbms_output_len = strlen ("dbms_output.");
  char owner_name[DB_MAX_USER_LENGTH];
  owner_name[0] = '\0';


  if (memcmp (str, "dbms_output.", dbms_output_len) == 0)
    {
      sprintf (buffer, "public.dbms_output.%s",
	       sm_downcase_name (str + dbms_output_len, tmp, strlen (str + dbms_output_len) + 1));
    }
  else
    {
      sm_user_specified_name (str, buffer, SM_MAX_IDENTIFIER_LENGTH);
    }

  name = strdup (buffer);

  return name;
}

/*
 * drop_stored_procedure -
 *   return: Error code
 *   name(in): jsp name
 *   expected_type(in):
 *
 * Note:
 */

static int
drop_stored_procedure (const char *name, SP_TYPE_ENUM expected_type)
{
  MOP sp_mop, arg_mop, owner;
  DB_VALUE sp_type_val, arg_cnt_val, args_val, owner_val, generated_val, target_cls_val, lang_val, temp;
  SP_TYPE_ENUM real_type;
  std::string class_name;
  const char *target_cls;
  DB_SET *arg_set_p;
  int save, i, arg_cnt, lang;
  int err;

  AU_DISABLE (save);

  db_make_null (&args_val);
  db_make_null (&owner_val);

  sp_mop = jsp_find_stored_procedure (name, DB_AUTH_SELECT);
  if (sp_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  err = db_get (sp_mop, SP_ATTR_OWNER, &owner_val);
  if (err != NO_ERROR)
    {
      goto error;
    }
  owner = db_get_object (&owner_val);

  if (!ws_is_same_object (owner, Au_user) && !au_is_dba_group_member (Au_user))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DROP_NOT_ALLOWED_PRIVILEGES, 0);
      err = er_errid ();
      goto error;
    }

  err = db_get (sp_mop, SP_ATTR_IS_SYSTEM_GENERATED, &generated_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  if (1 == db_get_int (&generated_val))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DROP_NOT_ALLOWED_SYSTEM_GENERATED, 0);
      err = er_errid ();
      goto error;
    }

  err = db_get (sp_mop, SP_ATTR_SP_TYPE, &sp_type_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  real_type = (SP_TYPE_ENUM) db_get_int (&sp_type_val);
  if (real_type != expected_type)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_TYPE, 2, name,
	      real_type == SP_TYPE_FUNCTION ? "FUNCTION" : "PROCEDURE");

      err = er_errid ();
      goto error;
    }

  // delete _db_stored_procedure_code

  err = db_get (sp_mop, SP_ATTR_LANG, &lang_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  lang = db_get_int (&lang_val);
  if (lang == SP_LANG_PLCSQL)
    {
      err = db_get (sp_mop, SP_ATTR_TARGET_CLASS, &target_cls_val);
      if (err != NO_ERROR)
	{
	  goto error;
	}

      target_cls = db_get_string (&target_cls_val);
      err = drop_stored_procedure_code (target_cls);
      if (err != NO_ERROR)
	{
	  goto error;
	}
    }

  err = db_get (sp_mop, SP_ATTR_ARG_COUNT, &arg_cnt_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  arg_cnt = db_get_int (&arg_cnt_val);

  err = db_get (sp_mop, SP_ATTR_ARGS, &args_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  arg_set_p = db_get_set (&args_val);

  for (i = 0; i < arg_cnt; i++)
    {
      set_get_element (arg_set_p, i, &temp);
      arg_mop = db_get_object (&temp);
      err = obj_delete (arg_mop);
      pr_clear_value (&temp);
      if (err != NO_ERROR)
	{
	  goto error;
	}
    }

  err = au_delete_auth_of_dropping_database_object (DB_OBJECT_PROCEDURE, name);
  if (err != NO_ERROR)
    {
      goto error;
    }

  err = obj_delete (sp_mop);

error:
  AU_ENABLE (save);

  pr_clear_value (&args_val);
  pr_clear_value (&owner_val);

  return err;
}

/*
 * drop_stored_procedure_code -
 *   return: Error code
 *   name(in): jsp name
 *
 * Note:
 */

static int
drop_stored_procedure_code (const char *name)
{
  MOP code_mop, owner;
  DB_VALUE owner_val, generated_val;
  int save;
  int err;

  AU_DISABLE (save);

  db_make_null (&owner_val);

  code_mop = jsp_find_stored_procedure_code (name);
  if (code_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  err = db_get (code_mop, SP_ATTR_OWNER, &owner_val);
  if (err != NO_ERROR)
    {
      goto error;
    }
  owner = db_get_object (&owner_val);

  if (!ws_is_same_object (owner, Au_user) && !au_is_dba_group_member (Au_user))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DROP_NOT_ALLOWED_PRIVILEGES, 0);
      err = er_errid ();
      goto error;
    }

  err = db_get (code_mop, SP_ATTR_IS_SYSTEM_GENERATED, &generated_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  if (!DB_IS_NULL (&generated_val) && 1 == db_get_int (&generated_val))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DROP_NOT_ALLOWED_SYSTEM_GENERATED, 0);
      err = er_errid ();
      goto error;
    }

  // TODO: If a unreloadable SP is deleted, mark a flag in PL server to block calling the deleted SP
  err = obj_delete (code_mop);

error:
  AU_ENABLE (save);

  pr_clear_value (&owner_val);

  return err;
}

/*
 * alter_stored_procedure_code -
 *   return: Error code
 *   name(in): jsp name
 *
 * Note:
 */

static int
alter_stored_procedure_code (PARSER_CONTEXT *parser, MOP sp_mop, const char *name, const char *owner_str,
			     int sp_recompile)
{
  const char *scode = NULL, *decl = NULL;
  int scode_len, save, err;
  MOP code_mop;
  DB_VALUE scode_val, value;
  PLCSQL_COMPILE_REQUEST compile_request;
  PLCSQL_COMPILE_RESPONSE compile_response;
  SP_INFO sp_info;
  SP_CODE_INFO code_info;
  DB_OBJECT *object_p;
  DB_OTMPL *obt_p = NULL;

  AU_DISABLE (save);

  db_make_null (&scode_val);
  db_make_null (&value);

  code_mop = jsp_find_stored_procedure_code (name);
  if (code_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  err = db_get (code_mop, SP_ATTR_SOURCE_CODE, &scode_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  scode = db_get_string (&scode_val);
  scode_len = db_get_string_size (&scode_val);

  sp_info.owner = db_find_user (owner_str);

  assert (scode && scode_len);
  compile_request.code.assign (scode, scode_len);
  compile_request.owner.assign (owner_str);
  pr_clear_value (&scode_val);

  // TODO: Only the owner's rights is supported for PL/CSQL
  au_perform_push_user (sp_info.owner);
  err = plcsql_transfer_file (compile_request, compile_response);
  au_perform_pop_user ();

  if (err == NO_ERROR && compile_response.err_code == NO_ERROR)
    {
      decl = compile_response.java_signature.c_str ();
    }
  else
    {
      // TODO: error handling needs to be improved
      err = ER_SP_COMPILE_ERROR;

      std::string err_msg;
      if (compile_response.err_msg.empty ())
	{
	  err_msg = "unknown";
	}
      else
	{
	  err_msg.assign (compile_response.err_msg);
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_COMPILE_ERROR, 3, compile_response.err_line,
	      compile_response.err_column, err_msg.c_str ());
      pt_record_error (parser, parser->statement_number, compile_response.err_line, compile_response.err_column, er_msg (),
		       NULL);
      goto error;
    }

  if (decl)
    {
      std::string target = decl;
      sp_split_target_signature (target, sp_info.target_class, sp_info.target_method);
    }

  code_info.name = sp_info.target_class;
  code_info.ocode = compile_response.compiled_code;
  code_info.icode = compile_response.translated_code;

  if (sp_recompile == 1)
    {
      /* recompile */
      code_info.owner = NULL;
    }
  else
    {
      /* owner to */
      code_info.owner = sp_info.owner;
    }

  err = sp_edit_stored_procedure_code (code_mop, code_info);
  if (err != NO_ERROR)
    {
      goto error;
    }

  /* Update the target_class column in the _db_stored_procedure catalog. */
  obt_p = dbt_edit_object (sp_mop);
  if (obt_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  db_make_string (&value, code_info.name.data ());
  err = dbt_put_internal (obt_p, SP_ATTR_TARGET_CLASS, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  object_p = dbt_finish_object (obt_p);
  if (!object_p)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }
  obt_p = NULL;

  err = locator_flush_instance (object_p);
  if (err != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      obj_delete (object_p);
      goto error;
    }

error:
  AU_ENABLE (save);

  pr_clear_value (&scode_val);
  pr_clear_value (&value);

  return err;
}

/*
 * jsp_set_prepare_call -
 *   return: none
 *
 * Note:
 */

void
jsp_set_prepare_call (void)
{
  int depth = tran_get_libcas_depth ();
  is_prepare_call[depth] = true;
}

/*
 * jsp_unset_prepare_call -
 *   return: none
 *
 * Note:
 */

void
jsp_unset_prepare_call (void)
{
  int depth = tran_get_libcas_depth ();
  is_prepare_call[depth] = false;
}

/*
 * jsp_is_prepare_call -
 *   return: bool
 *
 * Note:
 */

bool
jsp_is_prepare_call ()
{
  int depth = tran_get_libcas_depth ();
  return is_prepare_call[depth];
}

/*
 * jsp_make_method_sig_list () - converts a parse expression tree list of
 *                            method calls to method signature list
 *   return: A NULL return indicates a (memory) error occurred
 *   parser(in):
 *   node_list(in): should be parse method nodes
 *   subquery_as_attr_list(in):
 */
static int
jsp_make_method_sig_list (PARSER_CONTEXT *parser, PT_NODE *node, method_sig_list &sig_list)
{
  int error = NO_ERROR;
  int save;
  DB_VALUE cls, method, auth, param_cnt_val, mode, arg_type, optional_val, default_val, temp, lang_val, directive_val,
	   result_type, target_cls_val;

  int sig_num_args = pt_length_of_list (node->info.method_call.arg_list);
  std::vector <int> sig_arg_mode;
  std::vector <int> sig_arg_type;

  std::vector <char *> sig_arg_default;
  std::vector <int> sig_arg_default_size;

  int sig_result_type;
  int param_cnt = 0;
  MOP code_mop = NULL;

  METHOD_SIG *sig = nullptr;

  char owner[DB_MAX_USER_LENGTH + 1];
  owner[0] = '\0';

  db_make_null (&cls);
  db_make_null (&method);
  db_make_null (&auth);
  db_make_null (&param_cnt_val);
  db_make_null (&mode);
  db_make_null (&arg_type);
  db_make_null (&default_val);
  db_make_null (&temp);
  db_make_null (&result_type);
  db_make_null (&target_cls_val);

  {
    char *parsed_method_name = (char *) node->info.method_call.method_name->info.name.original;
    DB_OBJECT *mop_p = jsp_find_stored_procedure (parsed_method_name, DB_AUTH_EXECUTE);
    AU_DISABLE (save);
    if (mop_p)
      {
	/* check java stored prcedure target */
	error = db_get (mop_p, SP_ATTR_TARGET_CLASS, &cls);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	error = db_get (mop_p, SP_ATTR_TARGET_METHOD, &method);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	/* check arg count */
	error = db_get (mop_p, SP_ATTR_ARG_COUNT, &param_cnt_val);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	int num_trailing_default_args = 0;
	param_cnt = db_get_int (&param_cnt_val);
	if (sig_num_args > param_cnt)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_PARAM_COUNT, 2, param_cnt, sig_num_args);
	    error = er_errid ();
	    goto end;
	  }
	else if (sig_num_args < param_cnt)
	  {
	    // there are trailing default arguments
	    num_trailing_default_args = param_cnt - sig_num_args;
	  }

	sig_arg_default.resize (param_cnt);
	sig_arg_default_size.resize (param_cnt);

	DB_VALUE args;
	/* arg_mode, arg_type */
	error = db_get (mop_p, SP_ATTR_ARGS, &args);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	DB_SET *param_set = db_get_set (&args);
	for (int i = 0; i < param_cnt; i++)
	  {
	    set_get_element (param_set, i, &temp);
	    DB_OBJECT *arg_mop_p = db_get_object (&temp);
	    if (arg_mop_p)
	      {
		error = db_get (arg_mop_p, SP_ATTR_MODE, &mode);
		int arg_mode = db_get_int (&mode);

		error = jsp_check_out_param_in_query (parser, node, arg_mode);

		if (error == NO_ERROR)
		  {
		    sig_arg_mode.push_back (arg_mode);
		  }
		else
		  {
		    goto end;
		  }

		error = db_get (arg_mop_p, SP_ATTR_DATA_TYPE, &arg_type);
		if (error == NO_ERROR)
		  {
		    int type_val = db_get_int (&arg_type);
		    if (type_val == DB_TYPE_RESULTSET && !jsp_is_prepare_call ())
		      {
			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_RETURN_RESULTSET, 0);
			error = er_errid ();
			goto end;
		      }
		    sig_arg_type.push_back (type_val);
		  }
		else
		  {
		    goto end;
		  }

		sig_arg_default[i] = NULL;
		sig_arg_default_size[i] = -1;

		if (i >= sig_num_args)
		  {
		    error = db_get (arg_mop_p, SP_ATTR_IS_OPTIONAL, &optional_val);
		    if (error == NO_ERROR)
		      {
			int is_optional = db_get_int (&optional_val);
			if (is_optional == 1)
			  {
			    error = db_get (arg_mop_p, SP_ATTR_DEFAULT_VALUE, &default_val);
			    if (error == NO_ERROR)
			      {
				if (!DB_IS_NULL (&default_val))
				  {
				    sig_arg_default_size[i] = db_get_string_size (&default_val);
				    if (sig_arg_default_size[i] > 0)
				      {
					sig_arg_default[i] = db_private_strndup (NULL, db_get_string (&default_val), sig_arg_default_size[i]);
				      }
				  }
				else
				  {
				    // default value is NULL
				    sig_arg_default_size[i] = -2; // special value
				  }
			      }
			    else
			      {
				goto end;
			      }
			  }
			else
			  {
			    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_PARAM_COUNT, 2, param_cnt, sig_num_args);
			    error = er_errid ();
			    goto end;
			  }
		      }
		    else
		      {
			goto end;
		      }
		  }

		pr_clear_value (&mode);
		pr_clear_value (&arg_type);
		pr_clear_value (&temp);
		pr_clear_value (&default_val);
	      }
	  }
	pr_clear_value (&args);

	/* result type */
	error = db_get (mop_p, SP_ATTR_RETURN_TYPE, &result_type);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }
	sig_result_type = db_get_int (&result_type);
	if (sig_result_type == DB_TYPE_RESULTSET && !jsp_is_prepare_call ())
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_RETURN_RESULTSET, 0);
	    error = er_errid ();
	    goto end;
	  }

	/* OID */
	error = db_get (mop_p, SP_ATTR_TARGET_CLASS, &target_cls_val);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	const char *target_cls = db_get_string (&target_cls_val);
	if (target_cls != NULL)
	  {
	    code_mop = jsp_find_stored_procedure_code (target_cls);
	  }
	pr_clear_value (&target_cls_val);
      }
    else
      {
	error = er_errid ();
	goto end;
      }

    sig = sig_list.method_sig = (METHOD_SIG *) db_private_alloc (NULL, sizeof (METHOD_SIG));
    if (sig)
      {
	sig->next = nullptr;
	sig->num_method_args = param_cnt;
	sig->method_type = METHOD_TYPE_JAVA_SP;
	sig->result_type = sig_result_type;

	std::string target;

	target.assign (db_get_string (&cls))
	.append (".")
	.append (db_get_string (&method));

	sig->method_name = db_private_strndup (NULL, target.c_str (), target.size ());
	if (!sig->method_name)
	  {
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto end;
	  }

	// lang
	error = db_get (mop_p, SP_ATTR_LANG, &lang_val);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	if (db_get_int (&lang_val) == SP_LANG_PLCSQL)
	  {
	    sig->method_type = METHOD_TYPE_PLCSQL;
	  }
	else
	  {
	    sig->method_type = METHOD_TYPE_JAVA_SP;
	  }

	// auth_name
	error = db_get (mop_p, SP_ATTR_DIRECTIVE, &directive_val);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	int directive = db_get_int (&directive_val);

	if (jsp_get_owner_name (parsed_method_name, owner, DB_MAX_USER_LENGTH) == NULL)
	  {
	    error = ER_FAILED;
	    goto end;
	  }

	const char *auth_name = (directive == SP_DIRECTIVE_ENUM::SP_DIRECTIVE_RIGHTS_OWNER ? owner :
				 au_get_current_user_name ());
	int auth_name_len = strlen (auth_name);

	sig->auth_name = (char *) db_private_alloc (NULL, auth_name_len * sizeof (char));
	if (!sig->auth_name)
	  {
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto end;
	  }

	memcpy (sig->auth_name, auth_name, auth_name_len);
	sig->auth_name[auth_name_len] = 0;

	sig->method_arg_pos = (int *) db_private_alloc (NULL, (param_cnt + 1) * sizeof (int));
	if (!sig->method_arg_pos)
	  {
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto end;
	  }

	for (int i = 0; i < param_cnt + 1; i++)
	  {
	    sig->method_arg_pos[i] = (i < sig_num_args) ? i : -1;
	  }

	sig->class_name = nullptr;


	sig->arg_info = (METHOD_ARG_INFO *) db_private_alloc (NULL, sizeof (METHOD_ARG_INFO));
	if (sig->arg_info)
	  {
	    if (param_cnt > 0)
	      {
		sig->arg_info->arg_mode = (int *) db_private_alloc (NULL, (param_cnt) * sizeof (int));
		if (!sig->arg_info->arg_mode)
		  {
		    sig->arg_info->arg_mode = nullptr;
		    error = ER_OUT_OF_VIRTUAL_MEMORY;
		    goto end;
		  }

		sig->arg_info->arg_type = (int *) db_private_alloc (NULL, (param_cnt) * sizeof (int));
		if (!sig->arg_info->arg_type)
		  {
		    sig->arg_info->arg_type = nullptr;
		    error = ER_OUT_OF_VIRTUAL_MEMORY;
		    goto end;
		  }

		sig->arg_info->default_value = (char **) db_private_alloc (NULL, (param_cnt) * sizeof (char *));
		sig->arg_info->default_value_size = (int *) db_private_alloc (NULL, (param_cnt) * sizeof (int));

		for (int i = 0; i < param_cnt; i++)
		  {
		    sig->arg_info->arg_mode[i] = sig_arg_mode[i];
		    sig->arg_info->arg_type[i] = sig_arg_type[i];

		    sig->arg_info->default_value_size[i] = sig_arg_default_size[i];
		    if (sig->arg_info->default_value_size[i] > 0)
		      {
			sig->arg_info->default_value[i] = sig_arg_default[i];
		      }
		  }
	      }
	    else
	      {
		sig->arg_info->arg_mode = nullptr;
		sig->arg_info->arg_type = nullptr;
		sig->arg_info->default_value = nullptr;
		sig->arg_info->default_value_size = nullptr;
	      }

	    sig_list.num_methods = 1;
	  }
	else
	  {
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto end;
	  }

	if (code_mop)
	  {
	    sig->oid  = *WS_OID (code_mop);
	  }
	else
	  {
	    sig->oid  = OID_INITIALIZER;
	  }
      }
  }

end:
  AU_ENABLE (save);
  if (error != NO_ERROR)
    {
      if (sig)
	{
	  sig->freemem ();
	}
      sig_list.method_sig = nullptr;
      sig_list.num_methods = 0;
    }

  pr_clear_value (&method);
  pr_clear_value (&param_cnt_val);
  pr_clear_value (&result_type);

  return error;
}

/*
 * pt_to_method_arglist () - converts a parse expression tree list of
 *                           method call arguments to method argument array
 *   return: A NULL on error occurred
 *   parser(in):
 *   target(in):
 *   node_list(in): should be parse name nodes
 *   subquery_as_attr_list(in):
 */
static int *
jsp_make_method_arglist (PARSER_CONTEXT *parser, PT_NODE *node_list)
{
  int *arg_list = NULL;
  int i = 0;
  int num_args = pt_length_of_list (node_list);
  PT_NODE *node;

  arg_list = (int *) db_private_alloc (NULL, num_args * sizeof (int));
  if (!arg_list)
    {
      return NULL;
    }

  for (node = node_list; node != NULL; node = node->next)
    {
      arg_list[i] = i;
      i++;
      /*
         arg_list[i] = pt_find_attribute (parser, node, subquery_as_attr_list);
         if (arg_list[i] == -1)
         {
         return NULL;
         }
         i++;
       */
    }

  return arg_list;
}

static int
check_execute_authorization_by_query (const MOP sp_obj)
{
  int error = NO_ERROR, save;
  const char *query = "SELECT [au] FROM " CT_CLASSAUTH_NAME
		      " [au] WHERE [object_type] = ? and [auth_type] = 'EXECUTE' and [object_of] = ?";
  DB_QUERY_RESULT *result = NULL;
  DB_SESSION *session = NULL;
  DB_VALUE val[2];
  int stmt_id;
  int cnt = 0;

  db_make_null (&val[0]);
  db_make_null (&val[1]);

  /* Disable the checking for internal authorization object access */
  AU_DISABLE (save);

  session = db_open_buffer_local (query);
  if (session == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto release;
    }

  error = db_set_system_generated_statement (session);
  if (error != NO_ERROR)
    {
      goto release;
    }

  stmt_id = db_compile_statement_local (session);
  if (stmt_id < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto release;
    }

  db_make_int (&val[0], (int) DB_OBJECT_PROCEDURE);
  db_make_object (&val[1], sp_obj);

  error = db_push_values (session, 2, val);
  if (error != NO_ERROR)
    {
      goto release;
    }

  cnt = error = db_execute_statement_local (session, stmt_id, &result);
  if (error < 0)
    {
      goto release;
    }

  error = db_query_end (result);

release:
  if (session != NULL)
    {
      db_close_session (session);
    }
  pr_clear_value (&val[0]);
  pr_clear_value (&val[1]);

  AU_ENABLE (save);

  return cnt;
}

static int
check_execute_authorization (const MOP sp_obj, const DB_AUTH au_type)
{
  int error = NO_ERROR;
  MOP owner_mop = NULL;
  DB_VALUE owner;

  if (au_type != DB_AUTH_EXECUTE)
    {
      return NO_ERROR;
    }

  if (au_is_dba_group_member (Au_user))
    {
      return NO_ERROR;
    }

  error = db_get (sp_obj, SP_ATTR_OWNER, &owner);
  if (error == NO_ERROR)
    {
      // check sp's owner is current user
      owner_mop = db_get_object (&owner);
      if (ws_is_same_object (owner_mop, Au_user) || ws_is_same_object (owner_mop, Au_public_user))
	{
	  return NO_ERROR;
	}
      else if (check_execute_authorization_by_query (sp_obj) == 0)
	{
	  error = ER_AU_EXECUTE_FAILURE;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else
	{
	  error = er_errid ();
	}
    }

  return error;
}

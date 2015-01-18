/* SAD-Script
 * Copyright (c) 2015, Brian Luft.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 * disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
 * following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#pragma warning(push, 0) /* ignore warnings in system headers */
#endif

#include <assert.h>
#include <string.h>
#include <stdio.h>

#ifdef _MSC_VER
#pragma warning(pop) /* start showing warnings again */
#endif

#include "sad-script.h"

#ifdef _MSC_VER
#pragma warning(disable: 4820) /* '4' bytes padding added after data member '...' */
#endif

/* Helpers ***********************************************************************************************************/
static void* SdAlloc(size_t size);
static void* SdRealloc(void* ptr, size_t size);
static void SdFree(void* ptr);
static char* SdStrdup(const char* src);

void* SdAlloc(size_t size) {
   return calloc(1, size);
}

void* SdRealloc(void* ptr, size_t size) {
   return realloc(ptr, size);
}

void SdFree(void* ptr) {
   free(ptr);
}

char* SdStrdup(const char* src) {
   assert(src);
   size_t length = strlen(src);
   char* dst = SdAlloc(length + 1);
   memcpy(dst, src, length);
   return dst;
}

/* SdResult **********************************************************************************************************/
SdResult SdSuccess(void) {
   SdResult err;
   memset(&err, 0, sizeof(err));
   err.code = SdErr_SUCCESS;
   err.message[0] = 0;
   return err;
}

SdResult SdFail(SdErr code, const char* message) {
   SdResult err;
   memset(&err, 0, sizeof(err));
   err.code = code;
   strncpy(err.message, message, sizeof(err.message) - 1); 
      /* ok if strncpy doesn't add a null terminator; memset() above zeroed out the string. */
   return err;
}

/* SdString **********************************************************************************************************/
struct SdString_s {
   char* buffer; /* includes null terminator */
   int length; /* not including null terminator */
};

SdString* SdString_New(void) {
   SdString* self = SdAlloc(sizeof(SdString));
   self->buffer = SdAlloc(sizeof(char)); /* one char for the null terminator */
   return self;
}

SdString* SdString_FromCStr(const char* cstr) {
   assert(cstr);
   SdString* self = SdAlloc(sizeof(SdString));
   self->buffer = SdStrdup(cstr);
   self->length = strlen(cstr);
   return self;
}

void SdString_Delete(SdString* self) {
   assert(self);
   if (self) {
      SdFree(self->buffer);
      SdFree(self);
   }
}

const char* SdString_CStr(SdString_r self) {
   assert(self);
   return self->buffer;
}

bool SdString_Equals(SdString_r a, SdString_r b) {
   assert(a);
   assert(b);
   return a->length == b->length && strcmp(a->buffer, b->buffer) == 0;
}

bool SdString_EqualsCStr(SdString_r a, const char* b) {
   assert(a);
   assert(b);
   return strcmp(a->buffer, b) == 0;  
}

int SdString_Length(SdString_r self) {
   assert(self);
   return self->length;
}

int SdString_Compare(SdString_r a, SdString_r b) {
   assert(a);
   assert(b);
   const char* a_str = SdString_CStr(a);
   const char* b_str = SdString_CStr(b);
   return strcmp(a_str, b_str);
}

/* SdStringBuf *******************************************************************************************************/
struct SdStringBuf_s {
   char* str;
   int len;
};

SdStringBuf* SdStringBuf_New(void) {
   SdStringBuf_r self = SdAlloc(sizeof(SdStringBuf));
   self->str = SdAlloc(sizeof(char)); /* zero-length string, with null terminator */
   self->len = 0;
   return self;
}

void SdStringBuf_Delete(SdStringBuf* self) {
   assert(self);
   SdFree(self->str);
   SdFree(self);
}

void SdStringBuf_AppendString(SdStringBuf_r self, SdString_r suffix) {
   assert(self);
   assert(suffix);
   SdStringBuf_AppendCStr(self, SdString_CStr(suffix));
}

void SdStringBuf_AppendCStr(SdStringBuf_r self, const char* suffix) {
   assert(self);
   assert(suffix);
   int old_len = self->len;
   int suffix_len = strlen(suffix);
   int new_len = old_len + suffix_len;

   self->str = SdRealloc(self->str, new_len + 1);
   memcpy(&self->str[old_len], suffix, suffix_len);
   self->str[new_len] = 0;
}

void SdStringBuf_AppendInt(SdStringBuf_r self, int number) {
   assert(self);
   char number_buf[30];
   memset(number_buf, 0, sizeof(number_buf));
   sprintf(number_buf, "%d", number);
   SdStringBuf_AppendCStr(self, number_buf);
}

const char* SdStringBuf_CStr(SdStringBuf_r self) {
   assert(self);
   return self->str;
}

/* SdValue ***********************************************************************************************************/
typedef union SdValueUnion_u {
   int int_value;
   SdString* string_value;
   bool bool_value;
   double double_value;
   SdList* list_value;
} SdValueUnion;

struct SdValue_s {
   SdType type;
   SdValueUnion payload;
};

static SdValue* SdValue_New(void);

SdValue* SdValue_NewNil(void) {
   return SdAlloc(sizeof(SdValue));
}

SdValue* SdValue_NewInt(int x) {
   SdValue* value = SdValue_NewNil();
   value->type = SdType_INT;
   value->payload.int_value = x;
   return value;
}

SdValue* SdValue_NewDouble(double x) {
   SdValue* value = SdValue_NewNil();
   value->type = SdType_DOUBLE;
   value->payload.double_value = x;
   return value;
}

SdValue* SdValue_NewBool(bool x) {
   SdValue* value = SdValue_NewNil();
   value->type = SdType_BOOL;
   value->payload.bool_value = x;
   return value;
}

SdValue* SdValue_NewString(SdString* x) {
   assert(x);
   SdValue* value = SdValue_NewNil();
   value->type = SdType_STRING;
   value->payload.string_value = x;
   return value;
}

SdValue* SdValue_NewList(SdList* x) {
   assert(x);
   SdValue* value = SdValue_NewNil();
   value->type = SdType_LIST;
   value->payload.list_value = x;
   return value;
}

void SdValue_Delete(SdValue* self) {
   assert(self);
   switch (SdValue_Type(self)) {
      case SdType_STRING:
         SdString_Delete(SdValue_GetString(self));
         break;
      case SdType_LIST:
         SdList_Delete(SdValue_GetList(self));
         break;
   }
   SdFree(self);
}

SdType SdValue_Type(SdValue_r self) {
   assert(self);
   return self->type;
}

int SdValue_GetInt(SdValue_r self) {
   assert(self);
   assert(SdValue_Type(self) == SdType_INT);
   return self->payload.int_value;
}

double SdValue_GetDouble(SdValue_r self) {
   assert(self);
   assert(SdValue_Type(self) == SdType_DOUBLE);
   return self->payload.double_value;
}

bool SdValue_GetBool(SdValue_r self) {
   assert(self);
   assert(SdValue_Type(self) == SdType_BOOL);
   return self->payload.bool_value;
}

SdString_r SdValue_GetString(SdValue_r self) {
   assert(self);
   assert(SdValue_Type(self) == SdType_STRING);
   return self->payload.string_value;
}

SdList_r SdValue_GetList(SdValue_r self) {
   assert(self);
   assert(SdValue_Type(self) == SdType_LIST);
   return self->payload.list_value;
}

/* SdList ************************************************************************************************************/
struct SdList_s {
   SdValue_r* values;
   size_t count;
};

SdList* SdList_New(void) {
   return SdAlloc(sizeof(SdList));
}

void SdList_Delete(SdList* self) {
   assert(self);
   SdFree(self->values);
   SdFree(self);
}

void SdList_Append(SdList_r self, SdValue_r item) {
   assert(self);
   assert(item);
   int new_count = self->count + 1;
   self->values = SdRealloc(self->values, new_count * sizeof(SdValue_r));
   assert(self->values); /* we're growing the list so SdRealloc() shouldn't return NULL. */
   self->values[new_count - 1] = item;
   self->count = new_count;
}

void SdList_SetAt(SdList_r self, size_t index, SdValue_r item) {
   assert(self);
   assert(item);
   assert(index < self->count + 1);
   self->values[index] = item;
}

void SdList_InsertAt(SdList_r self, size_t index, SdValue_r item) {
   assert(self);
   assert(item);
   assert(index < self->count);
   if (index == self->count) {
      SdList_Append(self, item);
   } else {
      int new_count = self->count + 1;
      self->values = SdRealloc(self->values, new_count * sizeof(SdValue_r));
      assert(self->values); /* we're growing the list so SdRealloc() shouldn't return NULL. */
      memmove(&self->values[index + 1], &self->values[index], sizeof(SdValue_r) * (self->count - index));
      self->values[index] = item;
      self->count = new_count;
   }
}

SdValue_r SdList_GetAt(SdList_r self, size_t index) {
   assert(self);
   assert(index < self->count);
   return self->values[index];
}

size_t SdList_Count(SdList_r self) {
   assert(self);
   return self->count;
}

SdValue_r SdList_RemoveAt(SdList_r self, size_t index) {
   assert(self);
   assert(index < self->count);
   size_t i;
   SdValue_r old_value = SdList_GetAt(self, index);
   for (i = index + 1; i < self->count; i++) {
      self->values[i - 1] = self->values[i];
   }
   self->values[self->count - 1] = NULL;
   self->count--;
   return old_value;
}

void SdList_Clear(SdList_r self) {
   assert(self);
   SdFree(self->values);
   self->values = NULL;
   self->count = 0;
}

/* SdEnv *************************************************************************************************************/
typedef struct SdValueListNode_s {
   SdValue* value;
   struct SdValueListNode_s* next;
} SdValueListNode;

typedef struct SdEnv_BinarySearchResult_s {
   int index; /* could be one past the end of the list if search name > everything */
   bool exact; /* true = index is an exact match, false = index is the next highest match */
} SdEnv_BinarySearchResult;

struct SdEnv_s {
   SdValue_r root; /* contains all living/connected objects */
   SdValueListNode* values_list; /* contains all objects that haven't been deleted yet */
};

static SdEnv_BinarySearchResult SdEnv_BinarySearchByName(SdList_r list, SdString_r name);
static bool SdEnv_InsertByName(SdList_r list, SdValue_r item);

/* list is (list (list <unrelated> name1:str ...) (list <unrelated> name2:str ...) ...) 
The objects are sorted by name.  If an exact match is found, then its index is returned.  Otherwise the next highest 
match is returned.  The index may be one past the end of the list indicating that the search_name is higher than any 
name in the list. */
SdEnv_BinarySearchResult SdEnv_BinarySearchByName(SdList_r list, SdString_r search_name) {
   SdEnv_BinarySearchResult result = { 0, false };
   int first = 0;
   int last = SdList_Count(list) - 1;
   int pivot = (first + last) / 2;

   while (first <= last) {
      SdList_r pivot_item = SdValue_GetList(SdList_GetAt(list, pivot));
      SdString_r pivot_name = SdValue_GetString(SdList_GetAt(pivot_item, 1));
      int compare = SdString_Compare(pivot_name, search_name);

      if (compare < 0) { /* pivot_name < search_name */
         first = pivot + 1;

         if (first > last) { /* pivot is the closest non-match less than search_name, but we want the next highest. */
            result.index = pivot + 1;
            result.exact = false;
            return result;
         }
      } else if (compare > 0) { /* pivot_name > search_name */
         last = pivot - 1;

         if (first > last) { /* pivot is the closest non-match higher than search_name. */
            result.index = pivot;
            result.exact = false;
            return result;
         }
      } else { /* pivot_name = search_name */
         result.index = pivot;
         result.exact = true;
         return result;
      }
   }

   /* The list is empty. */
   result.index = 0;
   result.exact = false;
   return result;
}

/* list is (list (list <unrelated> name1:str ...) (list <unrelated> name2:str ...) ...)
   The objects are sorted by name. Returns true if the item was inserted, false if the name already exists. */
bool SdEnv_InsertByName(SdList_r list, SdValue_r item) {
   SdString_r item_name = SdValue_GetString(SdList_GetAt(SdValue_GetList(item), 1));
   SdEnv_BinarySearchResult result = SdEnv_BinarySearchByName(list, item_name);

   if (result.exact) { /* An item with this name already exists. */
      return false; 
   } else {
      SdList_InsertAt(list, result.index, item);
      return true;
   }
}

SdEnv* SdEnv_New(void) {
   SdEnv* env = SdAlloc(sizeof(SdEnv));
   env->root = SdEnv_Root_New(env);
   SdEnv_AddToGc(env, env->root);
   return env;
}

void SdEnv_Delete(SdEnv* self) {
   assert(self);
   /* Allow the garbage collector to clean up the tree starting at root. */
   self->root = NULL;
   SdEnv_CollectGarbage(self);
   assert(!self->values_list); /* shouldn't be anything left */
   SdFree(self);
}

SdValue_r SdEnv_Root(SdEnv_r self) {
   assert(self);
   return self->root;
}

void SdEnv_AddToGc(SdEnv* self, SdValue* value) {
   assert(self);
   assert(value);
   SdValueListNode* new_node = SdAlloc(sizeof(SdValueListNode));
   new_node->value = value;
   new_node->next = self->values_list;
   self->values_list = new_node;
}

SdResult SdEnv_AddProgramAst(SdEnv_r self, SdValue_r program_node) {
   assert(self);
   assert(program_node);
   SdResult result;
   SdList_r root_functions = SdEnv_Root_Functions(self->root);
   SdList_r root_statements = SdEnv_Root_Statements(self->root);
   SdList_r new_functions = SdAst_Program_Functions(program_node);
   size_t new_functions_count = SdList_Count(new_functions);
   SdList_r new_statements = SdAst_Program_Statements(program_node);
   size_t new_statements_count = SdList_Count(new_statements);
   size_t i;

   for (i = 0; i < new_functions_count; i++) {
      SdValue_r new_function = SdList_GetAt(new_functions, i);
      if (!SdEnv_InsertByName(root_functions, new_function)) {
         SdStringBuf* buf = SdStringBuf_New();
         SdStringBuf_AppendCStr(buf, "Duplicate function: ");
         SdStringBuf_AppendString(buf, SdAst_Function_Name(new_function));
         result = SdFail(SdErr_NAME_COLLISION, SdStringBuf_CStr(buf));
         SdStringBuf_Delete(buf);
         return result;
      }
   }

   for (i = 0; i < new_statements_count; i++) {
      SdValue_r new_statement = SdList_GetAt(new_statements, i);
      SdList_Append(root_statements, new_statement);
   }

   return SdSuccess();
}

SdValue_r SdEnv_Root_New(SdEnv_r env) {
   assert(env);
   SdValue_r frame = SdEnv_Frame_New(env, NULL);
   SdList* root_list = SdList_New();
   SdList_Append(root_list, SdEnv_BoxInt(env, SdNodeType_ROOT));
   SdList_Append(root_list, SdEnv_BoxList(env, SdList_New())); /* functions */
   SdList_Append(root_list, SdEnv_BoxList(env, SdList_New())); /* statements */
   SdList_Append(root_list, frame);
   return SdEnv_BoxList(env, root_list);
}

/* SdAst *************************************************************************************************************/
static SdValue_r SdAst_NodeValue(SdValue_r node, size_t value_index);
static SdValue_r SdAst_NewNode(SdEnv_r env, SdValue_r values[], int num_values);

/* A simple macro-based DSL for implementing the AST node functions. */
#define SdAst_MAX_NODE_VALUES 5
#define SdAst_BEGIN(node_type) \
   SdValue_r values[SdAst_MAX_NODE_VALUES]; \
   int i = 0; \
   values[i++] = SdEnv_BoxInt(env, node_type);
#define SdAst_VALUE(value) \
   values[i++] = value;
#define SdAst_INT(x) \
   values[i++] = SdEnv_BoxInt(env, x);
#define SdAst_DOUBLE(x) \
   values[i++] = SdEnv_BoxDouble(env, x);
#define SdAst_BOOL(x) \
   values[i++] = SdEnv_BoxBool(env, x);
#define SdAst_STRING(x) \
   values[i++] = SdEnv_BoxString(env, x);
#define SdAst_LIST(x) \
   values[i++] = SdEnv_BoxList(env, x);
#define SdAst_NIL() \
   values[i++] = SdEnv_BoxNil(env);
#define SdAst_END \
   return SdAst_NewNode(env, values, i);
#define SdAst_UNBOXED_GETTER(function_name, node_type, result_type, value_getter, index) \
   result_type function_name(SdValue_r self) { \
      assert(self); \
      assert(SdAst_NodeType(self) == node_type); \
      return value_getter(SdAst_NodeValue(self, index)); \
   }
#define SdAst_INT_GETTER(function_name, node_type, index) \
   SdAst_UNBOXED_GETTER(function_name, node_type, int, SdValue_GetInt, index)
#define SdAst_DOUBLE_GETTER(function_name, node_type, index) \
   SdAst_UNBOXED_GETTER(function_name, node_type, double, SdValue_GetDouble, index)
#define SdAst_BOOL_GETTER(function_name, node_type, index) \
   SdAst_UNBOXED_GETTER(function_name, node_type, bool, SdValue_GetBool, index)
#define SdAst_STRING_GETTER(function_name, node_type, index) \
   SdAst_UNBOXED_GETTER(function_name, node_type, SdString_r, SdValue_GetString, index)
#define SdAst_LIST_GETTER(function_name, node_type, index) \
   SdAst_UNBOXED_GETTER(function_name, node_type, SdList_r, SdValue_GetList, index)
#define SdAst_VALUE_GETTER(function_name, node_type, index) \
   SdValue_r function_name(SdValue_r self) { \
      assert(self); \
      assert(SdAst_NodeType(self) == node_type); \
      return SdAst_NodeValue(self, index); \
   }

SdValue_r SdAst_NodeValue(SdValue_r node, size_t value_index) {
   assert(node);
   return SdList_GetAt(SdValue_GetList(node), value_index);
}

SdNodeType SdAst_NodeType(SdValue_r node) {
   assert(node);
   return SdValue_GetInt(SdAst_NodeValue(node, 0));
}

SdValue_r SdAst_NewNode(SdEnv_r env, SdValue_r values[], int num_values) {
   assert(env);
   assert(values);
   SdList* node = SdList_New();
   int i;
   for (i = 0; i < num_values; i++) {
      assert(values[i]);
      SdList_Append(node, values[i]);
   }
   return SdEnv_BoxList(env, node);
}

SdValue_r SdAst_Program_New(SdEnv_r env, SdList* functions, SdList* statements) {
   SdAst_BEGIN(SdNodeType_PROGRAM)
   SdAst_LIST(functions)
   SdAst_LIST(statements)
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_Program_Functions, SdNodeType_PROGRAM, 1)
SdAst_LIST_GETTER(SdAst_Program_Statements, SdNodeType_PROGRAM, 2)

SdValue_r SdAst_Function_New(SdEnv_r env, SdString* function_name, SdValue_r body) {
   SdAst_BEGIN(SdNodeType_FUNCTION)
   SdAst_STRING(function_name)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_Function_Name, SdNodeType_FUNCTION, 1)
SdAst_VALUE_GETTER(SdAst_Function_Body, SdNodeType_FUNCTION, 2)

SdValue_r SdAst_Body_New(SdEnv_r env, SdList* statements) {
   SdAst_BEGIN(SdNodeType_BODY)
   SdAst_LIST(statements);
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_Body_Statements, SdNodeType_BODY, 1)

SdValue_r SdAst_Call_New(SdEnv_r env, SdString* function_name, SdList* arguments) {
   SdAst_BEGIN(SdNodeType_CALL)
   SdAst_STRING(function_name)
   SdAst_LIST(arguments)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_Call_FunctionName, SdNodeType_CALL, 1)
SdAst_LIST_GETTER(SdAst_Call_Arguments, SdNodeType_CALL, 2)

SdValue_r SdAst_Var_New(SdEnv_r env, SdString* variable_name, SdValue_r value_expr) {
   SdAst_BEGIN(SdNodeType_VAR)
   SdAst_STRING(variable_name)
   SdAst_VALUE(value_expr)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_Var_VariableName, SdNodeType_VAR, 1)
SdAst_VALUE_GETTER(SdAst_Var_ValueExpr, SdNodeType_VAR, 2)

SdValue_r SdAst_Set_New(SdEnv_r env, SdString* variable_name, SdValue_r value_expr) {
   SdAst_BEGIN(SdNodeType_SET)
   SdAst_STRING(variable_name)
   SdAst_VALUE(value_expr)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_Set_VariableName, SdNodeType_SET, 1)
SdAst_VALUE_GETTER(SdAst_Set_ValueExpr, SdNodeType_SET, 2)

SdValue_r SdAst_If_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r true_body, SdList* else_ifs,
   SdValue_r else_body) {
   SdAst_BEGIN(SdNodeType_IF)
   SdAst_VALUE(condition_expr)
   SdAst_VALUE(true_body)
   SdAst_LIST(else_ifs)
   SdAst_VALUE(else_body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_If_ConditionExpr, SdNodeType_IF, 1)
SdAst_VALUE_GETTER(SdAst_If_TrueBody, SdNodeType_IF, 2)
SdAst_LIST_GETTER(SdAst_If_ElseIfs, SdNodeType_IF, 3)
SdAst_VALUE_GETTER(SdAst_If_ElseBody, SdNodeType_IF, 4)

SdValue_r SdAst_ElseIf_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r body) {
   SdAst_BEGIN(SdNodeType_ELSEIF)
   SdAst_VALUE(condition_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_ElseIf_ConditionExpr, SdNodeType_IF, 1)
SdAst_VALUE_GETTER(SdAst_ElseIf_Body, SdNodeType_IF, 2)

SdValue_r SdAst_For_New(SdEnv_r env, SdString* variable_name, SdValue_r start_expr, SdValue_r stop_expr,
   SdValue_r body) {
   SdAst_BEGIN(SdNodeType_FOR)
   SdAst_STRING(variable_name)
   SdAst_VALUE(start_expr)
   SdAst_VALUE(stop_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_For_VariableName, SdNodeType_FOR, 1)
SdAst_VALUE_GETTER(SdAst_For_StartExpr, SdNodeType_FOR, 2)
SdAst_VALUE_GETTER(SdAst_For_StopExpr, SdNodeType_FOR, 3)
SdAst_VALUE_GETTER(SdAst_For_Body, SdNodeType_FOR, 4)

SdValue_r SdAst_ForEach_New(SdEnv_r env, SdString* iter_name, SdString* index_name_or_null, SdValue_r haystack_expr,
   SdValue_r body) {
   SdAst_BEGIN(SdNodeType_FOREACH)
   SdAst_STRING(iter_name)
   if (index_name_or_null) {
      SdAst_STRING(index_name_or_null);
   } else {
      SdAst_NIL()
   }
   SdAst_VALUE(haystack_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_ForEach_IterName, SdNodeType_FOREACH, 1)
SdAst_STRING_GETTER(SdAst_ForEach_IndexName, SdNodeType_FOREACH, 2)
SdAst_VALUE_GETTER(SdAst_ForEach_HaystackExpr, SdNodeType_FOREACH, 3)
SdAst_VALUE_GETTER(SdAst_ForEach_Body, SdNodeType_FOREACH, 4)

SdValue_r SdAst_While_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r body) {
   SdAst_BEGIN(SdNodeType_WHILE)
   SdAst_VALUE(condition_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_While_ConditionExpr, SdNodeType_WHILE, 1)
SdAst_VALUE_GETTER(SdAst_While_Body, SdNodeType_WHILE, 2)

SdValue_r SdAst_Do_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r body) {
   SdAst_BEGIN(SdNodeType_DO)
   SdAst_VALUE(condition_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Do_ConditionExpr, SdNodeType_DO, 1)
SdAst_VALUE_GETTER(SdAst_Do_Body, SdNodeType_DO, 2)

SdValue_r SdAst_Switch_New(SdEnv_r env, SdValue_r expr, SdList* cases, SdValue_r default_body) {
   SdAst_BEGIN(SdNodeType_SWITCH)
   SdAst_VALUE(expr)
   SdAst_LIST(cases)
   SdAst_VALUE(default_body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Switch_Expr, SdNodeType_SWITCH, 1)
SdAst_LIST_GETTER(SdAst_Switch_Cases, SdNodeType_SWITCH, 2)
SdAst_VALUE_GETTER(SdAst_Switch_DefaultBody, SdNodeType_SWITCH, 3)

SdValue_r SdAst_Case_New(SdEnv_r env, SdValue_r expr, SdValue_r body) {
   SdAst_BEGIN(SdNodeType_CASE)
   SdAst_VALUE(expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Case_Expr, SdNodeType_CASE, 1)
SdAst_VALUE_GETTER(SdAst_Case_Body, SdNodeType_CASE, 2)

SdValue_r SdAst_Return_New(SdEnv_r env, SdValue_r expr) {
   SdAst_BEGIN(SdNodeType_RETURN)
   SdAst_VALUE(expr)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Return_Expr, SdNodeType_RETURN, 1)

SdValue_r SdAst_Throw_New(SdEnv_r env, SdValue_r expr) {
   SdAst_BEGIN(SdNodeType_THROW)
   SdAst_VALUE(expr)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Throw_Expr, SdNodeType_THROW, 1)

SdValue_r SdAst_IntLit_New(SdEnv_r env, int value) {
   SdAst_BEGIN(SdNodeType_INT_LIT)
   SdAst_INT(value)
   SdAst_END
}
SdAst_INT_GETTER(SdAst_IntLit_Value, SdNodeType_INT_LIT, 1)

SdValue_r SdAst_DoubleLit_New(SdEnv_r env, double value) {
   SdAst_BEGIN(SdNodeType_DOUBLE_LIT)
   SdAst_DOUBLE(value)
   SdAst_END
}
SdAst_DOUBLE_GETTER(SdAst_DoubleLit_Value, SdNodeType_DOUBLE_LIT, 1)

SdValue_r SdAst_BoolLit_New(SdEnv_r env, bool value) {
   SdAst_BEGIN(SdNodeType_BOOL_LIT)
   SdAst_BOOL(value)
   SdAst_END
}
SdAst_BOOL_GETTER(SdAst_BoolLit_Value, SdNodeType_BOOL_LIT, 1)

SdValue_r SdAst_StringLit_New(SdEnv_r env, SdString* value) {
   SdAst_BEGIN(SdNodeType_STRING_LIT)
   SdAst_STRING(value)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_StringLit_Value, SdNodeType_STRING_LIT, 1)

SdValue_r SdAst_NilLit_New(SdEnv_r env) {
   SdAst_BEGIN(SdNodeType_NIL_LIT)
   SdAst_END
}

SdValue_r SdAst_VarRef_New(SdEnv_r env, SdString* identifier) {
   SdAst_BEGIN(SdNodeType_VAR_REF)
   SdAst_STRING(identifier)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_VarRef_Identifier, SdNodeType_VAR_REF, 1)

SdValue_r SdAst_Query_New(SdEnv_r env, SdValue_r initial_expr, SdList* steps) {
   SdAst_BEGIN(SdNodeType_QUERY)
   SdAst_VALUE(initial_expr)
   SdAst_LIST(steps)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Query_InitialExpr, SdNodeType_QUERY, 1)
SdAst_LIST_GETTER(SdAst_Query_Steps, SdNodeType_QUERY, 2)

SdValue_r SdAst_QueryStep_New(SdEnv_r env, SdString* function_name, SdList* arguments, SdValue_r pred_or_null) {
   SdAst_BEGIN(SdNodeType_QUERY_STEP)
   SdAst_STRING(function_name)
   SdAst_LIST(arguments)
   if (pred_or_null) {
      SdAst_VALUE(pred_or_null);
   } else {
      SdAst_NIL()
   }
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_QueryStep_FunctionName, SdNodeType_QUERY_STEP, 1)
SdAst_LIST_GETTER(SdAst_QueryStep_Arguments, SdNodeType_QUERY_STEP, 2)
SdAst_VALUE_GETTER(SdAst_QueryStep_Predicate, SdNodeType_QUERY_STEP, 3)

SdValue_r SdAst_QueryPred_New(SdEnv_r env, SdString* parameter_name, SdValue_r expr) {
   SdAst_BEGIN(SdNodeType_QUERY_PRED)
   SdAst_STRING(parameter_name)
   SdAst_VALUE(expr)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_QueryPred_ParameterName, SdNodeType_QUERY_PRED, 1)
SdAst_VALUE_GETTER(SdAst_QueryPred_Expr, SdNodeType_QUERY_PRED, 2)

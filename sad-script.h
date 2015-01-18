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

#ifndef _SAD_BASIC_H_
#define _SAD_BASIC_H_

#ifdef _MSC_VER
#pragma warning(push, 0) /* ignore warnings in system headers */
#endif

#include <stdlib.h> /* size_t */
#include <stdbool.h> /* bool */

#ifdef _MSC_VER
#pragma warning(pop) /* start showing warnings again */
#pragma warning(push)
#pragma warning(disable: 4820) /* '4' bytes padding added after data member '...' */
#endif

/* Raw pointer: ownership is being transferred.
_r suffix: a pointer is being borrowed, but ownership is not transferred. */
typedef struct SdResult_s SdResult;
typedef struct Sad_s Sad;
typedef struct Sad_s* Sad_r;
typedef struct SdString_s SdString;
typedef struct SdString_s* SdString_r;
typedef struct SdStringBuf_s SdStringBuf;
typedef struct SdStringBuf_s* SdStringBuf_r;
typedef struct SdValue_s SdValue;
typedef struct SdValue_s* SdValue_r;
typedef struct SdList_s SdList;
typedef struct SdList_s* SdList_r;
typedef struct SdEnv_s SdEnv;
typedef struct SdEnv_s* SdEnv_r;
typedef struct SdCallStack_s SdCallStack;
typedef struct SdCallStack_s* SdCallStack_r;
typedef struct SdValueSet_s SdValueSet;
typedef struct SdValueSet_s* SdValueSet_r;

typedef enum SdErr_e {
   SdErr_SUCCESS = 1,
   SdErr_NAME_COLLISION = 2
} SdErr;

typedef enum SdType_e {
   /* These numeric values are returned by (type-of) and must not change. */
   SdType_NIL = 0,
   SdType_INT = 1,
   SdType_DOUBLE = 2,
   SdType_BOOL = 3,
   SdType_STRING = 4,
   SdType_LIST = 5
} SdType;

typedef enum SdNodeType_e {
   /* Environment */
   SdNodeType_ROOT,
   SdNodeType_FRAME,
   SdNodeType_VAR_SLOT,

   /* AST */
   SdNodeType_PROGRAM,
   SdNodeType_FUNCTION,
   SdNodeType_BODY,
   
   /* Statements */
   SdNodeType_STATEMENT,
   SdNodeType_CALL, /* also an expression */
   SdNodeType_VAR,
   SdNodeType_SET,
   SdNodeType_IF,
   SdNodeType_ELSEIF,
   SdNodeType_FOR,
   SdNodeType_FOREACH,
   SdNodeType_WHILE,
   SdNodeType_DO,
   SdNodeType_SWITCH,
   SdNodeType_CASE,
   SdNodeType_RETURN,
   SdNodeType_THROW,

   /* Expressions */
   SdNodeType_INT_LIT,
   SdNodeType_DOUBLE_LIT,
   SdNodeType_BOOL_LIT,
   SdNodeType_STRING_LIT,
   SdNodeType_NIL_LIT,
   SdNodeType_VAR_REF,
   SdNodeType_QUERY,
   SdNodeType_QUERY_STEP,
   SdNodeType_QUERY_PRED
} SdNodeType;

typedef struct SdSearchResult_s {
   int index; /* could be one past the end of the list if search name > everything */
   bool exact; /* true = index is an exact match, false = index is the next highest match */
} SdSearchResult;

typedef int(*SdSearchCompareFunc)(SdValue_r lhs, void* context);

/* Sad ***************************************************************************************************************/
Sad*           Sad_New(void);
void           Sad_Delete(Sad* self);
SdResult       Sad_CallFunction(Sad_r self, SdString_r function_name, SdList_r arguments);
SdResult       Sad_ExecuteScript(Sad_r self, SdString_r code);
SdEnv_r        Sad_Env(Sad_r self);

/* SdResult ***********************************************************************************************************/
struct SdResult_s {
   SdErr code;
   char message[80];
};

SdResult       SdSuccess(void);
SdResult       SdFail(SdErr code, const char* message);

/* SdString **********************************************************************************************************/
SdString*      SdString_New(void);
SdString*      SdString_FromCStr(const char* cstr);
void           SdString_Delete(SdString* self);
const char*    SdString_CStr(SdString_r self);
bool           SdString_Equals(SdString_r a, SdString_r b);
bool           SdString_EqualsCStr(SdString_r a, const char* b);
int            SdString_Length(SdString_r self);
int            SdString_Compare(SdString_r a, SdString_r b);

/* SdStringBuf *******************************************************************************************************/
SdStringBuf*   SdStringBuf_New(void);
void           SdStringBuf_Delete(SdStringBuf* self);
void           SdStringBuf_AppendString(SdStringBuf_r self, SdString_r suffix);
void           SdStringBuf_AppendCStr(SdStringBuf_r self, const char* suffix);
void           SdStringBuf_AppendInt(SdStringBuf_r self, int number);
const char*    SdStringBuf_CStr(SdStringBuf_r self);

/* SdValue ***********************************************************************************************************/
SdValue*       SdValue_NewNil(void);
SdValue*       SdValue_NewInt(int x);
SdValue*       SdValue_NewDouble(double x);
SdValue*       SdValue_NewBool(bool x);
SdValue*       SdValue_NewString(SdString* x);
SdValue*       SdValue_NewList(SdList* x);
void           SdValue_Delete(SdValue* self);
SdType         SdValue_Type(SdValue_r self);
int            SdValue_GetInt(SdValue_r self);
double         SdValue_GetDouble(SdValue_r self);
bool           SdValue_GetBool(SdValue_r self);
SdString_r     SdValue_GetString(SdValue_r self);
SdList_r       SdValue_GetList(SdValue_r self);

/* SdList ************************************************************************************************************/
SdList*        SdList_New(void);
void           SdList_Delete(SdList* self);
void           SdList_Append(SdList_r self, SdValue_r item);
void           SdList_SetAt(SdList_r self, size_t index, SdValue_r item);
void           SdList_InsertAt(SdList_r self, size_t index, SdValue_r item);
SdValue_r      SdList_GetAt(SdList_r self, size_t index);
size_t         SdList_Count(SdList_r self);
SdValue_r      SdList_RemoveAt(SdList_r self, size_t index);
void           SdList_Clear(SdList_r self);
SdSearchResult SdList_Search(SdList_r list, SdSearchCompareFunc compare_func, void* context); /* list must be sorted */
bool           SdList_InsertBySearch(SdList_r list, SdValue_r item, SdSearchCompareFunc compare_func, void* context);

/* SdEnv *************************************************************************************************************/
/*
   Environment structure:
   Root: 
      (list ROOT (list Function ...) (list Statement ...) top:StackFrame)
                  ^-- sorted by name
   StackFrame:
      (list FRAME parent:StackFrame? (list VariableSlot ...))
                                      ^-- sorted by name
   VariableSlot:
      (list VAR_SLOT name:Str payload:Value)
*/

SdEnv*         SdEnv_New(void);
void           SdEnv_Delete(SdEnv* self);
SdValue_r      SdEnv_Root(SdEnv_r self);
SdValue_r      SdEnv_AddToGc(SdEnv_r self, SdValue* value);
SdResult       SdEnv_AddProgramAst(SdEnv_r self, SdValue_r program_node);
SdResult       SdEnv_ExecuteTopLevelStatements(SdEnv_r self);
SdResult       SdEnv_CallFunction(SdEnv_r self, SdString_r function_name, SdList_r arguments);
SdValue_r      SdEnv_GetGlobalVariable(SdEnv_r self, SdString_r variable_name); /* may return null */
void           SdEnv_CollectGarbage(SdEnv_r self);
void           SdEnv_PushFrame(SdEnv_r self);
void           SdEnv_PopFrame(SdEnv_r self);
void           SdEnv_Assign(SdEnv_r self, SdString_r name, SdValue_r value);
SdValue_r      SdEnv_FindStackVariable(SdEnv_r self, SdString_r name);

SdValue_r      SdEnv_BoxNil(SdEnv_r env);
SdValue_r      SdEnv_BoxInt(SdEnv_r env, int x);
SdValue_r      SdEnv_BoxDouble(SdEnv_r env, double x);
SdValue_r      SdEnv_BoxBool(SdEnv_r env, bool x);
SdValue_r      SdEnv_BoxString(SdEnv_r env, SdString* x);
SdValue_r      SdEnv_BoxList(SdEnv_r env, SdList* x);

SdValue_r      SdEnv_Root_New(SdEnv_r env);
SdList_r       SdEnv_Root_Functions(SdValue_r self);
SdList_r       SdEnv_Root_Statements(SdValue_r self);
SdValue_r      SdEnv_Root_TopFrame(SdValue_r self); /* may be null */

SdValue_r      SdEnv_Frame_New(SdEnv_r env, SdValue_r parent_or_null);
SdValue_r      SdEnv_Frame_Parent(SdValue_r self); /* may be null */
SdList_r       SdEnv_Frame_VariableSlots(SdValue_r self);

SdValue_r      SdEnv_VariableSlot_New(SdEnv_r env, SdString_r name, SdValue_r value);
SdString_r     SdEnv_VariableSlot_Name(SdValue_r self);
SdValue_r      SdEnv_VariableSlot_Value(SdValue_r self);

/* SdAst *************************************************************************************************************/
/*
   AST structure:
   ? - indicates that the value may be nil

               0           1                      2                      3                  4
   Program:                                    
      (list PROGRAM     (list Function ...)    (list Statement ...))
   Function:                                   
      (list FUNCTION    name:Str               Body)
   Statement:                                  
      (list CALL        function-name:Str      (list Expr...))
      (list VAR         variable-name:Str      value:Expr)
      (list SET         variable-name:Str      value:Expr)
      (list IF          condition:Expr         if-true:Body           (list ElseIf ...)  else:Body)
      (list FOR         variable-name:Str      start:Expr             stop:Expr          Body)
      (list FOREACH     iter-name:Str          index-name:Str?        haystack:Expr      Body)
      (list WHILE       condition:Expr         Body)
      (list DO          condition:Expr         Body)
      (list SWITCH      Expr                   (list Case ...)        default:Body)
      (list RETURN      Expr)
      (list THROW       Expr)
   ElseIf:                                     
      (list ELSEIF      condition:Expr         Body)
   Case:                                       
      (list CASE        Expr                   Body)
   Expr:                                       
      (list INT_LIT     Int)
      (list DOUBLE_LIT  Double)
      (list BOOL_LIT    Bool)
      (list STRING_LIT  Str)
      (list NIL_LIT)
      (list CALL        function-name:Str      (list Expr ...))
      (list VAR_REF     identifier:Str)        
      (list QUERY       Expr                   (list QueryStep ...))
   QueryStep:                                  
      (list QUERY_STEP  function-name:Str      (list arg:Expr ...)    QueryPred?)
   QueryPred:                                  
      (list QUERY_PRED  param-name:Str         Expression)
   Body:                                       
      (list BODY        (list Statement ...))  
*/

SdNodeType     SdAst_NodeType(SdValue_r node);

SdValue_r      SdAst_Program_New(SdEnv_r env, SdList* functions, SdList* statements);
SdList_r       SdAst_Program_Functions(SdValue_r self);
SdList_r       SdAst_Program_Statements(SdValue_r self);

SdValue_r      SdAst_Function_New(SdEnv_r env, SdString* function_name, SdValue_r body);
SdString_r     SdAst_Function_Name(SdValue_r self);
SdValue_r      SdAst_Function_Body(SdValue_r self);

SdValue_r      SdAst_Body_New(SdEnv_r env, SdList* statements);
SdList_r       SdAst_Body_Statements(SdValue_r self);

SdValue_r      SdAst_Call_New(SdEnv_r env, SdString* function_name, SdList* arguments);
SdString_r     SdAst_Call_FunctionName(SdValue_r self);
SdList_r       SdAst_Call_Arguments(SdValue_r self);

SdValue_r      SdAst_Var_New(SdEnv_r env, SdString* variable_name, SdValue_r value_expr);
SdString_r     SdAst_Var_VariableName(SdValue_r self);
SdValue_r      SdAst_Var_ValueExpr(SdValue_r self);

SdValue_r      SdAst_Set_New(SdEnv_r env, SdString* variable_name, SdValue_r value_expr);
SdString_r     SdAst_Set_VariableName(SdValue_r self);
SdValue_r      SdAst_Set_ValueExpr(SdValue_r self);

SdValue_r      SdAst_If_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r true_body, SdList* else_ifs, 
                  SdValue_r else_body);
SdValue_r      SdAst_If_ConditionExpr(SdValue_r self);
SdValue_r      SdAst_If_TrueBody(SdValue_r self);
SdList_r       SdAst_If_ElseIfs(SdValue_r self);
SdValue_r      SdAst_If_ElseBody(SdValue_r self);

SdValue_r      SdAst_ElseIf_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r body);
SdValue_r      SdAst_ElseIf_ConditionExpr(SdValue_r self);
SdValue_r      SdAst_ElseIf_Body(SdValue_r self);

SdValue_r      SdAst_For_New(SdEnv_r env, SdString* variable_name, SdValue_r start_expr, SdValue_r stop_expr,
                  SdValue_r body);
SdString_r     SdAst_For_VariableName(SdValue_r self);
SdValue_r      SdAst_For_StartExpr(SdValue_r self);
SdValue_r      SdAst_For_StopExpr(SdValue_r self);
SdValue_r      SdAst_For_Body(SdValue_r self);

SdValue_r      SdAst_ForEach_New(SdEnv_r env, SdString* iter_name, SdString* index_name_or_null, 
                  SdValue_r haystack_expr, SdValue_r body);
SdString_r     SdAst_ForEach_IterName(SdValue_r self);
SdString_r     SdAst_ForEach_IndexName(SdValue_r self); /* may be null */
SdValue_r      SdAst_ForEach_HaystackExpr(SdValue_r self);
SdValue_r      SdAst_ForEach_Body(SdValue_r self);

SdValue_r      SdAst_While_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r body);
SdValue_r      SdAst_While_ConditionExpr(SdValue_r self);
SdValue_r      SdAst_While_Body(SdValue_r self);

SdValue_r      SdAst_Do_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r body);
SdValue_r      SdAst_Do_ConditionExpr(SdValue_r self);
SdValue_r      SdAst_Do_Body(SdValue_r self);

SdValue_r      SdAst_Switch_New(SdEnv_r env, SdValue_r expr, SdList* cases, SdValue_r default_body);
SdValue_r      SdAst_Switch_Expr(SdValue_r self);
SdList_r       SdAst_Switch_Cases(SdValue_r self);
SdValue_r      SdAst_Switch_DefaultBody(SdValue_r self);

SdValue_r      SdAst_Case_New(SdEnv_r env, SdValue_r expr, SdValue_r body);
SdValue_r      SdAst_Case_Expr(SdValue_r self);
SdValue_r      SdAst_Case_Body(SdValue_r self);

SdValue_r      SdAst_Return_New(SdEnv_r env, SdValue_r expr);
SdValue_r      SdAst_Return_Expr(SdValue_r self);

SdValue_r      SdAst_Throw_New(SdEnv_r env, SdValue_r expr);
SdValue_r      SdAst_Throw_Expr(SdValue_r self);

SdValue_r      SdAst_IntLit_New(SdEnv_r env, int value);
int            SdAst_IntLit_Value(SdValue_r self);

SdValue_r      SdAst_DoubleLit_New(SdEnv_r env, double value);
double         SdAst_DoubleLit_Value(SdValue_r self);

SdValue_r      SdAst_BoolLit_New(SdEnv_r env, bool value);
bool           SdAst_BoolLit_Value(SdValue_r self);

SdValue_r      SdAst_StringLit_New(SdEnv_r env, SdString* value);
SdString_r     SdAst_StringLit_Value(SdValue_r self);

SdValue_r      SdAst_NilLit_New(SdEnv_r env);

SdValue_r      SdAst_VarRef_New(SdEnv_r env, SdString* identifier);
SdString_r     SdAst_VarRef_Identifier(SdValue_r self);

SdValue_r      SdAst_Query_New(SdEnv_r env, SdValue_r initial_expr, SdList* steps);
SdValue_r      SdAst_Query_InitialExpr(SdValue_r self);
SdList_r       SdAst_Query_Steps(SdValue_r self);

SdValue_r      SdAst_QueryStep_New(SdEnv_r env, SdString* function_name, SdList* arguments, SdValue_r pred_or_null);
SdString_r     SdAst_QueryStep_FunctionName(SdValue_r self);
SdList_r       SdAst_QueryStep_Arguments(SdValue_r self);
SdValue_r      SdAst_QueryStep_Predicate(SdValue_r self); /* may be null */

SdValue_r      SdAst_QueryPred_New(SdEnv_r env, SdString* parameter_name, SdValue_r expr);
SdString_r     SdAst_QueryPred_ParameterName(SdValue_r self);
SdValue_r      SdAst_QueryPred_Expr(SdValue_r self);

/* SdValueSet ********************************************************************************************************/
SdValueSet*    SdValueSet_New(void);
void           SdValueSet_Delete(SdValueSet* self);
bool           SdValueSet_Add(SdValueSet_r self, SdValue_r item); /* true = added, false = already exists */
bool           SdValueSet_Has(SdValueSet_r self, SdValue_r item);

#ifdef _MSC_VER
#pragma warning(pop) /* our disabled warnings won't affect files that #include this header */
#endif

#endif /* _SAD_BASIC_H_ */

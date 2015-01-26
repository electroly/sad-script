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

#ifndef _SAD_SCRIPT_H_
#define _SAD_SCRIPT_H_

#ifdef _MSC_VER
#pragma warning(push, 0) /* ignore warnings in system headers */
#endif

#include <stdlib.h>

/* stdbool.h is not part of C89 but we want to be compatible with client projects targeting later versions of C or 
   C++, so don't clobber bool/true/false with our own definitions. */
typedef int SdBool;
#define SdFalse 0
#define SdTrue 1

#ifdef _MSC_VER
#pragma warning(pop) /* start showing warnings again */
#pragma warning(push)
#pragma warning(disable: 4820) /* '4' bytes padding added after data member '...' */
#endif

/*********************************************************************************************************************/

/* Raw pointer: ownership is being transferred.
   _r suffix: a pointer is being borrowed only; ownership is not transferred. */
typedef struct SdResult_s SdResult;
typedef struct SdSearchResult_s SdSearchResult;
typedef union SdIntDoublePun_u SdIntDoublePun;
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
typedef struct SdChain_s SdChain;
typedef struct SdChain_s* SdChain_r;
typedef struct SdChainNode_s SdChainNode;
typedef struct SdChainNode_s* SdChainNode_r;
typedef struct SdToken_s SdToken;
typedef struct SdToken_s* SdToken_r;
typedef struct SdScanner_s SdScanner;
typedef struct SdScanner_s* SdScanner_r;
typedef struct SdEngine_s SdEngine;
typedef struct SdEngine_s* SdEngine_r;

/* Data Structures ***************************************************************************************************/
typedef enum SdErr_e {
   SdErr_SUCCESS = 0,
   SdErr_INTERPRETER_BUG,
   SdErr_DIED,
   SdErr_NAME_COLLISION,
   SdErr_UNEXPECTED_EOF,
   SdErr_UNEXPECTED_TOKEN,
   SdErr_UNDECLARED_VARIABLE,
   SdErr_TYPE_MISMATCH,
   SdErr_ARGUMENT_MISMATCH,
   SdErr_ARGUMENT_OUT_OF_RANGE,
   SdErr_CANNOT_OPEN_FILE,

   SdErr_FIRST = SdErr_SUCCESS,
   SdErr_LAST = SdErr_CANNOT_OPEN_FILE
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

typedef enum SdTokenType_e {
   SdTokenType_NONE = 0, /* indicates the lack of a token */
   SdTokenType_INT_LIT,
   SdTokenType_DOUBLE_LIT,
   SdTokenType_BOOL_LIT,
   SdTokenType_STRING_LIT,
   SdTokenType_OPEN_PAREN,
   SdTokenType_CLOSE_PAREN,
   SdTokenType_OPEN_BRACKET,
   SdTokenType_CLOSE_BRACKET,
   SdTokenType_OPEN_BRACE,
   SdTokenType_CLOSE_BRACE,
   SdTokenType_COLON,
   SdTokenType_IDENTIFIER,
   SdTokenType_FUNCTION,
   SdTokenType_VAR,
   SdTokenType_SET,
   SdTokenType_IF,
   SdTokenType_ELSE,
   SdTokenType_ELSEIF,
   SdTokenType_FOR,
   SdTokenType_FROM,
   SdTokenType_TO,
   SdTokenType_AT,
   SdTokenType_IN,
   SdTokenType_WHILE,
   SdTokenType_DO,
   SdTokenType_SWITCH,
   SdTokenType_CASE,
   SdTokenType_DEFAULT,
   SdTokenType_RETURN,
   SdTokenType_DIE,
   SdTokenType_IMPORT,
   SdTokenType_NIL,
   SdTokenType_ARROW,
   SdTokenType_QUERY
} SdTokenType;

typedef enum SdNodeType_e {
   /* Environment */
   SdNodeType_ROOT = 0,
   SdNodeType_FRAME = 1,
   SdNodeType_VAR_SLOT = 2,
   SdNodeType_CLOSURE = 3,

   /* Blocks */
   SdNodeType_PROGRAM = 4,
   SdNodeType_BODY = 5,

   /* Block or expression (depending on the context) */
   SdNodeType_FUNCTION = 6,

   /* Expressions */
   SdNodeType_INT_LIT = 7,
   SdNodeType_DOUBLE_LIT = 8,
   SdNodeType_BOOL_LIT = 9,
   SdNodeType_STRING_LIT = 10,
   SdNodeType_NIL_LIT = 11,
   SdNodeType_VAR_REF = 12,
   SdNodeType_QUERY = 13,

   /* Statement or expression (depending on the context) */
   SdNodeType_CALL = 14,

   /* Statements */
   SdNodeType_VAR = 15,
   SdNodeType_SET = 16,
   SdNodeType_IF = 17,
   SdNodeType_ELSEIF = 18,
   SdNodeType_FOR = 19,
   SdNodeType_FOREACH = 20,
   SdNodeType_WHILE = 21,
   SdNodeType_DO = 22,
   SdNodeType_SWITCH = 23,
   SdNodeType_CASE = 24,
   SdNodeType_RETURN = 25,
   SdNodeType_DIE = 26,

   SdNodeType_BLOCKS_FIRST = SdNodeType_PROGRAM,
   SdNodeType_BLOCKS_LAST = SdNodeType_FUNCTION,
   SdNodeType_EXPRESSIONS_FIRST = SdNodeType_FUNCTION,
   SdNodeType_EXPRESSIONS_LAST = SdNodeType_CALL,
   SdNodeType_STATEMENTS_FIRST = SdNodeType_CALL,
   SdNodeType_STATEMENTS_LAST = SdNodeType_DIE
} SdNodeType;

struct SdResult_s {
   SdErr code;
   char message[80];
};

struct SdSearchResult_s {
   int index; /* could be one past the end of the list if search name > everything */
   SdBool exact; /* true = index is an exact match, false = index is the next highest match */
};

typedef int (*SdSearchCompareFunc)(SdValue_r lhs, void* context);

/* SdResult ***********************************************************************************************************/
extern SdResult SdResult_SUCCESS;

SdResult       SdFail(SdErr code, const char* message);
SdResult       SdFailWithStringSuffix(SdErr code, const char* message, SdString_r suffix);
SdBool         SdFailed(SdResult result);

/* Sad ***************************************************************************************************************/
Sad*           Sad_New(void);
void           Sad_Delete(Sad* self);
SdResult       Sad_AddScript(Sad_r self, const char* code);
SdResult       Sad_Execute(Sad_r self);
SdEnv_r        Sad_Env(Sad_r self);

/* SdString **********************************************************************************************************/
SdString*      SdString_New(void);
SdString*      SdString_FromCStr(const char* cstr);
void           SdString_Delete(SdString* self);
const char*    SdString_CStr(SdString_r self);
SdBool         SdString_Equals(SdString_r a, SdString_r b);
SdBool         SdString_EqualsCStr(SdString_r a, const char* b);
size_t         SdString_Length(SdString_r self);
int            SdString_Compare(SdString_r a, SdString_r b);

/* SdStringBuf *******************************************************************************************************/
SdStringBuf*   SdStringBuf_New(void);
void           SdStringBuf_Delete(SdStringBuf* self);
void           SdStringBuf_AppendString(SdStringBuf_r self, SdString_r suffix);
void           SdStringBuf_AppendCStr(SdStringBuf_r self, const char* suffix);
void           SdStringBuf_AppendChar(SdStringBuf_r self, char ch);
void           SdStringBuf_AppendInt(SdStringBuf_r self, int number);
const char*    SdStringBuf_CStr(SdStringBuf_r self);
void           SdStringBuf_Clear(SdStringBuf_r self);
int            SdStringBuf_Length(SdStringBuf_r self);

/* SdValue ***********************************************************************************************************/
SdValue*       SdValue_NewNil(void);
SdValue*       SdValue_NewInt(int x);
SdValue*       SdValue_NewDouble(double x);
SdValue*       SdValue_NewBool(SdBool x);
SdValue*       SdValue_NewString(SdString* x);
SdValue*       SdValue_NewList(SdList* x);
void           SdValue_Delete(SdValue* self);
SdType         SdValue_Type(SdValue_r self);
int            SdValue_GetInt(SdValue_r self);
double         SdValue_GetDouble(SdValue_r self);
SdBool         SdValue_GetBool(SdValue_r self);
SdString_r     SdValue_GetString(SdValue_r self);
SdList_r       SdValue_GetList(SdValue_r self);
SdBool         SdValue_Equals(SdValue_r a, SdValue_r b);
int            SdValue_Hash(SdValue_r self);

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
SdBool         SdList_InsertBySearch(SdList_r list, SdValue_r item, SdSearchCompareFunc compare_func, void* context);
SdBool         SdList_Equals(SdList_r a, SdList_r b);
SdList*        SdList_Clone(SdList_r self);

/* SdFile ************************************************************************************************************/
SdResult       SdFile_WriteAllText(SdString_r file_path, SdString_r text);
SdResult       SdFile_ReadAllText(SdString_r file_path, SdString** out_text);

/* SdEnv *************************************************************************************************************/
/*
   Environment structure:
   Root: 
      (list ROOT (list Function ...) (list Statement ...) bottom:Frame)
                  ^-- sorted by name
   Frame:
      (list FRAME parent:Frame? (list VariableSlot ...))
                                 ^-- sorted by name
   VariableSlot:
      (list VAR_SLOT name:Str payload:Value)

   Closure: (shows up as a payload)
      (list CLOSURE context:Frame (list param-name:String ...) function-node:Function)
*/

SdEnv*         SdEnv_New(void);
void           SdEnv_Delete(SdEnv* self);
SdValue_r      SdEnv_Root(SdEnv_r self);
SdValue_r      SdEnv_AddToGc(SdEnv_r self, SdValue* value);
SdResult       SdEnv_AddProgramAst(SdEnv_r self, SdValue_r program_node);
void           SdEnv_CollectGarbage(SdEnv_r self, SdValue_r extra_in_use[], size_t extra_in_use_count);
SdResult       SdEnv_DeclareVar(SdEnv_r self, SdValue_r frame, SdValue_r name, SdValue_r value);
SdValue_r      SdEnv_FindVariableSlot(SdEnv_r self, SdValue_r frame, SdString_r name, SdBool traverse); /* may be null */
unsigned long  SdEnv_AllocationCount(SdEnv_r self);

SdValue_r      SdEnv_BoxNil(SdEnv_r env);
SdValue_r      SdEnv_BoxInt(SdEnv_r env, int x);
SdValue_r      SdEnv_BoxDouble(SdEnv_r env, double x);
SdValue_r      SdEnv_BoxBool(SdEnv_r env, SdBool x);
SdValue_r      SdEnv_BoxString(SdEnv_r env, SdString* x);
SdValue_r      SdEnv_BoxList(SdEnv_r env, SdList* x);

SdValue_r      SdEnv_Root_New(SdEnv_r env);
SdList_r       SdEnv_Root_Functions(SdValue_r self);
SdList_r       SdEnv_Root_Statements(SdValue_r self);
SdValue_r      SdEnv_Root_BottomFrame(SdValue_r self);

SdValue_r      SdEnv_Frame_New(SdEnv_r env, SdValue_r parent_or_null);
SdValue_r      SdEnv_Frame_Parent(SdValue_r self); /* may be nil */
SdList_r       SdEnv_Frame_VariableSlots(SdValue_r self);

SdValue_r      SdEnv_VariableSlot_New(SdEnv_r env, SdValue_r name, SdValue_r value);
SdString_r     SdEnv_VariableSlot_Name(SdValue_r self);
SdValue_r      SdEnv_VariableSlot_Value(SdValue_r self);
void           SdEnv_VariableSlot_SetValue(SdValue_r self, SdValue_r value);

SdValue_r      SdEnv_Closure_New(SdEnv_r env, SdValue_r frame, SdValue_r param_names, SdValue_r function_node);
SdValue_r      SdEnv_Closure_Frame(SdValue_r self);
SdList_r       SdEnv_Closure_ParameterNames(SdValue_r self);
SdValue_r      SdEnv_Closure_FunctionNode(SdValue_r self);

/* SdAst *************************************************************************************************************/
/*
   AST structure:
   ? - indicates that the value may be nil

               0           1                      2                      3                  4
   Program:                                    
      (list PROGRAM     (list Function ...)    (list Statement ...))
   Function:                                   
      (list FUNCTION    name:Str               (list param:Str ...)   Body               is-imported:Bool)
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
      (list DIE         Expr)
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
      (list QUERY       Expr                   (list Call ...))
      -- may also be FUNCTION
   Body:                                       
      (list BODY        (list Statement ...))  
*/

SdNodeType     SdAst_NodeType(SdValue_r node);

SdValue_r      SdAst_Program_New(SdEnv_r env, SdList* functions, SdList* statements);
SdList_r       SdAst_Program_Functions(SdValue_r self);
SdList_r       SdAst_Program_Statements(SdValue_r self);

SdValue_r      SdAst_Function_New(SdEnv_r env, SdString* function_name, SdList* parameter_names, SdValue_r body, 
                  SdBool is_imported);
SdValue_r      SdAst_Function_Name(SdValue_r self);
SdValue_r      SdAst_Function_Body(SdValue_r self);
SdValue_r      SdAst_Function_ParameterNames(SdValue_r self);
SdBool         SdAst_Function_IsImported(SdValue_r self);

SdValue_r      SdAst_Body_New(SdEnv_r env, SdList* statements);
SdList_r       SdAst_Body_Statements(SdValue_r self);

SdValue_r      SdAst_Call_New(SdEnv_r env, SdString* function_name, SdList* arguments);
SdString_r     SdAst_Call_FunctionName(SdValue_r self);
SdList_r       SdAst_Call_Arguments(SdValue_r self);

SdValue_r      SdAst_Var_New(SdEnv_r env, SdString* variable_name, SdValue_r value_expr);
SdValue_r      SdAst_Var_VariableName(SdValue_r self);
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
SdValue_r      SdAst_For_VariableName(SdValue_r self);
SdValue_r      SdAst_For_StartExpr(SdValue_r self);
SdValue_r      SdAst_For_StopExpr(SdValue_r self);
SdValue_r      SdAst_For_Body(SdValue_r self);

SdValue_r      SdAst_ForEach_New(SdEnv_r env, SdString* iter_name, SdString* index_name_or_null, 
                  SdValue_r haystack_expr, SdValue_r body);
SdValue_r      SdAst_ForEach_IterName(SdValue_r self);
SdValue_r      SdAst_ForEach_IndexName(SdValue_r self); /* may be null */
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

SdValue_r      SdAst_Die_New(SdEnv_r env, SdValue_r expr);
SdValue_r      SdAst_Die_Expr(SdValue_r self);

SdValue_r      SdAst_IntLit_New(SdEnv_r env, int value);
SdValue_r      SdAst_IntLit_Value(SdValue_r self);

SdValue_r      SdAst_DoubleLit_New(SdEnv_r env, double value);
SdValue_r      SdAst_DoubleLit_Value(SdValue_r self);

SdValue_r      SdAst_BoolLit_New(SdEnv_r env, SdBool value);
SdValue_r      SdAst_BoolLit_Value(SdValue_r self);

SdValue_r      SdAst_StringLit_New(SdEnv_r env, SdString* value);
SdValue_r      SdAst_StringLit_Value(SdValue_r self);

SdValue_r      SdAst_NilLit_New(SdEnv_r env);

SdValue_r      SdAst_VarRef_New(SdEnv_r env, SdString* identifier);
SdString_r     SdAst_VarRef_Identifier(SdValue_r self);

SdValue_r      SdAst_Query_New(SdEnv_r env, SdValue_r initial_expr, SdList* steps);
SdValue_r      SdAst_Query_InitialExpr(SdValue_r self);
SdList_r       SdAst_Query_Steps(SdValue_r self);

/* SdValueSet ********************************************************************************************************/
SdValueSet*    SdValueSet_New(void);
void           SdValueSet_Delete(SdValueSet* self);
SdBool         SdValueSet_Add(SdValueSet_r self, SdValue_r item); /* true = added, false = already exists */
SdBool         SdValueSet_Has(SdValueSet_r self, SdValue_r item);

/* SdChain ***********************************************************************************************************/
SdChain*       SdChain_New(void);
void           SdChain_Delete(SdChain* self);
size_t         SdChain_Count(SdChain_r self);
void           SdChain_Push(SdChain_r self, SdValue_r item);
SdValue_r      SdChain_Pop(SdChain_r self); /* may be null if the list is empty */
SdChainNode_r  SdChain_Head(SdChain_r self); /* may be null if the list is empty */
void           SdChain_Remove(SdChain_r self, SdChainNode_r node);

SdValue_r      SdChainNode_Value(SdChainNode_r self);
SdChainNode_r  SdChainNode_Prev(SdChainNode_r self); /* null for the head node */
SdChainNode_r  SdChainNode_Next(SdChainNode_r self); /* null for the tail node */

/* SdToken ***********************************************************************************************************/
SdToken*       SdToken_New(int source_line, SdTokenType type, char* text);
void           SdToken_Delete(SdToken* self);
int            SdToken_SourceLine(SdToken_r self);
SdTokenType    SdToken_Type(SdToken_r self);
const char*    SdToken_Text(SdToken_r self);

/* SdScanner *********************************************************************************************************/
SdScanner*     SdScanner_New(void);
void           SdScanner_Delete(SdScanner* self);
void           SdScanner_Tokenize(SdScanner_r self, const char* text);
SdBool         SdScanner_IsEof(SdScanner_r self);
SdBool         SdScanner_Peek(SdScanner_r self, SdToken_r* out_token); /* true = token was read, false = eof */
SdTokenType    SdScanner_PeekType(SdScanner_r self); /* SdTokenType_NONE if eof */
SdToken_r      SdScanner_PeekToken(SdScanner_r self); /* may be null */
SdBool         SdScanner_Read(SdScanner_r self, SdToken_r* out_token); /* true = token was read, false = eof */

/* SdParser **********************************************************************************************************/
SdResult       SdParser_ParseProgram(SdEnv_r env, const char* text, SdValue_r* out_program_node);

/* SdEngine **********************************************************************************************************/
SdEngine*      SdEngine_New(SdEnv_r env);
void           SdEngine_Delete(SdEngine* self);
SdResult       SdEngine_ExecuteProgram(SdEngine_r self);
SdResult       SdEngine_Call(SdEngine_r self, SdValue_r frame, SdString_r function_name, SdList_r arguments, 
                  SdValue_r* out_return);

/*********************************************************************************************************************/
#ifdef _MSC_VER
#pragma warning(pop) /* our disabled warnings won't affect files that #include this header */
#endif

#endif /* _SAD_SCRIPT_H_ */

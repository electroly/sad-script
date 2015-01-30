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

/* define SD_DEBUG for non-portable Visual C++ debugging (very slow) */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#pragma warning(push, 0) /* ignore warnings in system headers */
#endif

#ifdef SD_DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#define assert(x) do { if (!(x)) { __debugbreak(); } } while(0)
#else
#include <assert.h>
#endif

#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef _MSC_VER
#pragma warning(pop) /* start showing warnings again */
#endif

#include "sad-script.h"

#ifdef _MSC_VER
#pragma warning(disable: 4820) /* '4' bytes padding added after data member '...' */
#pragma warning(disable: 4127) /* conditional expression is constant */
#endif

#define SD_NUM_ALLOCATIONS_PER_GC 10000
   /* run the garbage collector after this many value allocations */

/*********************************************************************************************************************/
typedef struct SdScannerNode_s SdScannerNode;
typedef struct SdScannerNode_s* SdScannerNode_r;

typedef union SdValueUnion_u {
   int int_value;
   SdString* string_value;
   SdBool bool_value;
   double double_value;
   SdList* list_value;
} SdValueUnion;

struct Sad_s {
   SdEnv* env;
   SdEngine* engine;
};

struct SdString_s {
   char* buffer; /* includes null terminator */
   size_t length; /* not including null terminator */
#ifdef SD_DEBUG
   SdBool is_boxed; /* whether this string has been boxed already */
#endif
};

struct SdStringBuf_s {
   char* str;
   int len;
};

struct SdValue_s {
   SdType type;
   SdValueUnion payload;
};

struct SdList_s {
   SdValue_r* values;
   size_t count;
#ifdef SD_DEBUG
   SdBool is_boxed; /* whether this string has been boxed already */
#endif
};

struct SdEnv_s {
   SdValue_r root; /* contains all living/connected objects */
   SdChain* values_chain; /* contains all objects that haven't been deleted yet */
   unsigned long allocation_count;
};

struct SdValueSet_s {
   SdList* list; /* elements are sorted by pointer value */
};

struct SdChain_s {
   SdChainNode* head;
   size_t count;
};

struct SdChainNode_s {
   SdValue_r value;
   SdChainNode_r prev;
   SdChainNode_r next;
};

struct SdToken_s {
   int source_line;
   SdTokenType type;
   char* text;
};

struct SdScannerNode_s {
   SdToken* token;
   SdScannerNode_r next;
};

struct SdScanner_s {
   SdScannerNode* head;
   SdScannerNode* tail;
   SdScannerNode* cursor;
};

struct SdEngine_s {
   SdEnv_r env;
   unsigned long last_gc; /* the value of SdEnv_AllocationCount last time the GC was run */
};

static char* SdStrdup(const char* src);
static int SdMin(int a, int b);
static void SdUnreferenced(void* a);

static SdSearchResult SdEnv_BinarySearchByName(SdList_r list, SdString_r name);
static int SdEnv_BinarySearchByName_CompareFunc(SdValue_r lhs, void* context);
static SdBool SdEnv_InsertByName(SdList_r list, SdValue_r item);
static void SdEnv_CollectGarbage_FindConnectedValues(SdValue_r root, SdValueSet_r connected_values);
static SdValue_r SdEnv_FindVariableSlotInFrame(SdString_r name, SdValue_r frame);

static SdValue_r SdAst_NodeValue(SdValue_r node, size_t value_index);
static SdValue_r SdAst_NewNode(SdEnv_r env, SdValue_r values[], size_t num_values);

static int SdValueSet_CompareFunc(SdValue_r lhs, void* context);

static void SdScanner_AppendToken(SdScanner_r self, int source_line, const char* token_text);
static SdTokenType SdScanner_ClassifyToken(const char* token_text);
static SdBool SdScanner_IsDoubleLit(const char* text);
static SdBool SdScanner_IsIntLit(const char* text);

static SdResult SdParser_Fail(SdErr code, SdToken_r token, const char* message);
static SdResult SdParser_FailEof(void);
static SdResult SdParser_FailType(SdToken_r token, SdTokenType expected_type, SdTokenType actual_type);
static const char* SdParser_TypeString(SdTokenType type);
static SdResult SdParser_ReadExpectType(SdScanner_r scanner, SdTokenType expected_type, SdToken_r* out_token);
static SdResult SdParser_ParseFunction(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseBody(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseExpr(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseQuery(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseQueryStep(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseClosure(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseStatement(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseCall(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseVar(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseSet(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseIf(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseElseIf(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseFor(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseWhile(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseDo(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseSwitch(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseCase(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseReturn(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseDie(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);

static SdResult SdEngine_EvaluateExpr(SdEngine_r self, SdValue_r frame, SdValue_r expr, SdValue_r* out_value);
static SdResult SdEngine_EvaluateVarRef(SdEngine_r self, SdValue_r frame, SdValue_r var_ref, SdValue_r* out_value);
static SdResult SdEngine_EvaluateQuery(SdEngine_r self, SdValue_r frame, SdValue_r query, SdValue_r* out_value);
static SdResult SdEngine_EvaluateFunction(SdEngine_r self, SdValue_r frame, SdValue_r function, SdValue_r* out_value);
static SdResult SdEngine_ExecuteBody(SdEngine_r self, SdValue_r frame, SdValue_r body, SdValue_r* out_return);
static SdResult SdEngine_ExecuteStatement(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteCall(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteVar(SdEngine_r self, SdValue_r frame, SdValue_r statement);
static SdResult SdEngine_ExecuteSet(SdEngine_r self, SdValue_r frame, SdValue_r statement);
static SdResult SdEngine_ExecuteIf(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteFor(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteForEach(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteWhile(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteDo(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteSwitch(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteReturn(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteDie(SdEngine_r self, SdValue_r frame, SdValue_r statement);
static SdResult SdEngine_CallIntrinsic(SdEngine_r self, SdString_r name, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Args1(SdList_r arguments, SdValue_r* out_a, SdType* out_a_type);
static SdResult SdEngine_Args2(SdList_r arguments, SdValue_r* out_a, SdType* out_a_type, SdValue_r* out_b, 
   SdType* out_b_type);
static SdResult SdEngine_Args3(SdList_r arguments, SdValue_r* out_a, SdType* out_a_type, SdValue_r* out_b, 
   SdType* out_b_type, SdValue_r* out_c, SdType* out_c_type);
static SdResult SdEngine_Intrinsic_TypeOf(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Hash(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ToString(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Add(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Subtract(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Multiply(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Divide(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Modulus(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_BitwiseAnd(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_BitwiseOr(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_BitwiseXor(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Sin(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Cos(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Tan(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ASin(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ACos(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ATan(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ATan2(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_SinH(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_CosH(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_TanH(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Exp(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Log(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Log10(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Pow(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Sqrt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Ceil(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Floor(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_And(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Or(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Not(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Equals(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_LessThan(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_LessThanEquals(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_GreaterThan(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_GreaterThanEquals(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ShiftLeft(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ShiftRight(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_List(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ListLength(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ListGetAt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ListSetAt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ListInsertAt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ListRemoveAt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_StringLength(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_StringGetAt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Print(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);

/* Helpers ***********************************************************************************************************/
#ifdef NDEBUG
#define SdAssertValue(x,t) ((void)0)
#define SdAssertNode(x,t) ((void)0)
#define SdAssertExpr(x) ((void)0)
#define SdAssertAllValuesOfType(x,t) ((void)0)
#define SdAssertAllNodesOfTypes(x,l,h) ((void)0)
#define SdAssertAllNodesOfType(x,t) ((void)0)
#define SdAssertNonEmptyString(x) ((void)0)
#else
static void SdAssertValue(SdValue_r x, SdType t) {
   assert(x);
   assert(SdValue_Type(x) == t);
}

static void SdAssertNode(SdValue_r x, SdNodeType t) {
   SdAssertValue(x, SdType_LIST);
   assert(SdAst_NodeType(x) == t);
}

static void SdAssertExpr(SdValue_r x) {
   SdAssertValue(x, SdType_LIST);
   assert(SdAst_NodeType(x) >= SdNodeType_EXPRESSIONS_FIRST && SdAst_NodeType(x) <= SdNodeType_EXPRESSIONS_LAST);
}

static void SdAssertAllValuesOfType(SdList_r x, SdType t) {
   size_t i, count;
   assert(x);
   count = SdList_Count(x);
   for (i = 0; i < count; i++) {
      SdValue_r value;
      value = SdList_GetAt(x, i);
      SdAssertValue(value, t);
   }
}

static void SdAssertAllNodesOfTypes(SdList_r x, SdNodeType l, SdNodeType h) {
   size_t i, count;
   assert(x);
   assert(l <= h);
   count = SdList_Count(x);
   for (i = 0; i < count; i++) {
      SdValue_r value = SdList_GetAt(x, i);
      SdAssertValue(value, SdType_LIST);
      assert(SdAst_NodeType(value) >= l && SdAst_NodeType(value) <= h);
   }
}

static void SdAssertAllNodesOfType(SdList_r x, SdNodeType t) {
   SdAssertAllNodesOfTypes(x, t, t);
}

static void SdAssertNonEmptyString(SdString_r x) {
   assert(x);
   assert(SdString_Length(x) > 0);
}
#endif

#ifdef SD_DEBUG
void* SdDebugAllocCore(void* ptr, size_t size, int line, const char* s, const char* func) {
   memset(ptr, 0, size);
   printf("%x ALLOC -- %s -- Line %d -- %s\n", (unsigned int)ptr, s, line, func);
   return ptr;
}

#define SdDebugAlloc(size, line, s, func) SdDebugAllocCore(malloc(size), size, line, s, func)

void SdDebugFree(void* ptr, int line, const char* s, const char* func) {
   printf("%x FREE -- %s -- Line %d -- %s\n", (unsigned int)ptr, s, line, func);
   free(ptr);
}
#define SdAlloc(size) SdDebugAlloc(size, __LINE__, #size, __FUNCTION__)
#define SdFree(ptr) SdDebugFree(ptr, __LINE__, #ptr, __FUNCTION__)

static void SdDebugDumpValueCore(SdValue_r value, int indent, SdValueSet_r seen) {
   char* indent_str;

   indent_str = malloc(indent + 1);
   memset(indent_str, ' ', indent);
   indent_str[indent] = 0;

   if (SdValueSet_Has(seen, value)) {
      printf("%s(%x) seen already\n", indent_str, (unsigned int)value);
      free(indent_str);
      return;
   } else {
      SdValueSet_Add(seen, value);
   }

   switch (SdValue_Type(value)) {
      case SdType_NIL:
         printf("%s(%x) nil\n", indent_str, (unsigned int)value);
         break;
      case SdType_INT:
         printf("%s(%x) %d\n", indent_str, (unsigned int)value, SdValue_GetInt(value));
         break;
      case SdType_DOUBLE:
         printf("%s(%x) %f\n", indent_str, (unsigned int)value, SdValue_GetDouble(value));
         break;
      case SdType_BOOL:
         printf("%s(%x) %s\n", indent_str, (unsigned int)value, SdValue_GetBool(value) ? "true" : "false");
         break;
      case SdType_STRING: {
         SdString_r str;
         const char* cstr;
         str = SdValue_GetString(value);
         cstr = SdString_CStr(str);
         printf("%s(%x,Str:%x,CStr:%x) \"%s\"\n", indent_str, (unsigned int)value, (unsigned int)str, 
            (unsigned int)cstr, cstr);
         break;
      }
      case SdType_LIST: {
         SdList_r list;
         size_t i, count;
         list = SdValue_GetList(value);
         count = SdList_Count(list);
         printf("%s(%x,Lst:%x) list * %d\n", indent_str, (unsigned int)value, (unsigned int)list, count);
         for (i = 0; i < count; i++)
            SdDebugDumpValueCore(SdList_GetAt(list, i), indent + 2, seen);
         break;
      }
   }

   free(indent_str);
}

static void SdDebugDumpValue(SdValue_r value) {
   SdValueSet* seen = SdValueSet_New();
   printf("DUMP:\n");
   SdDebugDumpValueCore(value, 3, seen);
   SdValueSet_Delete(seen);
}

static void SdDebugDumpChain(SdChain_r chain) {
   SdChainNode_r node = SdChain_Head(chain);
   printf("CHAIN DUMP:\n");
   while (node) {
      printf("   (%x) Type %d\n", (unsigned int)SdChainNode_Value(node), SdValue_Type(SdChainNode_Value(node)));
      node = SdChainNode_Next(node);
   }
}
#else /* SD_DEBUG */
#define SdAlloc(size) calloc(1, (size))
#define SdFree(ptr) free((ptr))
#endif

#define SdRealloc(ptr, size) realloc(ptr, size)

char* SdStrdup(const char* src) {
   size_t length;
   char* dst;

   assert(src);
   length = strlen(src);
   dst = SdAlloc(length + 1);
   memcpy(dst, src, length);
   return dst;
}

int SdMin(int a, int b) {
   if (a < b)
      return a;
   else
      return b;
}

void SdUnreferenced(void* a) { 
   a = NULL; 
}

/* SdResult **********************************************************************************************************/
SdResult SdResult_SUCCESS = { SdErr_SUCCESS, { 0 }};

SdResult SdFail(SdErr code, const char* message) {
   SdResult err;

   assert(code >= SdErr_FIRST && code <= SdErr_LAST);
   assert(message);
   memset(&err, 0, sizeof(err));
   err.code = code;
   strncpy(err.message, message, sizeof(err.message) - 1); 
      /* ok if strncpy doesn't add a null terminator; memset() above zeroed out the string. */
   return err;
}

SdResult SdFailWithStringSuffix(SdErr code, const char* message, SdString_r suffix) {
   SdStringBuf* buf;
   SdResult result;

   assert(code >= SdErr_FIRST && code <= SdErr_LAST);
   assert(message);
   assert(suffix);
   buf = SdStringBuf_New();
   SdStringBuf_AppendCStr(buf, message);
   SdStringBuf_AppendString(buf, suffix);
   result = SdFail(code, SdStringBuf_CStr(buf));
   SdStringBuf_Delete(buf);
   return result;
}


SdBool SdFailed(SdResult result) {
   return result.code != SdErr_SUCCESS;
}

/* Sad ***************************************************************************************************************/
Sad* Sad_New(void) {
   Sad* self = SdAlloc(sizeof(Sad));
   self->env = SdEnv_New();
   self->engine = SdEngine_New(self->env);
   return self;
}

void Sad_Delete(Sad* self) {
   assert(self);
   SdEnv_Delete(self->env);
   SdEngine_Delete(self->engine);
   SdFree(self);
}

SdResult Sad_AddScript(Sad_r self, const char* code) {
   SdValue_r program_node;
   SdResult result;

   assert(self);
   assert(code);
   if (SdFailed(result = SdParser_ParseProgram(self->env, code, &program_node)))
      return result;
   return SdEnv_AddProgramAst(self->env, program_node);
}

SdResult Sad_Execute(Sad_r self) {
   assert(self);
   return SdEngine_ExecuteProgram(self->engine);
}

SdResult Sad_ExecuteScript(Sad_r self, const char* code) {
   SdValue_r program_node;
   SdResult result;

   assert(self);
   assert(code);
   if (SdFailed(result = SdParser_ParseProgram(self->env, code, &program_node)))
      return result;
   if (SdFailed(result = SdEnv_AddProgramAst(self->env, program_node)))
      return result;
   return SdEngine_ExecuteProgram(self->engine);
}

SdEnv_r Sad_Env(Sad_r self) {
   assert(self);
   return self->env;
}

/* SdString **********************************************************************************************************/
SdString* SdString_New(void) {
   SdString* self = SdAlloc(sizeof(SdString));
   self->buffer = SdAlloc(sizeof(char)); /* one char for the null terminator */
   return self;
}

SdString* SdString_FromCStr(const char* cstr) {
   SdString* self;

   assert(cstr);
   self = SdAlloc(sizeof(SdString));
   self->buffer = SdStrdup(cstr);
   self->length = strlen(cstr);
   return self;
}

void SdString_Delete(SdString* self) {
   assert(self);
   SdFree(self->buffer);
   SdFree(self);
}

const char* SdString_CStr(SdString_r self) {
   assert(self);
   return self->buffer;
}

SdBool SdString_Equals(SdString_r a, SdString_r b) {
   assert(a);
   assert(b);
   return a->length == b->length && strcmp(a->buffer, b->buffer) == 0;
}

SdBool SdString_EqualsCStr(SdString_r a, const char* b) {
   assert(a);
   assert(b);
   return strcmp(a->buffer, b) == 0;  
}

size_t SdString_Length(SdString_r self) {
   assert(self);
   return self->length;
}

int SdString_Compare(SdString_r a, SdString_r b) {
   const char* a_str;
   const char* b_str;

   assert(a);
   assert(b);
   a_str = SdString_CStr(a);
   b_str = SdString_CStr(b);
   return strcmp(a_str, b_str);
}

/* SdStringBuf *******************************************************************************************************/
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
   int old_len, suffix_len, new_len;

   assert(self);
   assert(suffix);
   old_len = self->len;
   suffix_len = strlen(suffix);
   new_len = old_len + suffix_len;

   self->str = SdRealloc(self->str, new_len + 1);
   assert(self->str);
   memcpy(&self->str[old_len], suffix, suffix_len);
   self->str[new_len] = 0;
   self->len = new_len;
}

void SdStringBuf_AppendChar(SdStringBuf_r self, char ch) {
   int old_len, new_len;

   assert(self);
   assert(ch != 0);
   old_len = self->len;
   new_len = old_len + 1;

   self->str = SdRealloc(self->str, new_len + 1);
   assert(self->str);
   self->str[old_len] = ch;
   self->str[new_len] = 0;
   self->len = new_len;
}

void SdStringBuf_AppendInt(SdStringBuf_r self, int number) {
   char number_buf[30];

   assert(self);
   memset(number_buf, 0, sizeof(number_buf));
   sprintf(number_buf, "%d", number);
   SdStringBuf_AppendCStr(self, number_buf);
}

const char* SdStringBuf_CStr(SdStringBuf_r self) {
   assert(self);
   return self->str;
}

void SdStringBuf_Clear(SdStringBuf_r self) {
   assert(self);
   self->str[0] = 0;
   self->len = 0;
}

int SdStringBuf_Length(SdStringBuf_r self) {
   assert(self);
   return self->len;
}

/* SdValue ***********************************************************************************************************/
SdValue* SdValue_NewNil(void) {
   return SdAlloc(sizeof(SdValue));
}

SdValue* SdValue_NewInt(int x) {
   SdValue* value = SdAlloc(sizeof(SdValue));
   value->type = SdType_INT;
   value->payload.int_value = x;
   return value;
}

SdValue* SdValue_NewDouble(double x) {
   SdValue* value = SdAlloc(sizeof(SdValue));
   value->type = SdType_DOUBLE;
   value->payload.double_value = x;
   return value;
}

SdValue* SdValue_NewBool(SdBool x) {
   SdValue* value = SdAlloc(sizeof(SdValue));
   value->type = SdType_BOOL;
   value->payload.bool_value = x;
   return value;
}

SdValue* SdValue_NewString(SdString* x) {
   SdValue* value;

   assert(x);
   value = SdAlloc(sizeof(SdValue));
   value->type = SdType_STRING;
   value->payload.string_value = x;

#ifdef SD_DEBUG
   assert(!x->is_boxed);
   x->is_boxed = SdTrue;
#endif

   return value;
}

SdValue* SdValue_NewList(SdList* x) {
   SdValue* value;

   assert(x);
   value = SdAlloc(sizeof(SdValue));
   value->type = SdType_LIST;
   value->payload.list_value = x;

#ifdef SD_DEBUG
   assert(!x->is_boxed);
   x->is_boxed = SdTrue;
#endif

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
      default:
         break; /* nothing to free for these types */
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

SdBool SdValue_GetBool(SdValue_r self) {
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

SdBool SdValue_Equals(SdValue_r a, SdValue_r b) {
   SdType a_type, b_type;
   
   assert(a);
   assert(b);
   a_type = SdValue_Type(a);
   b_type = SdValue_Type(b);
   if (a_type != b_type)
      return SdFalse;

   switch (a_type) {
      case SdType_NIL: return SdTrue;
      case SdType_INT: return SdValue_GetInt(a) == SdValue_GetInt(b);
      case SdType_DOUBLE: return SdValue_GetDouble(a) == SdValue_GetDouble(b);
      case SdType_BOOL: return SdValue_GetBool(a) == SdValue_GetBool(b);
      case SdType_STRING: return SdString_Equals(SdValue_GetString(a), SdValue_GetString(b));
      case SdType_LIST: return SdList_Equals(SdValue_GetList(a), SdValue_GetList(b));
      default: return SdFalse;
   }
}

int SdValue_Hash(SdValue_r self) {
   int hash = 0;

   assert(self);
   switch (SdValue_Type(self)) {
      case SdType_NIL:
         hash = 0;
         break;

      case SdType_INT:
         hash = SdValue_GetInt(self);
         break;

      case SdType_DOUBLE: {
         double number;
         size_t i, count;
         
         number = SdValue_GetDouble(self);
         count = sizeof(double) / sizeof(int);
         for (i = 0; i < count; i++) {
            hash ^= ((int*)&number)[i];
         }
         break;
      }

      case SdType_BOOL:
         hash = SdValue_GetBool(self);
         break;
         
      case SdType_STRING: {
         size_t i, length, count;
         SdString_r str;
         const char* cstr;

         str = SdValue_GetString(self);
         cstr = SdString_CStr(str);
         length = SdString_Length(str);
         count = SdMin(sizeof(int) * 8, length);
         for (i = 0; i < count; i++) {
            char ch = cstr[i];
            hash = (hash << 1) ^ ch;
         }
         hash ^= length;
         break;
      }

      case SdType_LIST: {
         size_t i, length, count;
         SdList_r list;

         list = SdValue_GetList(self);
         length = SdList_Count(list);
         count = SdMin(sizeof(int) * 8, length);
         for (i = 0; i < count; i++) {
            SdValue_r item = SdList_GetAt(list, i);
            hash = (hash << 1) ^ SdValue_Hash(item);
         }
         hash ^= length;
         break;
      }
   }

   return hash;
}

/* SdList ************************************************************************************************************/
SdList* SdList_New(void) {
   return SdAlloc(sizeof(SdList));
}

void SdList_Delete(SdList* self) {
   assert(self);
   SdFree(self->values);
   SdFree(self);
}

void SdList_Append(SdList_r self, SdValue_r item) {
   int new_count;
   
   assert(self);
   assert(item);
   new_count = self->count + 1;
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
   assert(index <= self->count);
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
   size_t i;
   SdValue_r old_value;

   assert(self);
   assert(index < self->count);
   old_value = SdList_GetAt(self, index);
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

SdSearchResult SdList_Search(SdList_r list, SdSearchCompareFunc compare_func, void* context) {
   SdSearchResult result = { 0, SdFalse };
   int first = 0;
   int last = SdList_Count(list) - 1;

   assert(list);
   assert(compare_func);
   assert(context);
   while (first <= last) {
      int pivot = (first + last) / 2;
      int compare = compare_func(SdList_GetAt(list, pivot), context);

      if (compare < 0) { /* pivot_name < search_name */
         first = pivot + 1;

         if (first > last) { /* pivot is the closest non-match less than search_name, but we want the next highest. */
            result.index = pivot + 1;
            result.exact = SdFalse;
            return result;
         }
      } else if (compare > 0) { /* pivot_name > search_name */
         last = pivot - 1;

         if (first > last) { /* pivot is the closest non-match higher than search_name. */
            result.index = pivot;
            result.exact = SdFalse;
            return result;
         }
      } else { /* pivot_name = search_name */
         result.index = pivot;
         result.exact = SdTrue;
         return result;
      }
   }

   /* The list is empty. */
   result.index = 0;
   result.exact = SdFalse;
   return result;
}

SdBool SdList_InsertBySearch(SdList_r list, SdValue_r item, SdSearchCompareFunc compare_func, void* context) {
   SdSearchResult result;
   
   assert(list);
   assert(item);
   assert(compare_func);
   assert(context);
   result = SdList_Search(list, compare_func, context);
   if (result.exact) { /* An item with this name already exists. */
      return SdFalse;
   } else {
      SdList_InsertAt(list, result.index, item);
      return SdTrue;
   }
}

SdBool SdList_Equals(SdList_r a, SdList_r b) {
   size_t i, a_count, b_count;

   assert(a);
   assert(b);
   a_count = SdList_Count(a);
   b_count = SdList_Count(b);
   if (a_count != b_count)
      return SdFalse;

   for (i = 0; i < a_count; i++)
      if (!SdValue_Equals(SdList_GetAt(a, i), SdList_GetAt(b, i)))
         return SdFalse;

   return SdTrue;
}

SdList* SdList_Clone(SdList_r self) {
   SdList* clone = SdList_New();
   clone->count = self->count;
   clone->values = SdAlloc(sizeof(SdValue_r) * self->count);
   memcpy(clone->values, self->values, sizeof(SdValue_r) * self->count);
   return clone;
}

/* SdFile ************************************************************************************************************/
SdResult SdFile_WriteAllText(SdString_r file_path, SdString_r text) {
   SdResult result = SdResult_SUCCESS;
   FILE* fp = NULL;
   size_t count;

   assert(file_path);
   assert(text);
   fp = fopen(SdString_CStr(file_path), "w");
   if (!fp) {
      result = SdFail(SdErr_CANNOT_OPEN_FILE, "Failed to open the file.");
      goto end;
   }

   count = SdString_Length(text);
   if (fwrite(SdString_CStr(text), sizeof(char), count, fp) != count) {
      result = SdFail(SdErr_CANNOT_OPEN_FILE, "Failed to write to the file.");
      goto end;
   }

end:
   if (fp) fclose(fp);
   return result;
}

SdResult SdFile_ReadAllText(SdString_r file_path, SdString** out_text) {
   SdResult result = SdResult_SUCCESS;
   SdStringBuf* buf = NULL;
   FILE* fp = NULL;
   char line[1000];

   assert(file_path);
   assert(out_text);
   fp = fopen(SdString_CStr(file_path), "r");
   if (!fp) {
      result = SdFail(SdErr_CANNOT_OPEN_FILE, "Failed to open the file.");
      goto end;
   }

   buf = SdStringBuf_New();
   while (fgets(line, sizeof(line) / sizeof(char), fp)) {
      SdStringBuf_AppendCStr(buf, line);
   }

   *out_text = SdString_FromCStr(SdStringBuf_CStr(buf));
end:
   if (buf) SdStringBuf_Delete(buf);
   if (fp) fclose(fp);
   return result;
}

/* SdEnv *************************************************************************************************************/
/* list is (list (list <unrelated> name1:str ...) (list <unrelated> name2:str ...) ...) 
The objects are sorted by name.  If an exact match is found, then its index is returned.  Otherwise the next highest 
match is returned.  The index may be one past the end of the list indicating that the search_name is higher than any 
name in the list. */
SdSearchResult SdEnv_BinarySearchByName(SdList_r list, SdString_r search_name) {
   assert(list);
   assert(search_name);
   return SdList_Search(list, SdEnv_BinarySearchByName_CompareFunc, search_name);
}

/* Assume lhs is a list, and compare by the string at index 1 (the name). */
static int SdEnv_BinarySearchByName_CompareFunc(SdValue_r lhs, void* context) {
   SdString_r search_name;
   SdList_r pivot_item;
   SdString_r pivot_name;

   assert(lhs);
   assert(context);
   search_name = context;
   pivot_item = SdValue_GetList(lhs);
   pivot_name = SdValue_GetString(SdList_GetAt(pivot_item, 1));
   return SdString_Compare(pivot_name, search_name);
}

/* list is (list (list <unrelated> name1:str ...) (list <unrelated> name2:str ...) ...)
   The objects are sorted by name. Returns true if the item was inserted, false if the name already exists. */
SdBool SdEnv_InsertByName(SdList_r list, SdValue_r item) {
   SdString_r item_name;
   
   assert(list);
   assert(item);
   item_name = SdValue_GetString(SdList_GetAt(SdValue_GetList(item), 1));
   return SdList_InsertBySearch(list, item, SdEnv_BinarySearchByName_CompareFunc, item_name);
}

#ifdef SD_DEBUG
SdValue_r SdEnv_DebugAddToGc(SdEnv_r self, SdValue* value, int line, const char* func) {
   printf("%x BOX -- Type %d -- Line %d -- %s\n", (unsigned int)value, SdValue_Type(value), line, func);
   assert(self);
   assert(value);

   /* ensure the value is not already in the chain. */
   do {
      SdChainNode_r node = SdChain_Head(self->values_chain);
      while (node) {
         if (SdChainNode_Value(node) == value) {
            __debugbreak(); /* don't attempt to add a value twice! */
         }
         node = SdChainNode_Next(node);
      }
   } while (0);

   SdChain_Push(self->values_chain, value);
   self->allocation_count++;
   return value;
}
#define SdEnv_AddToGc(self, value) SdEnv_DebugAddToGc(self, value, __LINE__, __FUNCTION__);
#else
SdValue_r SdEnv_AddToGc(SdEnv_r self, SdValue* value) {
   assert(self);
   assert(value);

   SdChain_Push(self->values_chain, value);
   self->allocation_count++;
   return value;
}
#endif

SdEnv* SdEnv_New(void) {
   SdEnv* env = SdAlloc(sizeof(SdEnv));
   env->values_chain = SdChain_New();
   env->root = SdEnv_Root_New(env);
   return env;
}

void SdEnv_Delete(SdEnv* self) {
   assert(self);
#ifdef SD_DEBUG
   SdDebugDumpValue(self->root);
   SdDebugDumpChain(self->values_chain);
#endif
   /* Allow the garbage collector to clean up the tree starting at root. */
   self->root = NULL;
   SdEnv_CollectGarbage(self, NULL, 0);
   assert(SdChain_Count(self->values_chain) == 0); /* shouldn't be anything left */
   SdChain_Delete(self->values_chain);
   SdFree(self);
}

SdValue_r SdEnv_Root(SdEnv_r self) {
   assert(self);
   return self->root;
}

SdResult SdEnv_AddProgramAst(SdEnv_r self, SdValue_r program_node) {
   SdResult result;
   SdList_r root_functions, root_statements, new_functions, new_statements;
   size_t new_functions_count, new_statements_count, i;

   assert(self);
   assert(program_node);
   root_functions = SdEnv_Root_Functions(self->root);
   root_statements = SdEnv_Root_Statements(self->root);
   new_functions = SdAst_Program_Functions(program_node);
   new_functions_count = SdList_Count(new_functions);
   new_statements = SdAst_Program_Statements(program_node);
   new_statements_count = SdList_Count(new_statements);

   for (i = 0; i < new_functions_count; i++) {
      SdValue_r new_function = SdList_GetAt(new_functions, i);
      if (!SdEnv_InsertByName(root_functions, new_function)) {
         SdStringBuf* buf = SdStringBuf_New();
         SdStringBuf_AppendCStr(buf, "Duplicate function: ");
         SdStringBuf_AppendString(buf, SdValue_GetString(SdAst_Function_Name(new_function)));
         result = SdFail(SdErr_NAME_COLLISION, SdStringBuf_CStr(buf));
         SdStringBuf_Delete(buf);
         return result;
      }
   }

   for (i = 0; i < new_statements_count; i++) {
      SdValue_r new_statement = SdList_GetAt(new_statements, i);
      SdList_Append(root_statements, new_statement);
   }

   return SdResult_SUCCESS;
}

void SdEnv_CollectGarbage(SdEnv_r self, SdValue_r extra_in_use[], size_t extra_in_use_count) {
   SdValueSet* connected_values;
   SdChainNode_r value_node;
   size_t i;

   assert(self);
   connected_values = SdValueSet_New();
   value_node = SdChain_Head(self->values_chain);
   
   /* mark */
   if (self->root)
      SdEnv_CollectGarbage_FindConnectedValues(self->root, connected_values);

   for (i = 0; i < extra_in_use_count; i++)
      SdValueSet_Add(connected_values, extra_in_use[i]);

   /* sweep */
   while (value_node) {
      SdChainNode_r next_node;
      SdValue_r value;

      next_node = SdChainNode_Next(value_node);
      value = SdChainNode_Value(value_node);
      if (!SdValueSet_Has(connected_values, value)) { 
         /* this value is garbage */
         SdValue_Delete(value);
         SdChain_Remove(self->values_chain, value_node);
      }
      value_node = next_node;
   }

   SdValueSet_Delete(connected_values);
}

void SdEnv_CollectGarbage_FindConnectedValues(SdValue_r root, SdValueSet_r connected_values) {
   SdChain* stack;
   
   assert(root);
   assert(connected_values);
   stack = SdChain_New();
   SdChain_Push(stack, root);

   while (SdChain_Count(stack) > 0) {
      SdValue_r node = SdChain_Pop(stack);
      if (SdValueSet_Add(connected_values, node) && SdValue_Type(node) == SdType_LIST) {
         SdList_r list;
         size_t i, count;

         list = SdValue_GetList(node);
         count = SdList_Count(list);
         for (i = 0; i < count; i++) {
            SdChain_Push(stack, SdList_GetAt(list, i));
         }
      }
   }

   SdChain_Delete(stack);
}

SdResult SdEnv_DeclareVar(SdEnv_r self, SdValue_r frame, SdValue_r name, SdValue_r value) {
   SdValue_r slot;
   SdList_r slots;

   assert(self);
   assert(frame);
   assert(name);
   assert(value);
   slots = SdEnv_Frame_VariableSlots(frame);
   slot = SdEnv_VariableSlot_New(self, name, value);
   if (SdEnv_InsertByName(slots, slot))
      return SdResult_SUCCESS;
   else
      return SdFailWithStringSuffix(SdErr_NAME_COLLISION, "Variable redeclaration: ", SdValue_GetString(name));
}

SdValue_r SdEnv_FindVariableSlot(SdEnv_r self, SdValue_r frame, SdString_r name, SdBool traverse) {
   SdValue_r value;

   assert(self);
   assert(frame);
   assert(name);
   while (SdValue_Type(frame) != SdType_NIL) {
      value = SdEnv_FindVariableSlotInFrame(name, frame);
      if (value)
         return value;

      if (traverse)
         frame = SdEnv_Frame_Parent(frame);
      else
         break;
   }

   return NULL;
}

SdValue_r SdEnv_FindVariableSlotInFrame(SdString_r name, SdValue_r frame) {
   SdList_r slots;
   SdSearchResult search_result;

   assert(name);
   assert(frame);
   slots = SdEnv_Frame_VariableSlots(frame);
   search_result = SdEnv_BinarySearchByName(slots, name);
   if (search_result.exact) {
      SdValue_r slot = SdList_GetAt(slots, search_result.index);
      return slot;
   } else {
      return NULL;
   }
}

unsigned long SdEnv_AllocationCount(SdEnv_r self) {
   assert(self);
   return self->allocation_count;
}

SdValue_r SdEnv_BoxNil(SdEnv_r env) {
   assert(env);
   return SdEnv_AddToGc(env, SdValue_NewNil());
}

SdValue_r SdEnv_BoxInt(SdEnv_r env, int x) {
   assert(env);
   return SdEnv_AddToGc(env, SdValue_NewInt(x));
}

SdValue_r SdEnv_BoxDouble(SdEnv_r env, double x) {
   assert(env);
   return SdEnv_AddToGc(env, SdValue_NewDouble(x));
}

SdValue_r SdEnv_BoxBool(SdEnv_r env, SdBool x) {
   assert(env);
   return SdEnv_AddToGc(env, SdValue_NewBool(x));
}

SdValue_r SdEnv_BoxString(SdEnv_r env, SdString* x) {
   assert(env);
   assert(x);
   return SdEnv_AddToGc(env, SdValue_NewString(x));
}

SdValue_r SdEnv_BoxList(SdEnv_r env, SdList* x) {
   assert(env);
   assert(x);
   return SdEnv_AddToGc(env, SdValue_NewList(x));
}

SdValue_r SdEnv_Root_New(SdEnv_r env) {
   SdValue_r frame;
   SdList* root_list;

   assert(env);
   frame = SdEnv_Frame_New(env, NULL);
   root_list = SdList_New();
   SdList_Append(root_list, SdEnv_BoxInt(env, SdNodeType_ROOT));
   SdList_Append(root_list, SdEnv_BoxList(env, SdList_New())); /* functions */
   SdList_Append(root_list, SdEnv_BoxList(env, SdList_New())); /* statements */
   SdList_Append(root_list, frame); /* bottom frame */
   return SdEnv_BoxList(env, root_list);
}

/* SdAst *************************************************************************************************************/
/* A simple macro-based DSL for implementing the AST node functions. */
#define SdAst_MAX_NODE_VALUES 5
#define SdAst_BEGIN(node_type) \
   SdValue_r values[SdAst_MAX_NODE_VALUES]; \
   int i = 0; \
   values[i++] = SdEnv_BoxInt(env, node_type);
#define SdAst_VALUE(x) \
   values[i++] = x;
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
   SdAst_UNBOXED_GETTER(function_name, node_type, SdBool, SdValue_GetBool, index)
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

SdValue_r SdAst_NewNode(SdEnv_r env, SdValue_r values[], size_t num_values) {
   SdList* node;
   size_t i;

   assert(env);
   assert(values);
   node = SdList_New();
   for (i = 0; i < num_values; i++) {
      assert(values[i]);
      SdList_Append(node, values[i]);
   }
   return SdEnv_BoxList(env, node);
}

SdValue_r SdAst_Program_New(SdEnv_r env, SdList* functions, SdList* statements) {
   SdAst_BEGIN(SdNodeType_PROGRAM)
   
   assert(env);
   SdAssertAllNodesOfType(functions, SdNodeType_FUNCTION);
   SdAssertAllNodesOfTypes(statements, SdNodeType_STATEMENTS_FIRST, SdNodeType_STATEMENTS_LAST);
   
   SdAst_LIST(functions)
   SdAst_LIST(statements)
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_Program_Functions, SdNodeType_PROGRAM, 1)
SdAst_LIST_GETTER(SdAst_Program_Statements, SdNodeType_PROGRAM, 2)

SdValue_r SdAst_Function_New(SdEnv_r env, SdString* function_name, SdList* parameter_names, SdValue_r body, 
   SdBool is_imported) {
   SdAst_BEGIN(SdNodeType_FUNCTION)

   assert(env);
   SdAssertNonEmptyString(function_name);
   SdAssertAllValuesOfType(parameter_names, SdType_STRING);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_STRING(function_name)
   SdAst_LIST(parameter_names)
   SdAst_VALUE(body)
   SdAst_BOOL(is_imported)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Function_Name, SdNodeType_FUNCTION, 1)
SdAst_VALUE_GETTER(SdAst_Function_ParameterNames, SdNodeType_FUNCTION, 2)
SdAst_VALUE_GETTER(SdAst_Function_Body, SdNodeType_FUNCTION, 3)
SdAst_BOOL_GETTER(SdAst_Function_IsImported, SdNodeType_FUNCTION, 4)

SdValue_r SdAst_Body_New(SdEnv_r env, SdList* statements) {
   SdAst_BEGIN(SdNodeType_BODY)

   assert(env);
   SdAssertAllNodesOfTypes(statements, SdNodeType_STATEMENTS_FIRST, SdNodeType_STATEMENTS_LAST);

   SdAst_LIST(statements);
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_Body_Statements, SdNodeType_BODY, 1)

SdValue_r SdAst_Call_New(SdEnv_r env, SdString* function_name, SdList* arguments) {
   SdAst_BEGIN(SdNodeType_CALL)

   assert(env);
   SdAssertNonEmptyString(function_name);
   assert(arguments);

   SdAst_STRING(function_name)
   SdAst_LIST(arguments)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_Call_FunctionName, SdNodeType_CALL, 1)
SdAst_LIST_GETTER(SdAst_Call_Arguments, SdNodeType_CALL, 2)

SdValue_r SdAst_Var_New(SdEnv_r env, SdString* variable_name, SdValue_r value_expr) {
   SdAst_BEGIN(SdNodeType_VAR)

   assert(env);
   SdAssertNonEmptyString(variable_name);
   SdAssertExpr(value_expr);

   SdAst_STRING(variable_name)
   SdAst_VALUE(value_expr)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Var_VariableName, SdNodeType_VAR, 1)
SdAst_VALUE_GETTER(SdAst_Var_ValueExpr, SdNodeType_VAR, 2)

SdValue_r SdAst_Set_New(SdEnv_r env, SdString* variable_name, SdValue_r value_expr) {
   SdAst_BEGIN(SdNodeType_SET)

   assert(env);
   SdAssertNonEmptyString(variable_name);
   SdAssertExpr(value_expr);

   SdAst_STRING(variable_name)
   SdAst_VALUE(value_expr)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_Set_VariableName, SdNodeType_SET, 1)
SdAst_VALUE_GETTER(SdAst_Set_ValueExpr, SdNodeType_SET, 2)

SdValue_r SdAst_If_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r true_body, SdList* else_ifs,
   SdValue_r else_body) {
   SdAst_BEGIN(SdNodeType_IF)

   assert(env);
   SdAssertExpr(condition_expr);
   SdAssertNode(true_body, SdNodeType_BODY);
   SdAssertAllNodesOfType(else_ifs, SdNodeType_ELSEIF);
   SdAssertNode(else_body, SdNodeType_BODY);

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
   
   assert(env);
   SdAssertExpr(condition_expr);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_VALUE(condition_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_ElseIf_ConditionExpr, SdNodeType_IF, 1)
SdAst_VALUE_GETTER(SdAst_ElseIf_Body, SdNodeType_IF, 2)

SdValue_r SdAst_For_New(SdEnv_r env, SdString* variable_name, SdValue_r start_expr, SdValue_r stop_expr,
   SdValue_r body) {
   SdAst_BEGIN(SdNodeType_FOR)
   
   assert(env);
   SdAssertNonEmptyString(variable_name);
   SdAssertExpr(start_expr);
   SdAssertExpr(stop_expr);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_STRING(variable_name)
   SdAst_VALUE(start_expr)
   SdAst_VALUE(stop_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_For_VariableName, SdNodeType_FOR, 1)
SdAst_VALUE_GETTER(SdAst_For_StartExpr, SdNodeType_FOR, 2)
SdAst_VALUE_GETTER(SdAst_For_StopExpr, SdNodeType_FOR, 3)
SdAst_VALUE_GETTER(SdAst_For_Body, SdNodeType_FOR, 4)

SdValue_r SdAst_ForEach_New(SdEnv_r env, SdString* iter_name, SdString* index_name_or_null, SdValue_r haystack_expr,
   SdValue_r body) {
   SdAst_BEGIN(SdNodeType_FOREACH)
   
   assert(env);
   SdAssertNonEmptyString(iter_name);
   SdAssertExpr(haystack_expr);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_STRING(iter_name)
   if (index_name_or_null) {
      SdAssertNonEmptyString(index_name_or_null);
      SdAst_STRING(index_name_or_null);
   } else {
      SdAst_NIL()
   }
   SdAst_VALUE(haystack_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_ForEach_IterName, SdNodeType_FOREACH, 1)
SdAst_VALUE_GETTER(SdAst_ForEach_IndexName, SdNodeType_FOREACH, 2)
SdAst_VALUE_GETTER(SdAst_ForEach_HaystackExpr, SdNodeType_FOREACH, 3)
SdAst_VALUE_GETTER(SdAst_ForEach_Body, SdNodeType_FOREACH, 4)

SdValue_r SdAst_While_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r body) {
   SdAst_BEGIN(SdNodeType_WHILE)

   assert(env);
   SdAssertExpr(condition_expr);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_VALUE(condition_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_While_ConditionExpr, SdNodeType_WHILE, 1)
SdAst_VALUE_GETTER(SdAst_While_Body, SdNodeType_WHILE, 2)

SdValue_r SdAst_Do_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r body) {
   SdAst_BEGIN(SdNodeType_DO)
   
   
   assert(env);
   SdAssertExpr(condition_expr);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_VALUE(condition_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Do_ConditionExpr, SdNodeType_DO, 1)
SdAst_VALUE_GETTER(SdAst_Do_Body, SdNodeType_DO, 2)

SdValue_r SdAst_Switch_New(SdEnv_r env, SdValue_r expr, SdList* cases, SdValue_r default_body) {
   SdAst_BEGIN(SdNodeType_SWITCH)
   
   assert(env);
   SdAssertExpr(expr);
   SdAssertAllNodesOfType(cases, SdNodeType_CASE);
   SdAssertNode(default_body, SdNodeType_BODY);
   
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

   assert(env);
   SdAssertExpr(expr);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_VALUE(expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Case_Expr, SdNodeType_CASE, 1)
SdAst_VALUE_GETTER(SdAst_Case_Body, SdNodeType_CASE, 2)

SdValue_r SdAst_Return_New(SdEnv_r env, SdValue_r expr) {
   SdAst_BEGIN(SdNodeType_RETURN)

   assert(env);
   SdAssertExpr(expr);

   SdAst_VALUE(expr)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Return_Expr, SdNodeType_RETURN, 1)

SdValue_r SdAst_Die_New(SdEnv_r env, SdValue_r expr) {
   SdAst_BEGIN(SdNodeType_DIE)

   assert(env);
   SdAssertExpr(expr);

   SdAst_VALUE(expr)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Die_Expr, SdNodeType_DIE, 1)

SdValue_r SdAst_IntLit_New(SdEnv_r env, int value) {
   SdAst_BEGIN(SdNodeType_INT_LIT)

   assert(env);

   SdAst_INT(value)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_IntLit_Value, SdNodeType_INT_LIT, 1)

SdValue_r SdAst_DoubleLit_New(SdEnv_r env, double value) {
   SdAst_BEGIN(SdNodeType_DOUBLE_LIT)

   assert(env);

   SdAst_DOUBLE(value)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_DoubleLit_Value, SdNodeType_DOUBLE_LIT, 1)

SdValue_r SdAst_BoolLit_New(SdEnv_r env, SdBool value) {
   SdAst_BEGIN(SdNodeType_BOOL_LIT)

   assert(env);

   SdAst_BOOL(value)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_BoolLit_Value, SdNodeType_BOOL_LIT, 1)

SdValue_r SdAst_StringLit_New(SdEnv_r env, SdString* value) {
   SdAst_BEGIN(SdNodeType_STRING_LIT)

   assert(env);
   assert(value);

   SdAst_STRING(value)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_StringLit_Value, SdNodeType_STRING_LIT, 1)

SdValue_r SdAst_NilLit_New(SdEnv_r env) {
   SdAst_BEGIN(SdNodeType_NIL_LIT)

   assert(env);

   SdAst_END
}

SdValue_r SdAst_VarRef_New(SdEnv_r env, SdString* identifier) {
   SdAst_BEGIN(SdNodeType_VAR_REF)

   assert(env);
   SdAssertNonEmptyString(identifier);

   SdAst_STRING(identifier)
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_VarRef_Identifier, SdNodeType_VAR_REF, 1)

SdValue_r SdAst_Query_New(SdEnv_r env, SdValue_r initial_expr, SdList* steps) {
   SdAst_BEGIN(SdNodeType_QUERY)

   assert(env);
   SdAssertExpr(initial_expr);
   SdAssertAllNodesOfType(steps, SdNodeType_CALL);

   SdAst_VALUE(initial_expr)
   SdAst_LIST(steps)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Query_InitialExpr, SdNodeType_QUERY, 1)
SdAst_LIST_GETTER(SdAst_Query_Steps, SdNodeType_QUERY, 2)

/* These are SdEnv nodes "above" the AST, but it's convenient to use the same macros to implement them. */
SdAst_LIST_GETTER(SdEnv_Root_Functions, SdNodeType_ROOT, 1)
SdAst_LIST_GETTER(SdEnv_Root_Statements, SdNodeType_ROOT, 2)
SdAst_VALUE_GETTER(SdEnv_Root_BottomFrame, SdNodeType_ROOT, 3)

SdValue_r SdEnv_Frame_New(SdEnv_r env, SdValue_r parent_or_null) {
   SdAst_BEGIN(SdNodeType_FRAME)

   assert(env);

   if (parent_or_null) {
      SdAssertNode(parent_or_null, SdNodeType_FRAME);
      SdAst_VALUE(parent_or_null)
   } else {
      SdAst_NIL()
   }
   SdAst_LIST(SdList_New()) /* variable slots list */
   SdAst_END
}
SdAst_VALUE_GETTER(SdEnv_Frame_Parent, SdNodeType_FRAME, 1)
SdAst_LIST_GETTER(SdEnv_Frame_VariableSlots, SdNodeType_FRAME, 2)

SdValue_r SdEnv_VariableSlot_New(SdEnv_r env, SdValue_r name, SdValue_r value) {
   SdAst_BEGIN(SdNodeType_VAR_SLOT)

   assert(env);
   SdAssertValue(name, SdType_STRING);
   SdAssertNonEmptyString(SdValue_GetString(name));
   assert(value);

   SdAst_VALUE(name)
   SdAst_VALUE(value)
   SdAst_END
}
SdAst_STRING_GETTER(SdEnv_VariableSlot_Name, SdNodeType_VAR_SLOT, 1)
SdAst_VALUE_GETTER(SdEnv_VariableSlot_Value, SdNodeType_VAR_SLOT, 2)

void SdEnv_VariableSlot_SetValue(SdValue_r self, SdValue_r value) {
   SdAssertNode(self, SdNodeType_VAR_SLOT);
   assert(value);

   SdList_SetAt(SdValue_GetList(self), 2, value);
}

SdValue_r SdEnv_Closure_New(SdEnv_r env, SdValue_r frame, SdValue_r param_names, SdValue_r function_node) {
   SdAst_BEGIN(SdNodeType_CLOSURE)

   assert(env);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssertAllValuesOfType(SdValue_GetList(param_names), SdType_STRING);
   SdAssertNode(function_node, SdNodeType_FUNCTION);

   SdAst_VALUE(frame)
   SdAst_VALUE(param_names)
   SdAst_VALUE(function_node)
   SdAst_END
}
SdAst_VALUE_GETTER(SdEnv_Closure_Frame, SdNodeType_CLOSURE, 1)
SdAst_LIST_GETTER(SdEnv_Closure_ParameterNames, SdNodeType_CLOSURE, 2)
SdAst_VALUE_GETTER(SdEnv_Closure_FunctionNode, SdNodeType_CLOSURE, 3)

/* SdValueSet ********************************************************************************************************/
SdValueSet* SdValueSet_New(void) {
   SdValueSet* self = SdAlloc(sizeof(SdValueSet));
   self->list = SdList_New();
   return self;
}

void SdValueSet_Delete(SdValueSet* self) {
   assert(self);
   SdList_Delete(self->list);
   SdFree(self);
}

int SdValueSet_CompareFunc(SdValue_r lhs, void* context) { /* compare the pointer values */
   SdValue_r rhs;

   assert(lhs);
   assert(context);
   rhs = context;
   if (lhs < rhs) {
      return -1;
   } else if (lhs > rhs) {
      return 1;
   } else {
      return 0;
   }
}

SdBool SdValueSet_Add(SdValueSet_r self, SdValue_r item) { /* true = added, false = already exists */
   assert(self);
   assert(item);
   return SdList_InsertBySearch(self->list, item, SdValueSet_CompareFunc, item);
}

SdBool SdValueSet_Has(SdValueSet_r self, SdValue_r item) {
   SdSearchResult result;

   assert(self);
   assert(item);
   result = SdList_Search(self->list, SdValueSet_CompareFunc, item);
   return result.exact;
}

/* SdChain ***********************************************************************************************************/
SdChain* SdChain_New(void) {
   return SdAlloc(sizeof(SdChain));
}

void SdChain_Delete(SdChain* self) {
   SdChainNode* node;

   assert(self);
   node = self->head;
   while (node) {
      SdChainNode* next = node->next;
      SdFree(node);
      node = next;
   }

   SdFree(self);
}

size_t SdChain_Count(SdChain_r self) {
   assert(self);
   return self->count;
}

void SdChain_Push(SdChain_r self, SdValue_r item) {
   SdChainNode* node;

   assert(self);
   assert(item);

   node = SdAlloc(sizeof(SdChainNode));
   node->value = item;

   if (self->head) {
      self->head->prev = node;
      node->next = self->head;
   }
   self->head = node;
   self->count++;
}

SdValue_r SdChain_Pop(SdChain_r self) { /* may be null if the list is empty */
   assert(self);

   if (self->head) {
      SdChainNode* next;
      SdValue_r value;

      value = self->head->value;
      next = self->head->next;
      if (next) {
         next->prev = NULL;
      }
      SdFree(self->head);
      self->head = next;
      self->count--;
      return value;
   } else {
      return NULL;
   }
}

SdChainNode_r SdChain_Head(SdChain_r self) { /* may be null if the list is empty */
   assert(self);
   return self->head;
}

void SdChain_Remove(SdChain_r self, SdChainNode_r node) {
   assert(self);
   assert(node);

   if (node->prev) {
      node->prev->next = node->next;
   } else { /* this is the head node */
      self->head = node->next;
   }

   if (node->next) {
      node->next->prev = node->prev;
   }

   SdFree(node);
   self->count--;
}

SdValue_r SdChainNode_Value(SdChainNode_r self) {
   assert(self);
   return self->value;
}

SdChainNode_r SdChainNode_Prev(SdChainNode_r self) { /* null for the head node */
   assert(self);
   return self->prev;
}

SdChainNode_r SdChainNode_Next(SdChainNode_r self) { /* null for the tail node */
   assert(self);
   return self->next;
}

/* SdToken ***********************************************************************************************************/
SdToken* SdToken_New(int source_line, SdTokenType type, char* text) {
   SdToken* self;

   assert(text);
   self = SdAlloc(sizeof(SdToken));
   self->source_line = source_line;
   self->type = type;
   self->text = text;
   return self;
}

void SdToken_Delete(SdToken* self) {
   assert(self);
   SdFree(self->text);
   SdFree(self);
}

int SdToken_SourceLine(SdToken_r self) {
   assert(self);
   return self->source_line;
}

SdTokenType SdToken_Type(SdToken_r self) {
   assert(self);
   return self->type;
}

const char* SdToken_Text(SdToken_r self) {
   assert(self);
   return self->text;
}

/* SdScanner *********************************************************************************************************/
SdScanner* SdScanner_New(void) {
   return SdAlloc(sizeof(SdScanner));
}

void SdScanner_Delete(SdScanner* self) {
   SdScannerNode* node;

   assert(self);
   node = self->head;
   while (node) {
      SdScannerNode* next = node->next;
      SdToken_Delete(node->token);
      SdFree(node);
      node = next;
   }
   SdFree(self);
}

void SdScanner_Tokenize(SdScanner_r self, const char* text) {
   SdBool in_string = SdFalse, in_escape = SdFalse, in_comment = SdFalse;
   size_t i, text_length;
   int source_line = 1;
   SdStringBuf* current_text;

   assert(self);
   assert(text);
   current_text = SdStringBuf_New();
   text_length = strlen(text);
   for (i = 0; i < text_length; i++) {
      char ch, peek;
      ch = text[i];
      peek = text[i + 1];

      if (ch == '\n')
         in_comment = SdFalse;

      if (in_comment)
         continue;

      if (in_string) {
         if (in_escape) {
            char real_ch;

            switch (ch) {
               case 'n': real_ch = '\n'; break;
               case 'r': real_ch = '\r'; break;
               case 't': real_ch = '\t'; break;
               case '\\': real_ch = '\\'; break;
               case '"': real_ch = '"'; break;
               default: real_ch = ch; break;
            }

            in_escape = SdFalse;
            SdStringBuf_AppendChar(current_text, real_ch);
            continue;
         } else if (ch == '\\') {
            in_escape = SdTrue;
            continue;
         } else if (ch == '"') {
            in_string = SdFalse;
            SdStringBuf_AppendChar(current_text, ch);
            continue;
         } else {
            SdStringBuf_AppendChar(current_text, ch);
            continue;
         }
      } else if (ch == '"') {
         if (SdStringBuf_Length(current_text) > 0) {
            SdScanner_AppendToken(self, source_line, SdStringBuf_CStr(current_text));
            SdStringBuf_Clear(current_text);
         }

         in_string = SdTrue;
         SdStringBuf_AppendChar(current_text, ch);
         continue;
      }

      /* These characters are never part of other tokens; they terminate the previous token. */
      switch (ch) {
         case ' ':
         case '\t':
         case '\n':
         case '\r':
         case '(':
         case ')':
         case '[':
         case ']':
         case '{':
         case '}':
         case ':':
            if (SdStringBuf_Length(current_text) > 0) {
               SdScanner_AppendToken(self, source_line, SdStringBuf_CStr(current_text));
               SdStringBuf_Clear(current_text);
            }
            break;
      }

      if (ch == '/' && peek == '/') {
         if (SdStringBuf_Length(current_text) > 0) {
            SdScanner_AppendToken(self, source_line, SdStringBuf_CStr(current_text));
            SdStringBuf_Clear(current_text);
         }
         in_comment = SdTrue;
         continue;
      }
      
      switch (ch) {
         /* These characters (a subset of the characters above) are their own tokens, even when jammed together with
            other stuff. */
         case '(':
         case ')':
         case '{':
         case '}':
         case '[':
         case ']':
         case ':': {
            char token_text[2];
            token_text[0] = ch;
            token_text[1] = 0;
            SdScanner_AppendToken(self, source_line, token_text);
            break;
         }

         /* Whitespace is ignored. */
         case ' ':
         case '\t':
         case '\r':
         case '\n':
            break;

         /* Anything else other than whitespace is part of this token. */
         default:
            SdStringBuf_AppendChar(current_text, ch);
            break;
      }

      if (ch == '\n')
         source_line++;
   }

   /* Source may end at the end of a token rather than with a separator. */
   if (SdStringBuf_Length(current_text) > 0) {
      SdScanner_AppendToken(self, source_line, SdStringBuf_CStr(current_text));
      SdStringBuf_Clear(current_text);
   }

   /* Set the cursor to the first token so we're ready to start reading them */
   self->cursor = self->head;
   SdStringBuf_Delete(current_text);
}

void SdScanner_AppendToken(SdScanner_r self, int source_line, const char* token_text) {
   SdToken* token;
   SdScannerNode* new_node;
   
   assert(self);
   assert(token_text);
   token = SdToken_New(source_line, SdScanner_ClassifyToken(token_text), SdStrdup(token_text));

   new_node = SdAlloc(sizeof(SdScannerNode));
   new_node->token = token;
   new_node->next = NULL;

   if (self->tail) {
      self->tail->next = new_node;
      self->tail = new_node;
   } else { /* list is empty */
      assert(!self->head);
      self->head = new_node;
      self->tail = new_node;
   }
}

SdTokenType SdScanner_ClassifyToken(const char* text) {
   assert(text);
   assert(strlen(text) > 0);
   switch (text[0]) {
      /* These characters are always tokens on their own, so we don't have to check the whole string. */
      case '"': return SdTokenType_STRING_LIT;
      case '(': return SdTokenType_OPEN_PAREN;
      case ')': return SdTokenType_CLOSE_PAREN;
      case '[': return SdTokenType_OPEN_BRACKET;
      case ']': return SdTokenType_CLOSE_BRACKET;
      case '{': return SdTokenType_OPEN_BRACE;
      case '}': return SdTokenType_CLOSE_BRACE;
      case ':': return SdTokenType_COLON;

      /* For the rest, we use the first character to speed up the check, but we have to compare the whole string. */
      case 'a':
         if (strcmp(text, "at") == 0) return SdTokenType_AT;
         break;

      case 'c':
         if (strcmp(text, "case") == 0) return SdTokenType_CASE;
         break;

      case 'd':
         if (strcmp(text, "default") == 0) return SdTokenType_DEFAULT;
         if (strcmp(text, "die") == 0) return SdTokenType_DIE;
         if (strcmp(text, "do") == 0) return SdTokenType_DO;
         break;

      case 'e':
         if (strcmp(text, "else") == 0) return SdTokenType_ELSE;
         if (strcmp(text, "elseif") == 0) return SdTokenType_ELSEIF;
         break;

      case 'f':
         if (strcmp(text, "false") == 0) return SdTokenType_BOOL_LIT;
         if (strcmp(text, "for") == 0) return SdTokenType_FOR;
         if (strcmp(text, "from") == 0) return SdTokenType_FROM;
         if (strcmp(text, "function") == 0) return SdTokenType_FUNCTION;
         break;

      case 'i':
         if (strcmp(text, "if") == 0) return SdTokenType_IF;
         if (strcmp(text, "import") == 0) return SdTokenType_IMPORT;
         if (strcmp(text, "in") == 0) return SdTokenType_IN;
         break;

      case 'n':
         if (strcmp(text, "nil") == 0) return SdTokenType_NIL;
         break;

      case 'q':
         if (strcmp(text, "query") == 0) return SdTokenType_QUERY;
         break;

      case 'r':
         if (strcmp(text, "return") == 0) return SdTokenType_RETURN;
         break;

      case 's':
         if (strcmp(text, "set") == 0) return SdTokenType_SET;
         if (strcmp(text, "switch") == 0) return SdTokenType_SWITCH;
         break;

      case 't':
         if (strcmp(text, "to") == 0) return SdTokenType_TO;
         if (strcmp(text, "true") == 0) return SdTokenType_BOOL_LIT;
         break;

      case 'v':
         if (strcmp(text, "var") == 0) return SdTokenType_VAR;
         break;

      case 'w':
         if (strcmp(text, "while") == 0) return SdTokenType_WHILE;
         break;

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '-':
      case '.':
         if (strcmp(text, "->") == 0) return SdTokenType_ARROW;
         if (SdScanner_IsIntLit(text)) return SdTokenType_INT_LIT;
         if (SdScanner_IsDoubleLit(text)) return SdTokenType_DOUBLE_LIT;
         break;
   }

   return SdTokenType_IDENTIFIER; /* anything goes */
}

SdBool SdScanner_IsIntLit(const char* text) {
   size_t i, length;
   int digits = 0;

   assert(text);
   length = strlen(text);
   assert(length > 0);
   for (i = 0; i < length; i++) {
      if (text[i] == '-' && i == 0) {
         /* negative sign okay in the first character position */
      } else if (text[i] >= '0' && text[i] <= '9') {
         digits++;
      } else {
         return SdFalse;
      }
   }
   return digits > 0;
}

SdBool SdScanner_IsDoubleLit(const char* text) {
   size_t i, length;
   SdBool dot = SdFalse;
   int digits = 0;

   assert(text);
   length = strlen(text);
   assert(length > 0);
   for (i = 0; i < length; i++) {
      if (text[i] == '-' && i == 0) {
         /* negative sign okay in the first character position */
      } else if (text[i] == '.' && !dot) {
         /* one dot required somewhere */
         dot = SdTrue;
      } else if (text[i] >= '0' && text[i] <= '9') {
         digits++;
      } else {
         return SdFalse;
      }
   }
   return digits > 0 && dot;
}

SdBool SdScanner_IsEof(SdScanner_r self) {
   assert(self);
   return !self->cursor;
}

SdBool SdScanner_Peek(SdScanner_r self, SdToken_r* out_token) { /* true = token was read, false = eof */
   assert(self);
   assert(out_token);
   if (self->cursor) {
      *out_token = self->cursor->token;
      return SdTrue;
   } else {
      return SdFalse;
   }
}

SdTokenType SdScanner_PeekType(SdScanner_r self) { /* SdTokenType_NONE if eof */
   SdToken_r token;
   if (!SdScanner_Peek(self, &token)) {
      return SdTokenType_NONE;
   } else {
      return SdToken_Type(token);
   }
}

SdToken_r SdScanner_PeekToken(SdScanner_r self) {
   SdToken_r token;
   if (!SdScanner_Peek(self, &token)) {
      return NULL;
   } else {
      return token;
   }
}

SdBool SdScanner_Read(SdScanner_r self, SdToken_r* out_token) { /* true = token was read, false = eof */
   assert(self);
   assert(out_token);
   if (self->cursor) {
      *out_token = self->cursor->token;
      self->cursor = self->cursor->next;
      return SdTrue;
   } else {
      return SdFalse;
   }
}

/* SdParser **********************************************************************************************************/
#define SdParser_READ() \
   do { \
      if (!SdScanner_Read(scanner, &token)) { \
         result = SdParser_FailEof(); \
         goto end; \
      } \
   } while (0)
#define SdParser_EXPECT_TYPE(expected_type) \
   do { \
      if (SdToken_Type(token) != expected_type) { \
         result = SdParser_FailType(token, expected_type, SdToken_Type(token)); \
         goto end; \
      } \
   } while (0)
#define SdParser_READ_EXPECT_TYPE(expected_type) \
   do { \
      if (SdFailed(result = SdParser_ReadExpectType(scanner, expected_type, &token))) { \
         goto end; \
      } \
   } while (0)
#define SdParser_READ_IDENTIFIER(identifier) \
   do { \
      SdParser_READ_EXPECT_TYPE(SdTokenType_IDENTIFIER); \
      identifier = SdString_FromCStr(SdToken_Text(token)); \
   } while (0)
#define SdParser_CALL(x) \
   do { \
      if (SdFailed(result = (x))) { \
         goto end; \
      } \
   } while (0)
#define SdParser_READ_EXPR(expr) \
   SdParser_CALL(SdParser_ParseExpr(env, scanner, &expr))
#define SdParser_READ_BODY(body) \
   SdParser_CALL(SdParser_ParseBody(env, scanner, &body))

SdResult SdParser_Fail(SdErr code, SdToken_r token, const char* message) {
   SdStringBuf* buf;
   SdResult result;
   
   assert(message);
   buf = SdStringBuf_New();
   if (token) {
      SdStringBuf_AppendCStr(buf, "Line: ");
      SdStringBuf_AppendInt(buf, SdToken_SourceLine(token));
      SdStringBuf_AppendCStr(buf, "\nToken: ");
      SdStringBuf_AppendCStr(buf, SdToken_Text(token));
      SdStringBuf_AppendCStr(buf, "\n");
   }
   SdStringBuf_AppendCStr(buf, message);
   result = SdFail(code, SdStringBuf_CStr(buf));
   SdStringBuf_Delete(buf);
   return result;
}

SdResult SdParser_FailEof(void) {
   return SdFail(SdErr_UNEXPECTED_EOF, "Unexpected EOF.");
}

SdResult SdParser_FailType(SdToken_r token, SdTokenType expected_type, SdTokenType actual_type) {
   SdStringBuf* buf;
   SdResult result;

   assert(token);
   buf = SdStringBuf_New();
   SdStringBuf_AppendCStr(buf, "Expected: ");
   SdStringBuf_AppendCStr(buf, SdParser_TypeString(expected_type));
   SdStringBuf_AppendCStr(buf, ", Actual: ");
   SdStringBuf_AppendCStr(buf, SdParser_TypeString(actual_type));
   result = SdParser_Fail(SdErr_UNEXPECTED_TOKEN, token, SdStringBuf_CStr(buf));
   SdStringBuf_Delete(buf);
   return result;
}

const char* SdParser_TypeString(SdTokenType type) {
   switch (type) {
      case SdTokenType_NONE: return "(none)";
      case SdTokenType_INT_LIT: return "<integer literal>";
      case SdTokenType_DOUBLE_LIT: return "<double literal>";
      case SdTokenType_BOOL_LIT: return "<boolean literal>";
      case SdTokenType_STRING_LIT: return "<string literal>";
      case SdTokenType_OPEN_PAREN: return "(";
      case SdTokenType_CLOSE_PAREN: return ")";
      case SdTokenType_OPEN_BRACKET: return "[";
      case SdTokenType_CLOSE_BRACKET: return "]";
      case SdTokenType_OPEN_BRACE: return "{";
      case SdTokenType_CLOSE_BRACE: return "}";
      case SdTokenType_COLON: return ":";
      case SdTokenType_IDENTIFIER: return "<identifier>";
      case SdTokenType_FUNCTION: return "function";
      case SdTokenType_VAR: return "var";
      case SdTokenType_SET: return "set";
      case SdTokenType_IF: return "if";
      case SdTokenType_ELSE: return "else";
      case SdTokenType_ELSEIF: return "elseif";
      case SdTokenType_FOR: return "for";
      case SdTokenType_FROM: return "from";
      case SdTokenType_TO: return "to";
      case SdTokenType_AT: return "at";
      case SdTokenType_IN: return "in";
      case SdTokenType_WHILE: return "while";
      case SdTokenType_DO: return "do";
      case SdTokenType_SWITCH: return "switch";
      case SdTokenType_CASE: return "case";
      case SdTokenType_DEFAULT: return "default";
      case SdTokenType_RETURN: return "return";
      case SdTokenType_DIE: return "die";
      case SdTokenType_IMPORT: return "import";
      case SdTokenType_NIL: return "nil";
      case SdTokenType_ARROW: return "->";
      case SdTokenType_QUERY: return "query";
      default: return "<unrecognized token type>";
   }
}

SdResult SdParser_ParseProgram(SdEnv_r env, const char* text, SdValue_r* out_program_node) {
   SdScanner* scanner;
   SdList* functions;
   SdList* statements;
   SdToken_r token;
   SdResult result;
   
   assert(env);
   assert(text);
   assert(out_program_node);
   scanner = SdScanner_New();
   functions = SdList_New();
   statements = SdList_New();
   
   SdScanner_Tokenize(scanner, text);

   while (SdScanner_Peek(scanner, &token)) {
      SdTokenType token_type = SdToken_Type(token);
      SdValue_r node;
      if (token_type == SdTokenType_IMPORT || token_type == SdTokenType_FUNCTION) {
         result = SdParser_ParseFunction(env, scanner, &node);
         if (SdFailed(result))
            goto end;
         else
            SdList_Append(functions, node);
      } else { /* if it's not a function, it must be a statement. */
         result = SdParser_ParseStatement(env, scanner, &node);
         if (SdFailed(result))
            goto end;
         else
            SdList_Append(statements, node);
      }
   }

   *out_program_node = SdAst_Program_New(env, functions, statements);
   functions = NULL;
   statements = NULL;
   result = SdResult_SUCCESS;

end:
   if (scanner) SdScanner_Delete(scanner);
   if (functions) SdList_Delete(functions);
   if (statements) SdList_Delete(statements);
   return result;
}

SdResult SdParser_ReadExpectType(SdScanner_r scanner, SdTokenType expected_type, SdToken_r* out_token) {
   SdToken_r token;
   SdTokenType actual_type;

   assert(scanner);
   assert(out_token);
   if (!SdScanner_Read(scanner, &token))
      return SdParser_FailEof();

   actual_type = SdToken_Type(token);
   if (actual_type != expected_type) {
      return SdParser_FailType(token, expected_type, actual_type);
   } else {
      *out_token = token;
      return SdResult_SUCCESS;
   }
}

SdResult SdParser_ParseFunction(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdBool is_imported = SdFalse;
   SdString* function_name = NULL;
   SdList* parameter_names = NULL;
   SdValue_r body = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   if (SdScanner_PeekType(scanner) == SdTokenType_IMPORT) {
      is_imported = SdTrue;
      SdParser_READ();
   }

   SdParser_READ_EXPECT_TYPE(SdTokenType_FUNCTION);
   SdParser_READ_IDENTIFIER(function_name);
   
   SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_PAREN);
   parameter_names = SdList_New();
   while (SdScanner_PeekType(scanner) == SdTokenType_IDENTIFIER) {
      SdString* param_name = NULL;
      SdParser_READ_IDENTIFIER(param_name);
      SdList_Append(parameter_names, SdEnv_BoxString(env, param_name));
   }
   SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_PAREN);

   if (is_imported) {
      body = SdAst_Body_New(env, SdList_New());
   } else {
      SdParser_READ_BODY(body);
   }

   *out_node = SdAst_Function_New(env, function_name, parameter_names, body, is_imported);
   function_name = NULL;
   parameter_names = NULL;
end:
   if (function_name) SdString_Delete(function_name);
   if (parameter_names) SdList_Delete(parameter_names);
   return result;
}

SdResult SdParser_ParseBody(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdList* statements = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_BRACE);

   statements = SdList_New();
   while (SdScanner_PeekType(scanner) != SdTokenType_CLOSE_BRACE) {
      SdValue_r statement;
      SdParser_CALL(SdParser_ParseStatement(env, scanner, &statement));
      SdList_Append(statements, statement);
   }

   SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_BRACE);

   *out_node = SdAst_Body_New(env, statements);
   statements = NULL;
end:
   if (statements) SdList_Delete(statements);
   return result;
}

SdResult SdParser_ParseExpr(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;

   assert(env);
   assert(scanner);
   assert(out_node);
   switch (SdScanner_PeekType(scanner)) {
      case SdTokenType_INT_LIT: {
         int num;
         SdParser_READ_EXPECT_TYPE(SdTokenType_INT_LIT);
         num = (int)strtol(SdToken_Text(token), NULL, 10);
         *out_node = SdAst_IntLit_New(env, num);
         break;
      }
      
      case SdTokenType_DOUBLE_LIT: {
         double num;
         SdParser_READ_EXPECT_TYPE(SdTokenType_DOUBLE_LIT);
         num = atof(SdToken_Text(token));
         *out_node = SdAst_DoubleLit_New(env, num);
         break;
      }

      case SdTokenType_BOOL_LIT: {
         SdBool val;
         SdParser_READ_EXPECT_TYPE(SdTokenType_BOOL_LIT);
         val = strcmp(SdToken_Text(token), "true") == 0;
         *out_node = SdAst_BoolLit_New(env, val);
         break;
      }

      case SdTokenType_STRING_LIT: {
         char* str;
         const char* inner_str;
         size_t len;

         SdParser_READ_EXPECT_TYPE(SdTokenType_STRING_LIT);
         str = SdStrdup(SdToken_Text(token));
         len = strlen(str);
         assert(str[0] == '"');
         assert(str[len - 1] == '"');
         
         str[len - 1] = 0; /* remove trailing quote */
         inner_str = &str[1]; /* remove leading quote */

         *out_node = SdAst_StringLit_New(env, SdString_FromCStr(inner_str));
         SdFree(str);
         break;
      }

      case SdTokenType_NIL: {
         SdParser_READ_EXPECT_TYPE(SdTokenType_NIL);
         *out_node = SdAst_NilLit_New(env);
         break;
      }

      case SdTokenType_IDENTIFIER: {
         SdString* identifier = NULL;
         SdParser_READ_IDENTIFIER(identifier);
         *out_node = SdAst_VarRef_New(env, identifier);
         break;
      }

      case SdTokenType_OPEN_PAREN:
      case SdTokenType_OPEN_BRACKET:
         result = SdParser_ParseCall(env, scanner, out_node);
         break;

      case SdTokenType_QUERY:
         result = SdParser_ParseQuery(env, scanner, out_node);
         break;

      case SdTokenType_COLON:
         result = SdParser_ParseClosure(env, scanner, out_node);
         break;

      default:
         result = SdParser_Fail(SdErr_UNEXPECTED_TOKEN, SdScanner_PeekToken(scanner), "Expected expression.");
         break;
   }

end:
   return result;
}

SdResult SdParser_ParseQuery(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r initial_expr = NULL;
   SdList* steps = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_QUERY);
   SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_PAREN);
   SdParser_READ_EXPR(initial_expr);

   steps = SdList_New();
   while (SdScanner_PeekType(scanner) == SdTokenType_ARROW) {
      SdValue_r step = NULL;
      SdParser_CALL(SdParser_ParseQueryStep(env, scanner, &step));
      assert(step);
      SdList_Append(steps, step);
   }

   SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_PAREN);

   *out_node = SdAst_Query_New(env, initial_expr, steps);
   steps = NULL;
end:
   if (steps) SdList_Delete(steps);
   return result;
}

SdResult SdParser_ParseQueryStep(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdString* function_name = NULL;
   SdList* arguments = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_IDENTIFIER(function_name);

   arguments = SdList_New();
   while (SdScanner_PeekType(scanner) != SdTokenType_ARROW) {
      SdValue_r argument_expr;
      SdParser_READ_EXPR(argument_expr);
      SdList_Append(arguments, argument_expr);
   }

   *out_node = SdAst_Call_New(env, function_name, arguments);
   function_name = NULL;
   arguments = NULL;
end:
   if (function_name) SdString_Delete(function_name);
   if (arguments) SdList_Delete(arguments);
   return result;
}

SdResult SdParser_ParseClosure(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdList* param_names = NULL;
   SdValue_r body = NULL, expr = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_COLON);

   param_names = SdList_New();
   if (SdScanner_PeekType(scanner) == SdTokenType_IDENTIFIER) {
      SdString* param_name;
      SdParser_READ_IDENTIFIER(param_name);
      SdList_Append(param_names, SdEnv_BoxString(env, param_name));
   } else {
      SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_PAREN);
      while (SdScanner_PeekType(scanner) == SdTokenType_IDENTIFIER) {
         SdString* param_name;
         SdParser_READ_IDENTIFIER(param_name);
         SdList_Append(param_names, SdEnv_BoxString(env, param_name));
      }
      SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_PAREN);
   }

   if (SdScanner_PeekType(scanner) == SdTokenType_OPEN_BRACE) {
      SdParser_READ_BODY(body);
   } else {
      SdList* statements;

      /* Transform this into a body with one return statement */
      SdParser_READ_EXPR(expr);
      statements = SdList_New();
      SdList_Append(statements, SdAst_Return_New(env, expr));
      body = SdAst_Body_New(env, statements);
   }

   *out_node = SdAst_Function_New(env, SdString_FromCStr("(closure)"), param_names, body, SdFalse);
   param_names = NULL;
end:
   if (param_names) SdList_Delete(param_names);
   return result;
}

SdResult SdParser_ParseStatement(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   assert(env);
   assert(scanner);
   assert(out_node);
   switch (SdScanner_PeekType(scanner)) {
      case SdTokenType_OPEN_PAREN: return SdParser_ParseCall(env, scanner, out_node);
      case SdTokenType_OPEN_BRACKET: return SdParser_ParseCall(env, scanner, out_node);
      case SdTokenType_VAR: return SdParser_ParseVar(env, scanner, out_node);
      case SdTokenType_SET: return SdParser_ParseSet(env, scanner, out_node);
      case SdTokenType_IF: return SdParser_ParseIf(env, scanner, out_node);
      case SdTokenType_FOR: return SdParser_ParseFor(env, scanner, out_node);
      case SdTokenType_WHILE: return SdParser_ParseWhile(env, scanner, out_node);
      case SdTokenType_DO: return SdParser_ParseDo(env, scanner, out_node);
      case SdTokenType_SWITCH: return SdParser_ParseSwitch(env, scanner, out_node);
      case SdTokenType_RETURN: return SdParser_ParseReturn(env, scanner, out_node);
      case SdTokenType_DIE: return SdParser_ParseDie(env, scanner, out_node);
      case SdTokenType_NONE: return SdParser_FailEof();
      default: {
         SdToken_r token;
         SdScanner_Read(scanner, &token);
         return SdParser_Fail(SdErr_UNEXPECTED_TOKEN, token, "Expected statement.");
      }
   }
}

SdResult SdParser_ParseCall(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdString* function_name = NULL;
   SdList* arguments = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   arguments = SdList_New();
   switch (SdScanner_PeekType(scanner)) {
      case SdTokenType_OPEN_PAREN: {
         SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_PAREN);
         SdParser_READ_IDENTIFIER(function_name);
         while (SdScanner_PeekType(scanner) != SdTokenType_CLOSE_PAREN) {
            SdValue_r arg_expr;
            SdParser_READ_EXPR(arg_expr);
            SdList_Append(arguments, arg_expr);
         }
         SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_PAREN);
         break;
      }

      case SdTokenType_OPEN_BRACKET: {
         SdValue_r arg_expr;
         SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_BRACKET);
            
         SdParser_READ_EXPR(arg_expr);
         SdList_Append(arguments, arg_expr);

         SdParser_READ_IDENTIFIER(function_name);

         while (SdScanner_PeekType(scanner) != SdTokenType_CLOSE_BRACKET) {
            SdParser_READ_EXPR(arg_expr);
            SdList_Append(arguments, arg_expr);
         }

         SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_BRACKET);
         break;
      }

      default:
         result = SdFail(SdErr_UNEXPECTED_TOKEN, "Expected: ( [");
         goto end;
   }

   *out_node = SdAst_Call_New(env, function_name, arguments);
   function_name = NULL;
   arguments = NULL;
end:
   if (function_name) SdString_Delete(function_name);
   if (arguments) SdList_Delete(arguments);
   return result;
}

SdResult SdParser_ParseVar(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdString* identifier = NULL;
   SdValue_r expr = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_VAR);
   SdParser_READ_IDENTIFIER(identifier);
   SdParser_READ_EXPR(expr);

   *out_node = SdAst_Var_New(env, identifier, expr);
   identifier = NULL;
end:
   if (identifier) SdString_Delete(identifier);
   return result;
}

SdResult SdParser_ParseSet(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdString* identifier = NULL;
   SdValue_r expr = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_SET);
   SdParser_READ_IDENTIFIER(identifier);
   SdParser_READ_EXPR(expr);

   *out_node = SdAst_Set_New(env, identifier, expr);
   identifier = NULL;
end:
   if (identifier) SdString_Delete(identifier);
   return result;
}

SdResult SdParser_ParseIf(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r condition_expr = NULL, true_body = NULL, else_body = NULL;
   SdList* elseifs = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_IF);
   SdParser_READ_EXPR(condition_expr);
   SdParser_READ_BODY(true_body);

   elseifs = SdList_New();
   while (SdScanner_PeekType(scanner) == SdTokenType_ELSEIF) {
      SdValue_r elseif = NULL;
      SdParser_CALL(SdParser_ParseElseIf(env, scanner, &elseif));
      assert(elseif);
      SdList_Append(elseifs, elseif);
   }

   if (SdScanner_PeekType(scanner) == SdTokenType_ELSE) {
      SdParser_READ_EXPECT_TYPE(SdTokenType_ELSE);
      SdParser_READ_BODY(else_body);
   } else {
      else_body = SdAst_Body_New(env, SdList_New());
   }

   *out_node = SdAst_If_New(env, condition_expr, true_body, elseifs, else_body);
   elseifs = NULL;
end:
   if (elseifs) SdList_Delete(elseifs);
   return result;
}

SdResult SdParser_ParseElseIf(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr = NULL, body = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_ELSEIF);
   SdParser_READ_EXPR(expr);
   SdParser_READ_BODY(body);

   *out_node = SdAst_ElseIf_New(env, expr, body);
end:
   return result;
}

SdResult SdParser_ParseFor(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdString* iter_name = NULL;
   SdString* indexer_name = NULL;
   SdValue_r start_expr = NULL, stop_expr = NULL, body = NULL, collection_expr = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_FOR);
   SdParser_READ_IDENTIFIER(iter_name);

   switch (SdScanner_PeekType(scanner)) {
      case SdTokenType_FROM: { /* for x from 1 to 10 */
         SdParser_READ_EXPECT_TYPE(SdTokenType_FROM);
         SdParser_READ_EXPR(start_expr);
         SdParser_READ_EXPECT_TYPE(SdTokenType_TO);
         SdParser_READ_EXPR(stop_expr);
         SdParser_READ_BODY(body);
         *out_node = SdAst_For_New(env, iter_name, start_expr, stop_expr, body);
         iter_name = NULL;
         break;
      }
      case SdTokenType_AT:
      case SdTokenType_IN: { /* for x at i in foo */
         if (SdScanner_PeekType(scanner) == SdTokenType_AT) {
            SdParser_READ_EXPECT_TYPE(SdTokenType_AT);
            SdParser_READ_IDENTIFIER(indexer_name);
         }
         SdParser_READ_EXPECT_TYPE(SdTokenType_IN);
         SdParser_READ_EXPR(collection_expr);
         SdParser_READ_BODY(body);
         *out_node = SdAst_ForEach_New(env, iter_name, indexer_name, collection_expr, body);
         iter_name = NULL;
         indexer_name = NULL;
         break;
      }
      default: {
         result = SdFail(SdErr_UNEXPECTED_TOKEN, "Expected: from, at, in");
         break;
      }
   }

end:
   if (iter_name) SdString_Delete(iter_name);
   if (indexer_name) SdString_Delete(indexer_name);
   return result;
}

SdResult SdParser_ParseWhile(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r body = NULL, expr = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_WHILE);
   SdParser_READ_EXPR(expr);
   SdParser_READ_BODY(body);

   *out_node = SdAst_While_New(env, expr, body);
end:
   return result;
}

SdResult SdParser_ParseDo(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r body = NULL, expr = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_DO);
   SdParser_READ_BODY(body);
   SdParser_READ_EXPECT_TYPE(SdTokenType_WHILE);
   SdParser_READ_EXPR(expr);

   *out_node = SdAst_Do_New(env, expr, body);
end:
   return result;
}

SdResult SdParser_ParseSwitch(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r condition_expr = NULL, default_body = NULL;
   SdList* cases = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_SWITCH);
   SdParser_READ_EXPR(condition_expr);
   SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_BRACE);

   cases = SdList_New();
   while (SdScanner_PeekType(scanner) == SdTokenType_CASE) {
      SdValue_r case_v = NULL;
      SdParser_CALL(SdParser_ParseCase(env, scanner, &case_v));
      assert(case_v);
      SdList_Append(cases, case_v);
   }

   if (SdScanner_PeekType(scanner) == SdTokenType_DEFAULT) {
      SdParser_READ_EXPECT_TYPE(SdTokenType_DEFAULT);
      SdParser_READ_BODY(default_body);
   } else {
      default_body = SdAst_Body_New(env, SdList_New());
   }

   SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_BRACE);

   *out_node = SdAst_Switch_New(env, condition_expr, cases, default_body);
   cases = NULL;
end:
   if (cases) SdList_Delete(cases);
   return result;
}

SdResult SdParser_ParseCase(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr = NULL, body = NULL;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_CASE);
   SdParser_READ_EXPR(expr);
   SdParser_READ_BODY(body);

   *out_node = SdAst_Case_New(env, expr, body);
end:
   return result;
}

SdResult SdParser_ParseReturn(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_RETURN);
   SdParser_READ_EXPR(expr);
   *out_node = SdAst_Return_New(env, expr);
end:
   return result;
}

SdResult SdParser_ParseDie(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr;

   assert(env);
   assert(scanner);
   assert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_DIE);
   SdParser_READ_EXPR(expr);
   *out_node = SdAst_Die_New(env, expr);
end:
   return result;
}

/* SdEngine **********************************************************************************************************/
#define SdEngine_INTRINSIC_START_ARGS1(name) \
   SdResult name(SdEngine_r self, SdList_r arguments, SdValue_r* out_return) { \
      SdResult result = SdResult_SUCCESS; \
      SdValue_r a_val = NULL; \
      SdType a_type = SdType_NIL; \
      assert(self); \
      assert(arguments); \
      assert(out_return); \
      *out_return = NULL; \
      if (SdFailed(result = SdEngine_Args1(arguments, &a_val, &a_type))) \
         return result;
#define SdEngine_INTRINSIC_START_ARGS2(name) \
   SdResult name(SdEngine_r self, SdList_r arguments, SdValue_r* out_return) { \
      SdResult result = SdResult_SUCCESS; \
      SdValue_r a_val = NULL, b_val = NULL; \
      SdType a_type = SdType_NIL, b_type = SdType_NIL; \
      assert(self); \
      assert(arguments); \
      assert(out_return); \
      *out_return = NULL; \
      if (SdFailed(result = SdEngine_Args2(arguments, &a_val, &a_type, &b_val, &b_type))) \
         return result;
#define SdEngine_INTRINSIC_START_ARGS3(name) \
   SdResult name(SdEngine_r self, SdList_r arguments, SdValue_r* out_return) { \
      SdResult result = SdResult_SUCCESS; \
      SdValue_r a_val = NULL, b_val = NULL, c_val = NULL; \
      SdType a_type = SdType_NIL, b_type = SdType_NIL, c_type = SdType_NIL; \
      assert(self); \
      assert(arguments); \
      assert(out_return); \
      *out_return = NULL; \
      if (SdFailed(result = SdEngine_Args3(arguments, &a_val, &a_type, &b_val, &b_type, &c_val, &c_type))) \
         return result;
#define SdEngine_INTRINSIC_END \
      if (*out_return) \
         return SdResult_SUCCESS; \
      else \
         return SdFail(SdErr_TYPE_MISMATCH, "Invalid argument type."); \
   }
#define SdEngine_INTRINSIC_INT2(name, expr) \
   SdEngine_INTRINSIC_START_ARGS2(name) \
   if (a_type == SdType_INT && b_type == SdType_INT) { \
      int a, b; \
      a = SdValue_GetInt(a_val); \
      b = SdValue_GetInt(b_val); \
      *out_return = SdEnv_BoxInt(self->env, expr); \
   } \
   SdEngine_INTRINSIC_END
#define SdEngine_INTRINSIC_INTDOUBLE2(name, expr) \
   SdEngine_INTRINSIC_START_ARGS2(name) \
   if (a_type == SdType_DOUBLE && b_type == SdType_DOUBLE) { \
      double a, b; \
      a = SdValue_GetDouble(a_val); \
      b = SdValue_GetDouble(b_val); \
      *out_return = SdEnv_BoxDouble(self->env, expr); \
   } else if (a_type == SdType_INT && b_type == SdType_INT) { \
      int a, b; \
      a = SdValue_GetInt(a_val); \
      b = SdValue_GetInt(b_val); \
      *out_return = SdEnv_BoxInt(self->env, expr); \
   } \
   SdEngine_INTRINSIC_END
#define SdEngine_INTRINSIC_INTDOUBLESTRING2_BOOL(name, int_double_expr, string_expr) \
   SdEngine_INTRINSIC_START_ARGS2(name) \
   if (a_type == SdType_DOUBLE && b_type == SdType_DOUBLE) { \
      double a, b; \
      a = SdValue_GetDouble(a_val); \
      b = SdValue_GetDouble(b_val); \
      *out_return = SdEnv_BoxBool(self->env, int_double_expr); \
   } else if (a_type == SdType_INT && b_type == SdType_INT) { \
      int a, b; \
      a = SdValue_GetInt(a_val); \
      b = SdValue_GetInt(b_val); \
      *out_return = SdEnv_BoxBool(self->env, int_double_expr); \
   } else if (a_type == SdType_STRING && b_type == SdType_STRING) { \
      SdString_r a, b; \
      a = SdValue_GetString(a_val); \
      b = SdValue_GetString(b_val); \
      *out_return = SdEnv_BoxBool(self->env, string_expr); \
   } \
   SdEngine_INTRINSIC_END
#define SdEngine_INTRINSIC_DOUBLE1(name, expr) \
   SdEngine_INTRINSIC_START_ARGS1(name) \
   if (a_type == SdType_DOUBLE) { \
      double a = SdValue_GetDouble(a_val); \
      *out_return = SdEnv_BoxDouble(self->env, expr); \
   } \
   SdEngine_INTRINSIC_END
#define SdEngine_INTRINSIC_DOUBLE2(name, expr) \
   SdEngine_INTRINSIC_START_ARGS2(name) \
   if (a_type == SdType_DOUBLE && b_type == SdType_DOUBLE) { \
      double a = SdValue_GetDouble(a_val); \
      double b = SdValue_GetDouble(b_val); \
      *out_return = SdEnv_BoxDouble(self->env, expr); \
   } \
   SdEngine_INTRINSIC_END
#define SdEngine_INTRINSIC_BOOL2(name, expr) \
   SdEngine_INTRINSIC_START_ARGS2(name) \
   if (a_type == SdType_BOOL && b_type == SdType_BOOL) { \
      SdBool a = SdValue_GetBool(a_val); \
      SdBool b = SdValue_GetBool(b_val); \
      *out_return = SdEnv_BoxBool(self->env, expr); \
   } \
   SdEngine_INTRINSIC_END
#define SdEngine_INTRINSIC_BOOL1(name, expr) \
   SdEngine_INTRINSIC_START_ARGS1(name) \
   if (a_type == SdType_BOOL) { \
      SdBool a = SdValue_GetBool(a_val); \
      *out_return = SdEnv_BoxBool(self->env, expr); \
   } \
   SdEngine_INTRINSIC_END
#define SdEngine_INTRINSIC_VALUE2(name, expr) \
   SdEngine_INTRINSIC_START_ARGS2(name) \
   do { \
      SdValue_r a = a_val; \
      SdValue_r b = b_val; \
      *out_return = expr; \
   } while (0); \
   SdEngine_INTRINSIC_END

SdEngine* SdEngine_New(SdEnv_r env) {
   SdEngine* self;
   
   assert(env);
   self = SdAlloc(sizeof(SdEngine));
   self->env = env;
   return self;
}

void SdEngine_Delete(SdEngine* self) {
   assert(self);
   SdFree(self);
}

SdResult SdEngine_ExecuteProgram(SdEngine_r self) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r root, frame;
   SdList_r functions, statements;
   size_t i, count;

   assert(self);
   root = SdEnv_Root(self->env);
   functions = SdEnv_Root_Functions(root);
   statements = SdEnv_Root_Statements(root);
   frame = SdEnv_Root_BottomFrame(root);

   count = SdList_Count(functions);
   for (i = 0; i < count; i++) {
      SdValue_r function, closure;

      function = SdList_GetAt(functions, i);
      if (SdFailed(result = SdEngine_EvaluateFunction(self, frame, function, &closure)))
         return result;
      if (SdFailed(result = SdEnv_DeclareVar(self->env, frame, SdAst_Function_Name(function), closure)))
         return result;
   }

   count = SdList_Count(statements);
   for (i = 0; i < count; i++) {
      SdValue_r statement, return_value = NULL;

      statement = SdList_GetAt(statements, i);
      if (SdFailed(result = SdEngine_ExecuteStatement(self, frame, statement, &return_value)))
         return result;
      if (return_value)
         return result; /* a return statement breaks the program's execution */
   }

   return result;
}

SdResult SdEngine_Call(SdEngine_r self, SdValue_r frame, SdString_r function_name, SdList_r arguments, 
   SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r closure_slot, closure, call_frame, function;
   SdList_r param_names;
   size_t i, count;

   assert(self);
   assert(frame);
   assert(function_name);
   assert(arguments);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);

   /* ensure that 'function_name' refers to a defined closure */
   closure_slot = SdEnv_FindVariableSlot(self->env, frame, function_name, SdTrue);
   if (!closure_slot)
      return SdFailWithStringSuffix(SdErr_UNDECLARED_VARIABLE, "Function not found: ", function_name);

   closure = SdEnv_VariableSlot_Value(closure_slot);
   if (SdAst_NodeType(closure) != SdNodeType_CLOSURE)
      return SdFailWithStringSuffix(SdErr_TYPE_MISMATCH, "Not a function: ", function_name);
   function = SdEnv_Closure_FunctionNode(closure);

   /* ensure that the argument list matches the parameter list. skip the check for intrinsics since they are more 
      flexible and will do the check themselves. */
   param_names = SdEnv_Closure_ParameterNames(closure);
   if (!SdAst_Function_IsImported(function) && SdList_Count(arguments) != SdList_Count(param_names))
      return SdFailWithStringSuffix(SdErr_ARGUMENT_MISMATCH, "Wrong number of arguments to function: ", function_name);

   /* if this is an intrinsic then call it now; no frame needed */
   if (SdAst_Function_IsImported(function))
      return SdEngine_CallIntrinsic(self, function_name, arguments, out_return);

   /* create a frame containing the argument values */
   call_frame = SdEnv_Frame_New(self->env, SdEnv_Closure_Frame(closure));
   count = SdList_Count(arguments);
   for (i = 0; i < count; i++) {
      SdValue_r param_name, arg_value;
      
      param_name = SdList_GetAt(param_names, i);
      arg_value = SdList_GetAt(arguments, i);
      if (SdFailed(result = SdEnv_DeclareVar(self->env, call_frame, param_name, arg_value)))
         return result;
   }

   /* execute the function body using the frame we just constructed */
   return SdEngine_ExecuteBody(self, call_frame, SdAst_Function_Body(function), out_return);
}

SdResult SdEngine_EvaluateExpr(SdEngine_r self, SdValue_r frame, SdValue_r expr, SdValue_r* out_value) {
   SdResult result = SdResult_SUCCESS;

   assert(self);
   assert(frame);
   assert(expr);
   assert(out_value);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);

   switch (SdAst_NodeType(expr)) {
      case SdNodeType_INT_LIT:
         *out_value = SdAst_IntLit_Value(expr);
         break;

      case SdNodeType_DOUBLE_LIT:
         *out_value = SdAst_DoubleLit_Value(expr);
         break;

      case SdNodeType_BOOL_LIT:
         *out_value = SdAst_BoolLit_Value(expr);
         break;

      case SdNodeType_STRING_LIT:
         *out_value = SdAst_StringLit_Value(expr);
         break;

      case SdNodeType_NIL_LIT:
         *out_value = SdEnv_BoxNil(self->env);
         break;

      case SdNodeType_VAR_REF:
         result = SdEngine_EvaluateVarRef(self, frame, expr, out_value);
         break;

      case SdNodeType_QUERY:
         result = SdEngine_EvaluateQuery(self, frame, expr, out_value);
         break;

      case SdNodeType_CALL:
         result = SdEngine_ExecuteCall(self, frame, expr, out_value);
         break;

      case SdNodeType_FUNCTION:
         result = SdEngine_EvaluateFunction(self, frame, expr, out_value);
         break;

      default:
         result = SdFail(SdErr_INTERPRETER_BUG, "Unexpected node type.");
         break;
   }

   return result;
}

SdResult SdEngine_EvaluateVarRef(SdEngine_r self, SdValue_r frame, SdValue_r var_ref, SdValue_r* out_value) {
   SdString_r identifier;
   SdValue_r slot;

   assert(self);
   assert(frame);
   assert(var_ref);
   assert(out_value);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(var_ref) == SdNodeType_VAR_REF);

   identifier = SdAst_VarRef_Identifier(var_ref);
   slot = SdEnv_FindVariableSlot(self->env, frame, identifier, SdTrue);
   if (!slot)
      return SdFailWithStringSuffix(SdErr_UNDECLARED_VARIABLE, "Undeclared variable: ", identifier);
   *out_value = SdEnv_VariableSlot_Value(slot);
   return SdResult_SUCCESS;
}

SdResult SdEngine_EvaluateQuery(SdEngine_r self, SdValue_r frame, SdValue_r query, SdValue_r* out_value) {
   SdResult result;
   SdValue_r value;
   SdList_r steps;
   size_t i, count;

   assert(self);
   assert(frame);
   assert(query);
   assert(out_value);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(query) == SdNodeType_QUERY);

   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, SdAst_Query_InitialExpr(query), &value)))
      return result;

   steps = SdAst_Query_Steps(query);
   count = SdList_Count(steps);
   for (i = 0; i < count; i++) {
      SdList* argument_values;
      SdList_r argument_exprs;
      SdValue_r call, step_frame;
      size_t j, num_arguments;

      call = SdList_GetAt(steps, i);
      step_frame = SdEnv_Frame_New(self->env, frame);

      argument_exprs = SdAst_Call_Arguments(call);
      argument_values = SdList_New();
      SdList_Append(argument_values, value);
      num_arguments = SdList_Count(argument_exprs);

      for (j = 0; j < num_arguments; j++) {
         SdValue_r argument_expr, argument_value;
         argument_expr = SdList_GetAt(argument_exprs, j);
         if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, argument_expr, &argument_value))) {
            SdList_Delete(argument_values);
            return result;
         }
         SdList_Append(argument_values, argument_value);
      }

      if (SdFailed(result = SdEngine_Call(self, step_frame, SdAst_Call_FunctionName(call), argument_values, &value))) {
         SdList_Delete(argument_values);
         return result;
      }
   }

   *out_value = value;
   return result;
}

SdResult SdEngine_EvaluateFunction(SdEngine_r self, SdValue_r frame, SdValue_r function, SdValue_r* out_value) {
   assert(self);
   assert(frame);
   assert(function);
   assert(out_value);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(function) == SdNodeType_FUNCTION);

   *out_value = SdEnv_Closure_New(self->env, frame, SdAst_Function_ParameterNames(function), function);
   return SdResult_SUCCESS;
}

SdResult SdEngine_ExecuteBody(SdEngine_r self, SdValue_r frame, SdValue_r body, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdList_r statements;
   SdValue_r statement;
   size_t i, count;

   assert(self);
   assert(frame);
   assert(body);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(body) == SdNodeType_BODY);

   statements = SdAst_Body_Statements(body);
   count = SdList_Count(statements);
   for (i = 0; i < count; i++) {
      statement = SdList_GetAt(statements, i);
      *out_return = NULL;
      if (SdFailed(result = SdEngine_ExecuteStatement(self, frame, statement, out_return)))
         return result;
      if (*out_return) /* a return statement breaks the body */
         return result;
   }

   return result;
}

SdResult SdEngine_ExecuteStatement(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdValue_r discarded_result;
   SdBool gc_needed = SdFalse;

   assert(self);
   assert(frame);
   assert(statement);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);

#ifdef SD_DEBUG
   /* when running the memory leak detection, collect garbage before every statement to fish for bugs */
   gc_needed = SdTrue;
#else
   /* run the garbage collector if necessary (more than SD_NUM_ALLOCATIONS_PER_GC allocations since the last GC) */
   gc_needed = (SdEnv_AllocationCount(self->env) - self->last_gc) > SD_NUM_ALLOCATIONS_PER_GC;
#endif

   if (gc_needed) {
      SdValue_r extra_in_use[1];
      extra_in_use[0] = frame;
      SdEnv_CollectGarbage(self->env, extra_in_use, 1);
      self->last_gc = SdEnv_AllocationCount(self->env);
   }

   switch (SdAst_NodeType(statement)) {
      case SdNodeType_CALL: return SdEngine_ExecuteCall(self, frame, statement, &discarded_result);
      case SdNodeType_VAR: return SdEngine_ExecuteVar(self, frame, statement);
      case SdNodeType_SET: return SdEngine_ExecuteSet(self, frame, statement);
      case SdNodeType_IF: return SdEngine_ExecuteIf(self, frame, statement, out_return);
      case SdNodeType_FOR: return SdEngine_ExecuteFor(self, frame, statement, out_return);
      case SdNodeType_FOREACH: return SdEngine_ExecuteForEach(self, frame, statement, out_return);
      case SdNodeType_WHILE: return SdEngine_ExecuteWhile(self, frame, statement, out_return);
      case SdNodeType_DO: return SdEngine_ExecuteDo(self, frame, statement, out_return);
      case SdNodeType_SWITCH: return SdEngine_ExecuteSwitch(self, frame, statement, out_return);
      case SdNodeType_RETURN: return SdEngine_ExecuteReturn(self, frame, statement, out_return);
      case SdNodeType_DIE: return SdEngine_ExecuteDie(self, frame, statement);
      default: return SdFail(SdErr_UNEXPECTED_TOKEN, "Unexpected node type; expected a statement type.");
   }
}

SdResult SdEngine_ExecuteCall(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result;
   SdList_r argument_exprs;
   SdList* argument_values;
   size_t i, num_arguments;
   
   assert(self);
   assert(frame);
   assert(statement);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_CALL);

   argument_exprs = SdAst_Call_Arguments(statement);
   argument_values = SdList_New();
   num_arguments = SdList_Count(argument_exprs);

   for (i = 0; i < num_arguments; i++) {
      SdValue_r argument_expr, argument_value;
      argument_expr = SdList_GetAt(argument_exprs, i);
      if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, argument_expr, &argument_value)))
         goto end;
      SdList_Append(argument_values, argument_value);
   }

   result = SdEngine_Call(self, frame, SdAst_Call_FunctionName(statement), argument_values, out_return);
end:
   if (argument_values) SdList_Delete(argument_values);
   return result;
}

SdResult SdEngine_ExecuteVar(SdEngine_r self, SdValue_r frame, SdValue_r statement) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r name, expr, value;

   assert(self);
   assert(frame);
   assert(statement);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_VAR);

   name = SdAst_Var_VariableName(statement);
   expr = SdAst_Var_ValueExpr(statement);
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
      return result;

   return SdEnv_DeclareVar(self->env, frame, name, value);
}

SdResult SdEngine_ExecuteSet(SdEngine_r self, SdValue_r frame, SdValue_r statement) {
   SdResult result = SdResult_SUCCESS;
   SdString_r name;
   SdValue_r slot, expr, value;

   assert(self);
   assert(frame);
   assert(statement);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_SET);

   /* the variable slot must already exist */
   name = SdAst_Set_VariableName(statement);
   slot = SdEnv_FindVariableSlot(self->env, frame, name, SdTrue);
   if (!slot)
      return SdFailWithStringSuffix(SdErr_UNDECLARED_VARIABLE, "Undeclared variable: ", name);

   /* evaluate the right hand side */
   expr = SdAst_Set_ValueExpr(statement);
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
      return result;

   /* make the assignment */
   SdEnv_VariableSlot_SetValue(slot, value);
   return result;
}

SdResult SdEngine_ExecuteIf(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r elseif, expr, value;
   SdList_r elseifs;
   size_t i, count;

   assert(self);
   assert(frame);
   assert(statement);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_IF);

   /* try the IF condition */
   expr = SdAst_If_ConditionExpr(statement);
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
      return result;
   if (SdValue_Type(value) != SdType_BOOL)
      return SdFail(SdErr_TYPE_MISMATCH, "IF condition expression does not evaluate to a Boolean.");
   if (SdValue_GetBool(value))
      return SdEngine_ExecuteBody(self, frame, SdAst_If_TrueBody(statement), out_return);

   /* try the ELSEIF conditions */
   elseifs = SdAst_If_ElseIfs(statement);
   count = SdList_Count(elseifs);
   for (i = 0; i < count; i++) {
      elseif = SdList_GetAt(elseifs, i);
      expr = SdAst_ElseIf_ConditionExpr(elseif);
      if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
         return result;
      if (SdValue_Type(value) != SdType_BOOL)
         return SdFail(SdErr_TYPE_MISMATCH, "ELSEIF condition expression does not evaluate to a Boolean.");
      if (SdValue_GetBool(value))
         return SdEngine_ExecuteBody(self, frame, SdAst_ElseIf_Body(elseif), out_return);
   }

   /* all conditions were false, so execute the ELSE block */
   return SdEngine_ExecuteBody(self, frame, SdAst_If_ElseBody(statement), out_return);
}

SdResult SdEngine_ExecuteFor(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r iter_name, start_expr, start_value, stop_expr, stop_value, body, loop_frame;
   int i, start, stop;

   assert(self);
   assert(frame);
   assert(statement);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_FOR);

   iter_name = SdAst_For_VariableName(statement);
   start_expr = SdAst_For_StartExpr(statement);
   stop_expr = SdAst_For_StopExpr(statement);
   body = SdAst_For_Body(statement);

   /* evaluate the FROM expression */
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, start_expr, &start_value)))
      return result;
   if (SdValue_Type(start_value) != SdType_INT)
      return SdFail(SdErr_TYPE_MISMATCH, "FOR...FROM expression does not evaluate to an Integer.");
   start = SdValue_GetInt(start_value);

   /* evaluate the TO expression */
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, stop_expr, &stop_value)))
      return result;
   if (SdValue_Type(stop_value) != SdType_INT)
      return SdFail(SdErr_TYPE_MISMATCH, "FOR...TO expression does not evaluate to an Integer.");
   stop = SdValue_GetInt(stop_value);

   /* execute the body in a new frame for each iteration */
   for (i = start; i <= stop; i++) {
      loop_frame = SdEnv_Frame_New(self->env, frame);
      if (SdFailed(result = SdEnv_DeclareVar(self->env, loop_frame, iter_name, SdEnv_BoxInt(self->env, i))))
         return result;
      *out_return = NULL;
      if (SdFailed(result = SdEngine_ExecuteBody(self, loop_frame, body, out_return)))
         return result;
      if (*out_return) /* a return statement inside the loop will break from the loop */
         return result;
   }

   return result;
}

SdResult SdEngine_ExecuteForEach(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r iter_name, index_name, haystack_expr, haystack_value, body, loop_frame, iter_value;
   SdList_r haystack;
   size_t i, count;

   assert(self);
   assert(frame);
   assert(statement);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_FOREACH);

   iter_name = SdAst_ForEach_IterName(statement);
   index_name = SdAst_ForEach_IndexName(statement);
   haystack_expr = SdAst_ForEach_HaystackExpr(statement);
   body = SdAst_ForEach_Body(statement);

   /* evaluate the IN expression */
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, haystack_expr, &haystack_value)))
      return result;
   if (SdValue_Type(haystack_value) != SdType_LIST)
      return SdFail(SdErr_TYPE_MISMATCH, "FOR...IN expression does not evaluate to a List.");
   haystack = SdValue_GetList(haystack_value);

   /* enumerate the list */
   count = SdList_Count(haystack);
   for (i = 0; i < count; i++) {
      iter_value = SdList_GetAt(haystack, i);
      loop_frame = SdEnv_Frame_New(self->env, frame);
      if (SdFailed(result = SdEnv_DeclareVar(self->env, loop_frame, iter_name, iter_value)))
         return result;
      if (SdValue_Type(index_name) != SdType_NIL) { /* user may not have specified an indexer variable */
         if (SdFailed(result = SdEnv_DeclareVar(self->env, loop_frame, index_name, SdEnv_BoxInt(self->env, i))))
            return result;
      }
      *out_return = NULL;
      if (SdFailed(result = SdEngine_ExecuteBody(self, loop_frame, body, out_return)))
         return result;
      if (*out_return) /* a return statement inside the loop will break from the loop */
         return result;
   }

   return result;
}

SdResult SdEngine_ExecuteWhile(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr, value, body, loop_frame;

   assert(self);
   assert(frame);
   assert(statement);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_WHILE);

   expr = SdAst_While_ConditionExpr(statement);
   body = SdAst_While_Body(statement);

   while (SdTrue) {
      if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
         return result;
      if (SdValue_Type(value) != SdType_BOOL)
         return SdFail(SdErr_TYPE_MISMATCH, "WHILE expression does not evaluate to a Boolean.");
      if (!SdValue_GetBool(value))
         break;

      loop_frame = SdEnv_Frame_New(self->env, frame);
      *out_return = NULL;
      if (SdFailed(result = SdEngine_ExecuteBody(self, loop_frame, body, out_return)))
         return result;
      if (*out_return) /* a return statement inside the loop will break from the loop */
         return result;
   }

   return result;
}

SdResult SdEngine_ExecuteDo(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr, value, body, loop_frame;

   assert(self);
   assert(frame);
   assert(statement);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_DO);

   expr = SdAst_Do_ConditionExpr(statement);
   body = SdAst_Do_Body(statement);

   while (SdTrue) {
      loop_frame = SdEnv_Frame_New(self->env, frame);
      *out_return = NULL;
      if (SdFailed(result = SdEngine_ExecuteBody(self, loop_frame, body, out_return)))
         return result;
      if (*out_return) /* a return statement inside the loop will break from the loop */
         return result;

      if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
         return result;
      if (SdValue_Type(value) != SdType_BOOL)
         return SdFail(SdErr_TYPE_MISMATCH, "WHILE expression does not evaluate to a Boolean.");
      if (!SdValue_GetBool(value))
         break;
   }

   return result;
}

SdResult SdEngine_ExecuteSwitch(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr, value, cas, case_expr, case_value, case_body, case_frame, default_body;
   SdList_r cases;
   size_t i, count;

   assert(self);
   assert(frame);
   assert(statement);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_SWITCH);

   expr = SdAst_Switch_Expr(statement);
   cases = SdAst_Switch_Cases(statement);
   default_body = SdAst_Switch_DefaultBody(statement);

   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
      return result;

   count = SdList_Count(cases);
   for (i = 0; i < count; i++) {
      cas = SdList_GetAt(cases, i);
      case_expr = SdAst_Case_Expr(cas);
      if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, case_expr, &case_value)))
         return result;
      if (SdValue_Equals(value, case_value)) {
         case_body = SdAst_Case_Body(cas);
         case_frame = SdEnv_Frame_New(self->env, frame);
         return SdEngine_ExecuteBody(self, case_frame, case_body, out_return);
      }
   }

   case_frame = SdEnv_Frame_New(self->env, frame);
   return SdEngine_ExecuteBody(self, case_frame, default_body, out_return);
}

SdResult SdEngine_ExecuteReturn(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   assert(self);
   assert(frame);
   assert(statement);
   assert(out_return);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_RETURN);

   return SdEngine_EvaluateExpr(self, frame, SdAst_Return_Expr(statement), out_return);
}

SdResult SdEngine_ExecuteDie(SdEngine_r self, SdValue_r frame, SdValue_r statement) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr, value;

   assert(self);
   assert(frame);
   assert(statement);
   assert(SdAst_NodeType(frame) == SdNodeType_FRAME);
   assert(SdAst_NodeType(statement) == SdNodeType_DIE);

   expr = SdAst_Die_Expr(statement);
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
      return result;
   if (SdValue_Type(value) != SdType_STRING)
      return SdFail(SdErr_TYPE_MISMATCH, "DIE expression does not evaluate to a String.");

   return SdFail(SdErr_DIED, SdString_CStr(SdValue_GetString(value)));
}

SdResult SdEngine_CallIntrinsic(SdEngine_r self, SdString_r name, SdList_r arguments, SdValue_r* out_return) {
   const char* cstr;
   
   assert(self);
   assert(name);
   assert(arguments);
   assert(out_return);

   cstr = SdString_CStr(name);

#define INTRINSIC(f_name, f) do { if (strcmp(cstr, f_name) == 0) return f(self, arguments, out_return); } while (0)

   switch (cstr[0]) {
      case 'a':
         INTRINSIC("asin", SdEngine_Intrinsic_ASin);
         INTRINSIC("acos", SdEngine_Intrinsic_ACos);
         INTRINSIC("atan", SdEngine_Intrinsic_ATan);
         INTRINSIC("atan2", SdEngine_Intrinsic_ATan2);
         INTRINSIC("and", SdEngine_Intrinsic_And);
         break;

      case 'b':
         INTRINSIC("bitwise-and", SdEngine_Intrinsic_BitwiseAnd);
         INTRINSIC("bitwise-or", SdEngine_Intrinsic_BitwiseOr);
         INTRINSIC("bitwise-xor", SdEngine_Intrinsic_BitwiseXor);
         break;

      case 'c':
         INTRINSIC("ceil", SdEngine_Intrinsic_Ceil);
         INTRINSIC("cos", SdEngine_Intrinsic_Cos);
         INTRINSIC("cosh", SdEngine_Intrinsic_CosH);
         break;

      case 'e':
         INTRINSIC("exp", SdEngine_Intrinsic_Exp);
         break;

      case 'f':
         INTRINSIC("floor", SdEngine_Intrinsic_Floor);
         break;

      case 'h':
         INTRINSIC("hash", SdEngine_Intrinsic_Hash);
         break;

      case 'l':
         INTRINSIC("log", SdEngine_Intrinsic_Log);
         INTRINSIC("log10", SdEngine_Intrinsic_Log10);
         INTRINSIC("list", SdEngine_Intrinsic_List);
         INTRINSIC("list.length", SdEngine_Intrinsic_ListLength);
         INTRINSIC("list.get-at", SdEngine_Intrinsic_ListGetAt);
         INTRINSIC("list.set-at!", SdEngine_Intrinsic_ListSetAt);
         INTRINSIC("list.insert-at!", SdEngine_Intrinsic_ListInsertAt);
         INTRINSIC("list.remove-at!", SdEngine_Intrinsic_ListRemoveAt);
         break;

      case 'n':
         INTRINSIC("not", SdEngine_Intrinsic_Not);
         break;

      case 'o':
         INTRINSIC("or", SdEngine_Intrinsic_Or);
         break;

      case 'p':
         INTRINSIC("print", SdEngine_Intrinsic_Print);
         break;

      case 's':
         INTRINSIC("sin", SdEngine_Intrinsic_Sin);
         INTRINSIC("sinh", SdEngine_Intrinsic_SinH);
         INTRINSIC("sqrt", SdEngine_Intrinsic_Sqrt);
         INTRINSIC("string.length", SdEngine_Intrinsic_StringLength);
         INTRINSIC("string.get-at", SdEngine_Intrinsic_StringGetAt);
         break;

      case 't':
         INTRINSIC("tan", SdEngine_Intrinsic_Tan);
         INTRINSIC("tanh", SdEngine_Intrinsic_TanH);
         INTRINSIC("to-string", SdEngine_Intrinsic_ToString);
         INTRINSIC("type-of", SdEngine_Intrinsic_TypeOf);
         break;

      case '+':
         INTRINSIC("+", SdEngine_Intrinsic_Add);
         break;

      case '-':
         INTRINSIC("-", SdEngine_Intrinsic_Subtract);
         break;

      case '*':
         INTRINSIC("*", SdEngine_Intrinsic_Multiply);
         INTRINSIC("**", SdEngine_Intrinsic_Pow);
         break;

      case '/':
         INTRINSIC("/", SdEngine_Intrinsic_Divide);
         break;

      case '%':
         INTRINSIC("%", SdEngine_Intrinsic_Modulus);
         break;

      case '=':
         INTRINSIC("=", SdEngine_Intrinsic_Equals);
         break;

      case '<':
         INTRINSIC("<", SdEngine_Intrinsic_LessThan);
         INTRINSIC("<=", SdEngine_Intrinsic_LessThanEquals);
         INTRINSIC("<<", SdEngine_Intrinsic_ShiftLeft);
         break;

      case '>':
         INTRINSIC(">", SdEngine_Intrinsic_GreaterThan);
         INTRINSIC(">=", SdEngine_Intrinsic_GreaterThanEquals);
         INTRINSIC(">>", SdEngine_Intrinsic_ShiftRight);
         break;
   }
   
#undef INTRINSIC

   return SdFailWithStringSuffix(SdErr_UNDECLARED_VARIABLE, "Not an intrinsic function: ", name);
}

SdResult SdEngine_Args1(SdList_r arguments, SdValue_r* out_a, SdType* out_a_type) {
   assert(arguments);
   assert(out_a);
   assert(out_a_type);

   if (SdList_Count(arguments) != 1)
      return SdFail(SdErr_ARGUMENT_MISMATCH, "Expected 1 argument.");
   *out_a = SdList_GetAt(arguments, 0);
   *out_a_type = SdValue_Type(*out_a);
   return SdResult_SUCCESS;
}

SdResult SdEngine_Args2(SdList_r arguments, SdValue_r* out_a, SdType* out_a_type, SdValue_r* out_b, 
   SdType* out_b_type) {
   assert(arguments);
   assert(out_a);
   assert(out_a_type);
   assert(out_b);
   assert(out_b_type);

   if (SdList_Count(arguments) != 2)
      return SdFail(SdErr_ARGUMENT_MISMATCH, "Expected 2 arguments.");
   *out_a = SdList_GetAt(arguments, 0);
   *out_a_type = SdValue_Type(*out_a);
   *out_b = SdList_GetAt(arguments, 1);
   *out_b_type = SdValue_Type(*out_b);
   return SdResult_SUCCESS;
}

SdResult SdEngine_Args3(SdList_r arguments, SdValue_r* out_a, SdType* out_a_type, SdValue_r* out_b, 
   SdType* out_b_type, SdValue_r* out_c, SdType* out_c_type) {
   assert(arguments);
   assert(out_a);
   assert(out_a_type);
   assert(out_b);
   assert(out_b_type);
   assert(out_c);
   assert(out_c_type);

   if (SdList_Count(arguments) != 3)
      return SdFail(SdErr_ARGUMENT_MISMATCH, "Expected 3 arguments.");
   *out_a = SdList_GetAt(arguments, 0);
   *out_a_type = SdValue_Type(*out_a);
   *out_b = SdList_GetAt(arguments, 1);
   *out_b_type = SdValue_Type(*out_b);
   *out_c = SdList_GetAt(arguments, 2);
   *out_c_type = SdValue_Type(*out_c);
   return SdResult_SUCCESS;
}

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_TypeOf)
   *out_return = SdEnv_BoxInt(self->env, a_type);
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_Hash)
   *out_return = SdEnv_BoxInt(self->env, SdValue_Hash(a_val));
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_ToString)
   switch (a_type) {
      case SdType_NIL: 
         *out_return = SdEnv_BoxString(self->env, SdString_FromCStr("(nil)"));
         break;
      case SdType_INT: {
         char buf[30];
         sprintf(buf, "%d", SdValue_GetInt(a_val));
         *out_return = SdEnv_BoxString(self->env, SdString_FromCStr(buf));
         break;
      }
      case SdType_DOUBLE: {
         char buf[500];
         sprintf(buf, "%f", SdValue_GetDouble(a_val));
         *out_return = SdEnv_BoxString(self->env, SdString_FromCStr(buf));
         break;
      }
      case SdType_BOOL:
         *out_return = SdEnv_BoxString(self->env, SdString_FromCStr(SdValue_GetBool(a_val) ? "true" : "false"));
         break;
      case SdType_STRING:
         *out_return = a_val;
         break;
      case SdType_LIST:
         *out_return = SdEnv_BoxString(self->env, SdString_FromCStr("(list)"));
         break;
      default:
         return SdFail(SdErr_INTERPRETER_BUG, "Unexpected type.");
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS2(SdEngine_Intrinsic_Add)
   if (a_type != b_type)
      return SdFail(SdErr_TYPE_MISMATCH, "Arguments must have the same type.");
   switch (a_type) {
      case SdType_INT:
         *out_return = SdEnv_BoxInt(self->env, SdValue_GetInt(a_val) + SdValue_GetInt(b_val));
         break;
      case SdType_DOUBLE:
         *out_return = SdEnv_BoxDouble(self->env, SdValue_GetDouble(a_val) + SdValue_GetDouble(b_val));
         break;
      case SdType_STRING: {
         SdStringBuf* buf = SdStringBuf_New();
         SdStringBuf_AppendString(buf, SdValue_GetString(a_val));
         SdStringBuf_AppendString(buf, SdValue_GetString(b_val));
         *out_return = SdEnv_BoxString(self->env, SdString_FromCStr(SdStringBuf_CStr(buf)));
         SdStringBuf_Delete(buf);
         break;
      }
      case SdType_LIST: {
         SdList* list;
         SdList_r a_list, b_list;
         size_t i, count;

         list = SdList_New();

         a_list = SdValue_GetList(a_val);
         count = SdList_Count(a_list);
         for (i = 0; i < count; i++)
            SdList_Append(list, SdList_GetAt(a_list, i));

         b_list = SdValue_GetList(b_val);
         count = SdList_Count(b_list);
         for (i = 0; i < count; i++)
            SdList_Append(list, SdList_GetAt(b_list, i));

         *out_return = SdEnv_BoxList(self->env, list);
         break;
      }
      default: {}
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_INTDOUBLE2(SdEngine_Intrinsic_Subtract, a - b)
SdEngine_INTRINSIC_INTDOUBLE2(SdEngine_Intrinsic_Multiply, a * b)
SdEngine_INTRINSIC_INTDOUBLE2(SdEngine_Intrinsic_Divide, a / b)
SdEngine_INTRINSIC_INT2(SdEngine_Intrinsic_Modulus, a % b)
SdEngine_INTRINSIC_INT2(SdEngine_Intrinsic_BitwiseAnd, a & b)
SdEngine_INTRINSIC_INT2(SdEngine_Intrinsic_BitwiseOr, a | b)
SdEngine_INTRINSIC_INT2(SdEngine_Intrinsic_BitwiseXor, a ^ b)
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_Sin, sin(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_Cos, cos(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_Tan, tan(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_ASin, asin(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_ACos, acos(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_ATan, atan(a))
SdEngine_INTRINSIC_DOUBLE2(SdEngine_Intrinsic_ATan2, atan2(a, b))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_SinH, sinh(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_CosH, cosh(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_TanH, tanh(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_Exp, exp(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_Log, log(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_Log10, log10(a))
SdEngine_INTRINSIC_DOUBLE2(SdEngine_Intrinsic_Pow, pow(a, b))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_Sqrt, sqrt(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_Ceil, ceil(a))
SdEngine_INTRINSIC_DOUBLE1(SdEngine_Intrinsic_Floor, floor(a))
SdEngine_INTRINSIC_BOOL2(SdEngine_Intrinsic_And, a && b)
SdEngine_INTRINSIC_BOOL2(SdEngine_Intrinsic_Or, a || b)
SdEngine_INTRINSIC_BOOL1(SdEngine_Intrinsic_Not, !a)
SdEngine_INTRINSIC_VALUE2(SdEngine_Intrinsic_Equals, SdEnv_BoxBool(self->env, SdValue_Equals(a, b)))
SdEngine_INTRINSIC_INTDOUBLESTRING2_BOOL(SdEngine_Intrinsic_LessThan, a < b, 
   strcmp(SdString_CStr(a), SdString_CStr(b)) < 0)
SdEngine_INTRINSIC_INTDOUBLESTRING2_BOOL(SdEngine_Intrinsic_LessThanEquals, a <= b, 
   strcmp(SdString_CStr(a), SdString_CStr(b)) <= 0)
SdEngine_INTRINSIC_INTDOUBLESTRING2_BOOL(SdEngine_Intrinsic_GreaterThan, a > b, 
   strcmp(SdString_CStr(a), SdString_CStr(b)) > 0)
SdEngine_INTRINSIC_INTDOUBLESTRING2_BOOL(SdEngine_Intrinsic_GreaterThanEquals, a >= b, 
   strcmp(SdString_CStr(a), SdString_CStr(b)) >= 0)
SdEngine_INTRINSIC_INT2(SdEngine_Intrinsic_ShiftLeft, a << b)
SdEngine_INTRINSIC_INT2(SdEngine_Intrinsic_ShiftRight, a >> b)

SdResult SdEngine_Intrinsic_List(SdEngine_r self, SdList_r arguments, SdValue_r* out_return) {
   assert(self);
   assert(arguments);
   assert(out_return);

   *out_return = SdEnv_BoxList(self->env, SdList_Clone(arguments));
   return SdResult_SUCCESS;
}

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_ListLength)
   if (a_type == SdType_LIST) {
      *out_return = SdEnv_BoxInt(self->env, SdList_Count(SdValue_GetList(a_val)));
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS2(SdEngine_Intrinsic_ListGetAt)
   SdUnreferenced(self);
   if (a_type == SdType_LIST && b_type == SdType_INT) {
      SdList_r a_list = SdValue_GetList(a_val);
      int b_int = SdValue_GetInt(b_val);
      if (b_int < 0 || (size_t)b_int >= SdList_Count(a_list))
         return SdFail(SdErr_ARGUMENT_OUT_OF_RANGE, "Index is out of range.");
      *out_return = SdList_GetAt(a_list, b_int);
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS3(SdEngine_Intrinsic_ListSetAt)
   SdUnreferenced(self);
   if (a_type == SdType_LIST && b_type == SdType_INT) {
      SdList_r a_list = SdValue_GetList(a_val);
      int b_int = SdValue_GetInt(b_val);
      if (b_int < 0 || (size_t)b_int >= SdList_Count(a_list))
         return SdFail(SdErr_ARGUMENT_OUT_OF_RANGE, "Index is out of range.");
      SdList_SetAt(a_list, b_int, c_val);
      *out_return = c_val;
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS3(SdEngine_Intrinsic_ListInsertAt)
   SdUnreferenced(self);
   if (a_type == SdType_LIST && b_type == SdType_INT) {
      SdList_r a_list;
      int b_int;

      a_list = SdValue_GetList(a_val);
      b_int = SdValue_GetInt(b_val);
      if (b_int < 0 || (size_t)b_int > SdList_Count(a_list))
         return SdFail(SdErr_ARGUMENT_OUT_OF_RANGE, "Index is out of range.");
      SdList_InsertAt(a_list, b_int, c_val);
      *out_return = c_val;
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS2(SdEngine_Intrinsic_ListRemoveAt)
   SdUnreferenced(self);
   if (a_type == SdType_LIST && b_type == SdType_INT) {
      SdList_r a_list = SdValue_GetList(a_val);
      int b_int = SdValue_GetInt(b_val);
      if (b_int < 0 || (size_t)b_int >= SdList_Count(a_list))
         return SdFail(SdErr_ARGUMENT_OUT_OF_RANGE, "Index is out of range.");
      *out_return = SdList_RemoveAt(a_list, b_int);
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_StringLength)
   if (a_type == SdType_STRING) {
      *out_return = SdEnv_BoxInt(self->env, SdString_Length(SdValue_GetString(a_val)));
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS2(SdEngine_Intrinsic_StringGetAt)
   if (a_type == SdType_STRING && b_type == SdType_INT) {
      char char_str[2];
      SdString_r a_str;
      int b_int;

      a_str = SdValue_GetString(a_val);
      b_int = SdValue_GetInt(b_val);
      if (b_int < 0 || (size_t)b_int >= SdString_Length(a_str))
         return SdFail(SdErr_ARGUMENT_OUT_OF_RANGE, "Index is out of range.");
      char_str[0] = SdString_CStr(a_str)[b_int];
      char_str[1] = 0;
      *out_return = SdEnv_BoxString(self->env, SdString_FromCStr(char_str));
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_Print)
   SdUnreferenced(self);
   if (a_type == SdType_STRING) {
      printf("%s", SdString_CStr(SdValue_GetString(a_val)));
      *out_return = a_val;
   }
SdEngine_INTRINSIC_END

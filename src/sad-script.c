/* Sad-Script
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

/* Debugging options:
   define SD_DEBUG_ALL to enable all debugging code (requires Visual C++). this implies all the flags below:
   define SD_DEBUG_GC to run the garbage collector at every opportunity, catch double-boxing.
   define SD_DEBUG_MEMUSE to track the total number and size of allocations
   define SD_DEBUG_MSVC to enable non-portable Visual C++ memory leak detection.
   define SD_DEBUG_GCC to enablbe non-portable GCC debugging.
   define NDEBUG to disable assertions. 
*/

#ifdef __cplusplus
#error sad-script.c must be compiled as C language, not C++.
#endif

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#pragma warning(push, 0) /* ignore warnings in system headers */
#endif

#if defined(SD_DEBUG_MSVC) || defined(SD_DEBUG_ALL)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#ifdef SD_DEBUG_GCC
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#include <assert.h>
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
#pragma warning(disable: 4711) /* function '...' selected for automatic inline expansion */
#endif

/* run the garbage collector after we've allocated this many bytes since the last GC. 67,108,864 bytes = 64MB */
#define SdEngine_ALLOCATED_BYTES_PER_GC 67108864

/* these constants define the number of item structs to fit on each page in the slab allocator. the values are chosen
   so that they yield pages that are just under 1MB. assuming that calloc rounds allocations up to the nearest power of
   two, this should waste very little memory. 1,048,576 bytes = 1MB */
#define SdValuePage_ITEMS_PER_PAGE (sizeof(void*) == 8 ? 32767 : 52428)
   /* 64-bit pages are 1,048,568 bytes. 32-bit pages are 1,048,572 bytes. */
#define SdListPage_ITEMS_PER_PAGE (sizeof(void*) == 8 ? 43689 : 87380)
   /* 64-bit pages are 1,048,560 bytes. 32-bit pages are 1,048,572 bytes. */
#define Sd1ElementArrayPage_ITEMS_PER_PAGE (sizeof(void*) == 8 ? 65534 : 131070)
   /* 64-bit pages are 1,048,568 bytes. 32-bit pages are 1,048,572 bytes. */
#define Sd2ElementArrayPage_ITEMS_PER_PAGE (sizeof(void*) == 8 ? 43689 : 87380)
   /* 64-bit pages are 1,048,560 bytes. 32-bit pages are 1,048,572 bytes. */
#define Sd3ElementArrayPage_ITEMS_PER_PAGE (sizeof(void*) == 8 ? 32767 : 65535)
   /* 64-bit pages are 1,048,568 bytes. 32-bit pages are 1,048,572 bytes. */
#define Sd4ElementArrayPage_ITEMS_PER_PAGE (sizeof(void*) == 8 ? 26213 : 52428)
   /* 64-bit pages are 1,048,544 bytes. 32-bit pages are 1,048,572 bytes. */

/*********************************************************************************************************************/
typedef struct SdValuePage_s SdValuePage;
typedef struct SdValuePage_s* SdValuePage_r;
typedef struct SdListPage_s SdListPage;
typedef struct SdListPage_s* SdListPage_r;
typedef struct Sd1ElementArray_s Sd1ElementArray;
typedef struct Sd1ElementArray_s* Sd1ElementArray_r;
typedef struct Sd2ElementArray_s Sd2ElementArray;
typedef struct Sd2ElementArray_s* Sd2ElementArray_r;
typedef struct Sd3ElementArray_s Sd3ElementArray;
typedef struct Sd3ElementArray_s* Sd3ElementArray_r;
typedef struct Sd4ElementArray_s Sd4ElementArray;
typedef struct Sd4ElementArray_s* Sd4ElementArray_r;
typedef struct Sd1ElementArrayPage_s Sd1ElementArrayPage;
typedef struct Sd1ElementArrayPage_s* Sd1ElementArrayPage_r;
typedef struct Sd2ElementArrayPage_s Sd2ElementArrayPage;
typedef struct Sd2ElementArrayPage_s* Sd2ElementArrayPage_r;
typedef struct Sd3ElementArrayPage_s Sd3ElementArrayPage;
typedef struct Sd3ElementArrayPage_s* Sd3ElementArrayPage_r;
typedef struct Sd4ElementArrayPage_s Sd4ElementArrayPage;
typedef struct Sd4ElementArrayPage_s* Sd4ElementArrayPage_r;
typedef struct SdScannerNode_s SdScannerNode;
typedef struct SdScannerNode_s* SdScannerNode_r;

typedef union SdValueUnion_u {
   int int_value;
   SdString* string_value;
   SdBool bool_value;
   double double_value;
   SdList* list_value;
} SdValueUnion;

typedef union SdListValuesUnion_u {
   Sd1ElementArray* array_1;
   Sd2ElementArray* array_2;
   Sd3ElementArray* array_3;
   Sd4ElementArray* array_4;
   SdValue_r* array_n;
} SdListValuesUnion;

struct Sad_s {
   SdEnv* env;
   SdEngine* engine;
};

struct SdString_s {
   char* buffer; /* includes null terminator */
   size_t length; /* not including null terminator */
#if defined(SD_DEBUG_ALL) || defined(SD_DEBUG_GC)
   SdBool is_boxed; /* whether this string has been boxed already */
#endif
};

struct SdStringBuf_s {
   char* str;
   size_t len;
};

struct SdValue_s {
   SdType type;
   SdValueUnion payload;
   SdBool gc_mark; /* used by the mark-and-sweep garbage collector */
};

struct SdList_s {
   size_t count;
   SdListValuesUnion values;
#if defined(SD_DEBUG_ALL) || defined(SD_DEBUG_GC)
   SdBool is_boxed; /* whether this list has been boxed already */
#endif
};

struct Sd1ElementArray_s {
   SdValue_r elements[1];
};

struct Sd2ElementArray_s {
   SdValue_r elements[2];
};

struct Sd3ElementArray_s {
   SdValue_r elements[3];
};

struct Sd4ElementArray_s {
   SdValue_r elements[4];
};

struct SdEnv_s {
   SdValue_r root; /* contains all living/connected objects */
   SdChain* values_chain; /* contains all objects that haven't been deleted yet */
   SdValueSet* active_frames; /* contains all currently active frames in the interpreter engine */
   SdChain* call_stack; /* information about each call in the call stack */
   SdChain* protected_values; /* internal interpreter values that we don't want to get GC'd right this moment */
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
};

#define SdSlabAllocator_DEFINE_PAGE_STRUCT(struct_name, item_type, items_per_page) \
   struct struct_name { \
      item_type values[items_per_page]; \
      struct struct_name* next_page; \
      size_t next_unused_index; \
      item_type* free_ptrs[items_per_page]; \
      size_t num_free_ptrs; \
   }

SdSlabAllocator_DEFINE_PAGE_STRUCT(SdValuePage_s, SdValue, SdValuePage_ITEMS_PER_PAGE);
SdSlabAllocator_DEFINE_PAGE_STRUCT(SdListPage_s, SdList, SdListPage_ITEMS_PER_PAGE);
SdSlabAllocator_DEFINE_PAGE_STRUCT(Sd1ElementArrayPage_s, Sd1ElementArray, Sd1ElementArrayPage_ITEMS_PER_PAGE);
SdSlabAllocator_DEFINE_PAGE_STRUCT(Sd2ElementArrayPage_s, Sd2ElementArray, Sd2ElementArrayPage_ITEMS_PER_PAGE);
SdSlabAllocator_DEFINE_PAGE_STRUCT(Sd3ElementArrayPage_s, Sd3ElementArray, Sd3ElementArrayPage_ITEMS_PER_PAGE);
SdSlabAllocator_DEFINE_PAGE_STRUCT(Sd4ElementArrayPage_s, Sd4ElementArray, Sd4ElementArrayPage_ITEMS_PER_PAGE);

#undef SdSlabAllocator_DEFINE_PAGE_STRUCT

static char* SdStrdup(const char* src);
static void* SdUnreferenced(void* x);
static void SdExit(const char* message);
static const char* SdType_Name(SdType x);

static SdValue* SdAllocValue(void);
static void SdFreeValue(SdValue* x);
static SdList* SdAllocList(void);
static void SdFreeList(SdList* x);
static Sd1ElementArray* SdAlloc1ElementArray(void);
static void SdFree1ElementArray(Sd1ElementArray* x);
static Sd2ElementArray* SdAlloc2ElementArray(void);
static void SdFree2ElementArray(Sd2ElementArray* x);
static Sd3ElementArray* SdAlloc3ElementArray(void);
static void SdFree3ElementArray(Sd3ElementArray* x);
static Sd4ElementArray* SdAlloc4ElementArray(void);
static void SdFree4ElementArray(Sd4ElementArray* x);

static SdSearchResult SdEnv_BinarySearchByName(SdList_r list, SdString_r name);
static int SdEnv_BinarySearchByName_CompareFunc(SdValue_r lhs, void* context);
static SdBool SdEnv_InsertByName(SdList_r list, SdValue_r item);
static void SdEnv_CollectGarbage_MarkConnectedValues(SdValue_r root);
static SdValue_r SdEnv_FindVariableSlotInFrame(SdString_r name, SdValue_r frame, int* out_index_in_frame);

static SdValue_r SdAst_NodeValue(SdValue_r node, size_t value_index);
static SdValue_r SdAst_NewNode(SdEnv_r env, SdValue_r values[], size_t num_values);
static SdValue_r SdAst_NewFunctionNode(SdEnv_r env, SdValue_r values[], size_t num_values);

static int SdValueSet_CompareFunc(SdValue_r lhs, void* context);

static void SdScanner_AppendToken(SdScanner_r self, int source_line, const char* token_text);
static SdTokenType SdScanner_ClassifyToken(const char* text);
static SdBool SdScanner_IsDoubleLit(const char* text);
static SdBool SdScanner_IsIntLit(const char* text);

static SdResult SdParser_Fail(SdErr code, SdToken_r token, const char* message);
static SdResult SdParser_FailEof(void);
static SdResult SdParser_FailType(SdToken_r token, SdTokenType expected_type, SdTokenType actual_type);
static const char* SdParser_TypeString(SdTokenType type);
static SdResult SdParser_ReadExpectType(SdScanner_r scanner, SdTokenType expected_type, SdToken_r* out_token);
static SdResult SdParser_ParseFunction(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseParameter(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseBody(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseExpr(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
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
static SdResult SdParser_ParseSwitchCase(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseMatch(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseMatchCase(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseReturn(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);
static SdResult SdParser_ParseDie(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node);

static SdResult SdEngine_CallClosure(SdEngine_r self, SdValue_r frame, SdValue_r closure, SdList_r arguments, 
   SdValue_r* out_return);
static SdResult SdEngine_EvaluateExpr(SdEngine_r self, SdValue_r frame, SdValue_r expr, SdValue_r* out_value);
static SdResult SdEngine_EvaluateVarRef(SdEngine_r self, SdValue_r frame, SdValue_r var_ref, SdValue_r* out_value);
static SdResult SdEngine_EvaluateFunction(SdEngine_r self, SdValue_r frame, SdValue_r function, SdValue_r* out_value);
static SdResult SdEngine_EvaluateMatch(SdEngine_r self, SdValue_r frame, SdValue_r match, SdValue_r* out_value);
static SdResult SdEngine_ExecuteBody(SdEngine_r self, SdValue_r frame, SdValue_r body, SdValue_r* out_return);
static SdResult SdEngine_ExecuteStatement(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteCall(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return);
static SdResult SdEngine_ExecuteVar(SdEngine_r self, SdValue_r frame, SdValue_r statement);
static SdResult SdEngine_ExecuteMultiVar(SdEngine_r self, SdValue_r frame, SdValue_r statement);
static SdResult SdEngine_ExecuteSet(SdEngine_r self, SdValue_r frame, SdValue_r statement);
static SdResult SdEngine_ExecuteMultiSet(SdEngine_r self, SdValue_r frame, SdValue_r statement);
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
static SdResult SdEngine_Intrinsic_ShiftLeft(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ShiftRight(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ListLength(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ListGetAt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ListSetAt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ListInsertAt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ListRemoveAt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_StringLength(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_StringGetAt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Print(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_Error(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_ErrorMessage(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_GetType(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_IntLessThan(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_DoubleLessThan(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_StringLessThan(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_IntToDouble(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_DoubleToInt(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);
static SdResult SdEngine_Intrinsic_StringJoin(SdEngine_r self, SdList_r arguments, SdValue_r* out_return);

/* Global variables */
SdResult SdResult_SUCCESS = { SdErr_SUCCESS };
static char SdResult_Message[500] = { 0 };
static SdValue SdValue_NIL = { SdType_NIL, { 0 }, SdFalse };
static SdValue SdValue_TRUE = { SdType_BOOL, { SdTrue }, SdFalse };
static SdValue SdValue_FALSE = { SdType_BOOL, { SdFalse }, SdFalse };
static SdValuePage* SdValuePage_FirstOpen = NULL;
static SdValuePage* SdValuePage_FirstFull = NULL;
static SdListPage* SdListPage_FirstOpen = NULL;
static SdListPage* SdListPage_FirstFull = NULL;
static Sd1ElementArrayPage* Sd1ElementArrayPage_FirstOpen = NULL;
static Sd1ElementArrayPage* Sd1ElementArrayPage_FirstFull = NULL;
static Sd2ElementArrayPage* Sd2ElementArrayPage_FirstOpen = NULL;
static Sd2ElementArrayPage* Sd2ElementArrayPage_FirstFull = NULL;
static Sd3ElementArrayPage* Sd3ElementArrayPage_FirstOpen = NULL;
static Sd3ElementArrayPage* Sd3ElementArrayPage_FirstFull = NULL;
static Sd4ElementArrayPage* Sd4ElementArrayPage_FirstOpen = NULL;
static Sd4ElementArrayPage* Sd4ElementArrayPage_FirstFull = NULL;
static size_t SdAlloc_BytesAllocatedSinceLastGc = 0;

/* Helpers ***********************************************************************************************************/
#define STRINGIFY(x) #x

#ifdef NDEBUG
#define SdAssert(x) ((void)0)
#define SdAssertValue(x,t) ((void)0)
#define SdAssertNode(x,t) ((void)0)
#define SdAssertExpr(x) ((void)0)
#define SdAssertAllValuesOfType(x,t) ((void)0)
#define SdAssertAllNodesOfTypes(x,l,h) ((void)0)
#define SdAssertAllNodesOfType(x,t) ((void)0)
#define SdAssertNonEmptyString(x) ((void)0)
#else

#ifdef SD_DEBUG_GCC
static void SdAssertCore(SdBool x, const char* condition) {
   void* array[10];
   size_t size;
   
   if (!x) {
      size = backtrace(array, 10);
      fprintf(stderr, "Assertion failed: %s\n", condition);
      backtrace_symbols_fd(array, size, STDERR_FILENO);
   }
   
   assert((x) != 0);
}
#define SdAssert(x) SdAssertCore((x) != 0, STRINGIFY(x))
#else
#define SdAssert(x) assert((x) != 0)
#endif

static void SdAssertValue(SdValue_r x, SdType t) {
   SdAssert(x);
   SdAssert(SdValue_Type(x) == t);
}

static void SdAssertNode(SdValue_r x, SdNodeType t) {
   SdAssert(SdAst_NodeType(x) == t);
}

static void SdAssertExpr(SdValue_r x) {
   SdAssertValue(x, SdType_LIST);
   SdAssert(SdAst_NodeType(x) >= SdNodeType_EXPRESSIONS_FIRST && SdAst_NodeType(x) <= SdNodeType_EXPRESSIONS_LAST);
}

static void SdAssertAllValuesOfType(SdList_r x, SdType t) {
   size_t i = 0, count = 0;
   SdAssert(x);
   count = SdList_Count(x);
   for (i = 0; i < count; i++) {
      SdValue_r value;
      value = SdList_GetAt(x, i);
      SdAssertValue(value, t);
   }
}

static void SdAssertAllNodesOfTypes(SdList_r x, SdNodeType l, SdNodeType h) {
   size_t i = 0, count = 0;
   SdAssert(x);
   SdAssert(l <= h);
   count = SdList_Count(x);
   for (i = 0; i < count; i++) {
      SdValue_r value = SdList_GetAt(x, i);
      SdAssertValue(value, SdType_LIST);
      SdAssert(SdAst_NodeType(value) >= l && SdAst_NodeType(value) <= h);
   }
}

static void SdAssertAllNodesOfType(SdList_r x, SdNodeType t) {
   SdAssertAllNodesOfTypes(x, t, t);
}

static void SdAssertNonEmptyString(SdString_r x) {
   SdAssert(x);
   SdAssert(SdString_Length(x) > 0);
}
#endif

#if defined(SD_DEBUG_MEMUSE) || defined(SD_DEBUG_ALL)
unsigned long sd_num_allocs = 0;
unsigned long sd_num_reallocs = 0;
#endif

void* SdAlloc(size_t size) {
   void* ptr = NULL;

   if (size == 0)
      size = 1;

#if defined(SD_DEBUG_MEMUSE) || defined(SD_DEBUG_ALL)
   sd_num_allocs++;
#endif
   
   ptr = calloc(1, size);
   if (!ptr) {
      char buf[1000];
      sprintf(buf, "calloc(1, %u) failed (SdAlloc)", (unsigned int)size);
      SdExit(buf);
   }

   SdAlloc_BytesAllocatedSinceLastGc += size;
   
   return ptr;
}

#define SdFree(ptr) free((ptr))

void* SdRealloc(void* ptr, size_t new_size, size_t old_size) {
   void* new_ptr = NULL;

   if (new_size == 0)
      new_size = 1;

   new_ptr = realloc(ptr, new_size);

#if defined(SD_DEBUG_MEMUSE) || defined(SD_DEBUG_ALL)
   sd_num_reallocs++;
#endif

   /* If there is not enough available memory to expand the block to the given size, the original block is left 
      unchanged, and NULL is returned. */
   if (!new_ptr)
      SdExit("realloc failed.");

   if (new_ptr != ptr) {
      /* realloc had to allocate a new buffer, copy everything over, and then free the old buffer. */
      SdAlloc_BytesAllocatedSinceLastGc -= old_size;
      SdAlloc_BytesAllocatedSinceLastGc += new_size;
   }
   
   return new_ptr;
}

static char* SdStrdup(const char* src) {
   size_t length = 0;
   char* dst = NULL;

   SdAssert(src);
   length = strlen(src);
   dst = SdAlloc(length + 1);
   memcpy(dst, src, length);
   return dst;
}

#define SdMin(a,b) ((a)<(b)?(a):(b))

static void* SdUnreferenced(void* x) {
   return x;
}

static void SdExit(const char* message) {
   fprintf(stderr, "FATAL ERROR: %s\n", message);
   exit(-1);
}

static const char* SdType_Name(SdType x) {
   switch (x) {
      case SdType_ANY: return "Any";
      case SdType_NIL: return "Nil";
      case SdType_INT: return "Int";
      case SdType_DOUBLE: return "Double";
      case SdType_BOOL: return "Bool";
      case SdType_STRING: return "String";
      case SdType_LIST: return "List";
      case SdType_FUNCTION: return "Function";
      case SdType_ERROR: return "Error";
      case SdType_TYPE: return "Type";
      default: SdAssert(SdFalse); return "unknown";
   }
}

/* SdSlabAllocator ***************************************************************************************************/
#define SdSlabAllocator_DEFINE_ALLOC_FUNC(name, page_type, item_type, first_open, first_full, items_per_page) \
   static item_type* name(void) { \
      page_type* page = NULL; \
      item_type* ptr = NULL; \
      \
      /* find a page with a slot free */ \
      page = first_open; \
      \
      /* if there's no open page, then create a new page */ \
      if (!page) { \
         page = SdAlloc(sizeof(page_type)); \
         first_open = page; \
      } \
      \
      /* if there's anything in the free list, then use that first because recycling is cool.  if not, then press \
       into service one of the slots that hasn't been used before. */ \
      if (page->num_free_ptrs > 0) { \
         /* remove it from the free list */ \
         ptr = page->free_ptrs[page->num_free_ptrs-- - 1]; \
         /* clean up after the last owner */ \
         memset(ptr, 0, sizeof(item_type)); \
      } else { \
         ptr = &page->values[page->next_unused_index++]; \
      } \
      \
      /* if this page is now full, then move it to the full list */ \
      if (page->num_free_ptrs == 0 && page->next_unused_index == items_per_page) { \
         first_open = page->next_page; \
         page->next_page = first_full; \
         first_full = page; \
      } \
      \
      return ptr; \
   }

SdSlabAllocator_DEFINE_ALLOC_FUNC(
   SdAllocValue, SdValuePage, SdValue, SdValuePage_FirstOpen, SdValuePage_FirstFull, SdValuePage_ITEMS_PER_PAGE)
SdSlabAllocator_DEFINE_ALLOC_FUNC(
   SdAllocList, SdListPage, SdList, SdListPage_FirstOpen, SdListPage_FirstFull, SdListPage_ITEMS_PER_PAGE)
SdSlabAllocator_DEFINE_ALLOC_FUNC(
   SdAlloc1ElementArray, Sd1ElementArrayPage, Sd1ElementArray, Sd1ElementArrayPage_FirstOpen,
   Sd1ElementArrayPage_FirstFull, Sd1ElementArrayPage_ITEMS_PER_PAGE)
SdSlabAllocator_DEFINE_ALLOC_FUNC(
   SdAlloc2ElementArray, Sd2ElementArrayPage, Sd2ElementArray, Sd2ElementArrayPage_FirstOpen,
   Sd2ElementArrayPage_FirstFull, Sd2ElementArrayPage_ITEMS_PER_PAGE)
SdSlabAllocator_DEFINE_ALLOC_FUNC(
   SdAlloc3ElementArray, Sd3ElementArrayPage, Sd3ElementArray, Sd3ElementArrayPage_FirstOpen,
   Sd3ElementArrayPage_FirstFull, Sd3ElementArrayPage_ITEMS_PER_PAGE)
SdSlabAllocator_DEFINE_ALLOC_FUNC(
   SdAlloc4ElementArray, Sd4ElementArrayPage, Sd4ElementArray, Sd4ElementArrayPage_FirstOpen,
   Sd4ElementArrayPage_FirstFull, Sd4ElementArrayPage_ITEMS_PER_PAGE)

#undef SdSlabAllocator_DEFINE_ALLOC_FUNC

#define SdSlabAllocator_DEFINE_FREE_FUNC(name, page_type, item_type, first_open, first_full, items_per_page) \
   static void name(item_type* x) { \
      page_type* page = NULL; \
      page_type* prev_page = NULL; \
      SdBool page_was_full = SdFalse; \
      \
      if (!x) \
         return; \
      \
      /* figure out which page this value belongs to */ \
      page = first_open; \
      while (page) { \
         if (x >= &page->values[0] && x < &page->values[items_per_page]) \
            break; /* it's on this page */ \
         prev_page = page; \
         page = page->next_page; \
      } \
      \
      if (!page) { \
         prev_page = NULL; \
         page = first_full; \
         while (page) { \
            if (x >= &page->values[0] && x < &page->values[items_per_page]) { \
               page_was_full = SdTrue; \
               break; /* it's on this page */ \
            } \
            prev_page = page; \
            page = page->next_page; \
         } \
      } \
      \
      if (!page) { \
         SdExit("Attempt to free a bogus pointer."); \
         return; \
      } \
      \
      /* add to the free list in this page */ \
      page->free_ptrs[page->num_free_ptrs++] = x; \
      \
      if (page_was_full) { \
         /* if this page was full, then move it to the open list */ \
         if (prev_page) \
            prev_page->next_page = page->next_page; \
         else \
            first_full = page->next_page; \
         page->next_page = first_open; \
         first_open = page; \
      } else if (page->num_free_ptrs == page->next_unused_index) { \
         /* if this page was open and is now empty, then we can remove this page */ \
         if (prev_page) \
            prev_page->next_page = page->next_page; \
         else \
            first_open = page->next_page; \
         SdFree(page); \
      } \
   }

SdSlabAllocator_DEFINE_FREE_FUNC(
   SdFreeValue, SdValuePage, SdValue, SdValuePage_FirstOpen, SdValuePage_FirstFull, SdValuePage_ITEMS_PER_PAGE)
SdSlabAllocator_DEFINE_FREE_FUNC(
   SdFreeList, SdListPage, SdList, SdListPage_FirstOpen, SdListPage_FirstFull, SdListPage_ITEMS_PER_PAGE)
SdSlabAllocator_DEFINE_FREE_FUNC(
   SdFree1ElementArray, Sd1ElementArrayPage, Sd1ElementArray, Sd1ElementArrayPage_FirstOpen,
   Sd1ElementArrayPage_FirstFull, Sd1ElementArrayPage_ITEMS_PER_PAGE)
SdSlabAllocator_DEFINE_FREE_FUNC(
   SdFree2ElementArray, Sd2ElementArrayPage, Sd2ElementArray, Sd2ElementArrayPage_FirstOpen,
   Sd2ElementArrayPage_FirstFull, Sd2ElementArrayPage_ITEMS_PER_PAGE)
SdSlabAllocator_DEFINE_FREE_FUNC(
   SdFree3ElementArray, Sd3ElementArrayPage, Sd3ElementArray, Sd3ElementArrayPage_FirstOpen,
   Sd3ElementArrayPage_FirstFull, Sd3ElementArrayPage_ITEMS_PER_PAGE)
SdSlabAllocator_DEFINE_FREE_FUNC(
   SdFree4ElementArray, Sd4ElementArrayPage, Sd4ElementArray, Sd4ElementArrayPage_FirstOpen,
   Sd4ElementArrayPage_FirstFull, Sd4ElementArrayPage_ITEMS_PER_PAGE)

#undef SdSlabAllocator_DEFINE_FREE_FUNC

/* SdResult **********************************************************************************************************/
SdResult SdFail(SdErr code, const char* message) {
   SdResult err;

   SdAssert(message);
   memset(&err, 0, sizeof(err));
   err.code = code;
   strncpy(SdResult_Message, message, sizeof(SdResult_Message) - 1);
   SdResult_Message[sizeof(SdResult_Message) - 1] = 0; /* strncpy won't add a null terminator at max length */
   return err;
}

SdResult SdFailWithStringSuffix(SdErr code, const char* message, SdString_r suffix) {
   SdStringBuf* buf = NULL;
   SdResult result = SdResult_SUCCESS;

   SdAssert(message);
   SdAssert(suffix);
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

const char* SdGetLastFailMessage(void) {
   return SdResult_Message;
}

/* Sad ***************************************************************************************************************/
SdErr SdRunScript(const char* prelude_file_path, const char* script_code) {
   SdResult result = SdResult_SUCCESS;
   SdString* prelude_file_path_str = NULL;
   SdString* prelude_code = NULL;
   Sad* sad = NULL;

   sad = Sad_New();
   prelude_file_path_str = SdString_FromCStr(prelude_file_path);
   if (SdFailed(result = SdFile_ReadAllText(prelude_file_path_str, &prelude_code)))
      goto end;
   if (SdFailed(result = Sad_AddScript(sad, SdString_CStr(prelude_code))))
      goto end;
   if (SdFailed(result = Sad_AddScript(sad, script_code)))
      goto end;
   result = Sad_Execute(sad);

end:
   if (SdFailed(result))
      fprintf(stderr, "ERROR: %s\n", SdGetLastFailMessage());
   else
      printf("\n");

   if (prelude_file_path_str) SdString_Delete(prelude_file_path_str);
   if (prelude_code) SdString_Delete(prelude_code);
   if (sad) Sad_Delete(sad);

   return result.code;
}

Sad* Sad_New(void) {
   Sad* self = SdAlloc(sizeof(Sad));
   self->env = SdEnv_New();
   self->engine = SdEngine_New(self->env);
   
   return self;
}

void Sad_Delete(Sad* self) {
   SdAssert(self);
   SdEnv_Delete(self->env);
   SdEngine_Delete(self->engine);
   SdFree(self);
}

SdResult Sad_AddScript(Sad_r self, const char* code) {
   SdValue_r program_node = NULL;
   SdResult result = SdResult_SUCCESS;

   SdAssert(self);
   SdAssert(code);
   if (SdFailed(result = SdParser_ParseProgram(self->env, code, &program_node)))
      return result;
   return SdEnv_AddProgramAst(self->env, program_node);
}

SdResult Sad_Execute(Sad_r self) {
   SdAssert(self);
   return SdEngine_ExecuteProgram(self->engine);
}

SdResult Sad_ExecuteScript(Sad_r self, const char* code) {
   SdValue_r program_node = NULL;
   SdResult result = SdResult_SUCCESS;

   SdAssert(self);
   SdAssert(code);
   if (SdFailed(result = SdParser_ParseProgram(self->env, code, &program_node)))
      return result;
   if (SdFailed(result = SdEnv_AddProgramAst(self->env, program_node)))
      return result;
   return SdEngine_ExecuteProgram(self->engine);
}

SdEnv_r Sad_Env(Sad_r self) {
   SdAssert(self);
   return self->env;
}

/* SdString **********************************************************************************************************/
SdString* SdString_New(void) {
   SdString* self = SdAlloc(sizeof(SdString));
   self->buffer = SdAlloc(sizeof(char)); /* one char for the null terminator */
   return self;
}

SdString* SdString_FromCStr(const char* cstr) {
   SdString* self = NULL;

   SdAssert(cstr);
   self = SdAlloc(sizeof(SdString));
   self->buffer = SdStrdup(cstr);
   self->length = strlen(cstr);
   return self;
}

void SdString_Delete(SdString* self) {
   SdAssert(self);
   SdFree(self->buffer);
   SdFree(self);
}

const char* SdString_CStr(SdString_r self) {
   SdAssert(self);
   return self->buffer;
}

SdBool SdString_Equals(SdString_r a, SdString_r b) {
   SdAssert(a);
   SdAssert(b);
   return a->length == b->length && strcmp(a->buffer, b->buffer) == 0;
}

SdBool SdString_EqualsCStr(SdString_r a, const char* b) {
   SdAssert(a);
   SdAssert(b);
   return strcmp(a->buffer, b) == 0;  
}

size_t SdString_Length(SdString_r self) {
   SdAssert(self);
   return self->length;
}

int SdString_Compare(SdString_r a, SdString_r b) {
   const char* a_str = NULL;
   const char* b_str = NULL;

   SdAssert(a);
   SdAssert(b);
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
   SdAssert(self);
   SdFree(self->str);
   SdFree(self);
}

void SdStringBuf_AppendString(SdStringBuf_r self, SdString_r suffix) {
   SdAssert(self);
   SdAssert(suffix);
   SdStringBuf_AppendCStr(self, SdString_CStr(suffix));
}

void SdStringBuf_AppendCStr(SdStringBuf_r self, const char* suffix) {
   size_t old_len = 0, suffix_len = 0, new_len = 0;

   SdAssert(self);
   SdAssert(suffix);
   old_len = self->len;
   suffix_len = strlen(suffix);
   new_len = old_len + suffix_len;

   self->str = SdRealloc(self->str, new_len + 1, old_len + 1);
   SdAssert(self->str);
   memcpy(&self->str[old_len], suffix, suffix_len);
   self->str[new_len] = 0;
   self->len = new_len;
}

void SdStringBuf_AppendChar(SdStringBuf_r self, char ch) {
   size_t old_len = 0, new_len = 0;

   SdAssert(self);
   SdAssert(ch != 0);
   old_len = self->len;
   new_len = old_len + 1;

   self->str = SdRealloc(self->str, new_len + 1, old_len + 1);
   SdAssert(self->str);
   self->str[old_len] = ch;
   self->str[new_len] = 0;
   self->len = new_len;
}

void SdStringBuf_AppendInt(SdStringBuf_r self, int number) {
   char number_buf[30] = { 0 };

   SdAssert(self);
   memset(number_buf, 0, sizeof(number_buf));
   sprintf(number_buf, "%d", number);
   SdStringBuf_AppendCStr(self, number_buf);
}

const char* SdStringBuf_CStr(SdStringBuf_r self) {
   SdAssert(self);
   return self->str;
}

void SdStringBuf_Clear(SdStringBuf_r self) {
   SdAssert(self);
   self->str[0] = 0;
   self->len = 0;
}

size_t SdStringBuf_Length(SdStringBuf_r self) {
   SdAssert(self);
   return self->len;
}

/* SdValue ***********************************************************************************************************/
SdValue* SdValue_NewNil(void) {
   return SdAllocValue();
}

SdValue* SdValue_NewInt(int x) {
   SdValue* value = SdAllocValue();
   value->type = SdType_INT;
   value->payload.int_value = x;
   return value;
}

SdValue* SdValue_NewDouble(double x) {
   SdValue* value = SdAllocValue();
   value->type = SdType_DOUBLE;
   value->payload.double_value = x;
   return value;
}

SdValue* SdValue_NewBool(SdBool x) {
   SdValue* value = SdAllocValue();
   value->type = SdType_BOOL;
   value->payload.bool_value = x;
   return value;
}

SdValue* SdValue_NewString(SdString* x) {
   SdValue* value = NULL;

   SdAssert(x);
   value = SdAllocValue();
   value->type = SdType_STRING;
   value->payload.string_value = x;

#if defined(SD_DEBUG_ALL) || defined(SD_DEBUG_GC)
   SdAssert(!x->is_boxed);
   x->is_boxed = SdTrue;
#endif

   return value;
}

SdValue* SdValue_NewList(SdList* x) {
   SdValue* value = NULL;

   SdAssert(x);
   value = SdAllocValue();
   value->type = SdType_LIST;
   value->payload.list_value = x;

#if defined(SD_DEBUG_ALL) || defined(SD_DEBUG_GC)
   SdAssert(!x->is_boxed);
   x->is_boxed = SdTrue;
#endif

   return value;
}

SdValue* SdValue_NewFunction(SdList* x) {
   SdValue* value = SdValue_NewList(x);
   value->type = SdType_FUNCTION;
   return value;
}

SdValue* SdValue_NewError(SdList* x) {
   SdValue* value = SdValue_NewList(x);
   value->type = SdType_ERROR;
   return value;
}

SdValue* SdValue_NewType(SdType x) {
   SdValue* value = SdValue_NewInt((int)x);
   value->type = SdType_TYPE;
   return value;
}

void SdValue_Delete(SdValue* self) {
   SdAssert(self);
   switch (SdValue_Type(self)) {
      case SdType_STRING:
         SdString_Delete(SdValue_GetString(self));
         break;
      case SdType_LIST:
      case SdType_FUNCTION:
      case SdType_ERROR:
         SdList_Delete(SdValue_GetList(self));
         break;
      default:
         break; /* nothing to free for these types */
   }
   SdFreeValue(self);
}

SdType SdValue_Type(SdValue_r self) {
   SdAssert(self);
   return self->type;
}

int SdValue_GetInt(SdValue_r self) {
   SdAssert(self);
   SdAssert(SdValue_Type(self) == SdType_INT || SdValue_Type(self) == SdType_TYPE);
   return self->payload.int_value;
}

double SdValue_GetDouble(SdValue_r self) {
   SdAssert(self);
   SdAssert(SdValue_Type(self) == SdType_DOUBLE);
   return self->payload.double_value;
}

SdBool SdValue_GetBool(SdValue_r self) {
   SdAssert(self);
   SdAssert(SdValue_Type(self) == SdType_BOOL);
   return self->payload.bool_value;
}

SdString_r SdValue_GetString(SdValue_r self) {
   SdAssert(self);
   SdAssert(SdValue_Type(self) == SdType_STRING);
   return self->payload.string_value;
}

SdList_r SdValue_GetList(SdValue_r self) {
   SdAssert(self);
   SdAssert(
      SdValue_Type(self) == SdType_LIST ||
      SdValue_Type(self) == SdType_FUNCTION ||
      SdValue_Type(self) == SdType_ERROR);
   return self->payload.list_value;
}

SdBool SdValue_Equals(SdValue_r a, SdValue_r b) {
   SdType a_type = SdType_NIL, b_type = SdType_NIL;
   
   SdAssert(a);
   SdAssert(b);
   a_type = SdValue_Type(a);
   b_type = SdValue_Type(b);

   /* If one of the values is the type Any, then it's always equal. */
   if ((a_type == SdType_TYPE && SdValue_GetInt(a) == SdType_ANY) || 
       (b_type == SdType_TYPE && SdValue_GetInt(b) == SdType_ANY))
      return SdTrue;

   /* If one of the values is a Type, then this acts like the "is" operator. */
   if (a_type == SdType_TYPE && b_type != SdType_TYPE)
      return (SdType)SdValue_GetInt(a) == b_type;
   else if (a_type != SdType_TYPE && b_type == SdType_TYPE)
      return a_type == (SdType)SdValue_GetInt(b);
   else if (a_type == SdType_TYPE && b_type == SdType_TYPE)
      return (SdType)SdValue_GetInt(a) == (SdType)SdValue_GetInt(b);

   /* Otherwise the types have to match for them to be equal. */
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

   SdAssert(self);
   switch (SdValue_Type(self)) {
      case SdType_ANY:
      case SdType_NIL:
         hash = 0;
         break;

      case SdType_INT:
      case SdType_TYPE:
         hash = SdValue_GetInt(self);
         break;

      case SdType_DOUBLE: {
         double number = 0;
         size_t i = 0, count = 0;
         
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
         size_t i = 0, length = 0, count = 0;
         SdString_r str = NULL;
         const char* cstr = NULL;

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

      case SdType_LIST:
      case SdType_FUNCTION:
      case SdType_ERROR: {
         size_t i = 0, length = 0, count = 0;
         SdList_r list = NULL;

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

SdBool SdValue_IsGcMarked(SdValue_r self) {
   SdAssert(self);
   return self->gc_mark;
}

void SdValue_SetGcMark(SdValue_r self, SdBool mark) {
   SdAssert(self);
   self->gc_mark = mark;
}

/* SdList ************************************************************************************************************/
SdList* SdList_New(void) {
   return SdAllocList();
}

SdList* SdList_NewWithLength(size_t length) {
   SdList* list = NULL;
   SdValue_r* elements = NULL;
   size_t i = 0;
   
   list = SdAllocList();
   if (length == 1) {
      list->count = 1;
      list->values.array_1 = SdAlloc1ElementArray();
      elements = list->values.array_1->elements;
   } else if (length == 2) {
      list->count = 2;
      list->values.array_2 = SdAlloc2ElementArray();
      elements = list->values.array_2->elements;
   } else if (length == 3) {
      list->count = 3;
      list->values.array_3 = SdAlloc3ElementArray();
      elements = list->values.array_3->elements;
   } else if (length == 4) {
      list->count = 4;
      list->values.array_4 = SdAlloc4ElementArray();
      elements = list->values.array_4->elements;
   } else if (length > 0) {
      list->count = length;
      list->values.array_n = SdAlloc(length * sizeof(SdValue_r));
      elements = list->values.array_n;
   }
   
   for (i = 0; i < length; i++)
      elements[i] = &SdValue_NIL;
   
   return list;
}

void SdList_Delete(SdList* self) {
   SdAssert(self);
   if (self->count == 1)
      SdFree1ElementArray(self->values.array_1);
   else if (self->count == 2)
      SdFree2ElementArray(self->values.array_2);
   else if (self->count == 3)
      SdFree3ElementArray(self->values.array_3);
   else if (self->count == 4)
      SdFree4ElementArray(self->values.array_4);
   else
      SdFree(self->values.array_n);
   SdFreeList(self);
}

void SdList_Append(SdList_r self, SdValue_r item) {
   size_t new_count = 0;
   
   SdAssert(self);
   SdAssert(item);
   new_count = self->count + 1;

   switch (self->count) {
      case 0: {
         Sd1ElementArray* array = SdAlloc1ElementArray();
         array->elements[0] = item;
         self->values.array_1 = array;
         break;
      }
      case 1: {
         Sd2ElementArray* array = SdAlloc2ElementArray();
         array->elements[0] = self->values.array_1->elements[0];
         array->elements[1] = item;
         SdFree1ElementArray(self->values.array_1);
         self->values.array_2 = array;
         break;
      }
      case 2: {
         Sd3ElementArray* array = SdAlloc3ElementArray();
         array->elements[0] = self->values.array_2->elements[0];
         array->elements[1] = self->values.array_2->elements[1];
         array->elements[2] = item;
         SdFree2ElementArray(self->values.array_2);
         self->values.array_3 = array;
         break;
      }
      case 3: {
         Sd4ElementArray* array = SdAlloc4ElementArray();
         array->elements[0] = self->values.array_3->elements[0];
         array->elements[1] = self->values.array_3->elements[1];
         array->elements[2] = self->values.array_3->elements[2];
         array->elements[3] = item;
         SdFree3ElementArray(self->values.array_3);
         self->values.array_4 = array;
         break;
      }
      case 4: {
         SdValue_r* array = SdAlloc(5 * sizeof(SdValue_r));
         array[0] = self->values.array_4->elements[0];
         array[1] = self->values.array_4->elements[1];
         array[2] = self->values.array_4->elements[2];
         array[3] = self->values.array_4->elements[3];
         array[4] = item;
         SdFree4ElementArray(self->values.array_4);
         self->values.array_n = array;
         break;
      }
      default: {
         self->values.array_n = SdRealloc(
            self->values.array_n, new_count * sizeof(SdValue_r), self->count * sizeof(SdValue_r));
         SdAssert(self->values.array_n); /* we're growing the list so SdRealloc() shouldn't return NULL. */
         self->values.array_n[new_count - 1] = item;
         break;
      }
   }
   
   self->count = new_count;
}

void SdList_SetAt(SdList_r self, size_t index, SdValue_r item) {
   SdAssert(self);
   SdAssert(item);
   SdAssert(index < self->count + 1);
   
   switch (self->count) {
      case 1: self->values.array_1->elements[index] = item; break;
      case 2: self->values.array_2->elements[index] = item; break;
      case 3: self->values.array_3->elements[index] = item; break;
      case 4: self->values.array_4->elements[index] = item; break;
      default: self->values.array_n[index] = item; break;
   }
}

void SdList_InsertAt(SdList_r self, size_t index, SdValue_r item) {
   SdListValuesUnion old_values = { 0 };
   SdValue_r* old_elements = NULL;
   SdValue_r* elements = NULL;
   size_t i = 0, old_count = 0;
   SdBool is_new_buffer = SdFalse;
   
   SdAssert(self);
   SdAssert(item);
   SdAssert(index <= self->count);
   
   if (index == self->count) {
      SdList_Append(self, item);
      return;
   }
   
   old_count = self->count;
   old_values = self->values;
   switch (old_count) {
      case 1: old_elements = self->values.array_1->elements; break;
      case 2: old_elements = self->values.array_2->elements; break;
      case 3: old_elements = self->values.array_3->elements; break;
      case 4: old_elements = self->values.array_4->elements; break;
      default: old_elements = self->values.array_n; break;
   }
   
   switch (old_count) {
      case 0:
         self->values.array_1 = SdAlloc1ElementArray();
         elements = self->values.array_1->elements;
         is_new_buffer = SdTrue;
         break;
      case 1:
         self->values.array_2 = SdAlloc2ElementArray();
         elements = self->values.array_2->elements;
         is_new_buffer = SdTrue;
         break;
      case 2:
         self->values.array_3 = SdAlloc3ElementArray();
         elements = self->values.array_3->elements;
         is_new_buffer = SdTrue;
         break;
      case 3:
         self->values.array_4 = SdAlloc4ElementArray();
         elements = self->values.array_4->elements;
         is_new_buffer = SdTrue;
         break;
      case 4:
         self->values.array_n = SdAlloc(5 * sizeof(SdValue_r));
         elements = self->values.array_n;
         is_new_buffer = SdTrue;
         break;
      default:
         self->values.array_n = SdRealloc(
            self->values.array_n, (old_count + 1) * sizeof(SdValue_r), old_count * sizeof(SdValue_r));
         assert(self->values.array_n);
         
         for (i = old_count; i > index && i <= old_count; i--)
            self->values.array_n[i] = self->values.array_n[i - 1];
         self->values.array_n[index] = item;
         break;
   }
   
   if (is_new_buffer) {
      switch (old_count) {
         case 0:
         case 1:
         case 2:
         case 3:
         case 4:
            for (i = 0; i < index; i++) {
               elements[i] = old_elements[i];
            }
            elements[index] = item;
            for (i = index; i < old_count; i++) {
               elements[i + 1] = old_elements[i];
            }
            break;
      }
   }
      
   switch (old_count) {
      case 1: SdFree1ElementArray(old_values.array_1); break;
      case 2: SdFree2ElementArray(old_values.array_2); break;
      case 3: SdFree3ElementArray(old_values.array_3); break;
      case 4: SdFree4ElementArray(old_values.array_4); break;
   }
   
   self->count++;
}

SdValue_r SdList_GetAt(SdList_r self, size_t index) {
   SdAssert(self);
   SdAssert(index < self->count);
   switch (self->count) {
      case 1: return self->values.array_1->elements[index];
      case 2: return self->values.array_2->elements[index];
      case 3: return self->values.array_3->elements[index];
      case 4: return self->values.array_4->elements[index];
      default: return self->values.array_n[index];
   }
}

size_t SdList_Count(SdList_r self) {
   SdAssert(self);
   return self->count;
}

SdValue_r SdList_RemoveAt(SdList_r self, size_t index) {
   SdListValuesUnion old_values = { 0 };
   SdValue_r old_value = NULL;
   SdValue_r* old_elements = NULL;
   SdValue_r* elements = NULL;
   size_t i = 0, old_count = 0;
   SdBool is_new_buffer = SdFalse;
   
   SdAssert(self);
   SdAssert(index < self->count);
   
   old_count = self->count;
   old_values = self->values;

   switch (old_count) {
      case 1: old_elements = self->values.array_1->elements; break;
      case 2: old_elements = self->values.array_2->elements; break;
      case 3: old_elements = self->values.array_3->elements; break;
      case 4: old_elements = self->values.array_4->elements; break;
      default: old_elements = self->values.array_n; break;
   }
   
   old_value = old_elements[index];
   for (i = index; i < old_count - 1; i++) {
      old_elements[i] = old_elements[i + 1];
   }
   
   switch (old_count) {
      case 1:
         self->values.array_n = NULL;
         break;
      case 2:
         self->values.array_1 = SdAlloc1ElementArray();
         elements = self->values.array_1->elements;
         is_new_buffer = SdTrue;
         break;
      case 3:
         self->values.array_2 = SdAlloc2ElementArray();
         elements = self->values.array_2->elements;
         is_new_buffer = SdTrue;
         break;
      case 4:
         self->values.array_3 = SdAlloc3ElementArray();
         elements = self->values.array_3->elements;
         is_new_buffer = SdTrue;
         break;
      case 5:
         self->values.array_4 = SdAlloc4ElementArray();
         elements = self->values.array_4->elements;
         is_new_buffer = SdTrue;
         break;
      default:
         self->values.array_n = SdRealloc(
            self->values.array_n, (old_count - 1) * sizeof(SdValue_r), old_count * sizeof(SdValue_r));
         break;
   }
   
   if (is_new_buffer) {
      for (i = 0; i < old_count - 1; i++)
         elements[i] = old_elements[i];
   }

   switch (old_count) {
      case 1: SdFree1ElementArray(old_values.array_1); break;
      case 2: SdFree2ElementArray(old_values.array_2); break;
      case 3: SdFree3ElementArray(old_values.array_3); break;
      case 4: SdFree4ElementArray(old_values.array_4); break;
      case 5: SdFree(old_values.array_n); break;
   }

   self->count--;
   
   return old_value;
}

void SdList_Clear(SdList_r self) {
   SdAssert(self);
   
   switch (self->count) {
      case 1: SdFree1ElementArray(self->values.array_1); break;
      case 2: SdFree2ElementArray(self->values.array_2); break;
      case 3: SdFree3ElementArray(self->values.array_3); break;
      case 4: SdFree4ElementArray(self->values.array_4); break;
      default: SdFree(self->values.array_n); break;
   }
   
   self->values.array_n = NULL;
   self->count = 0;
}

SdSearchResult SdList_Search(SdList_r list, SdSearchCompareFunc compare_func, void* context) {
   SdSearchResult result = { 0, SdFalse };
   int first = 0, last = 0;

   SdAssert(list);
   SdAssert(compare_func);
   SdAssert(context);
   last = (int)SdList_Count(list) - 1;
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
   SdSearchResult result = { 0, SdFalse };
   
   SdAssert(list);
   SdAssert(item);
   SdAssert(compare_func);
   SdAssert(context);
   result = SdList_Search(list, compare_func, context);
   if (result.exact) { /* An item with this name already exists. */
      return SdFalse;
   } else {
      SdList_InsertAt(list, result.index, item);
      return SdTrue;
   }
}

SdBool SdList_Equals(SdList_r a, SdList_r b) {
   size_t i = 0, a_count = 0, b_count = 0;

   SdAssert(a);
   SdAssert(b);
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
   SdList* clone = NULL;
   size_t i = 0, count = 0;
   
   count = SdList_Count(self);
   clone = SdList_NewWithLength(count);
   for (i = 0; i < count; i++)
      SdList_SetAt(clone, i, SdList_GetAt(self, i));
   return clone;
}

/* SdFile ************************************************************************************************************/
SdResult SdFile_WriteAllText(SdString_r file_path, SdString_r text) {
   SdResult result = SdResult_SUCCESS;
   FILE* fp = NULL;
   size_t count = 0;

   SdAssert(file_path);
   SdAssert(text);
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
   char line[1000] = { 0 };

   SdAssert(file_path);
   SdAssert(out_text);
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
static SdSearchResult SdEnv_BinarySearchByName(SdList_r list, SdString_r search_name) {
   SdAssert(list);
   SdAssert(search_name);
   return SdList_Search(list, SdEnv_BinarySearchByName_CompareFunc, search_name);
}

/* Assume lhs is a list, and compare by the string at index 1 (the name). */
static int SdEnv_BinarySearchByName_CompareFunc(SdValue_r lhs, void* context) {
   SdString_r search_name = NULL, pivot_name = NULL;
   SdList_r pivot_item = NULL;

   SdAssert(lhs);
   SdAssert(context);
   search_name = context;
   pivot_item = SdValue_GetList(lhs);
   pivot_name = SdValue_GetString(SdList_GetAt(pivot_item, 1));
   return SdString_Compare(pivot_name, search_name);
}

/* list is (list (list <unrelated> name1:str ...) (list <unrelated> name2:str ...) ...)
   The objects are sorted by name. Returns true if the item was inserted, false if the name already exists. */
static SdBool SdEnv_InsertByName(SdList_r list, SdValue_r item) {
   SdString_r item_name = NULL;
   
   SdAssert(list);
   SdAssert(item);
   item_name = SdValue_GetString(SdList_GetAt(SdValue_GetList(item), 1));
   return SdList_InsertBySearch(list, item, SdEnv_BinarySearchByName_CompareFunc, item_name);
}

#if defined(SD_DEBUG_ALL) || defined(SD_DEBUG_GC)
SdValue_r SdEnv_AddToGc(SdEnv_r self, SdValue* value) {
   SdAssert(self);
   SdAssert(value);

   /* ensure the value is not already in the chain. */
   do {
      SdChainNode_r node = SdChain_Head(self->values_chain);
      while (node) {
         if (SdChainNode_Value(node) == value) {
            SdAssert(SdFalse); /* don't attempt to add a value twice! */
         }
         node = SdChainNode_Next(node);
      }
   } while (0);

   SdChain_Push(self->values_chain, value);
   return value;
}
#else
SdValue_r SdEnv_AddToGc(SdEnv_r self, SdValue* value) {
   SdAssert(self);
   SdAssert(value);

   SdChain_Push(self->values_chain, value);
   return value;
}
#endif

SdEnv* SdEnv_New(void) {
   SdEnv* env = SdAlloc(sizeof(SdEnv));
   env->values_chain = SdChain_New(); /* must be done before calling SdEnv_Root_New */
   env->root = SdEnv_Root_New(env);
   env->active_frames = SdValueSet_New();
   env->call_stack = SdChain_New();
   env->protected_values = SdChain_New();
   return env;
}

void SdEnv_Delete(SdEnv* self) {
   SdAssert(self);
   /* Allow the garbage collector to clean up the tree starting at root. */
   self->root = NULL;
   SdValueSet_Delete(self->active_frames);
   self->active_frames = NULL;
   SdEnv_CollectGarbage(self);
   SdAssert(SdChain_Count(self->values_chain) == 0); /* shouldn't be anything left */
   SdChain_Delete(self->values_chain);
   SdChain_Delete(self->call_stack);
   SdChain_Delete(self->protected_values);
   SdFree(self);
}

SdValue_r SdEnv_Root(SdEnv_r self) {
   SdAssert(self);
   return self->root;
}

SdResult SdEnv_AddProgramAst(SdEnv_r self, SdValue_r program_node) {
   SdResult result = SdResult_SUCCESS;
   SdList_r root_functions = NULL, root_statements = NULL, new_functions = NULL, new_statements = NULL;
   size_t new_functions_count = 0, new_statements_count = 0, i = 0;

   SdAssert(self);
   SdAssert(program_node);
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

void SdEnv_CollectGarbage(SdEnv_r self) {
   SdChainNode_r value_node = NULL;
   SdList_r active_frames_list = NULL;
   size_t i = 0, count = 0;

   SdAssert(self);
   
   /* clear all marks */
   value_node = SdChain_Head(self->values_chain);
   while (value_node) {
      SdValue_SetGcMark(SdChainNode_Value(value_node), SdFalse);
      value_node = SdChainNode_Next(value_node);
   }

   /* mark connected values */
   if (self->root)
      SdEnv_CollectGarbage_MarkConnectedValues(self->root);

   if (self->active_frames) {
      active_frames_list = SdValueSet_GetList(self->active_frames);
      count = SdList_Count(active_frames_list);
      for (i = 0; i < count; i++)
         SdEnv_CollectGarbage_MarkConnectedValues(SdList_GetAt(active_frames_list, i));
   }

   if (self->call_stack) {
      value_node = SdChain_Head(self->call_stack);
      while (value_node) {
         SdEnv_CollectGarbage_MarkConnectedValues(SdChainNode_Value(value_node));
         value_node = SdChainNode_Next(value_node);
      }
   }

   if (self->protected_values) {
      value_node = SdChain_Head(self->protected_values);
      while (value_node) {
         SdEnv_CollectGarbage_MarkConnectedValues(SdChainNode_Value(value_node));
         value_node = SdChainNode_Next(value_node);
      }
   }

   /* sweep unmarked values */
   value_node = SdChain_Head(self->values_chain);
   while (value_node) {
      SdChainNode_r next_node = NULL;
      SdValue_r value = NULL;

      next_node = SdChainNode_Next(value_node);
      value = SdChainNode_Value(value_node);
      if (!SdValue_IsGcMarked(value)) { 
         /* this value is garbage */
         SdValue_Delete(value);
         SdChain_Remove(self->values_chain, value_node);
      }
      value_node = next_node;
   }
}

static void SdEnv_CollectGarbage_MarkConnectedValues(SdValue_r root) {
   SdChain* stack = NULL;
   
   SdAssert(root);
   stack = SdChain_New();
   SdChain_Push(stack, root);

   while (SdChain_Count(stack) > 0) {
      SdValue_r node = SdChain_Pop(stack);
      if (!SdValue_IsGcMarked(node)) {
         SdValue_SetGcMark(node, SdTrue);
         if (SdValue_Type(node) == SdType_LIST ||
             SdValue_Type(node) == SdType_FUNCTION ||
             SdValue_Type(node) == SdType_ERROR) {
            SdList_r list = NULL;
            size_t i = 0, count = 0;

            list = SdValue_GetList(node);
            count = SdList_Count(list);
            for (i = 0; i < count; i++) {
               SdChain_Push(stack, SdList_GetAt(list, i));
            }
         }
      }
   }

   SdChain_Delete(stack);
}

SdResult SdEnv_DeclareVar(SdEnv_r self, SdValue_r frame, SdValue_r name, SdValue_r value) {
   SdValue_r slot = NULL;
   SdList_r slots = NULL;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(name);
   SdAssert(value);
   slots = SdEnv_Frame_VariableSlots(frame);
   slot = SdEnv_VariableSlot_New(self, name, value);
   if (SdEnv_InsertByName(slots, slot))
      return SdResult_SUCCESS;
   else
      return SdFailWithStringSuffix(SdErr_NAME_COLLISION, "Variable redeclaration: ", SdValue_GetString(name));
}

SdValue_r SdEnv_ResolveVarRefToSlot(SdEnv_r self, SdValue_r frame, SdValue_r var_ref) {
   SdString_r identifier = NULL;
   SdValue_r slot = NULL, frame_hops_val = NULL, index_in_frame_val = NULL;
   
   SdAssert(self);
   SdAssert(frame);
   SdAssert(var_ref);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(var_ref) == SdNodeType_VAR_REF);
   
   identifier = SdAst_VarRef_Identifier(var_ref);
   
   /* we can skip the search if this variable reference contains the binding information */
   frame_hops_val = SdAst_VarRef_FrameHops(var_ref);
   index_in_frame_val = SdAst_VarRef_IndexInFrame(var_ref);
   if (SdValue_Type(frame_hops_val) == SdType_INT && SdValue_Type(index_in_frame_val) == SdType_INT) {
      int i = 0, frame_hops = 0, index_in_frame = 0;
      SdValue_r bound_frame = NULL;
      SdList_r slots = NULL;
      SdString_r slot_name = NULL;
      
      frame_hops = SdValue_GetInt(frame_hops_val);
      index_in_frame = SdValue_GetInt(index_in_frame_val);
      SdAssert(frame_hops >= 0);
      SdAssert(index_in_frame >= 0);
      
      bound_frame = frame;
      for (i = 0; i < frame_hops; i++) {
         SdAssert(bound_frame);
         bound_frame = SdEnv_Frame_Parent(bound_frame);
      }
      
      slots = SdEnv_Frame_VariableSlots(bound_frame);
      SdAssert(index_in_frame < (int)SdList_Count(slots));
      slot = SdList_GetAt(slots, index_in_frame);
      slot_name = SdEnv_VariableSlot_Name(slot);
      if (!SdString_Equals(slot_name, identifier))
         slot = NULL;
   }
   
   if (!slot) {
      int frame_hops = 0, index_in_frame = 0;
      
      slot = SdEnv_FindVariableSlotLocation(self, frame, identifier, SdTrue, &frame_hops, &index_in_frame);

      if (slot) {
         /* save this binding information for next time */
         SdAst_VarRef_SetFrameHops(self, var_ref, frame_hops);
         SdAst_VarRef_SetIndexInFrame(self, var_ref, index_in_frame);
      }
   }
   
   return slot;
}


SdValue_r SdEnv_FindVariableSlot(SdEnv_r self, SdValue_r frame, SdString_r name, SdBool traverse) {
   int frame_hops = 0, index_in_frame = 0;
   return SdEnv_FindVariableSlotLocation(self, frame, name, traverse, &frame_hops, &index_in_frame);
}

SdValue_r SdEnv_FindVariableSlotLocation(SdEnv_r self, SdValue_r frame, SdString_r name, SdBool traverse, 
   int* out_frame_hops, int* out_index_in_frame) {
   SdValue_r value = NULL;
   int frame_hops = 0, index_in_frame = 0;

   SdUnreferenced(self);
   SdAssert(self);
   SdAssert(frame);
   SdAssert(name);
   SdAssert(out_frame_hops);
   SdAssert(out_index_in_frame);
   while (SdValue_Type(frame) != SdType_NIL) {
      value = SdEnv_FindVariableSlotInFrame(name, frame, &index_in_frame);
      if (value) {
         *out_frame_hops = frame_hops;
         *out_index_in_frame = index_in_frame;
         return value;
      }

      if (traverse) {
         frame = SdEnv_Frame_Parent(frame);
         frame_hops++;
      } else {
         break;
      }
   }

   return NULL;
}

static SdValue_r SdEnv_FindVariableSlotInFrame(SdString_r name, SdValue_r frame, int* out_index_in_frame) {
   SdList_r slots = NULL;
   SdSearchResult search_result = { 0, SdFalse };

   SdAssert(name);
   SdAssert(frame);
   SdAssert(out_index_in_frame);
   slots = SdEnv_Frame_VariableSlots(frame);
   search_result = SdEnv_BinarySearchByName(slots, name);
   if (search_result.exact) {
      SdValue_r slot = SdList_GetAt(slots, search_result.index);
      *out_index_in_frame = (int)search_result.index;
      return slot;
   } else {
      return NULL;
   }
}

SdValue_r SdEnv_BeginFrame(SdEnv_r self, SdValue_r parent) {
   SdValue_r frame = NULL;
   
   SdAssert(self);
   SdAssert(parent);
   frame = SdEnv_Frame_New(self, parent);
   SdValueSet_Add(self->active_frames, frame);
   return frame;
}

void SdEnv_EndFrame(SdEnv_r self, SdValue_r frame) {
   SdAssert(self);
   SdAssert(frame);
   if (!SdValueSet_Remove(self->active_frames, frame)) {
      SdAssert(SdFalse); /* frame was supposed to be there, but was not */
   }
}

void SdEnv_PushCall(SdEnv_r self, SdValue_r calling_frame, SdValue_r name, SdValue_r arguments) {
   SdAssert(self);
   SdAssert(name);
   SdAssertValue(name, SdType_STRING);
   SdAssertValue(arguments, SdType_LIST);

   SdChain_Push(self->call_stack, SdEnv_CallTrace_New(self, name, arguments, calling_frame));
}

void SdEnv_PopCall(SdEnv_r self) {
   SdValue_r popped = NULL;
   
   SdAssert(self);
   popped = SdChain_Pop(self->call_stack);
   SdUnreferenced(popped);
   SdAssert(popped);
}

void SdEnv_PushProtectedValue(SdEnv_r self, SdValue_r value) {
   SdAssert(self);
   SdAssert(value);
   SdChain_Push(self->protected_values, value);
}

void SdEnv_PopProtectedValue(SdEnv_r self) {
   SdValue_r popped = NULL;
   
   SdAssert(self);
   popped = SdChain_Pop(self->protected_values);
   SdUnreferenced(popped);
   SdAssert(popped);
}

SdValue_r SdEnv_GetCurrentCallTrace(SdEnv_r self) {
   SdChainNode_r node;

   SdAssert(self);
   node = SdChain_Head(self->call_stack);
   if (node)
      return SdChainNode_Value(node);
   else
      return NULL;
}

SdChain_r SdEnv_GetCallTraceChain(SdEnv_r self) {
   SdAssert(self);
   return self->call_stack;
}

SdValue_r SdEnv_BoxNil(SdEnv_r env) {
   SdAssert(env);
   SdUnreferenced(env);
   return &SdValue_NIL;
}

SdValue_r SdEnv_BoxInt(SdEnv_r env, int x) {
   SdAssert(env);
   return SdEnv_AddToGc(env, SdValue_NewInt(x));
}

SdValue_r SdEnv_BoxDouble(SdEnv_r env, double x) {
   SdAssert(env);
   return SdEnv_AddToGc(env, SdValue_NewDouble(x));
}

SdValue_r SdEnv_BoxBool(SdEnv_r env, SdBool x) {
   SdAssert(env);
   SdUnreferenced(env);
   return x ? &SdValue_TRUE : &SdValue_FALSE;
}

SdValue_r SdEnv_BoxString(SdEnv_r env, SdString* x) {
   SdAssert(env);
   SdAssert(x);
   return SdEnv_AddToGc(env, SdValue_NewString(x));
}

SdValue_r SdEnv_BoxList(SdEnv_r env, SdList* x) {
   SdAssert(env);
   SdAssert(x);
   return SdEnv_AddToGc(env, SdValue_NewList(x));
}

SdValue_r SdEnv_BoxFunction(SdEnv_r env, SdList* x) {
   SdAssert(env);
   SdAssert(x);
   return SdEnv_AddToGc(env, SdValue_NewFunction(x));
}

SdValue_r SdEnv_BoxError(SdEnv_r env, SdList* x) {
   SdAssert(env);
   SdAssert(x);
   return SdEnv_AddToGc(env, SdValue_NewError(x));
}

SdValue_r SdEnv_BoxType(SdEnv_r env, SdType x) {
   SdAssert(env);
   return SdEnv_AddToGc(env, SdValue_NewType(x));
}

SdValue_r SdEnv_Root_New(SdEnv_r env) {
   SdValue_r frame = NULL;
   SdList* root_list = NULL;

   SdAssert(env);
   frame = SdEnv_Frame_New(env, NULL);
   root_list = SdList_NewWithLength(4);
   SdList_SetAt(root_list, 0, SdEnv_BoxInt(env, SdNodeType_ROOT));
   SdList_SetAt(root_list, 1, SdEnv_BoxList(env, SdList_New())); /* functions */
   SdList_SetAt(root_list, 2, SdEnv_BoxList(env, SdList_New())); /* statements */
   SdList_SetAt(root_list, 3, frame); /* bottom frame */
   return SdEnv_BoxList(env, root_list);
}

/* SdAst *************************************************************************************************************/
/* A simple macro-based DSL for implementing the AST node functions. */
#define SdAst_MAX_NODE_VALUES 6
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
      SdAssert(self); \
      SdAssert(SdAst_NodeType(self) == node_type); \
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
      SdAssert(self); \
      SdAssert(SdAst_NodeType(self) == node_type); \
      return SdAst_NodeValue(self, index); \
   }

static SdValue_r SdAst_NodeValue(SdValue_r node, size_t value_index) {
   SdAssert(node);
   return SdList_GetAt(SdValue_GetList(node), value_index);
}

SdNodeType SdAst_NodeType(SdValue_r node) {
   SdAssert(node);
   return SdValue_GetInt(SdAst_NodeValue(node, 0));
}

static SdValue_r SdAst_NewNode(SdEnv_r env, SdValue_r values[], size_t num_values) {
   SdList* node = NULL;
   size_t i = 0;

   SdAssert(env);
   SdAssert(values);
   SdAssert(SdValue_Type(values[0]) == SdType_INT);

   node = SdList_NewWithLength(num_values);
   for (i = 0; i < num_values; i++) {
      SdAssert(values[i]);
      SdList_SetAt(node, i, values[i]);
   }
   return SdEnv_BoxList(env, node);
}

static SdValue_r SdAst_NewFunctionNode(SdEnv_r env, SdValue_r values[], size_t num_values) {
   SdList* node = NULL;
   size_t i = 0;

   SdAssert(env);
   SdAssert(values);
   SdAssert(SdValue_Type(values[0]) == SdType_INT);

   node = SdList_NewWithLength(num_values);
   for (i = 0; i < num_values; i++) {
      SdAssert(values[i]);
      SdList_SetAt(node, i, values[i]);
   }
   return SdEnv_BoxFunction(env, node);
}

SdValue_r SdAst_Program_New(SdEnv_r env, SdList* functions, SdList* statements) {
   SdAst_BEGIN(SdNodeType_PROGRAM)
   
   SdAssert(env);
   SdAssertAllNodesOfType(functions, SdNodeType_FUNCTION);
   SdAssertAllNodesOfTypes(statements, SdNodeType_STATEMENTS_FIRST, SdNodeType_STATEMENTS_LAST);
   
   SdAst_LIST(functions)
   SdAst_LIST(statements)
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_Program_Functions, SdNodeType_PROGRAM, 1)
SdAst_LIST_GETTER(SdAst_Program_Statements, SdNodeType_PROGRAM, 2)

SdValue_r SdAst_Function_New(SdEnv_r env, SdString* function_name, SdList* parameters, SdValue_r body,
   SdBool is_imported, SdBool has_var_args) {
   SdAst_BEGIN(SdNodeType_FUNCTION)

   SdAssert(env);
   SdAssertNonEmptyString(function_name);
   SdAssertAllNodesOfType(parameters, SdNodeType_PARAMETER);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_STRING(function_name)
   SdAst_LIST(parameters)
   SdAst_VALUE(body)
   SdAst_BOOL(is_imported)
   SdAst_BOOL(has_var_args)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Function_Name, SdNodeType_FUNCTION, 1)
SdAst_VALUE_GETTER(SdAst_Function_Parameters, SdNodeType_FUNCTION, 2)
SdAst_VALUE_GETTER(SdAst_Function_Body, SdNodeType_FUNCTION, 3)
SdAst_BOOL_GETTER(SdAst_Function_IsImported, SdNodeType_FUNCTION, 4)
SdAst_BOOL_GETTER(SdAst_Function_HasVariableLengthArgumentList, SdNodeType_FUNCTION, 5)

SdValue_r SdAst_Parameter_New(SdEnv_r env, SdString* identifier, SdList* type_names) {
   SdAst_BEGIN(SdNodeType_PARAMETER)
   
   SdAssert(env);
   SdAssertNonEmptyString(identifier);
   SdAssertAllValuesOfType(type_names, SdType_STRING);
   
   SdAst_STRING(identifier)
   SdAst_LIST(type_names)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Parameter_Identifier, SdNodeType_PARAMETER, 1)
SdAst_LIST_GETTER(SdAst_Parameter_TypeNames, SdNodeType_PARAMETER, 2)

SdValue_r SdAst_Body_New(SdEnv_r env, SdList* statements) {
   SdAst_BEGIN(SdNodeType_BODY)

   SdAssert(env);
   SdAssertAllNodesOfTypes(statements, SdNodeType_STATEMENTS_FIRST, SdNodeType_STATEMENTS_LAST);

   SdAst_LIST(statements);
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_Body_Statements, SdNodeType_BODY, 1)

SdValue_r SdAst_Call_New(SdEnv_r env, SdValue_r var_ref, SdList* arguments) {
   SdAst_BEGIN(SdNodeType_CALL)

   SdAssert(env);
   SdAssertNode(var_ref, SdNodeType_VAR_REF);
   SdAssert(arguments);

   SdAst_VALUE(var_ref)
   SdAst_LIST(arguments)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Call_VarRef, SdNodeType_CALL, 1)
SdAst_LIST_GETTER(SdAst_Call_Arguments, SdNodeType_CALL, 2)

SdValue_r SdAst_Var_New(SdEnv_r env, SdString* variable_name, SdValue_r value_expr) {
   SdAst_BEGIN(SdNodeType_VAR)

   SdAssert(env);
   SdAssertNonEmptyString(variable_name);
   SdAssertExpr(value_expr);

   SdAst_STRING(variable_name)
   SdAst_VALUE(value_expr)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Var_VariableName, SdNodeType_VAR, 1)
SdAst_VALUE_GETTER(SdAst_Var_ValueExpr, SdNodeType_VAR, 2)

SdValue_r SdAst_Set_New(SdEnv_r env, SdValue_r var_ref, SdValue_r value_expr) {
   SdAst_BEGIN(SdNodeType_SET)

   SdAssert(env);
   SdAssertNode(var_ref, SdNodeType_VAR_REF);
   SdAssertExpr(value_expr);

   SdAst_VALUE(var_ref)
   SdAst_VALUE(value_expr)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Set_VarRef, SdNodeType_SET, 1)
SdAst_VALUE_GETTER(SdAst_Set_ValueExpr, SdNodeType_SET, 2)

SdValue_r SdAst_MultiVar_New(SdEnv_r env, SdList* variable_names, SdValue_r value_expr) {
   SdAst_BEGIN(SdNodeType_MULTI_VAR)

   SdAssert(env);
   SdAssertAllValuesOfType(variable_names, SdType_STRING);
   SdAssertExpr(value_expr);

   SdAst_LIST(variable_names)
   SdAst_VALUE(value_expr)
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_MultiVar_VariableNames, SdNodeType_MULTI_VAR, 1)
SdAst_VALUE_GETTER(SdAst_MultiVar_ValueExpr, SdNodeType_MULTI_VAR, 2)

SdValue_r SdAst_MultiSet_New(SdEnv_r env, SdList* var_refs, SdValue_r value_expr) {
   SdAst_BEGIN(SdNodeType_MULTI_SET)

   SdAssert(env);
   SdAssertAllNodesOfType(var_refs, SdNodeType_VAR_REF);
   SdAssertExpr(value_expr);

   SdAst_LIST(var_refs)
   SdAst_VALUE(value_expr)
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_MultiSet_VarRefs, SdNodeType_MULTI_SET, 1)
SdAst_VALUE_GETTER(SdAst_MultiSet_ValueExpr, SdNodeType_MULTI_SET, 2)

SdValue_r SdAst_If_New(SdEnv_r env, SdValue_r condition_expr, SdValue_r true_body, SdList* else_ifs,
   SdValue_r else_body) {
   SdAst_BEGIN(SdNodeType_IF)

   SdAssert(env);
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
   
   SdAssert(env);
   SdAssertExpr(condition_expr);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_VALUE(condition_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_ElseIf_ConditionExpr, SdNodeType_ELSEIF, 1)
SdAst_VALUE_GETTER(SdAst_ElseIf_Body, SdNodeType_ELSEIF, 2)

SdValue_r SdAst_For_New(SdEnv_r env, SdString* variable_name, SdValue_r start_expr, SdValue_r stop_expr,
   SdValue_r body) {
   SdAst_BEGIN(SdNodeType_FOR)
   
   SdAssert(env);
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
   
   SdAssert(env);
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

   SdAssert(env);
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
   
   
   SdAssert(env);
   SdAssertExpr(condition_expr);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_VALUE(condition_expr)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Do_ConditionExpr, SdNodeType_DO, 1)
SdAst_VALUE_GETTER(SdAst_Do_Body, SdNodeType_DO, 2)

SdValue_r SdAst_Switch_New(SdEnv_r env, SdList* exprs, SdList* cases, SdValue_r default_body) {
   SdAst_BEGIN(SdNodeType_SWITCH)
   
   SdAssert(env);
   SdAssertAllNodesOfTypes(exprs, SdNodeType_EXPRESSIONS_FIRST, SdNodeType_EXPRESSIONS_LAST);
   SdAssertAllNodesOfType(cases, SdNodeType_SWITCH_CASE);
   SdAssertNode(default_body, SdNodeType_BODY);
   
   SdAst_LIST(exprs)
   SdAst_LIST(cases)
   SdAst_VALUE(default_body)
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_Switch_Exprs, SdNodeType_SWITCH, 1)
SdAst_LIST_GETTER(SdAst_Switch_Cases, SdNodeType_SWITCH, 2)
SdAst_VALUE_GETTER(SdAst_Switch_DefaultBody, SdNodeType_SWITCH, 3)

SdValue_r SdAst_SwitchCase_New(SdEnv_r env, SdList* exprs, SdValue_r body) {
   SdAst_BEGIN(SdNodeType_SWITCH_CASE)

   SdAssert(env);
   SdAssertAllNodesOfTypes(exprs, SdNodeType_EXPRESSIONS_FIRST, SdNodeType_EXPRESSIONS_LAST);
   SdAssertNode(body, SdNodeType_BODY);

   SdAst_LIST(exprs)
   SdAst_VALUE(body)
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_SwitchCase_IfExprs, SdNodeType_SWITCH_CASE, 1)
SdAst_VALUE_GETTER(SdAst_SwitchCase_ThenBody, SdNodeType_SWITCH_CASE, 2)

SdValue_r SdAst_Return_New(SdEnv_r env, SdValue_r expr) {
   SdAst_BEGIN(SdNodeType_RETURN)

   SdAssert(env);
   SdAssertExpr(expr);

   SdAst_VALUE(expr)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Return_Expr, SdNodeType_RETURN, 1)

SdValue_r SdAst_Die_New(SdEnv_r env, SdValue_r expr) {
   SdAst_BEGIN(SdNodeType_DIE)

   SdAssert(env);
   SdAssertExpr(expr);

   SdAst_VALUE(expr)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_Die_Expr, SdNodeType_DIE, 1)

SdValue_r SdAst_IntLit_New(SdEnv_r env, int value) {
   SdAst_BEGIN(SdNodeType_INT_LIT)

   SdAssert(env);

   SdAst_INT(value)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_IntLit_Value, SdNodeType_INT_LIT, 1)

SdValue_r SdAst_DoubleLit_New(SdEnv_r env, double value) {
   SdAst_BEGIN(SdNodeType_DOUBLE_LIT)

   SdAssert(env);

   SdAst_DOUBLE(value)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_DoubleLit_Value, SdNodeType_DOUBLE_LIT, 1)

SdValue_r SdAst_BoolLit_New(SdEnv_r env, SdBool value) {
   SdAst_BEGIN(SdNodeType_BOOL_LIT)

   SdAssert(env);

   SdAst_BOOL(value)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_BoolLit_Value, SdNodeType_BOOL_LIT, 1)

SdValue_r SdAst_StringLit_New(SdEnv_r env, SdString* value) {
   SdAst_BEGIN(SdNodeType_STRING_LIT)

   SdAssert(env);
   SdAssert(value);

   SdAst_STRING(value)
   SdAst_END
}
SdAst_VALUE_GETTER(SdAst_StringLit_Value, SdNodeType_STRING_LIT, 1)

SdValue_r SdAst_NilLit_New(SdEnv_r env) {
   SdAst_BEGIN(SdNodeType_NIL_LIT)

   SdAssert(env);

   SdAst_END
}

SdValue_r SdAst_VarRef_New(SdEnv_r env, SdString* identifier) {
   SdAst_BEGIN(SdNodeType_VAR_REF)

   SdAssert(env);
   SdAssertNonEmptyString(identifier);

   SdAst_STRING(identifier)
   SdAst_NIL()
   SdAst_NIL()
   SdAst_END
}
SdAst_STRING_GETTER(SdAst_VarRef_Identifier, SdNodeType_VAR_REF, 1)
SdAst_VALUE_GETTER(SdAst_VarRef_FrameHops, SdNodeType_VAR_REF, 2)
SdAst_VALUE_GETTER(SdAst_VarRef_IndexInFrame, SdNodeType_VAR_REF, 3)

void SdAst_VarRef_SetFrameHops(SdEnv_r env, SdValue_r self, int frame_hops) {
   SdAssertNode(self, SdNodeType_VAR_REF);
   SdAssert(frame_hops >= 0);

   SdList_SetAt(SdValue_GetList(self), 2, SdEnv_BoxInt(env, frame_hops));
}

void SdAst_VarRef_SetIndexInFrame(SdEnv_r env, SdValue_r self, int index_in_frame) {
   SdAssertNode(self, SdNodeType_VAR_REF);
   SdAssert(index_in_frame >= 0);

   SdList_SetAt(SdValue_GetList(self), 3, SdEnv_BoxInt(env, index_in_frame));
}

SdValue_r SdAst_Match_New(SdEnv_r env, SdList* exprs, SdList* cases, SdValue_r default_expr) {
   SdAst_BEGIN(SdNodeType_MATCH)
   
   SdAssert(env);
   SdAssertAllNodesOfTypes(exprs, SdNodeType_EXPRESSIONS_FIRST, SdNodeType_EXPRESSIONS_LAST);
   SdAssertAllNodesOfType(cases, SdNodeType_MATCH_CASE);
   SdAssertExpr(default_expr);
   
   SdAst_LIST(exprs)
   SdAst_LIST(cases)
   SdAst_VALUE(default_expr)
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_Match_Exprs, SdNodeType_MATCH, 1)
SdAst_LIST_GETTER(SdAst_Match_Cases, SdNodeType_MATCH, 2)
SdAst_VALUE_GETTER(SdAst_Match_DefaultExpr, SdNodeType_MATCH, 3)

SdValue_r SdAst_MatchCase_New(SdEnv_r env, SdList* if_exprs, SdValue_r then_expr) {
   SdAst_BEGIN(SdNodeType_MATCH_CASE)

   SdAssert(env);
   SdAssertAllNodesOfTypes(if_exprs, SdNodeType_EXPRESSIONS_FIRST, SdNodeType_EXPRESSIONS_LAST);
   SdAssertExpr(then_expr);

   SdAst_LIST(if_exprs)
   SdAst_VALUE(then_expr)
   SdAst_END
}
SdAst_LIST_GETTER(SdAst_MatchCase_IfExprs, SdNodeType_MATCH_CASE, 1)
SdAst_VALUE_GETTER(SdAst_MatchCase_ThenExpr, SdNodeType_MATCH_CASE, 2)

/* These are SdEnv nodes "above" the AST, but it's convenient to use the same macros to implement them. */
SdAst_LIST_GETTER(SdEnv_Root_Functions, SdNodeType_ROOT, 1)
SdAst_LIST_GETTER(SdEnv_Root_Statements, SdNodeType_ROOT, 2)
SdAst_VALUE_GETTER(SdEnv_Root_BottomFrame, SdNodeType_ROOT, 3)

SdValue_r SdEnv_Frame_New(SdEnv_r env, SdValue_r parent_or_null) {
   SdAst_BEGIN(SdNodeType_FRAME)

   SdAssert(env);

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

   SdAssert(env);
   SdAssertValue(name, SdType_STRING);
   SdAssertNonEmptyString(SdValue_GetString(name));
   SdAssert(value);

   SdAst_VALUE(name)
   SdAst_VALUE(value)
   SdAst_END
}
SdAst_STRING_GETTER(SdEnv_VariableSlot_Name, SdNodeType_VAR_SLOT, 1)
SdAst_VALUE_GETTER(SdEnv_VariableSlot_Value, SdNodeType_VAR_SLOT, 2)

void SdEnv_VariableSlot_SetValue(SdValue_r self, SdValue_r value) {
   SdAssertNode(self, SdNodeType_VAR_SLOT);
   SdAssert(value);

   SdList_SetAt(SdValue_GetList(self), 2, value);
}

SdValue_r SdEnv_Closure_New(SdEnv_r env, SdValue_r frame, SdValue_r function_node, SdValue_r partial_arguments) {
   SdAst_BEGIN(SdNodeType_CLOSURE)

   SdAssert(env);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssertNode(function_node, SdNodeType_FUNCTION);
   SdAssertValue(partial_arguments, SdType_LIST);

   SdAst_VALUE(frame)
   SdAst_VALUE(function_node)
   SdAst_VALUE(partial_arguments)
   return SdAst_NewFunctionNode(env, values, i);
}
SdAst_VALUE_GETTER(SdEnv_Closure_Frame, SdNodeType_CLOSURE, 1)
SdAst_VALUE_GETTER(SdEnv_Closure_FunctionNode, SdNodeType_CLOSURE, 2)
SdAst_VALUE_GETTER(SdEnv_Closure_PartialArguments, SdNodeType_CLOSURE, 3)

SdValue_r SdEnv_Closure_CopyWithPartialArguments(SdValue_r self, SdEnv_r env, SdList_r arguments) {
   SdList* partial_arguments = NULL;
   size_t i = 0, count = 0;

   SdAssert(self);
   SdAssert(env);
   SdAssert(arguments);

   partial_arguments = SdList_Clone(SdValue_GetList(SdEnv_Closure_PartialArguments(self)));
   count = SdList_Count(arguments);
   for (i = 0; i < count; i++)
      SdList_Append(partial_arguments, SdList_GetAt(arguments, i));

   return SdEnv_Closure_New(env,
      SdEnv_Closure_Frame(self),
      SdEnv_Closure_FunctionNode(self),
      SdEnv_BoxList(env, partial_arguments));
}

SdValue_r SdEnv_CallTrace_New(SdEnv_r env, SdValue_r name, SdValue_r arguments, SdValue_r calling_frame) {
   SdAst_BEGIN(SdNodeType_CALL_TRACE)

   SdAssert(env);
   SdAssertValue(name, SdType_STRING);
   SdAssertValue(arguments, SdType_LIST);
   SdAssertNode(calling_frame, SdNodeType_FRAME);

   SdAst_VALUE(name)
   SdAst_VALUE(arguments)
   SdAst_VALUE(calling_frame)
   SdAst_END
}
SdAst_VALUE_GETTER(SdEnv_CallTrace_Name, SdNodeType_CALL_TRACE, 1)
SdAst_VALUE_GETTER(SdEnv_CallTrace_Arguments, SdNodeType_CALL_TRACE, 2)
SdAst_VALUE_GETTER(SdEnv_CallTrace_CallingFrame, SdNodeType_CALL_TRACE, 3)

/* SdValueSet ********************************************************************************************************/
SdValueSet* SdValueSet_New(void) {
   SdValueSet* self = SdAlloc(sizeof(SdValueSet));
   self->list = SdList_New();
   return self;
}

void SdValueSet_Delete(SdValueSet* self) {
   SdAssert(self);
   SdList_Delete(self->list);
   SdFree(self);
}

static int SdValueSet_CompareFunc(SdValue_r lhs, void* context) { /* compare the pointer values */
   SdValue_r rhs = NULL;

   SdAssert(lhs);
   SdAssert(context);
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
   SdAssert(self);
   SdAssert(item);
   return SdList_InsertBySearch(self->list, item, SdValueSet_CompareFunc, item);
}

SdBool SdValueSet_Has(SdValueSet_r self, SdValue_r item) {
   SdSearchResult result = { 0, SdFalse };

   SdAssert(self);
   SdAssert(item);
   result = SdList_Search(self->list, SdValueSet_CompareFunc, item);
   return result.exact;
}

SdBool SdValueSet_Remove(SdValueSet_r self, SdValue_r item) { /* true = removed, false = wasn't there */
   SdSearchResult result = { 0, SdFalse };

   SdAssert(self);
   SdAssert(item);
   result = SdList_Search(self->list, SdValueSet_CompareFunc, item);
   if (result.exact) { 
      SdList_RemoveAt(self->list, result.index);
      return SdTrue;
   } else {
      return SdFalse;
   }
}

SdList_r SdValueSet_GetList(SdValueSet_r self) {
   SdAssert(self);
   return self->list;
}

/* SdChain ***********************************************************************************************************/
SdChain* SdChain_New(void) {
   return SdAlloc(sizeof(SdChain));
}

void SdChain_Delete(SdChain* self) {
   SdChainNode* node = NULL;

   SdAssert(self);
   node = self->head;
   while (node) {
      SdChainNode* next = node->next;
      SdFree(node);
      node = next;
   }

   SdFree(self);
}

size_t SdChain_Count(SdChain_r self) {
   SdAssert(self);
   return self->count;
}

void SdChain_Push(SdChain_r self, SdValue_r item) {
   SdChainNode* node = NULL;

   SdAssert(self);
   SdAssert(item);

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
   SdAssert(self);

   if (self->head) {
      SdChainNode* next = NULL;
      SdValue_r value = NULL;

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
   SdAssert(self);
   return self->head;
}

void SdChain_Remove(SdChain_r self, SdChainNode_r node) {
   SdAssert(self);
   SdAssert(node);

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
   SdAssert(self);
   return self->value;
}

SdChainNode_r SdChainNode_Prev(SdChainNode_r self) { /* null for the head node */
   SdAssert(self);
   return self->prev;
}

SdChainNode_r SdChainNode_Next(SdChainNode_r self) { /* null for the tail node */
   SdAssert(self);
   return self->next;
}

/* SdToken ***********************************************************************************************************/
SdToken* SdToken_New(int source_line, SdTokenType type, char* text) {
   SdToken* self = NULL;

   SdAssert(text);
   self = SdAlloc(sizeof(SdToken));
   self->source_line = source_line;
   self->type = type;
   self->text = text;
   return self;
}

void SdToken_Delete(SdToken* self) {
   SdAssert(self);
   SdFree(self->text);
   SdFree(self);
}

int SdToken_SourceLine(SdToken_r self) {
   SdAssert(self);
   return self->source_line;
}

SdTokenType SdToken_Type(SdToken_r self) {
   SdAssert(self);
   return self->type;
}

const char* SdToken_Text(SdToken_r self) {
   SdAssert(self);
   return self->text;
}

/* SdScanner *********************************************************************************************************/
SdScanner* SdScanner_New(void) {
   return SdAlloc(sizeof(SdScanner));
}

void SdScanner_Delete(SdScanner* self) {
   SdScannerNode* node = NULL;

   SdAssert(self);
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
   size_t i = 0, text_length = 0;
   int source_line = 1;
   SdStringBuf* current_text = NULL;

   SdAssert(self);
   SdAssert(text);
   current_text = SdStringBuf_New();
   text_length = strlen(text);
   for (i = 0; i < text_length; i++) {
      unsigned char ch, peek;
      ch = (unsigned char)text[i];
      peek = (unsigned char)text[i + 1];

      if (ch == '\n')
         in_comment = SdFalse;

      if (in_comment)
         continue;

      if (in_string) {
         if (in_escape) {
            char real_ch = 0;

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
         case '|':
         case '\\':
            if (SdStringBuf_Length(current_text) > 0) {
               SdScanner_AppendToken(self, source_line, SdStringBuf_CStr(current_text));
               SdStringBuf_Clear(current_text);
            }
            break;
      }

      /* The lambda character is 0xCE 0xBB in UTF-8. */
      if (ch == 0xCE && peek == 0xBB) {
         if (SdStringBuf_Length(current_text) > 0) {
            SdScanner_AppendToken(self, source_line, SdStringBuf_CStr(current_text));
            SdStringBuf_Clear(current_text);
         }
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
         case ':':
         case '|':
         case '\\': {
            char token_text[2] = { 0 };
            token_text[0] = ch;
            token_text[1] = 0;
            SdScanner_AppendToken(self, source_line, token_text);
            break;
         }

         case 0xCE: {
            if (peek == 0xBB) { /* lambda character */
               char token_text[3] = { 0 };
               token_text[0] = ch;
               token_text[1] = peek;
               token_text[2] = 0;
               SdScanner_AppendToken(self, source_line, token_text);
               i++; /* consume the peek character */
            } else { /* something else starting with 0xCE */
               SdStringBuf_AppendChar(current_text, ch); /* same as default case */
            }
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

static void SdScanner_AppendToken(SdScanner_r self, int source_line, const char* token_text) {
   SdToken* token = NULL;
   SdScannerNode* new_node = NULL;
   
   SdAssert(self);
   SdAssert(token_text);
   token = SdToken_New(source_line, SdScanner_ClassifyToken(token_text), SdStrdup(token_text));

   new_node = SdAlloc(sizeof(SdScannerNode));
   new_node->token = token;
   new_node->next = NULL;

   if (self->tail) {
      self->tail->next = new_node;
      self->tail = new_node;
   } else { /* list is empty */
      SdAssert(!self->head);
      self->head = new_node;
      self->tail = new_node;
   }
}

static SdTokenType SdScanner_ClassifyToken(const char* text) {
   SdAssert(text);
   SdAssert(strlen(text) > 0);

   switch ((unsigned char)text[0]) {
      case '"': return SdTokenType_STRING_LIT;

      /* These characters are always tokens on their own, so we don't have to check the whole string. */
      case '(': return SdTokenType_OPEN_PAREN;
      case ')': return SdTokenType_CLOSE_PAREN;
      case '[': return SdTokenType_OPEN_BRACKET;
      case ']': return SdTokenType_CLOSE_BRACKET;
      case '{': return SdTokenType_OPEN_BRACE;
      case '}': return SdTokenType_CLOSE_BRACE;
      case ':': return SdTokenType_COLON;
      case '|': return SdTokenType_PIPE;
      case '\\': return SdTokenType_LAMBDA;

      case 0xCE:
         if (strlen(text) == 2 && (unsigned char)text[1] == 0xBB) return SdTokenType_LAMBDA;
         break;

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

      case 'm':
         if (strcmp(text, "match") == 0) return SdTokenType_MATCH;
         break;

      case 'n':
         if (strcmp(text, "nil") == 0) return SdTokenType_NIL;
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
         if (SdScanner_IsIntLit(text)) return SdTokenType_INT_LIT;
         if (SdScanner_IsDoubleLit(text)) return SdTokenType_DOUBLE_LIT;
         break;
   }

   return SdTokenType_IDENTIFIER; /* anything goes */
}

static SdBool SdScanner_IsIntLit(const char* text) {
   size_t i = 0, length = 0;
   int digits = 0;

   SdAssert(text);
   length = strlen(text);
   SdAssert(length > 0);
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

static SdBool SdScanner_IsDoubleLit(const char* text) {
   size_t i = 0, length = 0;
   SdBool dot = SdFalse;
   int digits = 0;

   SdAssert(text);
   length = strlen(text);
   SdAssert(length > 0);
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
   SdAssert(self);
   return !self->cursor;
}

SdBool SdScanner_Peek(SdScanner_r self, SdToken_r* out_token) { /* true = token was read, false = eof */
   SdAssert(self);
   SdAssert(out_token);
   if (self->cursor) {
      *out_token = self->cursor->token;
      return SdTrue;
   } else {
      return SdFalse;
   }
}

SdTokenType SdScanner_PeekType(SdScanner_r self) { /* SdTokenType_NONE if eof */
   SdToken_r token = NULL;
   if (!SdScanner_Peek(self, &token)) {
      return SdTokenType_NONE;
   } else {
      return SdToken_Type(token);
   }
}

SdToken_r SdScanner_PeekToken(SdScanner_r self) {
   SdToken_r token = NULL;
   if (!SdScanner_Peek(self, &token)) {
      return NULL;
   } else {
      return token;
   }
}

SdBool SdScanner_Read(SdScanner_r self, SdToken_r* out_token) { /* true = token was read, false = eof */
   SdAssert(self);
   SdAssert(out_token);
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

static SdResult SdParser_Fail(SdErr code, SdToken_r token, const char* message) {
   SdStringBuf* buf = NULL;
   SdResult result = SdResult_SUCCESS;
   
   SdAssert(message);
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

static SdResult SdParser_FailEof(void) {
   return SdFail(SdErr_UNEXPECTED_EOF, "Unexpected EOF.");
}

static SdResult SdParser_FailType(SdToken_r token, SdTokenType expected_type, SdTokenType actual_type) {
   SdStringBuf* buf = NULL;
   SdResult result = SdResult_SUCCESS;

   SdAssert(token);
   buf = SdStringBuf_New();
   SdStringBuf_AppendCStr(buf, "Expected: ");
   SdStringBuf_AppendCStr(buf, SdParser_TypeString(expected_type));
   SdStringBuf_AppendCStr(buf, ", Actual: ");
   SdStringBuf_AppendCStr(buf, SdParser_TypeString(actual_type));
   result = SdParser_Fail(SdErr_UNEXPECTED_TOKEN, token, SdStringBuf_CStr(buf));
   SdStringBuf_Delete(buf);
   return result;
}

static const char* SdParser_TypeString(SdTokenType type) {
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
      case SdTokenType_PIPE: return "|";
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
      case SdTokenType_LAMBDA: return "\\";
      default: return "<unrecognized token type>";
   }
}

SdResult SdParser_ParseProgram(SdEnv_r env, const char* text, SdValue_r* out_program_node) {
   SdScanner* scanner = NULL;
   SdList* functions = NULL;
   SdList* statements = NULL;
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   
   SdAssert(env);
   SdAssert(text);
   SdAssert(out_program_node);
   scanner = SdScanner_New();
   functions = SdList_New();
   statements = SdList_New();
   
   SdScanner_Tokenize(scanner, text);

   while (SdScanner_Peek(scanner, &token)) {
      SdTokenType token_type = SdToken_Type(token);
      SdValue_r node = NULL;
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

static SdResult SdParser_ReadExpectType(SdScanner_r scanner, SdTokenType expected_type, SdToken_r* out_token) {
   SdToken_r token = NULL;
   SdTokenType actual_type = SdTokenType_NONE;

   SdAssert(scanner);
   SdAssert(out_token);
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

static SdResult SdParser_ReadExpectEquals(SdScanner_r scanner) {
   SdToken_r token = NULL;

   SdAssert(scanner);
   if (!SdScanner_Read(scanner, &token))
      return SdParser_FailEof();

   if (SdToken_Type(token) != SdTokenType_IDENTIFIER || strcmp(SdToken_Text(token), "=") != 0)
      return SdParser_Fail(SdErr_UNEXPECTED_TOKEN, token, "Expected: =");
   else
      return SdResult_SUCCESS;
}

static SdResult SdParser_ParseFunction(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdBool is_imported = SdFalse;
   SdString* function_name = NULL;
   SdList* parameter_names = NULL;
   SdValue_r body = NULL;
   SdBool has_var_args = SdFalse;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   if (SdScanner_PeekType(scanner) == SdTokenType_IMPORT) {
      is_imported = SdTrue;
      SdParser_READ();
   }

   SdParser_READ_EXPECT_TYPE(SdTokenType_FUNCTION);
   SdParser_READ_IDENTIFIER(function_name);
   
   parameter_names = SdList_New();
   if (SdScanner_PeekType(scanner) == SdTokenType_OPEN_PAREN) {
      SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_PAREN);
      while (SdScanner_PeekType(scanner) == SdTokenType_IDENTIFIER) {
         SdValue_r parameter = NULL;
         SdParser_CALL(SdParser_ParseParameter(env, scanner, &parameter));
         SdList_Append(parameter_names, parameter);
      }
      SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_PAREN);
   } else {
      SdString* param_name = NULL;
      SdParser_READ_IDENTIFIER(param_name);
      SdList_Append(parameter_names, SdAst_Parameter_New(env, param_name, SdList_New()));
      has_var_args = SdTrue;
   }

   if (is_imported) {
      body = SdAst_Body_New(env, SdList_New());
   } else {
      if (SdScanner_PeekType(scanner) == SdTokenType_OPEN_BRACE) {
         SdParser_READ_BODY(body);
      } else {
         SdValue_r body_expr = NULL;
         SdList* body_statements = NULL;

         SdParser_CALL(SdParser_ReadExpectEquals(scanner));
         SdParser_READ_EXPR(body_expr);

         body_statements = SdList_New();
         SdList_Append(body_statements, SdAst_Return_New(env, body_expr));
         body = SdAst_Body_New(env, body_statements);
      }
   }

   *out_node = SdAst_Function_New(env, function_name, parameter_names, body, is_imported, has_var_args);
   function_name = NULL;
   parameter_names = NULL;
end:
   if (function_name) SdString_Delete(function_name);
   if (parameter_names) SdList_Delete(parameter_names);
   return result;
}

static SdResult SdParser_ParseParameter(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdString* identifier = NULL;
   SdString* type_name = NULL;
   SdList* type_names = NULL;
   
   SdParser_READ_IDENTIFIER(identifier);
   type_names = SdList_New();
   if (SdScanner_PeekType(scanner) == SdTokenType_COLON) {
      SdParser_READ_EXPECT_TYPE(SdTokenType_COLON);
      SdParser_READ_IDENTIFIER(type_name);
      SdList_Append(type_names, SdEnv_BoxString(env, type_name));
      type_name = NULL;
      
      while (SdScanner_PeekType(scanner) == SdTokenType_PIPE) {
         SdParser_READ_EXPECT_TYPE(SdTokenType_PIPE);
         SdParser_READ_IDENTIFIER(type_name);
         SdList_Append(type_names, SdEnv_BoxString(env, type_name));
         type_name = NULL;
      }
   }
   
   *out_node = SdAst_Parameter_New(env, identifier, type_names);
   identifier = NULL;
   type_names = NULL;
end:
   if (identifier) SdString_Delete(identifier);
   if (type_name) SdString_Delete(type_name);
   if (type_names) SdList_Delete(type_names);
   return result;
   
}

static SdResult SdParser_ParseBody(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdList* statements = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_BRACE);

   statements = SdList_New();
   while (SdScanner_PeekType(scanner) != SdTokenType_CLOSE_BRACE) {
      SdValue_r statement = NULL;
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

static SdResult SdParser_ParseExpr(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   switch (SdScanner_PeekType(scanner)) {
      case SdTokenType_INT_LIT: {
         int num = 0;
         SdParser_READ_EXPECT_TYPE(SdTokenType_INT_LIT);
         num = (int)strtol(SdToken_Text(token), NULL, 10);
         *out_node = SdAst_IntLit_New(env, num);
         break;
      }
      
      case SdTokenType_DOUBLE_LIT: {
         double num = 0;
         SdParser_READ_EXPECT_TYPE(SdTokenType_DOUBLE_LIT);
         num = atof(SdToken_Text(token));
         *out_node = SdAst_DoubleLit_New(env, num);
         break;
      }

      case SdTokenType_BOOL_LIT: {
         SdBool val = SdFalse;
         SdParser_READ_EXPECT_TYPE(SdTokenType_BOOL_LIT);
         val = strcmp(SdToken_Text(token), "true") == 0;
         *out_node = SdAst_BoolLit_New(env, val);
         break;
      }

      case SdTokenType_STRING_LIT: {
         char* str = NULL;
         const char* inner_str = NULL;
         size_t len = 0;

         SdParser_READ_EXPECT_TYPE(SdTokenType_STRING_LIT);
         str = SdStrdup(SdToken_Text(token));
         len = strlen(str);
         SdAssert(str[0] == '"');
         SdAssert(str[len - 1] == '"');
         
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

      case SdTokenType_LAMBDA:
         result = SdParser_ParseClosure(env, scanner, out_node);
         break;

      case SdTokenType_MATCH:
         result = SdParser_ParseMatch(env, scanner, out_node);
         break;

      default:
         result = SdParser_Fail(SdErr_UNEXPECTED_TOKEN, SdScanner_PeekToken(scanner), "Expected expression.");
         break;
   }

end:
   return result;
}

static SdResult SdParser_ParseClosure(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdList* param_names = NULL;
   SdValue_r body = NULL, expr = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_LAMBDA);

   param_names = SdList_New();
   if (SdScanner_PeekType(scanner) == SdTokenType_IDENTIFIER) {
      SdString* param_name = NULL;
      SdParser_READ_IDENTIFIER(param_name);
      SdList_Append(param_names, SdAst_Parameter_New(env, param_name, SdList_New()));
   } else {
      SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_PAREN);
      while (SdScanner_PeekType(scanner) == SdTokenType_IDENTIFIER) {
         SdString* param_name = NULL;
         SdParser_READ_IDENTIFIER(param_name);
         SdList_Append(param_names, SdAst_Parameter_New(env, param_name, SdList_New()));
      }
      SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_PAREN);
   }

   if (SdScanner_PeekType(scanner) == SdTokenType_OPEN_BRACE) {
      SdParser_READ_BODY(body);
   } else {
      SdList* statements = NULL;

      /* Transform this into a body with one return statement */
      SdParser_READ_EXPR(expr);
      statements = SdList_New();
      SdList_Append(statements, SdAst_Return_New(env, expr));
      body = SdAst_Body_New(env, statements);
   }

   *out_node = SdAst_Function_New(env, SdString_FromCStr("(closure)"), param_names, body, SdFalse, SdFalse);
   param_names = NULL;
end:
   if (param_names) SdList_Delete(param_names);
   return result;
}

static SdResult SdParser_ParseStatement(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
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
         SdToken_r token = NULL;
         SdScanner_Read(scanner, &token);
         return SdParser_Fail(SdErr_UNEXPECTED_TOKEN, token, "Expected statement.");
      }
   }
}

static SdResult SdParser_ParseCall(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdString* function_name = NULL;
   SdList* arguments = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
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

   *out_node = SdAst_Call_New(env, SdAst_VarRef_New(env, function_name), arguments);
   function_name = NULL;
   arguments = NULL;
end:
   if (function_name) SdString_Delete(function_name);
   if (arguments) SdList_Delete(arguments);
   return result;
}

static SdResult SdParser_ParseVar(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdString* identifier = NULL;
   SdValue_r expr = NULL;
   SdList* identifiers = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_VAR);
   
   if (SdScanner_PeekType(scanner) == SdTokenType_OPEN_PAREN) { /* multiple list element assignment */
      SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_PAREN);
      identifiers = SdList_New();
      while (SdScanner_PeekType(scanner) != SdTokenType_CLOSE_PAREN) {
         SdParser_READ_IDENTIFIER(identifier);
         SdList_Append(identifiers, SdEnv_BoxString(env, identifier));
         identifier = NULL;
      }
      SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_PAREN);

      SdParser_CALL(SdParser_ReadExpectEquals(scanner));
      SdParser_READ_EXPR(expr);

      *out_node = SdAst_MultiVar_New(env, identifiers, expr);
      identifiers = NULL;
   } else { /* single assignment */
      SdParser_READ_IDENTIFIER(identifier);
      SdParser_CALL(SdParser_ReadExpectEquals(scanner));
      SdParser_READ_EXPR(expr);

      *out_node = SdAst_Var_New(env, identifier, expr);
      identifier = NULL;
   }

end:
   if (identifiers) SdList_Delete(identifiers);
   if (identifier) SdString_Delete(identifier);
   return result;
}

static SdResult SdParser_ParseSet(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdString* identifier = NULL;
   SdValue_r expr = NULL;
   SdList* identifiers = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_SET);
   
   if (SdScanner_PeekType(scanner) == SdTokenType_OPEN_PAREN) { /* multiple list element assignment */
      SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_PAREN);
      identifiers = SdList_New();
      while (SdScanner_PeekType(scanner) != SdTokenType_CLOSE_PAREN) {
         SdParser_READ_IDENTIFIER(identifier);
         SdList_Append(identifiers, SdAst_VarRef_New(env, identifier));
         identifier = NULL;
      }
      SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_PAREN);

      SdParser_CALL(SdParser_ReadExpectEquals(scanner));
      SdParser_READ_EXPR(expr);

      *out_node = SdAst_MultiSet_New(env, identifiers, expr);
      identifiers = NULL;
   } else { /* single assignment */
      SdParser_READ_IDENTIFIER(identifier);
      SdParser_CALL(SdParser_ReadExpectEquals(scanner));
      SdParser_READ_EXPR(expr);

      *out_node = SdAst_Set_New(env, SdAst_VarRef_New(env, identifier), expr);
      identifier = NULL;
   }

end:
   if (identifiers) SdList_Delete(identifiers);
   if (identifier) SdString_Delete(identifier);
   return result;
}

static SdResult SdParser_ParseIf(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r condition_expr = NULL, true_body = NULL, else_body = NULL;
   SdList* elseifs = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_IF);
   SdParser_READ_EXPR(condition_expr);
   SdParser_READ_BODY(true_body);

   elseifs = SdList_New();
   while (SdScanner_PeekType(scanner) == SdTokenType_ELSEIF) {
      SdValue_r elseif = NULL;
      SdParser_CALL(SdParser_ParseElseIf(env, scanner, &elseif));
      SdAssert(elseif);
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

static SdResult SdParser_ParseElseIf(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr = NULL, body = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_ELSEIF);
   SdParser_READ_EXPR(expr);
   SdParser_READ_BODY(body);

   *out_node = SdAst_ElseIf_New(env, expr, body);
end:
   return result;
}

static SdResult SdParser_ParseFor(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdString* iter_name = NULL;
   SdString* indexer_name = NULL;
   SdValue_r start_expr = NULL, stop_expr = NULL, body = NULL, collection_expr = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
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

static SdResult SdParser_ParseWhile(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r body = NULL, expr = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_WHILE);
   SdParser_READ_EXPR(expr);
   SdParser_READ_BODY(body);

   *out_node = SdAst_While_New(env, expr, body);
end:
   return result;
}

static SdResult SdParser_ParseDo(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r body = NULL, expr = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_DO);
   SdParser_READ_BODY(body);
   SdParser_READ_EXPECT_TYPE(SdTokenType_WHILE);
   SdParser_READ_EXPR(expr);

   *out_node = SdAst_Do_New(env, expr, body);
end:
   return result;
}

static SdResult SdParser_ParseSwitch(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r default_body = NULL;
   SdList* condition_exprs = NULL;
   SdList* cases = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_SWITCH);

   condition_exprs = SdList_New();
   while (SdScanner_PeekType(scanner) != SdTokenType_OPEN_BRACE) {
      SdValue_r condition_expr = NULL;
      SdParser_READ_EXPR(condition_expr);
      SdList_Append(condition_exprs, condition_expr);
   }
   
   SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_BRACE);

   cases = SdList_New();
   while (SdScanner_PeekType(scanner) == SdTokenType_CASE) {
      SdValue_r case_v = NULL;
      SdParser_CALL(SdParser_ParseSwitchCase(env, scanner, &case_v));
      SdAssert(case_v);
      SdList_Append(cases, case_v);
   }

   if (SdScanner_PeekType(scanner) == SdTokenType_DEFAULT) {
      SdParser_READ_EXPECT_TYPE(SdTokenType_DEFAULT);
      SdParser_READ_EXPECT_TYPE(SdTokenType_COLON);
      SdParser_READ_BODY(default_body);
   } else {
      default_body = SdAst_Body_New(env, SdList_New());
   }

   SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_BRACE);

   *out_node = SdAst_Switch_New(env, condition_exprs, cases, default_body);
   condition_exprs = NULL;
   cases = NULL;
end:
   if (condition_exprs) SdList_Delete(condition_exprs);
   if (cases) SdList_Delete(cases);
   return result;
}

static SdResult SdParser_ParseSwitchCase(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r body = NULL;
   SdList* exprs = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_CASE);

   exprs = SdList_New();
   while (SdScanner_PeekType(scanner) != SdTokenType_COLON) {
      SdValue_r expr = NULL;
      SdParser_READ_EXPR(expr);
      SdList_Append(exprs, expr);
   }

   SdParser_READ_EXPECT_TYPE(SdTokenType_COLON);
   SdParser_READ_BODY(body);

   *out_node = SdAst_SwitchCase_New(env, exprs, body);
   exprs = NULL;
end:
   if (exprs) SdList_Delete(exprs);
   return result;
}

static SdResult SdParser_ParseMatch(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r default_expr = NULL;
   SdList* condition_exprs = NULL;
   SdList* cases = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_MATCH);

   condition_exprs = SdList_New();
   while (SdScanner_PeekType(scanner) != SdTokenType_OPEN_BRACE) {
      SdValue_r condition_expr = NULL;
      SdParser_READ_EXPR(condition_expr);
      SdList_Append(condition_exprs, condition_expr);
   }
   
   SdParser_READ_EXPECT_TYPE(SdTokenType_OPEN_BRACE);

   cases = SdList_New();
   while (SdScanner_PeekType(scanner) == SdTokenType_CASE) {
      SdValue_r case_v = NULL;
      SdParser_CALL(SdParser_ParseMatchCase(env, scanner, &case_v));
      SdAssert(case_v);
      SdList_Append(cases, case_v);
   }

   if (SdScanner_PeekType(scanner) == SdTokenType_DEFAULT) {
      SdParser_READ_EXPECT_TYPE(SdTokenType_DEFAULT);
      SdParser_READ_EXPECT_TYPE(SdTokenType_COLON);
      SdParser_READ_EXPR(default_expr);
   } else {
      default_expr = SdAst_NilLit_New(env);
   }

   SdParser_READ_EXPECT_TYPE(SdTokenType_CLOSE_BRACE);

   *out_node = SdAst_Match_New(env, condition_exprs, cases, default_expr);
   condition_exprs = NULL;
   cases = NULL;
end:
   if (condition_exprs) SdList_Delete(condition_exprs);
   if (cases) SdList_Delete(cases);
   return result;
}

static SdResult SdParser_ParseMatchCase(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r then_expr = NULL;
   SdList* exprs = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_CASE);

   exprs = SdList_New();
   while (SdScanner_PeekType(scanner) != SdTokenType_COLON) {
      SdValue_r expr = NULL;
      SdParser_READ_EXPR(expr);
      SdList_Append(exprs, expr);
   }

   SdParser_READ_EXPECT_TYPE(SdTokenType_COLON);
   SdParser_READ_EXPR(then_expr);

   *out_node = SdAst_MatchCase_New(env, exprs, then_expr);
   exprs = NULL;
end:
   if (exprs) SdList_Delete(exprs);
   return result;
}

static SdResult SdParser_ParseReturn(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_RETURN);
   SdParser_READ_EXPR(expr);
   *out_node = SdAst_Return_New(env, expr);
end:
   return result;
}

static SdResult SdParser_ParseDie(SdEnv_r env, SdScanner_r scanner, SdValue_r* out_node) {
   SdToken_r token = NULL;
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr = NULL;

   SdAssert(env);
   SdAssert(scanner);
   SdAssert(out_node);
   SdParser_READ_EXPECT_TYPE(SdTokenType_DIE);
   SdParser_READ_EXPR(expr);
   *out_node = SdAst_Die_New(env, expr);
end:
   return result;
}

/* SdEngine **********************************************************************************************************/
#define SdEngine_INTRINSIC_START_ARGS1(name) \
   static SdResult name(SdEngine_r self, SdList_r arguments, SdValue_r* out_return) { \
      SdResult result = SdResult_SUCCESS; \
      SdValue_r a_val = NULL; \
      SdType a_type = SdType_NIL; \
      const char* function_name = STRINGIFY(name); \
      SdAssert(self); \
      SdAssert(arguments); \
      SdAssert(out_return); \
      *out_return = NULL; \
      if (SdFailed(result = SdEngine_Args1(arguments, &a_val, &a_type))) \
         return result;
#define SdEngine_INTRINSIC_START_ARGS2(name) \
   static SdResult name(SdEngine_r self, SdList_r arguments, SdValue_r* out_return) { \
      SdResult result = SdResult_SUCCESS; \
      SdValue_r a_val = NULL, b_val = NULL; \
      SdType a_type = SdType_NIL, b_type = SdType_NIL; \
      const char* function_name = STRINGIFY(name); \
      SdAssert(self); \
      SdAssert(arguments); \
      SdAssert(out_return); \
      *out_return = NULL; \
      if (SdFailed(result = SdEngine_Args2(arguments, &a_val, &a_type, &b_val, &b_type))) \
         return result;
#define SdEngine_INTRINSIC_START_ARGS3(name) \
   static SdResult name(SdEngine_r self, SdList_r arguments, SdValue_r* out_return) { \
      SdResult result = SdResult_SUCCESS; \
      SdValue_r a_val = NULL, b_val = NULL, c_val = NULL; \
      SdType a_type = SdType_NIL, b_type = SdType_NIL, c_type = SdType_NIL; \
      const char* function_name = STRINGIFY(name); \
      SdAssert(self); \
      SdAssert(arguments); \
      SdAssert(out_return); \
      *out_return = NULL; \
      if (SdFailed(result = SdEngine_Args3(arguments, &a_val, &a_type, &b_val, &b_type, &c_val, &c_type))) \
         return result;
#define SdEngine_INTRINSIC_END \
      if (*out_return) \
         return SdResult_SUCCESS; \
      else { \
         SdString* name_str = SdString_FromCStr(function_name); \
         result = SdFailWithStringSuffix(SdErr_TYPE_MISMATCH, "Invalid argument type in function: ", name_str); \
         SdString_Delete(name_str); \
         return result; \
      } \
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
   SdEngine* self = NULL;
   
   SdAssert(env);
   self = SdAlloc(sizeof(SdEngine));
   self->env = env;
   return self;
}

void SdEngine_Delete(SdEngine* self) {
   SdAssert(self);
   SdFree(self);
}

SdResult SdEngine_ExecuteProgram(SdEngine_r self) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r root = NULL, frame = NULL;
   SdList_r functions = NULL, statements = NULL;
   size_t i = 0, count = 0;

   SdAssert(self);
   root = SdEnv_Root(self->env);
   functions = SdEnv_Root_Functions(root);
   statements = SdEnv_Root_Statements(root);
   frame = SdEnv_Root_BottomFrame(root);

   count = SdList_Count(functions);
   for (i = 0; i < count; i++) {
      SdValue_r function = NULL, closure = NULL;

      function = SdList_GetAt(functions, i);
      if (SdFailed(result = SdEngine_EvaluateFunction(self, frame, function, &closure)))
         return result;
      if (SdFailed(result = SdEnv_DeclareVar(self->env, frame, SdAst_Function_Name(function), closure)))
         return result;
   }

   count = SdList_Count(statements);
   for (i = 0; i < count; i++) {
      SdValue_r statement = NULL, return_value = NULL;

      statement = SdList_GetAt(statements, i);
      if (SdFailed(result = SdEngine_ExecuteStatement(self, frame, statement, &return_value)))
         return result;
      if (return_value)
         return result; /* a return statement breaks the program's execution */
   }

   return result;
}

SdResult SdEngine_Call(SdEngine_r self, SdValue_r frame, SdValue_r var_ref, SdList_r arguments,
   SdValue_r* out_return) {
   SdValue_r closure_slot = NULL, closure = NULL;
   SdString_r function_name = NULL;

   SdAssert(self);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(var_ref);
   SdAssert(arguments);
   SdAssert(out_return);
   
   function_name = SdAst_VarRef_Identifier(var_ref);
   
   /* ensure that 'function_name' refers to a defined closure */
   closure_slot = SdEnv_ResolveVarRefToSlot(self->env, frame, var_ref);
   if (!closure_slot)
      return SdFailWithStringSuffix(SdErr_UNDECLARED_VARIABLE, "Function not found: ", function_name);

   closure = SdEnv_VariableSlot_Value(closure_slot);
   if (SdValue_Type(closure) != SdType_FUNCTION)
      return SdFailWithStringSuffix(SdErr_TYPE_MISMATCH, "Not a function: ", function_name);

   return SdEngine_CallClosure(self, frame, closure, arguments, out_return);
}

static SdResult SdEngine_CallClosure(SdEngine_r self, SdValue_r frame, SdValue_r closure, SdList_r arguments, 
   SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r function = NULL, call_frame = NULL, total_arguments_value = NULL, actual_function_name = NULL;
   SdList_r parameters = NULL, partial_arguments = NULL, total_arguments = NULL;
   SdBool has_var_args = SdFalse, in_call = SdFalse, gc_needed = SdFalse;
   size_t i = 0, count = 0, partial_arguments_count = 0, total_arguments_count = 0;

   SdAssert(self);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssertValue(closure, SdType_FUNCTION);
   SdAssert(arguments);
   SdAssert(out_return);

   function = SdEnv_Closure_FunctionNode(closure);
   actual_function_name = SdAst_Function_Name(function);
      /* we may be calling through a closure stored in a variable with an arbitrary name, so grab the actual
         name that this function was originally defined with. */

   partial_arguments = SdValue_GetList(SdEnv_Closure_PartialArguments(closure));
   partial_arguments_count = SdList_Count(partial_arguments);
   total_arguments_count = partial_arguments_count + SdList_Count(arguments);

   /* ensure that the argument list matches the parameter list. skip the check for intrinsics since they are more 
      flexible and will do the check themselves. also skip the check for variable argument functions. */
   parameters = SdValue_GetList(SdAst_Function_Parameters(function));
   has_var_args = SdAst_Function_HasVariableLengthArgumentList(function);
   if (!SdAst_Function_IsImported(function) && !has_var_args && total_arguments_count > SdList_Count(parameters)) {
      result = SdFailWithStringSuffix(SdErr_ARGUMENT_MISMATCH, "Too many arguments to function: ", 
         SdValue_GetString(actual_function_name));
      goto end;
   }

   /* if any of the arguments are errors, then immediately return that error so that it propagates up the call chain,
      rather than executing the function. exception: type-of and error.message; these two functions are needed to
      actually handle errors. */
   if (!SdString_EqualsCStr(SdValue_GetString(actual_function_name), "type-of") && 
       !SdString_EqualsCStr(SdValue_GetString(actual_function_name), "error.message")) {
      count = SdList_Count(arguments);
      for (i = 0; i < count; i++) {
         SdValue_r argument = SdList_GetAt(arguments, i);
         if (SdValue_Type(argument) == SdType_ERROR) {
            *out_return = argument;
            goto end;
         }
      }
   }

   /* if this is a partial function application, then construct the closure and return it. */
   if (!has_var_args && total_arguments_count < SdList_Count(parameters)) {
      *out_return = SdEnv_Closure_CopyWithPartialArguments(closure, self->env, arguments);
      goto end;
   }

   /* construct the total arguments list from the partial arguments and the current arguments */
   count = SdList_Count(arguments);
   total_arguments = SdList_NewWithLength(count + partial_arguments_count);
   total_arguments_value = SdEnv_BoxList(self->env, total_arguments);
   for (i = 0; i < partial_arguments_count; i++)
      SdList_SetAt(total_arguments, i, SdList_GetAt(partial_arguments, i));
   for (i = 0; i < count; i++)
      SdList_SetAt(total_arguments, partial_arguments_count + i, SdList_GetAt(arguments, i));

   /* push an entry in the call stack so that call traces work */
   SdEnv_PushCall(self->env, frame, actual_function_name, total_arguments_value);
   in_call = SdTrue;

   /* if this is an intrinsic then call it now; no frame needed */
   if (SdAst_Function_IsImported(function)) {
      result = SdEngine_CallIntrinsic(self, SdValue_GetString(SdAst_Function_Name(function)), 
         total_arguments, out_return);
      goto end;
   }
   
   /* check the types of the arguments against any type annotations that may be present */
   if (!has_var_args) {
      count = SdList_Count(total_arguments);
      for (i = 0; i < count; i++) {
         SdValue_r parameter = NULL, argument = NULL;
         SdList_r type_names = NULL;
         size_t type_count = 0;
         
         parameter = SdList_GetAt(parameters, i);
         argument = SdList_GetAt(total_arguments, i);
         type_names = SdAst_Parameter_TypeNames(parameter);
         type_count = SdList_Count(type_names);
         
         if (type_count > 0) {
            SdBool is_match = SdFalse;
            size_t j = 0;
            
            for (j = 0; j < type_count; j++) {
               SdString_r type_name = NULL;
               SdValue_r slot = NULL, type_val = NULL;
               
               type_name = SdValue_GetString(SdList_GetAt(type_names, j));
               slot = SdEnv_FindVariableSlot(self->env, frame, type_name, SdTrue);
               if (!slot) {
                  result = SdFail(SdErr_UNDECLARED_VARIABLE, "Type annotation must evaluate to a type.");
                  goto end;
               }
               type_val = SdEnv_VariableSlot_Value(slot);
               if (SdValue_Equals(argument, type_val)) {
                  is_match = SdTrue;
                  break;
               }
            }
            
            if (!is_match) {
               result = SdFailWithStringSuffix(SdErr_TYPE_MISMATCH, "Type mismatch in function: ",
                  SdValue_GetString(actual_function_name));
               goto end;
            }
         }
      }
   }

   /* create a frame containing the argument values */
   call_frame = SdEnv_BeginFrame(self->env, SdEnv_Closure_Frame(closure));
   if (has_var_args) {
      SdValue_r param_name = SdAst_Parameter_Identifier(SdList_GetAt(parameters, 0));
      if (SdFailed(result = SdEnv_DeclareVar(self->env, call_frame, param_name, total_arguments_value)))
         goto end;
   } else {
      count = SdList_Count(total_arguments);
      for (i = 0; i < count; i++) {
         SdValue_r param_name, arg_value;
         
         param_name = SdAst_Parameter_Identifier(SdList_GetAt(parameters, i));
         arg_value = SdList_GetAt(total_arguments, i);
         if (SdFailed(result = SdEnv_DeclareVar(self->env, call_frame, param_name, arg_value)))
            goto end;
      }
   }

#if defined(SD_DEBUG_ALL) || defined(SD_DEBUG_GC)
   /* when running the memory leak detection, collect garbage before every statement to fish for bugs */
   gc_needed = SdTrue;
#else
   gc_needed = SdAlloc_BytesAllocatedSinceLastGc > SdEngine_ALLOCATED_BYTES_PER_GC;
#endif
   if (gc_needed) {
      SdEnv_CollectGarbage(self->env);
      SdAlloc_BytesAllocatedSinceLastGc = 0;
   }

   /* execute the function body using the frame we just constructed */
   result = SdEngine_ExecuteBody(self, call_frame, SdAst_Function_Body(function), out_return);

end:
   /* if the function did not return a value, then implicitly return a nil */
   if (!*out_return)
      *out_return = SdEnv_BoxNil(self->env);

   if (in_call) SdEnv_PopCall(self->env);
   if (call_frame) SdEnv_EndFrame(self->env, call_frame);
   return result;
}

static SdResult SdEngine_EvaluateExpr(SdEngine_r self, SdValue_r frame, SdValue_r expr, SdValue_r* out_value) {
   SdResult result = SdResult_SUCCESS;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(expr);
   SdAssert(out_value);
   SdAssertNode(frame, SdNodeType_FRAME);

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

      case SdNodeType_CALL:
         result = SdEngine_ExecuteCall(self, frame, expr, out_value);
         break;

      case SdNodeType_FUNCTION:
         result = SdEngine_EvaluateFunction(self, frame, expr, out_value);
         break;

      case SdNodeType_MATCH:
         result = SdEngine_EvaluateMatch(self, frame, expr, out_value);
         break;

      default:
         result = SdFail(SdErr_INTERPRETER_BUG, "Unexpected node type.");
         break;
   }

   return result;
}

static SdResult SdEngine_EvaluateVarRef(SdEngine_r self, SdValue_r frame, SdValue_r var_ref, SdValue_r* out_value) {
   SdValue_r slot = NULL;

   SdAssert(self);
   SdAssert(out_value);
   slot = SdEnv_ResolveVarRefToSlot(self->env, frame, var_ref);
   if (slot) {
      *out_value = SdEnv_VariableSlot_Value(slot);
      return SdResult_SUCCESS;
   } else {
      SdString_r identifier = SdAst_VarRef_Identifier(var_ref);
      return SdFailWithStringSuffix(SdErr_UNDECLARED_VARIABLE, "Undeclared variable: ", identifier);
   }
}

static SdResult SdEngine_EvaluateFunction(SdEngine_r self, SdValue_r frame, SdValue_r function, SdValue_r* out_value) {
   SdAssert(self);
   SdAssert(frame);
   SdAssert(function);
   SdAssert(out_value);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(function) == SdNodeType_FUNCTION);

   *out_value = SdEnv_Closure_New(self->env, frame, function, SdEnv_BoxList(self->env, SdList_New()));
   return SdResult_SUCCESS;
}

static SdResult SdEngine_EvaluateMatch(SdEngine_r self, SdValue_r frame, SdValue_r match, SdValue_r* out_value) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r cas = NULL, default_expr = NULL;
   SdList_r exprs = NULL, cases = NULL;
   SdValue_r* exprs_values = NULL;
   SdValue_r* case_exprs_values = NULL;
   size_t i = 0, j = 0, exprs_count = 0, cases_count = 0;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(match);
   SdAssert(out_value);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(match) == SdNodeType_MATCH);

   exprs = SdAst_Match_Exprs(match);
   cases = SdAst_Match_Cases(match);
   default_expr = SdAst_Match_DefaultExpr(match);

   if (SdList_Count(exprs) == 0) { /* we're matching the function arguments */
      SdValue_r trace = SdEnv_GetCurrentCallTrace(self->env);
      if (trace) {
         SdList_r args_list = SdValue_GetList(SdEnv_CallTrace_Arguments(trace));
         exprs_count = SdList_Count(args_list);
         exprs_values = SdAlloc(exprs_count * sizeof(SdValue_r));
         for (i = 0; i < exprs_count; i++)
            exprs_values[i] = SdList_GetAt(args_list, i);
      } else {
         exprs_count = 0;
         exprs_values = SdAlloc(0);
      }
   } else {
      exprs_count = SdList_Count(exprs);
      exprs_values = SdAlloc(exprs_count * sizeof(SdValue_r));
      for (i = 0; i < exprs_count; i++) {
         if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, SdList_GetAt(exprs, i), &exprs_values[i])))
            goto end;
      }
   }

   cases_count = SdList_Count(cases);
   for (i = 0; i < cases_count; i++) {
      SdList_r case_exprs = NULL;
      size_t case_exprs_count = 0;
      SdBool is_match = SdTrue;

      cas = SdList_GetAt(cases, i);
      case_exprs = SdAst_MatchCase_IfExprs(cas);
      case_exprs_count = SdList_Count(case_exprs);
      if (case_exprs_count != exprs_count) {
         result = SdFail(SdErr_ARGUMENT_MISMATCH, 
            "The number of case values does not match the number of match arguments.");
         goto end;
      }

      case_exprs_values = SdAlloc(exprs_count * sizeof(SdValue_r));
      for (j = 0; j < case_exprs_count; j++) {
         if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, SdList_GetAt(case_exprs, j), &case_exprs_values[j])))
            goto end;
      }
      
      for (j = 0; j < case_exprs_count; j++) {
         SdValue_r match_arg = NULL, case_value = NULL;

         match_arg = exprs_values[j];
         case_value = case_exprs_values[j];
         if (!SdValue_Equals(match_arg, case_value)) {
            is_match = SdFalse;
            break;
         }
      }

      if (is_match) {
         SdValue_r then_expr = SdAst_MatchCase_ThenExpr(cas);
         result = SdEngine_EvaluateExpr(self, frame, then_expr, out_value);
         goto end;
      }

      SdFree(case_exprs_values);
      case_exprs_values = NULL;
   }

   result = SdEngine_EvaluateExpr(self, frame, default_expr, out_value);
end:
   if (exprs_values) SdFree(exprs_values);
   if (case_exprs_values) SdFree(case_exprs_values);
   return result;
}

static SdResult SdEngine_ExecuteBody(SdEngine_r self, SdValue_r frame, SdValue_r body, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdList_r statements = NULL;
   SdValue_r statement = NULL, body_frame = NULL;
   size_t i = 0, count = 0;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(body);
   SdAssert(out_return);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(body) == SdNodeType_BODY);

   statements = SdAst_Body_Statements(body);
   body_frame = SdEnv_BeginFrame(self->env, frame);

   count = SdList_Count(statements);
   for (i = 0; i < count; i++) {
      statement = SdList_GetAt(statements, i);
      *out_return = NULL;
      if (SdFailed(result = SdEngine_ExecuteStatement(self, frame, statement, out_return)))
         goto end;
      if (*out_return) /* a return statement breaks the body */
         goto end;
   }

end:
   if (body_frame) SdEnv_EndFrame(self->env, body_frame);
   return result;
}

static SdResult SdEngine_ExecuteStatement(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdValue_r discarded_result = NULL;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssert(out_return);
   SdAssertNode(frame, SdNodeType_FRAME);

   switch (SdAst_NodeType(statement)) {
      case SdNodeType_CALL: return SdEngine_ExecuteCall(self, frame, statement, &discarded_result);
      case SdNodeType_VAR: return SdEngine_ExecuteVar(self, frame, statement);
      case SdNodeType_SET: return SdEngine_ExecuteSet(self, frame, statement);
      case SdNodeType_MULTI_VAR: return SdEngine_ExecuteMultiVar(self, frame, statement);
      case SdNodeType_MULTI_SET: return SdEngine_ExecuteMultiSet(self, frame, statement);
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

static SdResult SdEngine_ExecuteCall(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdList_r argument_exprs = NULL;
   SdList* argument_values = NULL;
   size_t i = 0, num_arguments = 0, num_protected_values = 0;
   
   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssert(out_return);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_CALL);

   argument_exprs = SdAst_Call_Arguments(statement);
   num_arguments = SdList_Count(argument_exprs);
   argument_values = SdList_NewWithLength(num_arguments);

   for (i = 0; i < num_arguments; i++) {
      SdValue_r argument_expr = NULL, argument_value = NULL;
      argument_expr = SdList_GetAt(argument_exprs, i);
      if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, argument_expr, &argument_value)))
         goto end;
      SdEnv_PushProtectedValue(self->env, argument_value);
      num_protected_values++;
      SdList_SetAt(argument_values, i, argument_value);
   }

   result = SdEngine_Call(self, frame, SdAst_Call_VarRef(statement), argument_values, out_return);
end:
   while (num_protected_values-- > 0)
      SdEnv_PopProtectedValue(self->env);
   if (argument_values) SdList_Delete(argument_values);
   return result;
}

static SdResult SdEngine_ExecuteVar(SdEngine_r self, SdValue_r frame, SdValue_r statement) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r name = NULL, expr = NULL, value = NULL;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_VAR);

   name = SdAst_Var_VariableName(statement);
   expr = SdAst_Var_ValueExpr(statement);
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
      return result;

   return SdEnv_DeclareVar(self->env, frame, name, value);
}

static SdResult SdEngine_ExecuteMultiVar(SdEngine_r self, SdValue_r frame, SdValue_r statement) {
   SdResult result = SdResult_SUCCESS;
   SdList_r names = NULL, list = NULL;
   SdValue_r expr = NULL, list_value = NULL, element_value = NULL, name = NULL;
   size_t i = 0, names_count = 0, list_count = 0;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_MULTI_VAR);

   names = SdAst_MultiVar_VariableNames(statement);
   expr = SdAst_MultiVar_ValueExpr(statement);
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &list_value)))
      return result;
   if (SdValue_Type(list_value) != SdType_LIST)
      return SdFail(SdErr_TYPE_MISMATCH, "Multi-VAR statement expected a list on the right-hand side.");
   
   list = SdValue_GetList(list_value);
   list_count = SdList_Count(list);
   names_count = SdList_Count(names);
   for (i = 0; i < names_count; i++) {
      name = SdList_GetAt(names, i);
      element_value = (i < list_count) ? SdList_GetAt(list, i) : SdEnv_BoxNil(self->env);

      if (SdFailed(result = SdEnv_DeclareVar(self->env, frame, name, element_value)))
         return result;
   }

   return result;
}

static SdResult SdEngine_ExecuteSet(SdEngine_r self, SdValue_r frame, SdValue_r statement) {
   SdResult result = SdResult_SUCCESS;
   SdString_r name = NULL;
   SdValue_r slot = NULL, expr = NULL, value = NULL, var_ref = NULL;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_SET);

   /* the variable slot must already exist */
   var_ref = SdAst_Set_VarRef(statement);
   slot = SdEnv_ResolveVarRefToSlot(self->env, frame, var_ref);
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

static SdResult SdEngine_ExecuteMultiSet(SdEngine_r self, SdValue_r frame, SdValue_r statement) {
   SdResult result = SdResult_SUCCESS;
   SdList_r list = NULL, var_refs = NULL;
   SdValue_r expr = NULL, list_value = NULL, element_value = NULL, slot = NULL, name = NULL;
   size_t i = 0, names_count = 0, list_count = 0;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_MULTI_SET);

   var_refs = SdAst_MultiSet_VarRefs(statement);
   expr = SdAst_MultiSet_ValueExpr(statement);
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &list_value)))
      return result;
   if (SdValue_Type(list_value) != SdType_LIST)
      return SdFail(SdErr_TYPE_MISMATCH, "Multi-VAR statement expected a list on the right-hand side.");
   
   list = SdValue_GetList(list_value);
   list_count = SdList_Count(list);
   names_count = SdList_Count(var_refs);
   for (i = 0; i < names_count; i++) {
      SdValue_r var_ref = SdList_GetAt(var_refs, i);
      element_value = (i < list_count) ? SdList_GetAt(list, i) : SdEnv_BoxNil(self->env);

      /* the variable slot must already exist */
      slot = SdEnv_ResolveVarRefToSlot(self->env, frame, var_ref);
      if (!slot)
         return SdFailWithStringSuffix(SdErr_UNDECLARED_VARIABLE, "Undeclared variable: ", SdValue_GetString(name));

      SdEnv_VariableSlot_SetValue(slot, element_value);
   }

   return result;
}

static SdResult SdEngine_ExecuteIf(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r elseif = NULL, expr = NULL, value = NULL;
   SdList_r elseifs = NULL;
   size_t i = 0, count = 0;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssert(out_return);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_IF);

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

static SdResult SdEngine_ExecuteFor(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r iter_name = NULL, start_expr = NULL, start_value = NULL, stop_expr = NULL, stop_value = NULL, 
      body = NULL, loop_frame = NULL;
   int i = 0, start = 0, stop = 0;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssert(out_return);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_FOR);

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
      loop_frame = SdEnv_BeginFrame(self->env, frame);
      if (SdFailed(result = SdEnv_DeclareVar(self->env, loop_frame, iter_name, SdEnv_BoxInt(self->env, (int)i))))
         goto end;
      *out_return = NULL;
      if (SdFailed(result = SdEngine_ExecuteBody(self, loop_frame, body, out_return)))
         goto end;
      if (*out_return) /* a return statement inside the loop will break from the loop */
         goto end;
      SdEnv_EndFrame(self->env, loop_frame);
      loop_frame = NULL;
   }

end:
   if (loop_frame) SdEnv_EndFrame(self->env, loop_frame);
   return result;
}

static SdResult SdEngine_ExecuteForEach(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r iter_name = NULL, index_name = NULL, haystack_expr = NULL, haystack_value = NULL, body = NULL, 
      loop_frame = NULL;
   SdList_r haystack = NULL;
   SdList* empty_list = NULL;
   size_t i = 0, count = 0, num_protected_values = 0;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssert(out_return);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_FOREACH);

   iter_name = SdAst_ForEach_IterName(statement);
   index_name = SdAst_ForEach_IndexName(statement);
   haystack_expr = SdAst_ForEach_HaystackExpr(statement);
   body = SdAst_ForEach_Body(statement);

   /* evaluate the IN expression */
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, haystack_expr, &haystack_value)))
      return result;
   
   if (SdValue_Type(haystack_value) == SdType_LIST) {
      haystack = SdValue_GetList(haystack_value);

      /* enumerate the list */
      count = SdList_Count(haystack);
      for (i = 0; i < count; i++) {
         SdValue_r iter_value = NULL;

         iter_value = SdList_GetAt(haystack, i);
         loop_frame = SdEnv_BeginFrame(self->env, frame);
         if (SdFailed(result = SdEnv_DeclareVar(self->env, loop_frame, iter_name, iter_value)))
            goto end;
         if (SdValue_Type(index_name) != SdType_NIL) { /* user may not have specified an indexer variable */
            if (SdFailed(result = SdEnv_DeclareVar(self->env, loop_frame, index_name, SdEnv_BoxInt(self->env, (int)i))))
               goto end;
         }
         *out_return = NULL;
         if (SdFailed(result = SdEngine_ExecuteBody(self, loop_frame, body, out_return)))
            goto end;
         if (*out_return) /* a return statement inside the loop will break from the loop */
            goto end;
         SdEnv_EndFrame(self->env, loop_frame);
         loop_frame = NULL;
      }
   } else if (SdValue_Type(haystack_value) == SdType_FUNCTION) { /* haystack_value is a stream */
      SdValue_r stream_func = haystack_value, iterator_func = NULL;
      empty_list = SdList_New();

      SdEnv_PushProtectedValue(self->env, stream_func);
      num_protected_values++;

      /* call stream_func to get an iterator_func */
      if (SdFailed(result = SdEngine_CallClosure(self, frame, stream_func, empty_list, &iterator_func)))
         goto end;
      if (SdValue_Type(iterator_func) != SdType_FUNCTION) {
         result = SdFail(SdErr_TYPE_MISMATCH, "FOREACH expected a list or stream.");
         goto end;
      }

      SdEnv_PushProtectedValue(self->env, iterator_func);
      num_protected_values++;

      /* repeatedly call iterator_func to get values */
      for (i = 0; SdTrue; i++) {
         SdValue_r iter_value = NULL;

         if (SdFailed(result = SdEngine_CallClosure(self, frame, iterator_func, empty_list, &iter_value)))
            goto end;
         if (SdValue_Type(iter_value) == SdType_NIL)
            break;

         loop_frame = SdEnv_BeginFrame(self->env, frame);
         if (SdFailed(result = SdEnv_DeclareVar(self->env, loop_frame, iter_name, iter_value)))
            goto end;
         if (SdValue_Type(index_name) != SdType_NIL) { /* user may not have specified an indexer variable */
            if (SdFailed(result = SdEnv_DeclareVar(self->env, loop_frame, index_name, SdEnv_BoxInt(self->env, (int)i))))
               goto end;
         }
         *out_return = NULL;
         if (SdFailed(result = SdEngine_ExecuteBody(self, loop_frame, body, out_return)))
            goto end;
         if (*out_return) /* a return statement inside the loop will break from the loop */
            goto end;
         SdEnv_EndFrame(self->env, loop_frame);
         loop_frame = NULL;
      }
   }

end:
   while (num_protected_values-- > 0)
      SdEnv_PopProtectedValue(self->env);
   if (empty_list) SdList_Delete(empty_list);
   if (loop_frame) SdEnv_EndFrame(self->env, loop_frame);
   return result;
}

static SdResult SdEngine_ExecuteWhile(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr = NULL, value = NULL, body = NULL, loop_frame = NULL;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssert(out_return);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_WHILE);

   expr = SdAst_While_ConditionExpr(statement);
   body = SdAst_While_Body(statement);

   while (SdTrue) {
      if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
         return result;
      if (SdValue_Type(value) != SdType_BOOL)
         return SdFail(SdErr_TYPE_MISMATCH, "WHILE expression does not evaluate to a Boolean.");
      if (!SdValue_GetBool(value))
         break;

      loop_frame = SdEnv_BeginFrame(self->env, frame);
      *out_return = NULL;
      if (SdFailed(result = SdEngine_ExecuteBody(self, loop_frame, body, out_return)))
         goto end;
      if (*out_return) /* a return statement inside the loop will break from the loop */
         goto end;
      SdEnv_EndFrame(self->env, loop_frame);
      loop_frame = NULL;
   }

end:
   if (loop_frame) SdEnv_EndFrame(self->env, loop_frame);
   return result;
}

static SdResult SdEngine_ExecuteDo(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr = NULL, value = NULL, body = NULL, loop_frame = NULL;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssert(out_return);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_DO);

   expr = SdAst_Do_ConditionExpr(statement);
   body = SdAst_Do_Body(statement);

   while (SdTrue) {
      loop_frame = SdEnv_BeginFrame(self->env, frame);
      *out_return = NULL;
      if (SdFailed(result = SdEngine_ExecuteBody(self, loop_frame, body, out_return)))
         goto end;
      if (*out_return) /* a return statement inside the loop will break from the loop */
         goto end;

      if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
         goto end;
      if (SdValue_Type(value) != SdType_BOOL) {
         result = SdFail(SdErr_TYPE_MISMATCH, "WHILE expression does not evaluate to a Boolean.");
         goto end;
      }
      if (!SdValue_GetBool(value))
         goto end;
      SdEnv_EndFrame(self->env, loop_frame);
      loop_frame = NULL;
   }

end:
   if (loop_frame) SdEnv_EndFrame(self->env, loop_frame);
   return result;
}

static SdResult SdEngine_ExecuteSwitch(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r cas = NULL, default_body = NULL, case_frame = NULL;
   SdList_r exprs = NULL, cases = NULL;
   SdValue_r* exprs_values = NULL;
   SdValue_r* case_exprs_values = NULL;
   size_t i = 0, j = 0, exprs_count = 0, cases_count = 0;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssert(out_return);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_SWITCH);

   exprs = SdAst_Switch_Exprs(statement);
   cases = SdAst_Switch_Cases(statement);
   default_body = SdAst_Switch_DefaultBody(statement);

   if (SdList_Count(exprs) == 0) { /* we're matching the function arguments */
      SdValue_r trace = SdEnv_GetCurrentCallTrace(self->env);
      if (trace) {
         SdList_r args_list = SdValue_GetList(SdEnv_CallTrace_Arguments(trace));
         exprs_count = SdList_Count(args_list);
         exprs_values = SdAlloc(exprs_count * sizeof(SdValue_r));
         for (i = 0; i < exprs_count; i++)
            exprs_values[i] = SdList_GetAt(args_list, i);
      } else {
         exprs_count = 0;
         exprs_values = SdAlloc(0);
      }
   } else {
      exprs_count = SdList_Count(exprs);
      exprs_values = SdAlloc(exprs_count * sizeof(SdValue_r));
      for (i = 0; i < exprs_count; i++) {
         if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, SdList_GetAt(exprs, i), &exprs_values[i])))
            goto end;
      }
   }

   cases_count = SdList_Count(cases);
   for (i = 0; i < cases_count; i++) {
      SdList_r case_exprs = NULL;
      size_t case_exprs_count = 0;
      SdBool is_match = SdTrue;

      cas = SdList_GetAt(cases, i);
      case_exprs = SdAst_SwitchCase_IfExprs(cas);
      case_exprs_count = SdList_Count(case_exprs);
      if (case_exprs_count != exprs_count) {
         result = SdFail(SdErr_ARGUMENT_MISMATCH, 
            "The number of case values does not match the number of switch arguments.");
         goto end;
      }

      case_exprs_values = SdAlloc(exprs_count * sizeof(SdValue_r));
      for (j = 0; j < case_exprs_count; j++) {
         if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, SdList_GetAt(case_exprs, j), &case_exprs_values[j])))
            goto end;
      }
      
      for (j = 0; j < case_exprs_count; j++) {
         SdValue_r switch_arg = NULL, case_value = NULL;

         switch_arg = exprs_values[j];
         case_value = case_exprs_values[j];
         if (!SdValue_Equals(switch_arg, case_value)) {
            is_match = SdFalse;
            break;
         }
      }

      if (is_match) {
         SdValue_r case_body = NULL;

         case_body = SdAst_SwitchCase_ThenBody(cas);
         case_frame = SdEnv_BeginFrame(self->env, frame);
         result = SdEngine_ExecuteBody(self, case_frame, case_body, out_return);
         SdEnv_EndFrame(self->env, case_frame);
         goto end;
      }

      SdFree(case_exprs_values);
      case_exprs_values = NULL;
   }

   case_frame = SdEnv_BeginFrame(self->env, frame);
   result = SdEngine_ExecuteBody(self, case_frame, default_body, out_return);
   SdEnv_EndFrame(self->env, case_frame);
end:
   if (exprs_values) SdFree(exprs_values);
   if (case_exprs_values) SdFree(case_exprs_values);
   return result;
}

static SdResult SdEngine_ExecuteReturn(SdEngine_r self, SdValue_r frame, SdValue_r statement, SdValue_r* out_return) {
   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssert(out_return);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_RETURN);

   return SdEngine_EvaluateExpr(self, frame, SdAst_Return_Expr(statement), out_return);
}

static SdResult SdEngine_ExecuteDie(SdEngine_r self, SdValue_r frame, SdValue_r statement) {
   SdResult result = SdResult_SUCCESS;
   SdValue_r expr = NULL, value = NULL;

   SdAssert(self);
   SdAssert(frame);
   SdAssert(statement);
   SdAssertNode(frame, SdNodeType_FRAME);
   SdAssert(SdAst_NodeType(statement) == SdNodeType_DIE);

   expr = SdAst_Die_Expr(statement);
   if (SdFailed(result = SdEngine_EvaluateExpr(self, frame, expr, &value)))
      return result;
   if (SdValue_Type(value) != SdType_STRING)
      return SdFail(SdErr_TYPE_MISMATCH, "DIE expression does not evaluate to a String.");

   return SdFail(SdErr_DIED, SdString_CStr(SdValue_GetString(value)));
}

static SdResult SdEngine_CallIntrinsic(SdEngine_r self, SdString_r name, SdList_r arguments, SdValue_r* out_return) {
   const char* cstr = NULL;
   
   SdAssert(self);
   SdAssert(name);
   SdAssert(arguments);
   SdAssert(out_return);

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
         INTRINSIC("bitwise-shift-left", SdEngine_Intrinsic_ShiftLeft);
         INTRINSIC("bitwise-shift-right", SdEngine_Intrinsic_ShiftRight);
         break;

      case 'c':
         INTRINSIC("ceil", SdEngine_Intrinsic_Ceil);
         INTRINSIC("cos", SdEngine_Intrinsic_Cos);
         INTRINSIC("cosh", SdEngine_Intrinsic_CosH);
         break;

      case 'd':
         INTRINSIC("double.<", SdEngine_Intrinsic_DoubleLessThan);
         INTRINSIC("double.to-int", SdEngine_Intrinsic_DoubleToInt);
         break;

      case 'e':
         INTRINSIC("exp", SdEngine_Intrinsic_Exp);
         INTRINSIC("error", SdEngine_Intrinsic_Error);
         INTRINSIC("error.message", SdEngine_Intrinsic_ErrorMessage);
         break;

      case 'f':
         INTRINSIC("floor", SdEngine_Intrinsic_Floor);
         break;

      case 'g':
         INTRINSIC("get-type", SdEngine_Intrinsic_GetType);
         break;

      case 'h':
         INTRINSIC("hash", SdEngine_Intrinsic_Hash);
         break;

      case 'i':
         INTRINSIC("int.<", SdEngine_Intrinsic_IntLessThan);
         INTRINSIC("int.to-double", SdEngine_Intrinsic_IntToDouble);
         break;

      case 'l':
         INTRINSIC("log", SdEngine_Intrinsic_Log);
         INTRINSIC("log10", SdEngine_Intrinsic_Log10);
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
         INTRINSIC("string.<", SdEngine_Intrinsic_StringLessThan);
         INTRINSIC("string.join", SdEngine_Intrinsic_StringJoin);
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
   }
   
#undef INTRINSIC

   return SdFailWithStringSuffix(SdErr_UNDECLARED_VARIABLE, "Not an intrinsic function: ", name);
}

static SdResult SdEngine_Args1(SdList_r arguments, SdValue_r* out_a, SdType* out_a_type) {
   SdAssert(arguments);
   SdAssert(out_a);
   SdAssert(out_a_type);

   if (SdList_Count(arguments) != 1)
      return SdFail(SdErr_ARGUMENT_MISMATCH, "Expected 1 argument.");
   *out_a = SdList_GetAt(arguments, 0);
   *out_a_type = SdValue_Type(*out_a);
   return SdResult_SUCCESS;
}

static SdResult SdEngine_Args2(SdList_r arguments, SdValue_r* out_a, SdType* out_a_type, SdValue_r* out_b, 
   SdType* out_b_type) {
   SdAssert(arguments);
   SdAssert(out_a);
   SdAssert(out_a_type);
   SdAssert(out_b);
   SdAssert(out_b_type);

   if (SdList_Count(arguments) != 2)
      return SdFail(SdErr_ARGUMENT_MISMATCH, "Expected 2 arguments.");
   *out_a = SdList_GetAt(arguments, 0);
   *out_a_type = SdValue_Type(*out_a);
   *out_b = SdList_GetAt(arguments, 1);
   *out_b_type = SdValue_Type(*out_b);
   return SdResult_SUCCESS;
}

static SdResult SdEngine_Args3(SdList_r arguments, SdValue_r* out_a, SdType* out_a_type, SdValue_r* out_b, 
   SdType* out_b_type, SdValue_r* out_c, SdType* out_c_type) {
   SdAssert(arguments);
   SdAssert(out_a);
   SdAssert(out_a_type);
   SdAssert(out_b);
   SdAssert(out_b_type);
   SdAssert(out_c);
   SdAssert(out_c_type);

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
   *out_return = SdEnv_BoxType(self->env, a_type);
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
         char buf[30] = { 0 };
         sprintf(buf, "%d", SdValue_GetInt(a_val));
         *out_return = SdEnv_BoxString(self->env, SdString_FromCStr(buf));
         break;
      }
      case SdType_DOUBLE: {
         char buf[500] = { 0 };
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
      case SdType_FUNCTION:
         *out_return = SdEnv_BoxString(self->env, SdString_FromCStr("(function)"));
         break;
      case SdType_ERROR:
         *out_return = SdEnv_BoxString(self->env, SdString_FromCStr("(error)"));
         break;
      case SdType_TYPE:
         *out_return = SdEnv_BoxString(self->env, SdString_FromCStr(SdType_Name(SdValue_GetInt(a_val))));
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
         SdList* list = NULL;
         SdList_r a_list = NULL, b_list = NULL;
         size_t i = 0, a_count = 0, b_count = 0;

         a_list = SdValue_GetList(a_val);
         a_count = SdList_Count(a_list);
         b_list = SdValue_GetList(b_val);
         b_count = SdList_Count(b_list);
         list = SdList_NewWithLength(a_count + b_count);

         for (i = 0; i < a_count; i++)
            SdList_SetAt(list, i, SdList_GetAt(a_list, i));

         for (i = 0; i < b_count; i++)
            SdList_SetAt(list, a_count + i, SdList_GetAt(b_list, i));

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
SdEngine_INTRINSIC_INT2(SdEngine_Intrinsic_ShiftLeft, a << b)
SdEngine_INTRINSIC_INT2(SdEngine_Intrinsic_ShiftRight, a >> b)

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_ListLength)
   if (a_type == SdType_LIST) {
      *out_return = SdEnv_BoxInt(self->env, (int)SdList_Count(SdValue_GetList(a_val)));
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
      SdList_r a_list = NULL;
      int b_int = 0;

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
      *out_return = SdEnv_BoxInt(self->env, (int)SdString_Length(SdValue_GetString(a_val)));
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS2(SdEngine_Intrinsic_StringGetAt)
   if (a_type == SdType_STRING && b_type == SdType_INT) {
      char char_str[2] = { 0 };
      SdString_r a_str = NULL;
      int b_int = 0;

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

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_Error)
   if (a_type == SdType_STRING) {
      SdList* error_list = SdList_NewWithLength(1);
      SdList_SetAt(error_list, 0, a_val);
      *out_return = SdEnv_BoxError(self->env, error_list);
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_ErrorMessage)
   SdUnreferenced(self);
   if (a_type == SdType_ERROR) {
      SdList_r error_list = SdValue_GetList(a_val);
      *out_return = SdList_GetAt(error_list, 0);
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_GetType)
   if (a_type == SdType_INT) {
      *out_return = SdEnv_BoxType(self->env, (SdType)SdValue_GetInt(a_val));
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS2(SdEngine_Intrinsic_IntLessThan)
   if (a_type != SdType_INT || b_type != SdType_INT)
      return SdFail(SdErr_TYPE_MISMATCH, "Arguments must be integers.");
   *out_return = SdEnv_BoxBool(self->env, SdValue_GetInt(a_val) < SdValue_GetInt(b_val));
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS2(SdEngine_Intrinsic_DoubleLessThan)
   if (a_type != SdType_DOUBLE || b_type != SdType_DOUBLE)
      return SdFail(SdErr_TYPE_MISMATCH, "Arguments must be doubles.");
   *out_return = SdEnv_BoxBool(self->env, SdValue_GetDouble(a_val) < SdValue_GetDouble(b_val));
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS2(SdEngine_Intrinsic_StringLessThan)
   if (a_type != SdType_STRING || b_type != SdType_STRING)
      return SdFail(SdErr_TYPE_MISMATCH, "Arguments must be strings.");
   *out_return = SdEnv_BoxBool(self->env, SdString_Compare(SdValue_GetString(a_val), SdValue_GetString(b_val)) < 0);
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_IntToDouble)
   if (a_type == SdType_INT) {
      *out_return = SdEnv_BoxDouble(self->env, (double)SdValue_GetInt(a_val));
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS1(SdEngine_Intrinsic_DoubleToInt)
   if (a_type == SdType_DOUBLE) {
      *out_return = SdEnv_BoxInt(self->env, (int)SdValue_GetDouble(a_val));
   }
SdEngine_INTRINSIC_END

SdEngine_INTRINSIC_START_ARGS2(SdEngine_Intrinsic_StringJoin)
   if (a_type == SdType_STRING && b_type == SdType_LIST) {
      SdStringBuf* buf = NULL;
      SdString_r separator = NULL;
      SdList_r strings = NULL;
      size_t i = 0, count = 0;

      separator = SdValue_GetString(a_val);
      strings = SdValue_GetList(b_val);
      count = SdList_Count(strings);

      if (count == 0) {
         *out_return = SdEnv_BoxString(self->env, SdString_New());
         return SdResult_SUCCESS;
      }

      buf = SdStringBuf_New();

      for (i = 0; i < count; i++) {
         SdValue_r str = NULL;

         if (i > 0)
            SdStringBuf_AppendString(buf, separator);

         str = SdList_GetAt(strings, i);
         if (SdValue_Type(str) == SdType_STRING) {
            SdStringBuf_AppendString(buf, SdValue_GetString(str));
         } else {
            SdStringBuf_Delete(buf);
            return SdFail(SdErr_TYPE_MISMATCH, "All arguments to string.join must be strings.");
         }
      }

      *out_return = SdEnv_BoxString(self->env, SdString_FromCStr(SdStringBuf_CStr(buf)));
      SdStringBuf_Delete(buf);
   }
SdEngine_INTRINSIC_END

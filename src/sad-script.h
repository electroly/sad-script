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

#ifdef __cplusplus
extern "C" {
#endif

/* A raw pointer means that ownership is being transferred.
   An _r typedef means that a pointer is only being borrowed; ownership is not transferred. */
typedef struct SdResult_s SdResult;
typedef struct Sad_s Sad;
typedef struct Sad_s* Sad_r;
typedef struct SdString_s SdString;
typedef struct SdString_s* SdString_r;
typedef struct SdValue_s SdValue;
typedef struct SdValue_s* SdValue_r;
typedef struct SdList_s SdList;
typedef struct SdList_s* SdList_r;

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
   SdErr_CANNOT_OPEN_FILE
} SdErr;

typedef enum SdType_e {
   SdType_NIL = 0,
   SdType_INT = 1,
   SdType_DOUBLE = 2,
   SdType_BOOL = 3,
   SdType_STRING = 4,
   SdType_LIST = 5,
   SdType_MUTALIST = 6,
   SdType_FUNCTION = 7, /* really a list */
   SdType_ERROR = 8, /* really a list */
   SdType_TYPE = 9, /* really an integer */
   SdType_ANY = 10 /* can't create a value of this type; exists only for pattern matching*/
} SdType;

struct SdResult_s {
   SdErr code;
};

/* SdResult ***********************************************************************************************************/
SdBool         SdFailed(SdResult result);
const char*    SdGetLastFailMessage(void);

/* Sad ***************************************************************************************************************/
SdErr          SdRunScript(const char* prelude_file_path, const char* script_code);

Sad*           Sad_New(void);
void           Sad_Delete(Sad* self);
SdResult       Sad_AddScript(Sad_r self, const char* code);
SdResult       Sad_Execute(Sad_r self);

/* SdString **********************************************************************************************************/
SdString*      SdString_New(void);
SdString*      SdString_FromCStr(const char* cstr);
void           SdString_Delete(SdString* self);
const char*    SdString_CStr(SdString_r self);
SdBool         SdString_Equals(SdString_r a, SdString_r b);
SdBool         SdString_EqualsCStr(SdString_r a, const char* b);
size_t         SdString_Length(SdString_r self);
int            SdString_Compare(SdString_r a, SdString_r b);

/* SdValue ***********************************************************************************************************/
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
SdList*        SdList_NewWithLength(size_t length);
void           SdList_Delete(SdList* self);
void           SdList_MakeReadOnly(SdList_r self);
SdBool         SdList_IsReadOnly(SdList_r self);
void           SdList_Append(SdList_r self, SdValue_r item);
void           SdList_SetAt(SdList_r self, size_t index, SdValue_r item);
void           SdList_InsertAt(SdList_r self, size_t index, SdValue_r item);
SdValue_r      SdList_GetAt(SdList_r self, size_t index);
size_t         SdList_Count(SdList_r self);
SdValue_r      SdList_RemoveAt(SdList_r self, size_t index);
void           SdList_Clear(SdList_r self);
SdBool         SdList_Equals(SdList_r a, SdList_r b);
SdList*        SdList_Clone(SdList_r self);

/* SdFile ************************************************************************************************************/
SdResult       SdFile_WriteAllText(SdString_r file_path, SdString_r text);
SdResult       SdFile_ReadAllText(SdString_r file_path, SdString** out_text);

/*********************************************************************************************************************/
#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef _MSC_VER
#pragma warning(pop) /* our disabled warnings won't affect files that #include this header */
#endif

#endif /* _SAD_SCRIPT_H_ */

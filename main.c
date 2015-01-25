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
#pragma warning(push, 0) /* ignore warnings in system headers */
#endif

#include <stdio.h>

#ifdef _MSC_VER
#pragma warning(pop) /* start showing warnings again */
#endif

#include "sad-script.h"

/* These unit tests leak memory like crazy, but it doesn't matter. */

#define EXPECT(condition) Expect(condition, __LINE__)

void Expect(SdBool condition, int line) {
   if (!condition) {
      printf("Failed test on line %d.\n", line);
      exit(1);
   }
}

#if 0
void Test_Simple1(void) { /* Program: var a = 5 */
   SdValue_r program, var_a;
   SdList_r functions, statements;
   SdResult error;
   Sad* sad;
   SdEnv_r env;

   functions = SdList_New();
   statements = SdList_New();
   sad = Sad_New();
   env = Sad_Env(sad);
   SdList_Append(statements, SdAst_Var_New(env, SdString_FromCStr("a"), SdValue_NewInt(5)));
   program = SdAst_Program_New(env, functions, statements);
   SdEnv_AddProgramAst(env, program);
   error = SdEnv_ExecuteTopLevelStatements(env);
   EXPECT(error.code == SdErr_SUCCESS);

   /* Expect a global variable named "a" with integer value 5. */
   var_a = SdEnv_GetGlobalVariable(env, SdString_FromCStr("a"));
   EXPECT(var_a);
   EXPECT(SdValue_Type(var_a) == SdType_INT);
   EXPECT(SdValue_GetInt(var_a) == 5);
}
#endif

int main(void) {
   /*Test_Simple1();*/
   return 0;
}
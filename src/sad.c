/* SAD-Script Command Line Interpreter
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
#pragma warning(push, 0) /* ignore warnings in system headers */
#endif

#ifdef SD_DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <stdio.h>

#ifdef _MSC_VER
#pragma warning(pop) /* start showing warnings again */
#endif

#include "sad-script.h"

int main(int argc, const char* argv[]) {
   int ret = 0;
   SdResult result = SdResult_SUCCESS;
   Sad* sad = NULL;
   SdString* file_path = NULL;
   SdString* file_text = NULL;
   
#ifdef SD_DEBUG
   /* dump memory leaks when the program exits */
   _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

   if (argc != 2) {
      fprintf(stderr, "Syntax: sad <script filename>\n");
      ret = -1;
      goto end;
   }

   sad = Sad_New();
   file_path = SdString_FromCStr(argv[1]);

   if (SdFailed(result = SdFile_ReadAllText(file_path, &file_text))) {
      fprintf(stderr, "ERROR: %s\n", result.message);
      ret = -1;
      goto end;
   }

   if (SdFailed(result = Sad_ExecuteScript(sad, SdString_CStr(file_text)))) {
      fprintf(stderr, "ERROR: %s\n", result.message);
      ret = -1;
      goto end;
   }
   
end:
   if (file_text) SdString_Delete(file_text);
   if (file_path) SdString_Delete(file_path);
   if (sad) Sad_Delete(sad);
   return ret;
}

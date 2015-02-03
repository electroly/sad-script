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

/* see sad-script.c for debugging options */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#pragma warning(push, 0) /* ignore warnings in system headers */
#endif

#if defined(SD_DEBUG_ALL) || defined(SD_DEBUG_MSVC)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#pragma warning(pop) /* start showing warnings again */
#endif

#include "sad-script.h"

int main(int argc, char* argv[]) {
   int ret = 0;
   SdResult result = SdResult_SUCCESS;
   Sad* sad = NULL;
   SdString* prelude_path = NULL;
   SdString* file_path = NULL;
   SdString* prelude_text = NULL;
   SdString* file_text = NULL;
   const char* prelude = NULL;
   const char* file_path_cstr = NULL;
   
#if defined(SD_DEBUG_ALL) || defined(SD_DEBUG_MSVC)
   /* dump memory leaks when the program exits */
   _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

   prelude = getenv("SAD_PRELUDE");

   /* consume the exe filename in argv*/
   argv++; argc--;

   /* --prelude <file-path> */
   if (argc > 2 && strcmp(argv[0], "--prelude") == 0) {
      prelude = argv[1];
      argv += 2; argc -= 2;
   }

   /* <script-file-path> */
   if (argc == 1) {
      file_path_cstr = argv[0];
   } else { 
      fprintf(stderr, "Syntax: sad [--prelude <filename>] <filename>\n");
      ret = -1;
      goto end;
   }

   if (!prelude) {
      /* try in the current directory */
      prelude = "prelude.sad";
   }

   sad = Sad_New();
   file_path = SdString_FromCStr(file_path_cstr);
   prelude_path = SdString_FromCStr(prelude);

   if (SdFailed(result = SdFile_ReadAllText(prelude_path, &prelude_text))) {
      fprintf(stderr, "ERROR: Could not find the prelude file.  Please specify the path to the prelude file using the "
         "environment variable SAD_PRELUDE, the --prelude command line switch, or by placing the prelude in the "
         "current directory.\n");
      ret = -1;
      goto end;
   }

   if (SdFailed(result = Sad_AddScript(sad, SdString_CStr(prelude_text)))) {
      fprintf(stderr, "ERROR: Failed to parse the prelude script file.\n%s\n", result.message);
      ret = -1;
      goto end;
   }

   if (SdFailed(result = SdFile_ReadAllText(file_path, &file_text))) {
      fprintf(stderr, "ERROR: Failed to read the script file.\n%s\n", result.message);
      ret = -1;
      goto end;
   }

   if (SdFailed(result = Sad_AddScript(sad, SdString_CStr(file_text)))) {
      fprintf(stderr, "ERROR: Failed to parse the script file.\n%s\n", result.message);
      ret = -1;
      goto end;
   }

   if (SdFailed(result = Sad_Execute(sad))) {
      fprintf(stderr, "ERROR: %s\n", result.message);
      ret = -1;
      goto end;
   }

end:
   if (prelude_text) SdString_Delete(prelude_text);
   if (file_text) SdString_Delete(file_text);
   if (prelude_path) SdString_Delete(prelude_path);
   if (file_path) SdString_Delete(file_path);
   if (sad) Sad_Delete(sad);
   return ret;
}

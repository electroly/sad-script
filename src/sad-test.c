/* SAD-Script Test Checker
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char* ReadActualFile(const char* file_path) {
   FILE* fp;
   char* str = NULL;
   char buf[1000];
   size_t len = 0;

   fp = fopen(file_path, "r");
   if (!fp)
      return NULL;

   str = calloc(1, 1);
   while (fgets(buf, sizeof(buf), fp)) {
      size_t new_len = strlen(buf);
      str = realloc(str, len + new_len + 1);
      memcpy(&str[len], buf, new_len);
      len += new_len;
      str[len] = 0;
   }

   fclose(fp);

   /* trim from the back */
   while (str[len - 1] == ' ' || str[len - 1] == '\r' || str[len - 1] == '\n') {
      str[len - 1] = 0;
      len--;
   }

   return str;
}

static char* ReadExpectedFile(const char* file_path) {
   /* the file is the source .sad file, where the initial comments contain the expected output */
   FILE* fp;
   char* str = NULL;
   char buf[1000];
   size_t len = 0;

   fp = fopen(file_path, "r");
   if (!fp)
      return NULL;

   str = calloc(1, 1);
   while (fgets(buf, sizeof(buf), fp)) {
      size_t new_len;

      if (strlen(buf) > 2 && buf[0] == '/' && buf[1] == '/') {
         new_len = strlen(buf) - 2;
         str = realloc(str, len + new_len + 1);
         memcpy(&str[len], &buf[2], new_len);
         len += new_len;
         str[len] = 0;
      } else {
         break;
      }
   }

   fclose(fp);

   /* trim from the back */
   while (str[len - 1] == ' ' || str[len - 1] == '\r' || str[len - 1] == '\n') {
      str[len - 1] = 0;
      len--;
   }

   return str;
}

int main(int argc, const char* argv[]) {
   int ret = 0;
   const char* expected_file_path;
   char* expected = NULL;
   const char* actual_file_path;
   char* actual = NULL;

   if (argc != 3) {
      fprintf(stderr, "Syntax: sad-test <.sad file> <actual file>\n");
      ret = -1;
      goto end;
   }

   expected_file_path = argv[1];
   actual_file_path = argv[2];

   expected = ReadExpectedFile(expected_file_path);
   if (!expected) {
      fprintf(stderr, "Could not open the expected file.\n");
      ret = -1;
      goto end;
   }

   actual = ReadActualFile(actual_file_path);
   if (!actual) {
      fprintf(stderr, "Could not open the actual file.\n");
      ret = -1;
      goto end;
   }

   if (strcmp(expected, actual) != 0) {
      printf("FAIL\n");
      printf("************ Expected:\n\"%s\"\n", expected);
      printf("************ Actual:\n\"%s\"\n", actual);
   } else {
      printf("PASS\n");
   }

end:
   free(expected);
   free(actual);
   return ret;
}

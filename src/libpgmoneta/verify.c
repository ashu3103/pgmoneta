/*
 * Copyright (C) 2024 The pgmoneta community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <deque.h>
#include <info.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <string.h>
#include <utils.h>
#include <verify.h>
#include <workflow.h>

/* system */
#include <stdlib.h>
#include <unistd.h>

static int verify_backup(SSL* ssl, int client_fd, int server, char* backup_id, char* directory, char* files, char** output, char** identifier);

void
pgmoneta_verify(SSL* ssl, int client_fd, int server, char* backup_id, char* directory, char* files, char** argv)
{
   char elapsed[128];
   time_t start_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char* output = NULL;
   char* id = NULL;
   int result = 1;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "verify", config->servers[server].name);

   start_time = time(NULL);

   if (!verify_backup(ssl, client_fd, server, backup_id, directory, files, &output, &id))
   {
      result = 0;

      total_seconds = (int)difftime(time(NULL), start_time);
      hours = total_seconds / 3600;
      minutes = (total_seconds % 3600) / 60;
      seconds = total_seconds % 60;

      memset(&elapsed[0], 0, sizeof(elapsed));
      sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

      pgmoneta_log_info("Verify: %s/%s (Elapsed: %s)", config->servers[server].name, id, &elapsed[0]);
   }

   pgmoneta_management_process_result(ssl, client_fd, server, NULL, result, true);
   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(output);
   free(id);

   free(backup_id);
   free(directory);
   free(files);

   exit(0);
}

static int
verify_backup(SSL* ssl, int client_fd, int server, char* backup_id, char* directory, char* files, char** output, char** identifier)
{
   char* o = NULL;
   char* ident = NULL;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct deque* nodes = NULL;
   struct deque* failed = NULL;
   struct deque* all = NULL;

   *output = NULL;
   *identifier = NULL;

   pgmoneta_deque_create(false, &nodes);

   if (pgmoneta_deque_add(nodes, "position", (uintptr_t)"", ValueString))
   {
      goto error;
   }

   if (pgmoneta_deque_add(nodes, "directory", (uintptr_t)directory, ValueString))
   {
      goto error;
   }

   if (pgmoneta_deque_add(nodes, "files", (uintptr_t)files, ValueString))
   {
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_VERIFY);

   current = workflow;
   while (current != NULL)
   {
      if (current->setup(server, backup_id, nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->execute(server, backup_id, nodes))
      {
         goto error;
      }
      current = current->next;
   }

   current = workflow;
   while (current != NULL)
   {
      if (current->teardown(server, backup_id, nodes))
      {
         goto error;
      }
      current = current->next;
   }

   o = (char*)pgmoneta_deque_get(nodes, "output");

   if (o == NULL)
   {
      goto error;
   }

   ident = (char*)pgmoneta_deque_get(nodes, "identifier");

   if (ident == NULL)
   {
      goto error;
   }

   *output = pgmoneta_append(*output, o);
   *identifier = pgmoneta_append(*identifier, ident);

   pgmoneta_deque_list(nodes);

   failed = (struct deque*)pgmoneta_deque_get(nodes, "failed");
   all = (struct deque*)pgmoneta_deque_get(nodes, "all");

   pgmoneta_management_write_verify(ssl, client_fd, failed, all);

   pgmoneta_workflow_delete(workflow);

   pgmoneta_deque_destroy(nodes);

   return 0;

error:

   pgmoneta_workflow_delete(workflow);

   pgmoneta_deque_destroy(nodes);

   return 1;
}

char*
pgmoneta_verify_entry_to_string(struct verify_entry* entry, int32_t format, char* tag, int indent)
{
   struct json* obj = NULL;
   char* str = NULL;
   pgmoneta_json_create(&obj);
   pgmoneta_json_put(obj, "directory", (uintptr_t)entry->directory, ValueString);
   pgmoneta_json_put(obj, "filename", (uintptr_t)entry->filename, ValueString);
   pgmoneta_json_put(obj, "original", (uintptr_t)entry->original, ValueString);
   pgmoneta_json_put(obj, "calculated", (uintptr_t)entry->calculated, ValueString);
   pgmoneta_json_put(obj, "hash_algorithm", (uintptr_t)entry->hash_algoritm, ValueInt32);
   str = pgmoneta_json_to_string(obj, format, tag, indent);
   pgmoneta_json_destroy(obj);
   return str;
}
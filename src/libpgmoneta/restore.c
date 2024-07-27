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
#include <logging.h>
#include <management.h>
#include <network.h>
#include <restore.h>
#include <string.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdlib.h>
#include <unistd.h>

static char* restore_last_files_names[] = {"/global/pg_control"};

int
pgmoneta_get_restore_last_files_names(char*** output)
{
   int number_of_elements = 0;
   number_of_elements = sizeof(restore_last_files_names) / sizeof(restore_last_files_names[0]);

   *output = (char**)malloc((number_of_elements + 1) * sizeof(char*));
   if (*output == NULL)
   {
      return 1;
   }

   for (int i = 0; i < number_of_elements; i++)
   {
      (*output)[i] = strdup(restore_last_files_names[i]);
      if ((*output)[i] == NULL)
      {
         return 1;
      }
   }
   (*output)[number_of_elements] = NULL;

   return 0;
}

void
pgmoneta_restore(SSL* ssl, int client_fd, int server, char* backup_id, char* position, char* directory, char** argv)
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

   pgmoneta_set_proc_title(1, argv, "restore", config->servers[server].name);

   start_time = time(NULL);

   if (!pgmoneta_restore_backup(server, backup_id, position, directory, &output, &id))
   {
      result = 0;

      total_seconds = (int)difftime(time(NULL), start_time);
      hours = total_seconds / 3600;
      minutes = (total_seconds % 3600) / 60;
      seconds = total_seconds % 60;

      memset(&elapsed[0], 0, sizeof(elapsed));
      sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

      pgmoneta_log_info("Restore: %s/%s (Elapsed: %s)", config->servers[server].name, id, &elapsed[0]);
   }

   pgmoneta_management_process_result(ssl, client_fd, server, NULL, result, true);
   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   free(output);
   free(id);

   free(backup_id);
   free(position);
   free(directory);

   exit(0);
}

int
pgmoneta_restore_backup(int server, char* backup_id, char* position, char* directory, char** output, char** identifier)
{
   char* o = NULL;
   char* ident = NULL;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct deque* nodes = NULL;

   *output = NULL;
   *identifier = NULL;

   pgmoneta_deque_create(false, &nodes);

   if (pgmoneta_deque_put(nodes, "position", position, strlen(position) + 1))
   {
      goto error;
   }

   if (pgmoneta_deque_put(nodes, "directory", directory, strlen(directory) + 1))
   {
      goto error;
   }

   workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_RESTORE);

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

   *output = malloc(strlen(o) + 1);

   if (*output == NULL)
   {
      goto error;
   }

   memset(*output, 0, strlen(o) + 1);
   memcpy(*output, o, strlen(o));

   *identifier = malloc(strlen(ident) + 1);

   if (*identifier == NULL)
   {
      goto error;
   }

   memset(*identifier, 0, strlen(ident) + 1);
   memcpy(*identifier, ident, strlen(ident));

   pgmoneta_workflow_delete(workflow);

   pgmoneta_deque_destroy(nodes);

   return 0;

error:
   pgmoneta_workflow_delete(workflow);

   pgmoneta_deque_destroy(nodes);

   return 1;
}

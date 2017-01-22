/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2016 - Jean-André Santoni
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <errno.h>
#include <file/nbio.h>
#include <formats/image.h>
#include <compat/strl.h>
#include <retro_assert.h>
#include <retro_miscellaneous.h>
#include <lists/string_list.h>
#include <rhash.h>
#include <string/stdstring.h>
#include <file/file_path.h>
#include <lists/dir_list.h>

#include "tasks_internal.h"
#include "../file_path_special.h"
#include "../verbosity.h"
#include "../configuration.h"
#include "../playlist.h"
#include "../command.h"
#include "../core_info.h"

typedef struct
{
   struct string_list *lpl_list;
   char crc[PATH_MAX_LENGTH];
   char path[PATH_MAX_LENGTH];
   char hostname[512];
   char corename[PATH_MAX_LENGTH];
   bool found;
} netplay_crc_handle_t;

static void netplay_crc_scan_callback(void *task_data,
                               void *user_data, const char *error)
{
   netplay_crc_handle_t *state = (netplay_crc_handle_t*)task_data;
   core_info_list_t *info      = NULL;
   content_ctx_info_t content_info = {0};

   int i;
   core_info_get_list(&info);

   if (!state)
      return;

   for (i=0; i < info->count; i++)
   {
      if(string_is_equal(info->list[i].core_name, state->corename))
         break;
   }

   printf("Hostname: %s\n", state->hostname);
   printf("Content:  %s\n", state->path);
   printf("Corename: %s\n", state->corename);
   printf("Corepath: %s\n", info->list[i].path);

   command_event(CMD_EVENT_NETPLAY_INIT_DIRECT_DEFERRED, state->hostname);
   task_push_content_load_default(
         info->list[i].path, state->path,
         &content_info,
         CORE_TYPE_PLAIN,
         CONTENT_MODE_LOAD_CONTENT_WITH_NEW_CORE_FROM_MENU,
         NULL, NULL);

   free(state);
}

static void task_netplay_crc_scan_handler(retro_task_t *task)
{
   netplay_crc_handle_t *state = (netplay_crc_handle_t*)task->state;
   size_t i, j;

   task_set_progress(task, 0);
   task_set_title(task, strdup("Checking for ROM presence."));
   task_set_finished(task, false);

   if (!state->lpl_list)
   {
      task_set_progress(task, 100);
      task_set_title(task, strdup("Playlist directory not found."));
      task_set_finished(task, true);
      free(state);
      return;
   }

   if (state->lpl_list->size == 0)
      goto no_playlists;

   for (i = 0; i < state->lpl_list->size; i++)
   {
      const char *lpl_path = state->lpl_list->elems[i].data;

      if (!strstr(lpl_path, file_path_str(FILE_PATH_LPL_EXTENSION)))
         continue;

      printf("%s\n", lpl_path);

      playlist_t *playlist = playlist_init(lpl_path, 99999);

      for (j = 0; j < playlist->size; j++)
      {
         printf("%s\n", playlist->entries[j].crc32);
         if (string_is_equal(playlist->entries[j].crc32, state->crc))
         {
            strlcpy(state->path, playlist->entries[j].path, sizeof(state->path));
            state->found = true;
            task_set_data(task, state);
            task_set_progress(task, 100);
            task_set_title(task, strdup("Game found."));
            task_set_finished(task, true);
            string_list_free(state->lpl_list);
            return;
         }

         task_set_progress(task, (int)(j/playlist->size*100.0));
      }
   }

no_playlists:
   string_list_free(state->lpl_list);
   task_set_progress(task, 100);
   task_set_title(task, strdup("No game found."));
   task_set_finished(task, true);
   return;
}

bool task_push_netplay_crc_scan(uint32_t crc,
      const char *hostname, const char *corename)
{
   settings_t *settings = config_get_ptr();
   retro_task_t   *task = (retro_task_t *)calloc(1, sizeof(*task));
   netplay_crc_handle_t *state = (netplay_crc_handle_t*)calloc(1, sizeof(*state));

   if (!task || !state)
      goto error;

   state->crc[0] = '\0';
   snprintf(state->crc, sizeof(state->crc), "%08X|crc", crc);

   state->hostname[0] = '\0';
   snprintf(state->hostname, sizeof(state->hostname), "%s", hostname);

   state->corename[0] = '\0';
   snprintf(state->corename, sizeof(state->corename), "%s", corename);

   state->lpl_list = dir_list_new(settings->directory.playlist,
         NULL, true, true, true, false);

   state->found = false;

   /* blocking means no other task can run while this one is running, 
    * which is the default */
   task->type           = TASK_TYPE_BLOCKING;
   task->state          = state;
   task->handler        = task_netplay_crc_scan_handler;
   task->callback       = netplay_crc_scan_callback;
   task->title          = strdup("Checking for ROM presence.");

   task_queue_ctl(TASK_QUEUE_CTL_PUSH, task);

   return true;

error:
   if (state)
      free(state);
   if (task)
      free(task);

   return false;
}
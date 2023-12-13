#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>

//can this b here


#include "constants.h"
#include "operations.h"
#include "parser.h"

#define PATH "jobs"

int read_file_from_dir(DIR* dir, char* dir_path) {
    struct dirent* ent = readdir(dir);
    char file_path[128];
    memset(file_path, 0, 128);

    if (ent == NULL) return -1;

    strcat(file_path, dir_path);
    strcat(file_path, "/");
    strcat(file_path, ent->d_name);

    int fp = open(file_path, O_RDONLY);
    if (fp == -1) {
      fprintf(stderr, "Failed to open jobs file\n");
      return -1;
    }
    return fp;
}

void readCommandsFromFile (int fp) {
    unsigned int event_id, delay;
    size_t num_rows, num_columns, num_coords;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

    bool eof = true;
    while (eof) {
      switch (get_next(fp)) {
        case CMD_CREATE:
          if (parse_create(fp, &event_id, &num_rows, &num_columns) != 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (ems_create(event_id, num_rows, num_columns)) {
            fprintf(stderr, "Failed to create event\n");
          }

          break;

        case CMD_RESERVE:
          num_coords = parse_reserve(fp, MAX_RESERVATION_SIZE, &event_id, xs, ys);

          if (num_coords == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (ems_reserve(event_id, num_coords, xs, ys)) {
            fprintf(stderr, "Failed to reserve seats\n");
          }

          break;

        case CMD_SHOW:
          if (parse_show(fp, &event_id) != 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (ems_show(event_id)) {
            fprintf(stderr, "Failed to show event\n");
          }

          break;

        case CMD_LIST_EVENTS:
          if (ems_list_events()) {
            fprintf(stderr, "Failed to list events\n");
          }

          break;

        case CMD_WAIT:
          if (parse_wait(fp, &delay, NULL) == -1) {  // thread_id is not implemented
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (delay > 0) {
            printf("Waiting...\n");
            ems_wait(delay);
          }

          break;

        case CMD_INVALID:
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          break;

        case CMD_HELP:
          printf(
              "Available commands:\n"
              "  CREATE <event_id> <num_rows> <num_columns>\n"
              "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
              "  SHOW <event_id>\n"
              "  LIST\n"
              "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
              "  BARRIER\n"                      // Not implemented
              "  HELP\n");

          break;

        case CMD_BARRIER:  // Not implemented
        case CMD_EMPTY:
          break;

        case EOC:
          eof = false;
          exit(0);
          break;
      }
    }
}

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

  if (argc > 1) {
    char *endptr;
    unsigned long int delay = strtoul(argv[1], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_ms = (unsigned int)delay;
  }

  int MAX_PROC = atoi(argv[1]);
  int active_processes = 0;
  
  //unused, flagged in compiler (is correct though)
  //int MAX_THREADS = atoi(argv[2]); 


  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  char* dir_path = "jobs";
  DIR *dir = opendir(dir_path);
  if (dir == NULL) {
    fprintf(stderr, "Failed to open jobs directory\n");
    return 1;
  }

  bool not_done = true;
  while (not_done) {
    //processes variables
    pid_t pid;
    int status;

    char path[128];
    memset(path, 0, 128);
    strcat(path, dir_path);
    strcat(path, "/");

    struct dirent* ent;
    
    while (active_processes >= MAX_PROC) {
      // wait for a child process to finish before creating a new one
      pid = waitpid(-1, &status, WNOHANG);
      if (pid > 0) {
        active_processes--;
      }
    }

    ent = readdir(dir);
    
    if (ent == NULL) {
      not_done = false;
    } else if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
      continue;
    } else {  pid = fork(); }

  
    if (pid == -1) {
      fprintf(stderr, "Failed to fork\n");
      return 1;
    } else if (pid == 0) {

      strcat(path, ent->d_name);

      printf("File path: %s, pid: %d\n", path, pid);

      int fp = open(path, O_RDONLY);
      if (fp == -1) {
        fprintf(stderr, "Failed to open jobs file\n");
        return -1;
      }

      readCommandsFromFile(fp);


      printf("> ");
      fflush(stdout);

    } else if (pid > 0) {
      active_processes++;
    }
  }
  int p;
  do {
    p = wait(NULL);
    printf("p: %d\n", p);
  } while (p > 0);

  // printf("Waiting for a process to finish...\n");
  ems_terminate();
  closedir(dir);
}
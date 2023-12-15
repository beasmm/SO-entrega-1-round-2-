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

#include <sys/stat.h>


#include "constants.h"
#include "operations.h"
#include "parser.h"



int MAX_THREADS;
pthread_mutex_t cmd_lock = PTHREAD_MUTEX_INITIALIZER;


int getListOfFiles(DIR *dir, char *dir_name, char *files[]){
  char path[256];
  memset(path, 0, sizeof(path));

  sprintf(path, "%s", dir_name);
  strcat(path, "/");

  struct dirent *ent;

  int count = 0;
  while ((ent = readdir(dir)) != NULL) {
    if (strstr(ent->d_name, ".jobs") != NULL) {
      strcat(path, ent->d_name);
      files[count] = strdup(path);
      count++;
      memset(path, 0, sizeof(path));
      sprintf(path, "%s", dir_name);
      strcat(path, "/");
    }
  }
  return count;
}


void* processCommand (void* arg) {
  printf("Thread self: %ld\n", pthread_self());
  int* fp_ptr = (int*)arg;

  int fp = fp_ptr[0];
  int fp_out = fp_ptr[1];
  int *eof = &fp_ptr[2];

  unsigned int event_id, delay, thread_id;
  size_t num_rows, num_columns, num_coords;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

  enum Command cmd = get_next(fp);
  pthread_mutex_lock(&cmd_lock); 
  switch (cmd) {
    case CMD_CREATE:
      if (parse_create(fp, &event_id, &num_rows, &num_columns) != 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        //continue;
      }

      if (ems_create(event_id, num_rows, num_columns)) {
        fprintf(stderr, "Failed to create event\n");
      }

      break;

    case CMD_RESERVE:
      num_coords = parse_reserve(fp, MAX_RESERVATION_SIZE, &event_id, xs, ys);

      if (num_coords == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        //continue;
      }

      if (ems_reserve(event_id, num_coords, xs, ys)) {
        fprintf(stderr, "Failed to reserve seats\n");
      }

      break;

    case CMD_SHOW:
      if (parse_show(fp, &event_id) != 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        //continue;
      }

      if (ems_show(event_id, fp_out)) {
        fprintf(stderr, "Failed to show event\n");
      }

      break;

    case CMD_LIST_EVENTS:
      if (ems_list_events()) {
        fprintf(stderr, "Failed to list events\n");
      }

      break;

    case CMD_WAIT:
      if (parse_wait(fp, &delay, &thread_id) == -1) {  // thread_id is not implemented
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        //continue;
      }

      // printf("Delay Thread ID: %d\n", thread_id);

      if (thread_id != 0) {
        pthread_t tid = pthread_self();
        // printf("Thread self: %ld\n", tid);
        if (thread_id == tid) {
          // printf("Waiting...%d\n", thread_id);
          ems_wait(delay);
        }
      } else {
        if (delay > 0) {
          // printf("Waiting...\n");
          ems_wait(delay);
        }
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
      // printf("BARRIER\n");
      *eof = 2;
      break;
    case CMD_EMPTY:
      break;

    case EOC:
      *eof = 1;
      // printf("EOF\n");
      // exit(0);
      break;
  }
  pthread_mutex_unlock(&cmd_lock);
  pthread_exit(NULL);
}

void readCommandsFromFile (int fp, int fp_out) {
  int fp_ptr[3] = {fp, fp_out, 0};
  pthread_t tid[MAX_THREADS];
  int n_threads = 0;

  int i = 0;
  while (!fp_ptr[2]) {

    if (n_threads >= MAX_THREADS) {
      pthread_join(tid[MAX_THREADS - i], NULL);
      n_threads--;
    }

    pthread_create(&tid[i], NULL, processCommand, &fp_ptr);
    n_threads++;
    if (fp_ptr[2] == 1) break;
    if (fp_ptr[2] == 2) {
      if (n_threads <= MAX_THREADS) {
        for (int j = 0; j < n_threads; j++) {
          pthread_join(tid[j], NULL);
        }
      } else {
        for (int j = 0; j < MAX_THREADS; j++) {
          pthread_join(tid[j], NULL);
        }
      }
      break;
    }
    i = i % MAX_THREADS;
    i++;
    // printf("EOF %d\n", fp_ptr[2]);

  }

  printf("n_threads: %d\n", n_threads);
  
  if (n_threads <= MAX_THREADS) {
    for (int j = 0; j < i; j++) {
      pthread_join(tid[j], NULL);
    }
  } else {
    for (int j = 0; j < MAX_THREADS; j++) {
      pthread_join(tid[j], NULL);
    }
  }
}

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

  if (argc > 2) {
    char *endptr;
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_ms = (unsigned int)delay;
  }

  char dir_name[128];
  strncpy(dir_name, argv[1] + 2, sizeof(argv[1]) - 2);
  // printf("dir_name: %s\n", dir_name);

  int MAX_PROC = atoi(argv[2]);
  int active_processes = 0;
  
  //unused, flagged in compiler (is correct though)
  MAX_THREADS = atoi(argv[3]); 
  pthread_mutex_init(&cmd_lock, NULL);

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  
  char *files[128];
  DIR *dir = opendir(dir_name);
  if (dir == NULL) {
    fprintf(stderr, "Failed to open jobs directory\n");
    return 1;
  }

  int n_files = getListOfFiles(dir, dir_name, files);
  printf("n_files: %d\n", n_files);
  for (int i = 0; i < n_files; i++) {
    printf("File: %s\n", files[i]);
  }

  bool not_done = true;
  int i = 0;
  while (not_done) {
    if (i == n_files) {
      not_done = false;
      break;
    }
  
    //processes variables
    pid_t pid;
    int status;

    char path[128];
    strcpy(path, files[i]);

    char path_out[128];
    memset(path_out, 0, sizeof(path_out));
    strncpy(path_out, path, strlen(path) - 5);
    strcat(path_out, ".out");
    printf("path_out: %s\n", path_out);

    
    while (active_processes >= MAX_PROC) {
      // wait for a child process to finish before creating a new one
      pid = waitpid(-1, &status, WNOHANG);
      if (pid > 0) {
        active_processes--;
      }
    }

    printf("File out: %s, pid: %d\n", path_out, pid);
    pid = fork();
  
    if (pid == -1) {
      fprintf(stderr, "Failed to fork\n");
      return 1;

    } else if (pid == 0) {

      printf("File path: %s, pid: %d\n", path, pid);

      int fp = open(path, O_RDONLY);
    
      if (fp == -1) {
        fprintf(stderr, "Failed to open jobs file\n");
        return -1;
      }

      //create new file name NEWBEA
      
      int fp_out = open(path_out, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
      printf("fp_out: %d\n", fp_out);
      if (fp_out == -1) {
        fprintf(stderr, "Error creating .out file\n");
        return -1;
      }

      readCommandsFromFile(fp, fp_out);

      close(fp); 
      close(fp_out);
      exit(0);
      printf("> ");
      fflush(stdout);

    } else if (pid > 0) {
      i++;
      active_processes++;
    }
  }
  int p;
  do {
    p = wait(NULL);
    // printf("p: %d\n", p);
  } while (p > 0);

  for(int k = 0; k < n_files; k++) {
    free(files[k]);
  }
  // printf("Waiting for a process to finish...\n");
  ems_terminate();
  closedir(dir);
  // closedir(dir_out);
}
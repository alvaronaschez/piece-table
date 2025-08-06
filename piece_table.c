#include "piece_table.h"

#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#define ADD_BUFFER_INITIAL_CAPACITY 1024

struct piece;
struct change;

struct PieceTable {
  char *original, *add;
  // size_t original_size;
  size_t add_len, add_capacity;
  struct piece *head, *tail; // sentinel nodes
  size_t len;

  struct change *undo_stack, *redo_stack;
};

enum buffer_type { ORIGINAL, ADD };

struct piece {
  enum buffer_type buf;
  size_t offset, len;
  struct piece *next, *prev;
};

struct piece_range {
  struct piece *first, *last;
};

struct change {
  struct piece_range current, changed;
  struct change *next;
};

PieceTable *pt_new() {
  PieceTable *pt = calloc(1, sizeof(PieceTable));

  // init sentinel nodes
  pt->head = calloc(1, sizeof(struct piece));
  pt->tail = calloc(1, sizeof(struct piece));
  pt->head->next = pt->tail;
  pt->tail->prev = pt->head;

  return pt;
}

void pt_free(PieceTable *pt) {}

void handle_error(char *msg){
  printf("error: %s\n", msg);
  exit(EXIT_FAILURE);
}

void pt_load(char *file_name) {
  int fd = open(file_name, O_RDONLY);
  if(fd == -1)
    handle_error("open");

  // obtain file size
  struct stat sb;
  if(fstat(fd, &sb) == -1)
    handle_error("fstat");

  size_t length = sb.st_size;

  char *addr = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if(addr == MAP_FAILED)
    handle_error("mmap");

  write(STDOUT_FILENO, addr, length);

  munmap(addr, length);
}

int main() {
  printf("--start--\n");
  pt_load("/home/alvaro/src/piece-table/piece_table.c");
  printf("--end--\n");
}

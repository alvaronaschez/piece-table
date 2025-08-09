#include "piece_table.h"

#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define ADD_BUFFER_INITIAL_CAPACITY 1024

struct piece;
struct change;

struct PieceTable {
  char *original_buffer, *add_buffer;
  size_t original_buffer_len;
  size_t add_buffer_len, add_buffer_capacity;
  struct piece *head, *tail; // sentinel nodes
  struct piece *global_last; // for cleanup
  size_t len;

  struct change *undo_stack, *redo_stack;
};

enum buffer_type { ORIGINAL, ADD };

struct piece {
  enum buffer_type buf;
  size_t offset, len;
  struct piece *next, *prev;
  struct piece *global_next; // for cleanup
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

  pt->head->global_next = pt->global_last = pt->tail;

  return pt;
}

//// TODO: free pieces in range
//static void _free_piece_range(struct piece_range pr) {
//  if (pr.first == pr.last) {
//    free(pr.first);
//  } else {
//    struct piece *aux = pr.first;
//    pr.first = pr.first->next;
//    free(aux);
//    _free_piece_range(pr);
//  }
//}

void pt_free(PieceTable *pt) {
  // free pieces
  struct piece *p, *q;
  p = pt->head;
  while (p) {
    q = p->global_next;
    free(p);
    p = q;
  }

  // unmap original buffer
  if (pt->original_buffer)
    munmap(pt->original_buffer, pt->original_buffer_len);

  // free undo and redo stacks
  struct change *changes[] = {pt->undo_stack, pt->redo_stack};
  for (int i = 0; i < 2; i++) {
    struct change *c, *aux;
    c = changes[i];
  }
}

void handle_error(char *msg) {
  printf("error: %s\n", msg);
  exit(EXIT_FAILURE);
}

void pt_load(PieceTable *pt, char *file_name) {
  int fd = open(file_name, O_RDONLY);
  if (fd == -1)
    handle_error("open");

  // obtain file size
  struct stat sb;
  if (fstat(fd, &sb) == -1)
    handle_error("fstat");

  pt->original_buffer_len = sb.st_size;

  pt->original_buffer =
      mmap(NULL, pt->original_buffer_len, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (pt->original_buffer == MAP_FAILED)
    handle_error("mmap");

  struct piece *new_piece;
  new_piece = malloc(sizeof(struct piece));
  pt->head->next = pt->tail->prev = new_piece;
  new_piece->prev = pt->head;
  new_piece->next = pt->tail;
  new_piece->buf = ORIGINAL;
  new_piece->len = pt->original_buffer_len;
  new_piece->offset = 0;
  pt->global_last->global_next = new_piece;
  pt->global_last = new_piece;
}

void pt_print(PieceTable *pt) {
  struct piece *p = pt->head->next;
  while (p != pt->tail) {
    const void *buf = p->buf == ORIGINAL ? pt->original_buffer : pt->add_buffer;
    write(STDOUT_FILENO, buf + p->offset, p->len);
    p = p->next;
  }
}

int main() {
  printf("--start--\n");
  PieceTable *pt = pt_new();
  pt_load(pt, "/home/alvaro/ws/piece-table/piece_table.c");
  // write(STDOUT_FILENO, pt->original_buffer, pt->original_buffer_len);
  pt_print(pt);
  pt_free(pt);
  printf("--end--\n");
}

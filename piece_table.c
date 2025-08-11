#include "piece_table.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define ADD_BUFFER_INITIAL_CAPACITY 1024

struct piece;
struct change;

// TODO: hcreate to implement cache
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

struct position {
  struct piece *piece;
  size_t offset;
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
    while (c) {
      aux = c;
      c = c->next;
      free(aux);
    }
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

  struct piece *new_piece = malloc(sizeof(struct piece));
  *new_piece = (struct piece){
      .prev = pt->head,
      .next = pt->tail,
      .buf = ORIGINAL,
      .len = pt->original_buffer_len,
      .offset = 0,
      .global_next = NULL,
  };
  pt->head->next = pt->tail->prev = new_piece;
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

struct position find(struct piece *piece, size_t offset) {
  assert(piece);
  if (piece->len > offset)
    return (struct position){.piece = piece, .offset = offset};
  return find(piece->next, offset - piece->len);
}

struct piece *add(PieceTable *pt, char *string) {
  size_t len = strlen(string);
  if(!len) return NULL;
  // make room in add_buffer
  while (pt->add_buffer_capacity < pt->add_buffer_len + len + 1) {
    pt->add_buffer = realloc(pt->add_buffer, pt->add_buffer_capacity *= 2);
  }
  strcpy(string, pt->add_buffer + pt->add_buffer_len);
  struct piece *piece = calloc(1, sizeof(struct piece));
  *piece = (struct piece){.buf=ADD, .offset=pt->add_buffer_len, .len = len-1};
  pt->global_last->global_next = piece;
  pt->global_last = piece;
  pt->add_buffer_len += len;
  return piece;
}

bool pt_delete(PieceTable *pt, size_t offset, size_t len) {
  // TODO
  if (offset + len > pt->len)
    return false;

  struct position begin = find(pt->head, offset);
  struct position end = find(begin.piece, begin.offset + len);

  return true;
}

bool pt_insert(PieceTable *pt, size_t offset, char *str, size_t len) {
  // TODO
  if (offset + len > pt->len)
    return false;

  struct position begin = find(pt->head, offset);
  struct position end = find(begin.piece, begin.offset + len);

  return true;
}

bool pt_replace(PieceTable *pt, size_t offset, size_t len, char *new,
                size_t new_len) {
  if (offset + len > pt->len)
    return false;
  if (!len && !new_len) // nothing to do
    return true;

  // find first piece to replace
  struct piece *p;
  size_t o = offset;
  for (p = pt->head->next; p && p->len > o; o -= p->len, p = p->next) {
  }

  // find last piece to replace
  struct piece *q;
  o += len;
  for (q = p; q->len > o; o -= q->len, q = q->next) {
  }

  // create change
  // TODO
  struct change *ch = calloc(1, sizeof(struct change));

  // create ch->current

  // create ch->changed // create dummy piece_range* if in boundary

  // apply change ie put ch->current in place
  ch->changed.first->prev->next = ch->current.first;
  ch->changed.last->next->prev = ch->current.last;
  ch->current.first->prev = ch->changed.first->prev;
  ch->current.last->next = ch->changed.last->next;

  // save change
  ch->next = pt->redo_stack;
  pt->undo_stack = ch;

  // empty redo stack
  struct change *cc;
  while (pt->redo_stack) {
    cc = pt->redo_stack;
    pt->redo_stack = pt->redo_stack->next;
    free(cc);
  }

  return true;
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

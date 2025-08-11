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
  size_t len;

  struct change *undo_stack, *redo_stack;
};

enum buffer_type { ORIGINAL, ADD };

struct piece {
  enum buffer_type buf;
  size_t offset, len;
  struct piece *next, *prev;
};

struct position {
  struct piece *piece;
  size_t offset;
};

struct piece_range {
  struct piece *first, *last;
};

struct change {
  struct piece_range *current, *old;
  struct change *next;
};

void free_piece_range(struct piece *begin, struct piece *end);
void ch_free(struct change *);

PieceTable *pt_new() {
  PieceTable *pt = calloc(1, sizeof(PieceTable));

  // init sentinel nodes
  pt->head = calloc(1, sizeof(struct piece));
  pt->tail = calloc(1, sizeof(struct piece));
  pt->head->next = pt->tail;
  pt->tail->prev = pt->head;

  return pt;
}

void pt_free(PieceTable *pt) {
  // free pieces
  free_piece_range(pt->head, pt->tail);

  // unmap original buffer
  if (pt->original_buffer)
    munmap(pt->original_buffer, pt->original_buffer_len);

  // free undo and redo stacks
  ch_free(pt->undo_stack);
  ch_free(pt->redo_stack);
}

//struct piece *p_new(enum buffer_type buf, size_t offset, size_t len,
//                    struct piece *next, struct piece *prev) {
//  struct piece *new_piece = malloc(sizeof(struct piece));
//  new_piece->buf = buf;
//  new_piece->offset = offset;
//  new_piece->len = len;
//  new_piece->next = next;
//  new_piece->prev = prev;
//  return new_piece;
//}

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
  };
  pt->head->next = pt->tail->prev = new_piece;
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

struct piece *add(PieceTable *pt, char *string, size_t len) {
  if (len == 0)
    return NULL;
  // make room in add_buffer
  while (pt->add_buffer_capacity < pt->add_buffer_len + len) {
    pt->add_buffer = realloc(pt->add_buffer, pt->add_buffer_capacity *= 2);
  }
  memcpy(string, pt->add_buffer + pt->add_buffer_len, len);
  struct piece *piece = malloc(sizeof(struct piece));
  *piece = (struct piece){.buf = ADD,
                          .offset = pt->add_buffer_len,
                          .len = len - 1,
                          .next = NULL,
                          .prev = NULL};
  pt->add_buffer_len += len;
  return piece;
}

bool pt_delete(PieceTable *pt, size_t offset, size_t len) {
  if (offset + len > pt->len)
    return false;

  // find begin and end positions to delete
  struct position begin = find(pt->head, offset);
  struct position end = find(begin.piece, begin.offset + len);

  // create change
  struct change *ch = calloc(1, sizeof(struct change));
  ch->old->first = begin.piece;
  ch->old->last = end.piece;

  if (begin.offset > 0) {
    ch->current->first = malloc(sizeof(struct piece));
    *ch->current->first = (struct piece){
        .buf = begin.piece->buf,
        .offset = begin.piece->offset,
        .len = begin.offset,
        .prev = begin.piece->prev,
        .next = NULL,
    };
  }
  if (end.offset < end.piece->len - 1) {
    ch->current->last = malloc(sizeof(struct piece));
    *ch->current->last = (struct piece){
        .buf = end.piece->buf,
        .offset = begin.piece->offset,
        .len = begin.offset,
        .prev = NULL,
        .next = end.piece->next,
    };
  }
  if (!ch->current->first)
    ch->current->first = ch->current->last;
  if (!ch->current->last)
    ch->current->last = ch->current->first;

  // apply change
  // if(ch->current.first && ch->current.last){
  if (ch->current->first) {
    ch->current->first->next = ch->current->last;
    ch->current->last->prev = ch->current->first;
    begin.piece->prev->next = ch->current->first;
    end.piece->next->prev = ch->current->last;
  } else {
    begin.piece->prev->next = end.piece->next;
    end.piece->next->prev = begin.piece->prev;
  }

  // cleanup redo stack
  ch_free(pt->redo_stack);
  pt->redo_stack = NULL;

  // update undo stack
  ch->next = pt->undo_stack;
  pt->undo_stack = ch;

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

void free_piece_range(struct piece *begin, struct piece *end) {
  if (begin == NULL && end == NULL)
    return;
  assert(begin && end);
  if (begin == end) {
    free(begin);
    return;
  }
  struct piece *begin_next = begin->next;
  free(begin);
  free_piece_range(begin_next, end);
}

void pr_free(struct piece_range *pr) {
  if (!pr)
    return;
  free_piece_range(pr->first, pr->last);
  free(pr);
}

void ch_free(struct change *ch) {
  if (!ch)
    return;
  struct change *change_next = ch->next;
  pr_free(ch->old);
  free(ch);
  ch_free(change_next);
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

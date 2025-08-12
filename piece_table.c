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
struct change_stack;

// TODO: hcreate to implement cache
struct PieceTable {
  char *original_buffer, *add_buffer;
  size_t original_buffer_len;
  size_t add_buffer_len, add_buffer_capacity;
  struct piece *head, *tail; // sentinel nodes
  size_t len;

  struct change_stack *undo_stack, *redo_stack;
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
  struct piece *head, *tail;
};

struct change_stack {
  struct piece_range new, old;
  struct change_stack *next;
};

void free_piece_range(struct piece *begin, struct piece *end);
void chs_free(struct change_stack *);

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
  chs_free(pt->undo_stack);
  chs_free(pt->redo_stack);
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

/**
 * Check if a piece range with dummy head and tail nodes is empty.
 */
bool pr_empty(const struct piece_range *pr) {
  return pr->head->next == pr->tail->prev;
}

/**
 * Replace current piece range by new piece range. Both piece ranges has dummy
 * begin and end nodes, in other words, the efective range goes from
 * pr->head->next to pr->tail->prev, and the change is empty iff
 * pr->head == pr->tail.
 * After the swap the new range becomes the current one, and the current the old
 * one.
 */
void pr_swap(struct piece_range *current, struct piece_range *new) {
  // backup some pieces for later
  struct piece *current_head_next = current->head->next;
  struct piece *current_tail_prev = current->tail->prev;
  // put new piece range in place
  if (!pr_empty(new)) {
    current->head->next = new->head->next;
    current->tail->prev = new->tail->prev;
    new->head->next->prev = current->head;
    new->tail->prev->next = current->tail;
  } else {
    current->head->next = current->tail;
    current->tail->prev = current->head;
  }
  // put old piece range in place
  if (!pr_empty(current)) {
    new->head->next = current_head_next;
    new->tail->prev = current_tail_prev;
    current_head_next->prev = new->head;
    current_tail_prev->next = new->tail;
  } else {
    new->head->next = new->tail;
    new->tail->prev = new->head;
  }
}

// void pt_apply_change(PieceTable *pt, struct change *ch){
// }

bool pt_delete(PieceTable *pt, size_t offset, size_t len) {
  if (len == 0)
    return true;
  if (offset + len > pt->len)
    return false;

  // find begin and end positions to delete
  struct position begin = find(pt->head, offset);
  struct position end = find(begin.piece, begin.offset + len);

  // create change
  struct change_stack *chs = malloc(sizeof(struct change_stack));
  struct piece_range *new = &chs->new;
  struct piece_range *old = &chs->old;

  old->head = begin.piece;
  old->tail = end.piece;

  if (begin.offset > 0) {
    new->head = malloc(sizeof(struct piece));
    *new->head = (struct piece){
        .buf = begin.piece->buf,
        .offset = begin.piece->offset,
        .len = begin.offset,
        .prev = begin.piece->prev,
        .next = NULL,
    };
  }
  if (end.offset < end.piece->len - 1) {
    new->tail = malloc(sizeof(struct piece));
    *new->tail = (struct piece){
        .buf = end.piece->buf,
        .offset = begin.piece->offset,
        .len = begin.offset,
        .prev = NULL,
        .next = end.piece->next,
    };
  }
  if (!new->head)
    new->head = new->tail;
  if (!new->tail)
    new->tail = new->head;

  // apply change
  // if(new->first && new->last){
  if (new->head) {
    new->head->next = new->tail;
    new->tail->prev = new->head;
    begin.piece->prev->next = new->head;
    end.piece->next->prev = new->tail;
  } else {
    begin.piece->prev->next = end.piece->next;
    end.piece->next->prev = begin.piece->prev;
  }

  // cleanup redo stack
  chs_free(pt->redo_stack);
  pt->redo_stack = NULL;

  // update undo stack
  chs->next = pt->undo_stack;
  pt->undo_stack = chs;

  return true;
}

bool pt_insert(PieceTable *pt, size_t offset, char *str, size_t len) {
  // TODO
  if (len == 0)
    return true;

  if (pt->len == 0) {
    struct piece *new_piece = add(pt, str, len);
    struct change_stack *chs = malloc(sizeof(struct change_stack));
    chs->new->head = chs->new->tail = new_piece;
    chs->old = NULL;
    chs->next = NULL;
  }

  struct position pos = find(pt->head, offset);

  // create change
  struct change_stack *ch;
  if (pos.offset == 0) { // at boundary
  } else {
  }

  // apply change

  // cleanup redo stack
  chs_free(pt->redo_stack);
  pt->redo_stack = NULL;

  // update undo stack
  ch->next = pt->undo_stack;
  pt->undo_stack = ch;

  return true;
}

void free_piece_range(struct piece *first, struct piece *last) {
  if (first == NULL && last == NULL)
    return;
  assert(first && last);
  if (first == last) {
    free(first);
    return;
  }
  struct piece *begin_next = first->next;
  free(first);
  free_piece_range(begin_next, last);
}

void pr_free(struct piece_range *pr) {
  free_piece_range(pr->head, pr->tail);
  free(pr);
}

void chs_free(struct change_stack *chs) {
  if (!chs)
    return;
  struct change_stack *change_next = chs->next;
  free(chs);
  chs_free(change_next);
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

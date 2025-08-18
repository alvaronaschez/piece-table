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

struct PieceTable;
struct piece;
enum buffer_type { ORIGINAL, ADD };
struct piece_range;
struct piece_position;
struct change_stack;

void p_free_range(struct piece *begin, struct piece *end);
void p_append(struct piece *, struct piece *);
struct piece *p_copy_range(struct piece *p, size_t i, size_t j);
struct piece_position p_find(struct piece *p, size_t offset);

void chs_free(struct change_stack *);

bool pr_is_empty(const struct piece_range *pr);
void pr_swap(struct piece_range *current, struct piece_range *new);

// public
PieceTable *pt_new();
void pt_free(PieceTable *);
void pt_load_from_file(PieceTable *, char *);
void pt_print(PieceTable *);
char *pt_to_string(PieceTable *, size_t offset, size_t len);
void pt_insert(PieceTable *pt, size_t offset, char *str, size_t len);
void pt_delete(PieceTable *pt, size_t offset, size_t len);
// private
struct piece *pt_new_piece_from_string(PieceTable *pt, char *string,
                                       size_t len);

struct PieceTable {
  char *original_buffer, *add_buffer;
  size_t original_buffer_len;
  size_t add_buffer_len, add_buffer_capacity;
  // struct piece *head, *tail; // sentinel nodes
  struct piece_range *pieces;
  size_t len;

  struct change_stack *undo_stack, *redo_stack;
};

struct piece {
  enum buffer_type buffer;
  size_t offset, len;
  struct piece *next, *prev;
};

struct piece_range {
  struct piece *head, *tail;
};

struct piece_position {
  struct piece *piece;
  size_t offset;
};

struct change_stack {
  struct piece_range *new, *old;
  struct change_stack *next;
};

/*****************************************************************************/
struct piece_range *pr_create_empty() {
  struct piece_range *pr = malloc(sizeof(struct piece_range));
  pr->head = calloc(1, sizeof(struct piece));
  pr->tail = calloc(1, sizeof(struct piece));
  pr->head->next = pr->tail;
  pr->tail->prev = pr->head;
  return pr;
}
struct piece_range *pr_create_with(struct piece *head, struct piece *tail) {
  struct piece_range *pr = malloc(sizeof(struct piece_range));
  pr->head = head;
  pr->tail = tail;
  return pr;
}
void pr_free(struct piece_range *pr) {
  if (pr->head == pr->tail) {
    free(pr->head);
    free(pr);
    return;
  }
  struct piece *new_head = pr->head->next;
  free(pr->head);
  pr->head = new_head;
  pr_free(pr);
}
struct piece_range *pr_append_piece(struct piece_range *pr, struct piece *p) {
  struct piece *prev = pr->tail->prev;
  struct piece *next = pr->tail;
  prev->next = next->prev = p;
  p->prev = prev;
  p->next = next;
  return pr;
}
struct piece *p_create_with(enum buffer_type buffer, size_t offset, size_t len,
                            struct piece *prev, struct piece *next) {
  struct piece *p = malloc(sizeof(struct piece));
  p->buffer = buffer;
  p->offset = offset;
  p->len = len;
  p->prev = prev;
  p->next = next;
  return p;
}
void pt_free_redo_stack(PieceTable *pt) {
  chs_free(pt->redo_stack);
  pt->redo_stack = NULL;
}
void pt_save_change(PieceTable *pt, struct piece_range *old,
                    struct piece_range *new) {
  struct change_stack *change = malloc(sizeof(struct change_stack));
  change->new = new;
  change->old = old;
  change->next = pt->undo_stack;
  pt->undo_stack = change;
}
/*****************************************************************************/

PieceTable *pt_new() {
  PieceTable *pt = calloc(1, sizeof(PieceTable));

  // init dummy nodes
  pt->pieces = pr_create_empty();

  pt->original_buffer = NULL;
  pt->original_buffer_len = 0;

  pt->add_buffer = calloc(ADD_BUFFER_INITIAL_CAPACITY, sizeof(char));
  pt->add_buffer_len = 0;
  pt->add_buffer_capacity = ADD_BUFFER_INITIAL_CAPACITY;

  pt->len = 0;

  return pt;
}

void pt_free(PieceTable *pt) {
  pr_free(pt->pieces);

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

void pt_load_from_file(PieceTable *pt, char *file_name) {
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

  struct piece *new_piece =
      p_create_with(ORIGINAL, 0, pt->original_buffer_len, NULL, NULL);
  pr_append_piece(pt->pieces, new_piece);
}

void pt_print(PieceTable *pt) {
  struct piece *p = pt->pieces->head->next;
  while (p != pt->pieces->tail) {
    const void *buf =
        p->buffer == ORIGINAL ? pt->original_buffer : pt->add_buffer;
    write(STDOUT_FILENO, buf + p->offset, p->len);
    p = p->next;
  }
}

char *pt_to_string(PieceTable *pt, size_t offset, size_t len) {
  char *c = malloc(len + 1);
  c[len] = '\0';
  if (len == 0 || offset + len - 1 > pt->len)
    return c;
  // TODO
  return c;
}

struct piece_position p_find(struct piece *p, size_t offset) {
  assert(p);
  if (p->len > offset || p->next == 0 && offset == 0)
    return (struct piece_position){.piece = p, .offset = offset};
  return p_find(p->next, offset - p->len);
}

struct piece *pt_new_piece_from_string(PieceTable *pt, char *string,
                                       size_t len) {
  if (len == 0)
    return NULL;
  // make room in add_buffer
  while (pt->add_buffer_capacity < pt->add_buffer_len + len) {
    pt->add_buffer = realloc(pt->add_buffer, pt->add_buffer_capacity *= 2);
  }
  memcpy(string, pt->add_buffer + pt->add_buffer_len, len);
  struct piece *p = p_create_with(ADD, pt->add_buffer_len, len, NULL, NULL);
  pt->add_buffer_len += len;
  return p;
}

/**
 * Check if a piece range with dummy head and tail nodes is empty.
 */
bool pr_is_empty(const struct piece_range *pr) {
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
  if (!pr_is_empty(new)) {
    current->head->next = new->head->next;
    current->tail->prev = new->tail->prev;
    new->head->next->prev = current->head;
    new->tail->prev->next = current->tail;
  } else {
    current->head->next = current->tail;
    current->tail->prev = current->head;
  }
  // put old piece range in place
  if (!pr_is_empty(current)) {
    new->head->next = current_head_next;
    new->tail->prev = current_tail_prev;
    current_head_next->prev = new->head;
    current_tail_prev->next = new->tail;
  } else {
    new->head->next = new->tail;
    new->tail->prev = new->head;
  }
}

void p_append(struct piece *p, struct piece *q) {
  q->prev = p;
  q->next = p->next;
  p->next = q;
  p->next->prev = q;
}

/**
 * both indexes i and j are inclusive ie we copy the range [i, j]
 */
struct piece *p_copy_range(struct piece *p, size_t i, size_t j) {
  assert(j >= i);
  struct piece *q = malloc(sizeof(struct piece));
  q->buffer = p->buffer;
  q->offset = p->offset + i;
  q->len = j - i + 1;
  return q;
}

void pt_delete(PieceTable *pt, size_t offset, size_t len) {
  if (len == 0 || pt->len == 0 || offset + len > pt->len)
    return;

  // find begin and end positions to delete
  struct piece_position begin = p_find(pt->pieces->head, offset);
  struct piece_position end = p_find(begin.piece, begin.offset + len - 1);

  // create changes
  struct piece_range *old = pr_create_with(begin.piece->prev, end.piece->next);
  struct piece_range *new = pr_create_empty();

  // if not in boundary copy content to keep
  if (begin.offset != 0) {
    struct piece *p = p_copy_range(begin.piece, 0, begin.offset - 1);
    pr_append_piece(new, p);
  }
  // if not in boundary copy content to keep
  if (end.offset != end.piece->len - 1) {
    struct piece *p =
        p_copy_range(end.piece, end.offset + 1, end.piece->len - 1);
    pr_append_piece(new, p);
  }

  // apply change
  pr_swap(new, old);
  // update undo and redo stacks
  pt_save_change(pt, old, new);
  pt_free_redo_stack(pt);
}

void pt_insert(PieceTable *pt, size_t offset, char *str, size_t len) {
  if (len == 0 || offset > len)
    return;

  // find insertion position
  struct piece_position pos = p_find(pt->pieces->head, offset);

  if (pos.piece->buffer == ADD &&
      pos.piece->offset + pos.piece->len == pt->add_buffer_len) {
    // TODO:cache
    return;
  }

  // create change
  struct piece_range *new = pr_create_empty();
  struct piece_range *old = pr_create_with(
      pos.piece->prev, pos.offset == 0 ? pos.piece : pos.piece->next);

  // if not in boundary copy previous content before insertion point
  if (pos.offset != 0) {
    struct piece *p = p_copy_range(pos.piece, 0, pos.offset - 1);
    pr_append_piece(new, p);
  }

  // add new string to new piece
  pr_append_piece(new, pt_new_piece_from_string(pt, str, len));

  // if not in boundary copy previous content after insertion point (including
  // insertion point)
  if (pos.offset != 0) {
    struct piece *p = p_copy_range(pos.piece, pos.offset, pos.piece->len - 1);
    pr_append_piece(new, p);
  }

  // apply change
  pr_swap(new, old);
  // update undo and redo stacks
  pt_save_change(pt, old, new);
  pt_free_redo_stack(pt);
}

void p_free_range(struct piece *first, struct piece *last) {
  if (first == NULL && last == NULL)
    return;
  assert(first && last);
  if (first == last) {
    free(first);
    return;
  }
  struct piece *begin_next = first->next;
  free(first);
  p_free_range(begin_next, last);
}

// void pr_free(struct piece_range *pr) {
//   free_piece_range(pr->head, pr->tail);
//   free(pr);
// }

void chs_free(struct change_stack *chs) {
  if (!chs)
    return;
  struct change_stack *change_next = chs->next;
  free(chs);
  pr_free(chs->old);
  chs_free(change_next);
}

int main() {
  printf("--start--\n");
  PieceTable *pt = pt_new();
  pt_load_from_file(pt, "/home/alvaro/ws/piece-table/piece_table.c");
  // write(STDOUT_FILENO, pt->original_buffer, pt->original_buffer_len);
  pt_print(pt);
  pt_free(pt);
  printf("--end--\n");
}

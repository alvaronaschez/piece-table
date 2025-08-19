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

struct PieceTable {
  struct buffer *original_buffer, *add_buffer;
  struct piece_range *pieces;
  size_t len;
  struct change_stack *undo_stack, *redo_stack;
};

struct buffer {
  char *data;
  size_t len, capacity;
};

enum buffer_type { ORIGINAL, ADD };

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

/* buffer */
struct buffer *b_create_empty(size_t capacity) {
  struct buffer *b = malloc(sizeof(struct buffer));
  b->data = calloc(capacity, sizeof(char));
  b->len = 0;
  b->capacity = capacity;
  return b;
}
struct buffer *b_create_readonly(char *data, size_t len) {
  struct buffer *b = malloc(sizeof(struct buffer));
  b->data = data;
  b->len = len;
  b->capacity = 0;
  return b;
}
void b_free(struct buffer *b) {
  free(b->data);
  free(b);
}
void b_append(struct buffer *b, char *string, size_t len) {
  if (len == 0 || b->capacity == 0)
    return;
  // increase capacity
  if (b->capacity < b->len + len) {
    while (b->capacity < b->len + len) {
      b->capacity *= 2;
    }
    b->data = realloc(b->data, b->capacity);
  }
  b->len += len;
  memcpy(string, b->data + b->len, len);
}

/* piece */
struct piece_position p_find(struct piece *p, size_t offset) {
  assert(p);
  if (p->len > offset || p->next == 0 && offset == 0)
    return (struct piece_position){.piece = p, .offset = offset};
  return p_find(p->next, offset - p->len);
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
/**
 * both indexes i and j are inclusive ie we copy the range [i, j]
 */
struct piece *p_create_from(struct piece *p, size_t i, size_t j) {
  assert(j >= i);
  struct piece *q = malloc(sizeof(struct piece));
  q->buffer = p->buffer;
  q->offset = p->offset + i;
  q->len = j - i + 1;
  return q;
}

/* piece range */
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
void pr_swap(struct piece_range *pp, struct piece_range *qq) {
  // backup some pieces for later
  struct piece *pp_head_next = pp->head->next;
  struct piece *pp_tail_prev = pp->tail->prev;
  if (!pr_is_empty(qq)) {
    pp->head->next = qq->head->next;
    pp->tail->prev = qq->tail->prev;
    qq->head->next->prev = pp->head;
    qq->tail->prev->next = pp->tail;
  } else {
    pp->head->next = pp->tail;
    pp->tail->prev = pp->head;
  }
  if (!pr_is_empty(pp)) {
    qq->head->next = pp_head_next;
    qq->tail->prev = pp_tail_prev;
    pp_head_next->prev = qq->head;
    pp_tail_prev->next = qq->tail;
  } else {
    qq->head->next = qq->tail;
    qq->tail->prev = qq->head;
  }
}

/* change stack */
void chs_free(struct change_stack *chs) {
  if (!chs)
    return;
  struct change_stack *change_next = chs->next;
  free(chs);
  pr_free(chs->old);
  chs_free(change_next);
}

static inline void chs_push(struct change_stack **chs, struct change_stack *ch){
  ch->next = *chs;
  *chs = ch;
}

static inline struct change_stack *chs_pop(struct change_stack **chs){
  if(chs==NULL || *chs==NULL)
    return NULL;
  struct change_stack *ch = *chs;
  *chs = ch->next;
  ch->next = NULL;
  return ch;
}

void chs_swap(struct change_stack *ch){
  struct piece_range *aux = ch->new;
  ch->new = ch->old;
  ch->old = aux;
}

/* pt private */
struct piece *pt_create_piece_from_string(PieceTable *pt, char *string,
                                       size_t len) {
  if (len == 0)
    return NULL;
  b_append(pt->add_buffer, string, len);
  struct piece *p = p_create_with(ADD, pt->add_buffer->len, len, NULL, NULL);
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
void pt_print(PieceTable *pt, int file_descriptor) {
  struct piece *p = pt->pieces->head->next;
  while (p != pt->pieces->tail) {
    const void *buf = p->buffer == ORIGINAL ? pt->original_buffer->data
                                            : pt->add_buffer->data;
    write(file_descriptor, buf + p->offset, p->len);
    p = p->next;
  }
}

/* pt public */

PieceTable *pt_create() {
  PieceTable *pt = calloc(1, sizeof(PieceTable));

  pt->pieces = pr_create_empty();
  pt->original_buffer = NULL;
  pt->add_buffer = b_create_empty(ADD_BUFFER_INITIAL_CAPACITY);
  pt->len = 0;
  return pt;
}

void pt_free(PieceTable *pt) {
  pr_free(pt->pieces);

  // unmap original buffer
  if (pt->original_buffer)
    munmap(pt->original_buffer->data, pt->original_buffer->len);

  b_free(pt->add_buffer);

  // free undo and redo stacks
  chs_free(pt->undo_stack);
  chs_free(pt->redo_stack);
}

void pt_load_from_file(PieceTable *pt, char *file_name) {
  int fd = open(file_name, O_RDONLY);
  if (fd == -1){
    printf("ERROR: open file\n");
    exit(EXIT_FAILURE);
  }

  // obtain file size
  struct stat sb;
  if (fstat(fd, &sb) == -1){
    printf("ERROR: fstat\n");
    exit(EXIT_FAILURE);
  }

  pt->original_buffer = b_create_readonly(
      mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0), sb.st_size);
  close(fd);
  if (pt->original_buffer == MAP_FAILED){
    printf("ERROR: mmap\n");
    exit(EXIT_FAILURE);
  }

  struct piece *new_piece =
      p_create_with(ORIGINAL, 0, pt->original_buffer->len, NULL, NULL);
  pr_append_piece(pt->pieces, new_piece);
}

void pt_save_to_file(PieceTable *pt, char *file_name){
  int fd = open(file_name, O_WRONLY|O_CREAT);
  if(fd==-1){
    printf("ERROR: save file/n");
    exit(EXIT_FAILURE);
  }
  pt_print(pt, fd);
  close(fd);
}

//char *pt_to_string(PieceTable *pt, size_t offset, size_t len) {
//  char *c = malloc(len + 1);
//  c[len] = '\0';
//  if (len == 0 || offset + len - 1 > pt->len)
//    return c;
//  // TODO
//  return c;
//}

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
    struct piece *p = p_create_from(begin.piece, 0, begin.offset - 1);
    pr_append_piece(new, p);
  }
  // if not in boundary copy content to keep
  if (end.offset != end.piece->len - 1) {
    struct piece *p =
        p_create_from(end.piece, end.offset + 1, end.piece->len - 1);
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
      pos.piece->offset + pos.piece->len == pt->add_buffer->len) {
    b_append(pt->add_buffer, str, len);
    pos.piece->len += len;
    return;
  }

  // create change
  struct piece_range *new = pr_create_empty();
  struct piece_range *old = pr_create_with(
      pos.piece->prev, pos.offset == 0 ? pos.piece : pos.piece->next);

  // if not in boundary copy previous content before insertion point
  if (pos.offset != 0) {
    struct piece *p = p_create_from(pos.piece, 0, pos.offset - 1);
    pr_append_piece(new, p);
  }

  // add new string to new piece
  pr_append_piece(new, pt_create_piece_from_string(pt, str, len));

  // if not in boundary copy previous content after insertion point (including
  // insertion point)
  if (pos.offset != 0) {
    struct piece *p = p_create_from(pos.piece, pos.offset, pos.piece->len - 1);
    pr_append_piece(new, p);
  }

  // apply change
  pr_swap(new, old);
  // update undo and redo stacks
  pt_save_change(pt, old, new);
  pt_free_redo_stack(pt);
}

void pt_undo(PieceTable *pt){
  if(!pt->undo_stack)
    return;
  struct change_stack *ch = chs_pop(&pt->undo_stack);
  pr_swap(ch->old, ch->new);
  chs_swap(ch); // now new is old and old is new
  chs_push(&pt->redo_stack, ch);
}
void pt_redo(PieceTable *pt){
  if(!pt->redo_stack)
    return;
  struct change_stack *ch = chs_pop(&pt->redo_stack);
  pr_swap(ch->old, ch->new);
  chs_swap(ch); // now new is old and old is new
  chs_push(&pt->undo_stack, ch);
}

/* main */

int main() {
  printf("--start--\n");
  PieceTable *pt = pt_create();
  pt_load_from_file(pt, "/home/alvaro/ws/piece-table/piece_table.c");
  pt_print(pt, STDOUT_FILENO);
  pt_save_to_file(pt, "/home/alvaro/ws/piece-table/piece_table2.c");
  pt_free(pt);
  printf("--end--\n");
}

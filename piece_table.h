#ifndef piece_table_h
#define piece_table_h

//#include <stdbool.h>
#include <stddef.h>

typedef struct piece_table PieceTable;
typedef struct piece_position PieceTableIterator;

PieceTable *pt_create();
void pt_free(PieceTable *);

void pt_load_from_file(PieceTable *, char *);
void pt_save_to_file(PieceTable *, char *);

void pt_delete(PieceTable *, size_t offset, size_t len);
void pt_insert(PieceTable *, size_t offset, char *str, size_t len);

void pt_undo(PieceTable *);
void pt_redo(PieceTable *);

#endif

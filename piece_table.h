#ifndef piece_table
#define piece_table

#include <stdbool.h>
#include <stddef.h>

typedef struct PieceTable PieceTable;

PieceTable *pt_new();
void pt_free(PieceTable *);

void pt_load(PieceTable *, char *file_name);
void pt_save(PieceTable *);

//void pt_insert(PieceTable *, size_t offset, char *data, size_t len);
//void pt_delete(PieceTable *, size_t offset, size_t len);

bool pt_byte_at(PieceTable *, size_t pos, char *c, unsigned char len);
bool pt_codepoint_at(PieceTable *, size_t pos, char *c, unsigned char len);

void pt_undo(PieceTable *);
void pt_redo(PieceTable *);

#endif

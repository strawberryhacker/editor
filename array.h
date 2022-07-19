// Credit Alex Taradov.

#ifndef ARRAY_H
#define ARRAY_H

//--------------------------------------------------------------------------------------------------

#include "string.h"
#include "stdlib.h"

//--------------------------------------------------------------------------------------------------

#define define_array(name, ArrayType, ItemType)                                                                    \
                                                                                                                   \
typedef struct {                                                                                                   \
  ItemType* items;                                                                                                 \
  int capacity;                                                                                                    \
  int count;                                                                                                       \
} ArrayType;                                                                                                       \
                                                                                                                   \
static inline void name##_resize(ArrayType* array, int capacity) {                                                 \
  void* data = malloc(capacity * sizeof(ItemType));                                                                \
  memcpy(data, array->items, array->count * sizeof(ItemType));                                                     \
                                                                                                                   \
  if (array->capacity) {                                                                                           \
    free(array->items);                                                                                            \
  }                                                                                                                \
                                                                                                                   \
  array->items = data;                                                                                             \
  array->capacity = capacity;                                                                                      \
}                                                                                                                  \
                                                                                                                   \
static inline void name##_extend(ArrayType* array, int size) {                                                     \
  if (array->capacity < size) {                                                                                    \
    name##_resize(array, size * 2);                                                                                \
  }                                                                                                                \
}                                                                                                                  \
                                                                                                                   \
static inline void name##_clear(ArrayType* array) {                                                                \
  array->count = 0;                                                                                                \
}                                                                                                                  \
                                                                                                                   \
static inline void name##_init(ArrayType* array, int capacity) {                                                   \
  array->count = 0;                                                                                                \
  array->capacity = 0;                                                                                             \
  name##_resize(array, capacity);                                                                                  \
}                                                                                                                  \
                                                                                                                   \
static inline void name##_insert_multiple(ArrayType* array, ItemType* items, int count, int index) {               \
  name##_extend(array, array->count + count);                                                                      \
                                                                                                                   \
  if (index < array->count) {                                                                                      \
    memmove(&array->items[index + count], &array->items[index], (array->count - index) * sizeof(ItemType));        \
  }                                                                                                                \
                                                                                                                   \
  memcpy(&array->items[index], items, count * sizeof(ItemType));                                                   \
  array->count += count;                                                                                           \
}                                                                                                                  \
                                                                                                                   \
static inline void name##_insert(ArrayType* array, ItemType item, int index) {                                     \
  name##_insert_multiple(array, &item, 1, index);                                                                  \
}                                                                                                                  \
                                                                                                                   \
static inline int name##_append(ArrayType* array, ItemType item) {                                                 \
  name##_insert(array, item, array->count);                                                                        \
  return array->count - 1;                                                                                         \
}                                                                                                                  \
                                                                                                                   \
static inline int name##_append_multiple(ArrayType* array, ItemType* items, int size) {                            \
  int count = array->count;                                                                                        \
  name##_insert_multiple(array, items, size, array->count);                                                        \
  return count;                                                                                                    \
}                                                                                                                  \
                                                                                                                   \
static inline void name##_remove(ArrayType* array, int index) {                                                    \
  if (index < array->count) {                                                                                      \
    memmove(&array->items[index], &array->items[index + 1], (array->count - index) * sizeof(ItemType));            \
  }                                                                                                                \
                                                                                                                   \
  array->count--;                                                                                                  \
}

//--------------------------------------------------------------------------------------------------

#endif
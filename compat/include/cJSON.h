#ifndef ASTRAE_ORIGINAL_CJSON_H
#define ASTRAE_ORIGINAL_CJSON_H
/* ABI declaration reconstructed from the supplied libastraeoffline.so DWARF.
 * Original cJSON_Version() reports 1.7.19. Struct size and member offsets
 * below are taken from DWARF (cJSON: 64 bytes on AArch64 LP64). */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(_WIN32) && defined(CJSON_SHARED)
# ifdef CJSON_BUILD
#  define CJSON_PUBLIC(type) __declspec(dllexport) type
# else
#  define CJSON_PUBLIC(type) __declspec(dllimport) type
# endif
#elif defined(__GNUC__) || defined(__clang__)
# define CJSON_PUBLIC(type) __attribute__((visibility("default"))) type
#else
# define CJSON_PUBLIC(type) type
#endif

typedef int cJSON_bool;
typedef struct cJSON {
    struct cJSON *next;       /* DWARF offset 0 */
    struct cJSON *prev;       /* DWARF offset 8 */
    struct cJSON *child;      /* DWARF offset 16 */
    int type;                 /* DWARF offset 24 */
    char *valuestring;        /* DWARF offset 32 */
    int valueint;             /* DWARF offset 40 */
    double valuedouble;       /* DWARF offset 48 */
    char *string;             /* DWARF offset 56 */
} cJSON;
typedef struct cJSON_Hooks {
    void *(*malloc_fn)(size_t size);
    void (*free_fn)(void *ptr);
} cJSON_Hooks;

#define cJSON_Invalid 0
#define cJSON_False 1
#define cJSON_True 2
#define cJSON_NULL 4
#define cJSON_Number 8
#define cJSON_String 16
#define cJSON_Array 32
#define cJSON_Object 64
#define cJSON_Raw 128
#define cJSON_IsReference 256
#define cJSON_StringIsConst 512
#define cJSON_TypeMask 255

/* Public version and recursion limits from cJSON 1.7.19. */
#define CJSON_VERSION_MAJOR 1
#define CJSON_VERSION_MINOR 7
#define CJSON_VERSION_PATCH 19
#ifndef CJSON_NESTING_LIMIT
#define CJSON_NESTING_LIMIT 1000
#endif
#ifndef CJSON_CIRCULAR_LIMIT
#define CJSON_CIRCULAR_LIMIT 10000
#endif

CJSON_PUBLIC(const char *) cJSON_Version(void);
CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void);
CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON *item);
CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON *item);
CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks *hooks);
CJSON_PUBLIC(void) cJSON_Delete(cJSON *item);
CJSON_PUBLIC(double) cJSON_SetNumberHelper(cJSON *object, double number);
CJSON_PUBLIC(char *) cJSON_SetValuestring(cJSON *object, const char *valuestring);
CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length, const char **return_parse_end, cJSON_bool require_null_terminated);
CJSON_PUBLIC(char *) cJSON_Print(const cJSON *item);
CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item);
CJSON_PUBLIC(char *) cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt);
CJSON_PUBLIC(cJSON_bool) cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const cJSON_bool format);
CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array);
CJSON_PUBLIC(cJSON *) cJSON_GetArrayItem(const cJSON *array, int index);
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON *object, const char *string);
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string);
CJSON_PUBLIC(cJSON_bool) cJSON_HasObjectItem(const cJSON *object, const char *string);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *array, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item);
CJSON_PUBLIC(cJSON *) cJSON_AddNullToObject(cJSON * const object, const char *name);
CJSON_PUBLIC(cJSON *) cJSON_AddTrueToObject(cJSON * const object, const char *name);
CJSON_PUBLIC(cJSON *) cJSON_AddFalseToObject(cJSON * const object, const char *name);
CJSON_PUBLIC(cJSON *) cJSON_AddBoolToObject(cJSON * const object, const char *name, const cJSON_bool boolean);
CJSON_PUBLIC(cJSON *) cJSON_AddNumberToObject(cJSON * const object, const char *name, const double number);
CJSON_PUBLIC(cJSON *) cJSON_AddStringToObject(cJSON * const object, const char *name, const char *string);
CJSON_PUBLIC(cJSON *) cJSON_AddRawToObject(cJSON * const object, const char *name, const char *raw);
CJSON_PUBLIC(cJSON *) cJSON_AddObjectToObject(cJSON * const object, const char *name);
CJSON_PUBLIC(cJSON *) cJSON_AddArrayToObject(cJSON * const object, const char *name);
CJSON_PUBLIC(cJSON *) cJSON_CreateNull(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateTrue(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateFalse(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateBool(cJSON_bool boolean);
CJSON_PUBLIC(cJSON *) cJSON_CreateNumber(double number);
CJSON_PUBLIC(cJSON *) cJSON_CreateString(const char *string);
CJSON_PUBLIC(cJSON *) cJSON_CreateRaw(const char *raw);
CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateArray(void);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromArray(cJSON *array, int which);
CJSON_PUBLIC(void) cJSON_DeleteItemFromArray(cJSON *array, int which);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObject(cJSON *object, const char *string);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string);
CJSON_PUBLIC(void) cJSON_DeleteItemFromObject(cJSON *object, const char *string);
CJSON_PUBLIC(void) cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string);
CJSON_PUBLIC(cJSON_bool) cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON *replacement);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObject(cJSON *object, const char *string, cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object, const char *string, cJSON *newitem);
CJSON_PUBLIC(cJSON *) cJSON_CreateStringReference(const char *string);
CJSON_PUBLIC(cJSON *) cJSON_CreateObjectReference(const cJSON *child);
CJSON_PUBLIC(cJSON *) cJSON_CreateArrayReference(const cJSON *child);
CJSON_PUBLIC(cJSON *) cJSON_CreateIntArray(const int *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateFloatArray(const float *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateDoubleArray(const double *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateStringArray(const char * const *strings, int count);
CJSON_PUBLIC(cJSON *) cJSON_Duplicate(const cJSON *item, cJSON_bool recurse);
/* Exported in the original dynamic symbol table although not part of the usual public header. */
CJSON_PUBLIC(cJSON *) cJSON_Duplicate_rec(const cJSON *item, size_t depth, cJSON_bool recurse);
CJSON_PUBLIC(void) cJSON_Minify(char *json);
CJSON_PUBLIC(cJSON_bool) cJSON_IsInvalid(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsFalse(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsTrue(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsBool(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNull(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsArray(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsRaw(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_Compare(const cJSON * const a, const cJSON * const b, const cJSON_bool case_sensitive);
CJSON_PUBLIC(void *) cJSON_malloc(size_t size);
CJSON_PUBLIC(void) cJSON_free(void *object);

/* Source-level helper macros shipped by the upstream cJSON 1.7.19 header. */
#define cJSON_SetIntValue(object, number) ((object) ? (object)->valueint = (object)->valuedouble = (number) : (number))
#define cJSON_SetNumberValue(object, number) ((object) != NULL ? cJSON_SetNumberHelper((object), (double)(number)) : (number))
#define cJSON_SetBoolValue(object, boolValue) (((object) != NULL && (((object)->type & (cJSON_False | cJSON_True)) != 0)) ? ((object)->type = ((object)->type & (~(cJSON_False | cJSON_True))) | ((boolValue) ? cJSON_True : cJSON_False)) : cJSON_Invalid)
#define cJSON_ArrayForEach(element, array) for ((element) = ((array) != NULL) ? (array)->child : NULL; (element) != NULL; (element) = (element)->next)

#if defined(__cplusplus)
static_assert(sizeof(cJSON) == 64, "cJSON ABI size mismatch");
static_assert(sizeof(cJSON_Hooks) == 16 && offsetof(cJSON_Hooks, malloc_fn) == 0 && offsetof(cJSON_Hooks, free_fn) == 8, "cJSON hooks ABI mismatch");
static_assert(offsetof(cJSON, next) == 0 && offsetof(cJSON, prev) == 8 && offsetof(cJSON, child) == 16, "cJSON list ABI mismatch");
static_assert(offsetof(cJSON, type) == 24 && offsetof(cJSON, valuestring) == 32 && offsetof(cJSON, valueint) == 40, "cJSON scalar ABI mismatch");
static_assert(offsetof(cJSON, valuedouble) == 48 && offsetof(cJSON, string) == 56, "cJSON data ABI mismatch");
#endif
#ifdef __cplusplus
}
#endif
#endif

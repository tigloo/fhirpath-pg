#ifndef __FHIRPATH_H__
#define __FHIRPATH_H__

#include "access/gin.h"
#include "fmgr.h"
#include "utils/numeric.h"
#include "utils/jsonb.h"

/* fhirpath datum */
typedef struct
{
	int32	vl_len_;	/* varlena header (do not touch directly!) */
} Fhirpath;

#define DatumGetFhirpathP(d)	((Fhirpath*)PG_DETOAST_DATUM(d))
#define PG_GETARG_FHIRPATH(x)	DatumGetFhirpathP(PG_GETARG_DATUM(x))
#define PG_RETURN_FHIRPATH(p)	PG_RETURN_POINTER(p)

typedef enum FhirpathItemType {
	fpPipe,
	fpOr,
	fpKey,
	fpEqual,
	fpResourceType,
	fpString,
	fpValues,
	fpExists,
	fpNull
} FhirpathItemType;

typedef struct FhirpathParseItem FhirpathParseItem;
/* in memory representation parse tree of fhirpath */
struct FhirpathParseItem {
	FhirpathItemType	type;
	FhirpathParseItem	*next; /* next in path */

	union {
		struct {
			FhirpathParseItem	*left;
			FhirpathParseItem	*right;
		} args;

		FhirpathParseItem	*arg;
		int8		isType; /* jbv* values */

		Numeric		numeric;
		bool		boolean;
		struct {
			uint32      len;
			char        *val; /* could not be not null-terminated */
		} string;

		struct {
			int					nelems;
			FhirpathParseItem	**elems;
		} array;
	};
};

/* on disk representation of fhirpath */
typedef struct FhirpathItem {
	FhirpathItemType	type;
	int32			nextPos;
	char			*base;

	union {
		struct {
			char		*data;  /* for bool, numeric and string/key */
			int			datalen; /* filled only for string/key */
		} value;

		struct {
			int32	left;
			int32	right;
		} args;

		int32		arg;

		struct {
			int		nelems;
			int		current;
			int32	*arrayPtr;
		} array;
	};
} FhirpathItem;

extern FhirpathParseItem* parsefhirpath(const char *str, int len);

/* utils to convert on disk to inmemory representation */
extern void fpInit(FhirpathItem *v, Fhirpath *js);
extern void fpInitByBuffer(FhirpathItem *v, char *base, int32 pos);
extern bool fpGetNext(FhirpathItem *v, FhirpathItem *a);
extern void fpGetArg(FhirpathItem *v, FhirpathItem *a);
extern void fpGetLeftArg(FhirpathItem *v, FhirpathItem *a);
extern void fpGetRightArg(FhirpathItem *v, FhirpathItem *a);
extern Numeric	fpGetNumeric(FhirpathItem *v);
extern bool		fpGetBool(FhirpathItem *v);
extern int32	fpGetIsType(FhirpathItem *v);
extern char * fpGetString(FhirpathItem *v, int32 *len);
extern void fpIterateInit(FhirpathItem *v);
extern bool fpIterateArray(FhirpathItem *v, FhirpathItem *e);

void alignStringInfoInt(StringInfo buf);

extern int serializeFhirpathParseItem(StringInfo buf, FhirpathParseItem *item);
extern void printFhirpathItem(StringInfo buf, FhirpathItem *v, bool inKey);

/* copypaste from jsonb utils to build jsonb objects/arrays */
typedef struct JsonbInState
{
	JsonbParseState *parseState;
	JsonbValue *res;
} JsonbInState;

#endif

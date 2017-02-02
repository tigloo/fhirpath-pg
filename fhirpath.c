#include "postgres.h"
#include "miscadmin.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/json.h"
#include "fhirpath.h"
#include "utils/jsonb.h"

#define PG_RETURN_FHIRPATH(p)	PG_RETURN_POINTER(p)

PG_MODULE_MAGIC;

void initJsonbValue(JsonbValue *jbv, Jsonb *jb);

void appendJsonbValuePrimitives(StringInfoData *buf, JsonbValue *jbv, char *prefix, char *suffix, char *delim);

static long recursive_fhirpath_values(JsonbInState *result, JsonbValue *jbv);
static long do_fhirpath_extract(JsonbInState *result, Jsonb *jb, Fhirpath *fp_in);
static text *JsonbValueToText(JsonbValue *v);

PG_FUNCTION_INFO_V1(fhirpath_in);
Datum
fhirpath_in(PG_FUNCTION_ARGS)
{
	char				*in = PG_GETARG_CSTRING(0);
	int32				len = strlen(in);
	FhirpathParseItem	*fhirpath = parsefhirpath(in, len);
	Fhirpath    		*res;
	StringInfoData		buf;

	if (fhirpath != NULL)
	{

		initStringInfo(&buf);
		enlargeStringInfo(&buf, len /* estimation */);
		appendStringInfoSpaces(&buf, VARHDRSZ);

		serializeFhirpathParseItem(&buf, fhirpath);
		res = (Fhirpath*)buf.data;

		SET_VARSIZE(res, buf.len);
		PG_RETURN_FHIRPATH(res);
	} else {
		elog(INFO, "parse error");
		PG_RETURN_NULL();
	}

}


PG_FUNCTION_INFO_V1(fhirpath_out);
Datum
fhirpath_out(PG_FUNCTION_ARGS)
{

	Fhirpath			*in = PG_GETARG_FHIRPATH(0);

	StringInfoData  	buf;
	FhirpathItem		v;

	initStringInfo(&buf);
	enlargeStringInfo(&buf, VARSIZE(in) /* estimation */);

	fpInit(&v, in);

	printFhirpathItem(&buf, &v, false);


	PG_RETURN_CSTRING(buf.data);
}


static JsonbValue
*getKey(char *key, JsonbValue *jbv){
	JsonbValue	key_v;
	key_v.type = jbvString;
	key_v.val.string.len = strlen(key);
	key_v.val.string.val = key;

	if(jbv->type == jbvBinary){
		return findJsonbValueFromContainer((JsonbContainer *) jbv->val.binary.data , JB_FOBJECT, &key_v);
	} else {
		return NULL;
	}
}

static bool
checkScalarEquality(FhirpathItem *fpi,  JsonbValue *jb) {
	if (jb->type == jbvString) {
		return (fpi->value.datalen == jb->val.string.len
				&& memcmp(jb->val.string.val, fpi->value.data, fpi->value.datalen) == 0);
	}
	return false;
}

typedef void (*reduce_fn)(void *acc, JsonbValue *val);

void
appendJsonbValue(JsonbValue *jbval, void *acc, reduce_fn fn)
{
	JsonbIterator *it;
	JsonbValue	v;
	JsonbIteratorToken tok;

	/* elog(INFO, "Append!, %d", jbval->type == jbvArray); */
	/* elog(INFO, "Append: %s",text_to_cstring(JsonbValueToText(jbval))); */

	if (jbval != NULL) // && (jbval->type != jbvArray || jbval->type == jbvObject)
	{
		fn(acc, jbval);
	}

	/* /\* unpack the binary and add each piece to the pstate *\/ */
	/* it = JsonbIteratorInit(jbval->val.binary.data); */
	/* while ((tok = JsonbIteratorNext(&it, &v, false)) != WJB_DONE) */
	/* 	fn(acc, &v); */

}

/* this function convert JsonbValue to string, */
/* StringInfoData out buffer is optional */
char *jsonbv_to_string(StringInfoData *out, JsonbValue *v){
	if (out == NULL)
		out = makeStringInfo();

	switch(v->type)
	{
    case jbvNull:
		return NULL;
		break;
    case jbvBool:
		appendStringInfoString(out, (v->val.boolean ? "true" : "false"));
		break;
    case jbvString:
		appendBinaryStringInfo(out, v->val.string.val, v->val.string.len);
		/* appendStringInfoString(out, pnstrdup(v->val.string.val, v->val.string.len)); */
		break;
    case jbvNumeric:
		appendStringInfoString(out, DatumGetCString(DirectFunctionCall1(numeric_out, PointerGetDatum(v->val.numeric))));
		break;
    case jbvBinary:
    case jbvArray:
    case jbvObject:
	{
        (void) JsonbToCString(out, v->val.binary.data, -1);
	}
	break;
    default:
		elog(ERROR, "Wrong jsonb type: %d", v->type);
	}
	return out->data;
}



static long
reduce_fhirpath(JsonbValue *jbv, FhirpathItem *path_item, void *acc, reduce_fn fn)
{

	char *key;

	JsonbValue *next_v = NULL;
	FhirpathItem next_item;

	JsonbIterator *array_it;
	JsonbValue	array_value;
	int next_it;

	long num_results = 0;

	check_stack_depth();

	switch(path_item->type)
	{

	case fpOr:
		/* elog(INFO, "extract fppipe"); */
		fpGetLeftArg(path_item, &next_item);
		num_results = reduce_fhirpath(jbv, &next_item, acc, fn);

		if(num_results == 0){
			fpGetRightArg(path_item, &next_item);
			num_results += reduce_fhirpath(jbv, &next_item, acc, fn);
		}

		break;
	case fpPipe:
		/* elog(INFO, "extract Pipe"); */
		fpGetLeftArg(path_item, &next_item);
		num_results += reduce_fhirpath(jbv, &next_item, acc, fn);

		fpGetRightArg(path_item, &next_item);
		num_results += reduce_fhirpath(jbv, &next_item, acc, fn);

		break;
	case fpEqual:
		fpGetLeftArg(path_item, &next_item);
		key = fpGetString(&next_item, NULL);
		next_v = getKey(key, jbv);

		fpGetRightArg(path_item, &next_item);

		if(next_v != NULL &&  checkScalarEquality(&next_item, next_v)){
			if (fpGetNext(path_item, &next_item)) {
				num_results += reduce_fhirpath(jbv, &next_item, acc, fn);
			} else {
				fn(acc, jbv);
				num_results += 1;
			}
		}
		break;
	case fpResourceType:
		key = fpGetString(path_item, NULL);
		next_v = getKey("resourceType", jbv);
		/* elog(INFO, "fpResourceType: %s, %s",  key, next_v->val.string.val); */
		if(next_v != NULL && checkScalarEquality(path_item, next_v)){
			if (fpGetNext(path_item, &next_item)) {
				num_results = reduce_fhirpath(jbv, &next_item, acc, fn);
			}
		}
		break;
	case fpKey:
		key = fpGetString(path_item, NULL);
		next_v = getKey(key, jbv);

		/* elog(INFO, "got key: %s, %d", key, next_v); */

		if (next_v != NULL) {
			/* elog(INFO, "type %d", next_v->type); */
			if(next_v->type == jbvBinary){

				array_it = JsonbIteratorInit((JsonbContainer *) next_v->val.binary.data);
				next_it = JsonbIteratorNext(&array_it, &array_value, true);

				if(next_it == WJB_BEGIN_ARRAY){
					/* elog(INFO, "We are in array"); */
					while ((next_it = JsonbIteratorNext(&array_it, &array_value, true)) != WJB_DONE){
						if(next_it == WJB_ELEM){
							if(fpGetNext(path_item, &next_item)) {
								num_results += reduce_fhirpath(&array_value, &next_item, acc, fn);
							} else {
								appendJsonbValue(&array_value, acc, fn);
								num_results += 1;
							}
						}
					}
				}
				else if(next_it == WJB_BEGIN_OBJECT){
					if(fpGetNext(path_item, &next_item)) {
						num_results += reduce_fhirpath(next_v, &next_item, acc, fn);
					} else {
						appendJsonbValue(&array_value, acc, fn);
						num_results += 1;

					}
				}
			} else {
				appendJsonbValue(next_v, acc, fn);
				num_results += 1;
			}

		}
		break;
	case fpValues:
		elog(INFO, "Not impl");
		break;
	default:
		elog(INFO, "TODO extract");
	}
	return num_results;
}



void
initJsonbValue(JsonbValue *jbv, Jsonb *jb) {
	jbv->type = jbvBinary;
	jbv->val.binary.data = &jb->root;
	jbv->val.binary.len = VARSIZE_ANY_EXHDR(jb);
}


void reduce_jsonb(void *buf, JsonbValue *val){
	JsonbInState *result = (JsonbInState *) buf;
	/* elog(INFO, "COLLECT: %s",jsonbv_to_string(NULL, val)); */
	result->res = pushJsonbValue(&result->parseState, WJB_ELEM, val);
}



PG_FUNCTION_INFO_V1(fhirpath_extract);
Datum
fhirpath_extract(PG_FUNCTION_ARGS)
{

	Jsonb       *jb = PG_GETARG_JSONB(0);
	Fhirpath	*fp_in = PG_GETARG_FHIRPATH(1);

	FhirpathItem	fp;
	fpInit(&fp, fp_in);

	/* init jsonbvalue from in disck */
	JsonbValue	jbv;
	initJsonbValue(&jbv, jb);

	JsonbInState result;
	memset(&result, 0, sizeof(JsonbInState));

	result.res = pushJsonbValue(&(result.parseState), WJB_BEGIN_ARRAY, NULL);

	long num_results = reduce_fhirpath(&jbv, &fp, &result, reduce_jsonb);

	result.res = pushJsonbValue(&(result.parseState), WJB_END_ARRAY, NULL);

	/* elog(INFO, "num results %d", num_results); */
	if( num_results > 0 ){
		PG_RETURN_POINTER(JsonbValueToJsonb(result.res));
	} else {
		PG_RETURN_NULL();
	}

}

static long
recursive_fhirpath_values(JsonbInState *result, JsonbValue *jbv)
{

	JsonbIterator *array_it;
	JsonbValue	array_value;
	int num_results = 0;
	int next_it;

	if(jbv->type == jbvBinary) {

		array_it = JsonbIteratorInit((JsonbContainer *) jbv->val.binary.data);
		next_it = JsonbIteratorNext(&array_it, &array_value, true);

		if(next_it == WJB_BEGIN_ARRAY || next_it == WJB_BEGIN_OBJECT){
			while ((next_it = JsonbIteratorNext(&array_it, &array_value, true)) != WJB_DONE){
				if(next_it == WJB_ELEM || next_it == WJB_VALUE){
					num_results += recursive_fhirpath_values(result, &array_value);
				}
			}
		}
	} else {
		result->res = pushJsonbValue(&result->parseState, WJB_ELEM, jbv);
		num_results += 1;
	}

	return num_results;
}


PG_FUNCTION_INFO_V1(fhirpath_values);
Datum
fhirpath_values(PG_FUNCTION_ARGS)
{

	Jsonb       *jb = PG_GETARG_JSONB(0);

	/* init jsonbvalue from in disck */
	JsonbValue	jbv;
	initJsonbValue(&jbv, jb);

	/* init accumulator */
	JsonbInState result;
	memset(&result, 0, sizeof(JsonbInState));
	result.res = pushJsonbValue(&result.parseState, WJB_BEGIN_ARRAY, NULL);

	long num_results = recursive_fhirpath_values(&result, &jbv);

	result.res = pushJsonbValue(&result.parseState, WJB_END_ARRAY, NULL);
	if(num_results > 0){
		PG_RETURN_POINTER(JsonbValueToJsonb(result.res));
	} else {
		PG_RETURN_NULL();
	}
}

void
appendJsonbValuePrimitives(StringInfoData *buf, JsonbValue *jbv, char *prefix, char *suffix, char *delim){
	if(jbv != NULL) {
		switch(jbv->type){
		case jbvBinary:
		case jbvArray:
		case jbvObject:
		{
			JsonbValue next;
			JsonbIterator *it = JsonbIteratorInit((JsonbContainer *) jbv->val.binary.data);
			JsonbIteratorToken r = JsonbIteratorNext(&it, &next, true);
			switch(r){
			case WJB_BEGIN_ARRAY:
				while ((r = JsonbIteratorNext(&it, &next, true)) != WJB_DONE) {
					appendJsonbValuePrimitives(buf, &next, prefix, suffix, delim);
				}
				break;
			case WJB_BEGIN_OBJECT:
				elog(INFO, "not impl");
				break;
			default:
				elog(INFO, "not impl");
			}

		}
		case jbvBool:
		case jbvNull:
		case jbvNumeric:
			/* TODO */
		break;
		break;
		case jbvString:
			appendStringInfoString(buf, prefix);
			appendBinaryStringInfo(buf, jbv->val.string.val, jbv->val.string.len);
			appendStringInfoString(buf, suffix);
			appendStringInfoString(buf, delim);
			break;
		}
	}
}

text *JsonbValueToText(JsonbValue *v){
	switch(v->type)
	{
    case jbvNull:
		return cstring_to_text(""); // better pg null?
		break;
    case jbvBool:
		return cstring_to_text(v->val.boolean ? "true" : "false");
		break;
    case jbvString:
		return cstring_to_text_with_len(v->val.string.val, v->val.string.len);
		break;
    case jbvNumeric:
		return cstring_to_text(DatumGetCString(DirectFunctionCall1(numeric_out,
																   PointerGetDatum(v->val.numeric))));
		break;
    case jbvBinary:
    case jbvArray:
    case jbvObject:
	{
        StringInfo  jtext = makeStringInfo();
        (void) JsonbToCString(jtext, v->val.binary.data, -1);
        return cstring_to_text_with_len(jtext->data, jtext->len);
	}
	break;
    default:
		elog(ERROR, "Wrong jsonb type: %d", v->type);
	}
	return NULL;
}



void reduce_jsonb_values(JsonbValue *jbv, void *acc, reduce_fn fn)
{

	JsonbIterator *array_it;
	JsonbValue	array_value;
	int next_it;

	if(jbv->type == jbvBinary) {
		array_it = JsonbIteratorInit((JsonbContainer *) jbv->val.binary.data);
		next_it = JsonbIteratorNext(&array_it, &array_value, true);

		if(next_it == WJB_BEGIN_ARRAY || next_it == WJB_BEGIN_OBJECT){
			while ((next_it = JsonbIteratorNext(&array_it, &array_value, true)) != WJB_DONE){
				if(next_it == WJB_ELEM || next_it == WJB_VALUE){
					reduce_jsonb_values(&array_value, acc, fn);
				}
			}
		}
	} else {
		fn(acc, jbv);
	}
}

typedef struct StringAccumulator {
	char	*element_type;
	StringInfoData *buf;
} StringAccumulator;


void reduce_as_string_values(void *acc, JsonbValue *val) {
	StringAccumulator *sacc = (StringAccumulator *) acc;
	jsonbv_to_string(sacc->buf, val);
	appendStringInfoString(sacc->buf, "$");
}

void reduce_as_string(void *acc, JsonbValue *val){
	reduce_jsonb_values(val, acc, reduce_as_string_values);
}

PG_FUNCTION_INFO_V1(fhirpath_as_string);
Datum
fhirpath_as_string(PG_FUNCTION_ARGS)
{
	Jsonb      *jb = PG_GETARG_JSONB(0);
	Fhirpath   *fp_in = PG_GETARG_FHIRPATH(1);
	char       *type = text_to_cstring(PG_GETARG_TEXT_P(2));


	if(jb == NULL){ PG_RETURN_NULL();}

	FhirpathItem	fp;
	fpInit(&fp, fp_in);

	JsonbValue	jbv;
	initJsonbValue(&jbv, jb);

	StringInfoData		buf;
	initStringInfo(&buf);
	appendStringInfoSpaces(&buf, VARHDRSZ);
	appendStringInfoString(&buf, "$");

	StringAccumulator acc;
	acc.element_type = type;
	acc.buf = &buf;

	long num_results = reduce_fhirpath(&jbv, &fp, &acc, reduce_as_string);

	if( num_results > 0 ){
		SET_VARSIZE(buf.data, buf.len);
		PG_RETURN_TEXT_P(buf.data);
	} else {
		PG_RETURN_NULL();
	}

}

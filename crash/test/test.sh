#!/bin/bash
set -e

source ./test.cfg

function mk_query() {
	local SEARCH_TYPE=$1
  local PTH=$2
  local	DATA_TYPE=$3
	local MODIFICATION=""

	if [ "$SEARCH_TYPE" == number ] || [ "$SEARCH_TYPE" == date ]
  then
    MODIFICATION=", 'max'"
  fi

	echo "DO language plpgsql \$\$ BEGIN RAISE info '$SEARCH_TYPE $PTH $DATA_TYPE'; END \$\$;"
  echo "SELECT fhirpath_as_$SEARCH_TYPE(:resource, '$PTH', '$DATA_TYPE' $MODIFICATION);"
}

function gen_pths() {
  local	data_type=$1
  local paths=()
  paths+=(".$data_type.value")
  paths+=(".$data_type.array")
  paths+=(".$data_type.where.where(code=value).value")
  paths+=(".$data_type.where.where(code=array).array")
  paths+=(".$data_type.where.where(code=where).where.where(code=value).value")
  paths+=(".$data_type.where.where(code=where).where.where(code=array).array")
	echo "${paths[@]}"
}

#SEARCH_TYPES=(number date)
SEARCH_TYPES=(number date)
for search_type in "${SEARCH_TYPES[@]}"
do
  eval DATA_TYPES=( \${$search_type[@]} ) ;
	for data_type in "${DATA_TYPES[@]}"
	do
		_value=$data_type"_value"
		_array=$data_type"_array"
		value=""
		array=""
		eval "value=\$$_value"
		eval "array=\$$_array"
		if [ -n "$value" ]; then
			RESOURCE="$(printf  "$TEMPLATE" "$data_type" "$value" "$array" "$value" "$array" "$value" "$array")"
			echo "select '''$RESOURCE''' resource \\gset"
			paths=($(gen_pths $data_type))
			for path in "${paths[@]}"
			do
				mk_query $search_type $path $data_type
			done
		fi
	done
done



#!/bin/sh
tag=$(mmsg get focusing-client | jq -r '.tags[0]')
n=$(mmsg get all-clients | jq --argjson t "$tag" '[.clients[] | select(.is_group and (.tags | index($t)))] | length')

if [ "$n" -gt 0 ]; then
  for i in $(seq 1 "$n"); do mmsg dispatch groupleave >/dev/null; done
else
  for i in 1 2 3 4 5 6 7 8; do mmsg dispatch focusdir,left >/dev/null; done
  for i in 1 2 3 4 5 6 7 8; do mmsg dispatch groupjoin,right >/dev/null; done
fi

#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# synthis-aa4.90. usage: run-all.sh <bundle.wclap> <tag>; each mode runs in 2 processes (needs /tmp)
B=$1; T=$2
for prime in 1 0; do for mode in fresh snapshot warm; do for n in 1 2; do
  PRIME=$prime node "$(dirname "$0")/six-verify.mjs" "$B" $mode > /tmp/six-$T-$mode-$prime-$n.json 2>/tmp/six-$T-err.txt || { echo "FAIL $mode prime=$prime proc$n"; tail -3 /tmp/six-$T-err.txt; }
done
python3 - "$T" $mode $prime <<'PY'
import json,sys
t,m,pr=sys.argv[1:4]
try:
  a,b=[json.load(open(f"/tmp/six-{t}-{m}-{pr}-{n}.json")) for n in (1,2)]
  print(f"{t} prime={pr} {m}: params={a['paramCount']} peak={a['peak']:.4f} 5xIdentical p1={a['identical5']} p2={b['identical5']} crossProc={a['fps5']==b['fps5']} fp={a['fps5'][0]} stateBytes={a['stateBytes']} roundTripOk={a['roundTripAllOk']}")
except Exception as e: print(t,m,pr,'ERR',e)
PY
done; done

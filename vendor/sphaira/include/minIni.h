// SHIM OpenHomeNX (non la minIni originale): keys.cpp chiama ini_browse()
// solo con read_from_file=true; noi passiamo sempre false, quindi lo stub
// ritorna 0 e non legge mai /switch/prod.keys. Firma identica all'originale.
#pragma once

typedef char mTCHAR;

#ifdef __cplusplus
extern "C" {
#endif

int ini_browse(int (*cb)(const mTCHAR* Section, const mTCHAR* Key, const mTCHAR* Value, void* UserData),
               void* UserData, const mTCHAR* Filename);

#ifdef __cplusplus
}
#endif

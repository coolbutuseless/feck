
// #define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>


extern SEXP chiblihash64_   (SEXP raw_vec_, SEXP skip_, SEXP len_);
extern SEXP fec_prepare_raw_(SEXP raw_vec_, SEXP k_, SEXP m_, SEXP verbosity_);
extern SEXP fec_repair_raw_ (SEXP raw_vec_, SEXP blocks_, SEXP verbosity_);

static const R_CallMethodDef CEntries[] = {
  
  {"chiblihash64_"   , (DL_FUNC) &chiblihash64_   , 3},
  {"fec_prepare_raw_", (DL_FUNC) &fec_prepare_raw_, 4},
  {"fec_repair_raw_" , (DL_FUNC) &fec_repair_raw_ , 3},
  {NULL , NULL, 0}
};


void R_init_feck(DllInfo *info) {
  R_registerRoutines(
    info,      // DllInfo
    NULL,      // .C
    CEntries,  // .Call
    NULL,      // Fortran
    NULL       // External
  );
  R_useDynamicSymbols(info, FALSE);
}




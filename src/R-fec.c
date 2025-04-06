
#define R_NO_REMAP

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <inttypes.h>

#include <R.h>
#include <Rinternals.h>
#include <Rdefines.h>

#include "chibihash.h"
#include "fec.h"


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Chibli Hash of the this raw data
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
SEXP chiblihash64_(SEXP raw_vec_, SEXP skip_, SEXP len_) {
  
  uint64_t res = chibihash64(RAW(raw_vec_) + Rf_asInteger(skip_), Rf_asInteger(len_), 0xdeadbeef);
  
  char buf[16 + 1];
  snprintf(buf, sizeof(buf), "%" PRIx64, res);
  
  return Rf_mkString(buf);
}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Prepare repair blocks for a raw vector]
// In general, reconstruction requires:
// 1. the value of K, 
// 2. the value of M, 
// 3. the sharenum of each block, 
// 4. the number of bytes of padding that were used. (so we can truncate
//    back to the correct size of the original data)
//
// 
// It is important to do things in order: 
//   first archive, 
//   then compress, 
//   then either encrypt or integrity-check, 
//   then erasure code
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
SEXP fec_prepare_raw_(SEXP raw_vec_, SEXP k_, SEXP m_, SEXP verbosity_) {
  
  int nprotect = 0;
  unsigned char *raw = RAW(raw_vec_);
  int verbosity = Rf_asInteger(verbosity_);
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // k the number of blocks required to reconstruct
  // m the total number of blocks created. There will be (m - k) recovery blocks
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  int k = Rf_asInteger(k_);
  int m = Rf_asInteger(m_);
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Sanity check
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (k >= m) 
    Rf_error("fec_prepare_raw_() k >= m not allowed.  %i > %i", k, m);
  if (m < 1 || m > 255) 
    Rf_error("fec_prepare_raw_() must be in range [1, 255]. Got %i", m);
  if (k < 2)
    Rf_error("fec_prepare_raw_() k must be >= 2");
  
  int len = Rf_length(raw_vec_);
  int chunksize = (int)ceil((double)len / (double)k);
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // allocate space to keep a copy of the last chunk which includes
  // padding.
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  int padding = k * chunksize - len;
  uint8_t *last_chunk = calloc((size_t)chunksize, 1);
  if (last_chunk == NULL) {
    Rf_error("fec_prepare_raw_(): Couldn't allocate 'last_chunk'");
  }
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Copy as much as possible of the data into the last chunk
  // Uncopied bytes will be ZERO (as calloc() was used to initialise 'last_chunk')
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (verbosity > 0) {
    Rprintf("Len  : %i\n", len);
    Rprintf("k    : %i\n", k);
    Rprintf("Chunk: %i\n", chunksize);
    Rprintf("Pad  : %i\n", padding);
  }
  memcpy(last_chunk, raw + (k - 1) * chunksize, chunksize - padding);
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Currently require that data length is an exact multiple of chunksize
  // Eventually relax this criteria
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // if (len % k != 0) Rf_error("fec_prepare_raw_(): len not a multiple of k: %i / %i", len, k);
  
  if (verbosity > 0) {
    Rprintf("Primary blocks: k = %i\n", k);
    Rprintf("Repair  blocks: m - k = %i\n", m - k);
    Rprintf("Chunksize: %i\n", chunksize);
  }
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Data to return as the 'fec_blocks' structure
  //  - [4] magic:  FECK
  //  - [1] version: 01
  //  - [1] unused
  //  - [1] k
  //  - [1] m
  //  - [4] original data len 
  //  - [4] chunksize
  //  - [8 * m] chiblihash64 for each of the 'm' chunks
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  int header_size = 
    4 +    // magic: FECK
    4 +    // version + unused + k + m
    4 +    // orig data len
    4 +    // chunksize
    8 * m; // hashes
  
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Memory where the calculated recovery blocks will go
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  int n_repair_blocks = m - k;
  int repair_block_size = n_repair_blocks * chunksize;
  SEXP res_ = PROTECT(Rf_allocVector(RAWSXP, header_size + repair_block_size)); nprotect++;
  uint8_t *repair_blocks = RAW(res_) + header_size;

  uint8_t *res   = RAW(res_);
  memset(res, 0, header_size + repair_block_size);
  uint32_t *res4 = (uint32_t *)RAW(res_);
  uint64_t *res8 = (uint64_t *)RAW(res_) + 2;
  res[0] = 'F';
  res[1] = 'E';
  res[2] = 'C';
  res[3] = 'K';
  res[4] = 1; // version
  res[5] = 0; // unused
  res[6] = (uint8_t)k;
  res[7] = (uint8_t)m;
  res4[2] = (uint32_t)Rf_length(raw_vec_);
  res4[3] = (uint32_t)chunksize;
  
  for (int i = 0; i < k - 1; i++) {
    res8[i] = chibihash64(raw + i * chunksize, chunksize, 0xdeadbeef);
  }  
  res8[k - 1] = chibihash64(last_chunk, chunksize, 0xdeadbeef);
  
  for (int i = k; i < m; i++) {
    res8[i] = 0x1122334455667788;
  }
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Initialize the fec library.
  // Call this:
  // - at least once
  // - from at most one thread at a time
  // - before calling any other APIs from the library
  // - before creating any other threads that will use APIs from the library
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  fec_init();
  fec_t* fec = fec_new((unsigned short)k, (unsigned short)m);
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // @param inpkts the "primary blocks" i.e. the chunks of the input data
  // @param fecs buffers into which the secondary blocks will be written
  // @param block_nums the numbers of the desired check blocks (the id >= k) which fec_encode() will produce and store into the buffers of the fecs parameter
  // @param num_block_nums the length of the block_nums array
  // @param sz size of a packet in bytes
  // void fec_encode(
  //     const fec_t* code,
  //     const gf*restrict const*restrict const src,
  //     gf*restrict const*restrict const fecs,
  //     const unsigned*restrict const block_nums,
  //     size_t num_block_nums,
  //     size_t sz
  // );
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  
  const gf** p_primary_blocks = malloc((size_t)k * sizeof(const gf*));
  if (p_primary_blocks == NULL) Rf_error("malloc() failed - p_primary_blocks");
  for (int i = 0; i < k - 1; i++) {
    p_primary_blocks[i] = raw + (i * chunksize);
  }
  p_primary_blocks[k - 1] = last_chunk;
  
  gf** p_repair_blocks = malloc((size_t)n_repair_blocks * sizeof(const gf*));
  if (p_repair_blocks == NULL) Rf_error("malloc() failed - p_repair_blocks");
  for (int i = 0; i < n_repair_blocks; i++) {
    p_repair_blocks[i] = repair_blocks + (i * chunksize);
  }
  
  unsigned int *repair_block_idxs = malloc((size_t)n_repair_blocks * sizeof(unsigned int));
  for (int i = 0; i < n_repair_blocks; i++) {
    repair_block_idxs[i] = (unsigned int)(k + i);
  }
  
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Calculate the repair blocks
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  fec_encode(fec, p_primary_blocks, p_repair_blocks, repair_block_idxs, 
             (size_t)n_repair_blocks, (size_t)chunksize);

  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Capture hashes of repair blocks
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  for (int i = 0; i < n_repair_blocks; i++) {
    uint64_t this_hash = chibihash64(repair_blocks + i * chunksize, chunksize, 0xdeadbeef);
    res8[k + i] = this_hash;
  }
  
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Tidy and return
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  fec_free(fec);
  free(repair_block_idxs);
  free(p_repair_blocks);
  free(p_primary_blocks);
  free(last_chunk);
  
  UNPROTECT(nprotect);
  return res_;
}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Repair a raw vector using the given repair blocks
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
SEXP fec_repair_raw_(SEXP raw_vec_, SEXP blocks_, SEXP verbosity_) {
  int nprotect = 0;
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  //  Unpack header
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  uint8_t *raw = RAW(raw_vec_);
  int verbosity = Rf_asInteger(verbosity_);
  
  uint8_t  *header  = (uint8_t  *)RAW(blocks_);
  uint32_t *header4 = (uint32_t *)RAW(blocks_);
  uint64_t *header8 = (uint64_t *)RAW(blocks_) + 2;
  
  bool magic_ok = 
    header[0] == 'F' &&
    header[1] == 'E' &&
    header[2] == 'C' &&
    header[3] == 'K';
  if (!magic_ok) {
    Rf_error("fec_repair_raw_(): Couldn't find magic bytes 'FECK'");
  }
  int version   = header[4];
  int k         = header[6];
  int m         = header[7];
  int len_orig  = (int)header4[2];
  int chunksize = (int)header4[3];
  if (verbosity > 0) {
    Rprintf("k/m: %i/%i.  Chunksize: %i. OrigLen: %i\n", k, m, chunksize, len_orig);
  }
  int n_repair_blocks = m - k;
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Sanity check header
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (version != 1) Rf_error("fec_repair_raw_(): Version %i not supported", version);
  if (k >= m) Rf_error("fec_repair_raw_(): k (%i) must be less than m (%i)", k, m);
  if (Rf_length(raw_vec_) != len_orig) {
    Rf_error("fec_repair_raw_(): Expecting data length %i, but given %i", len_orig, Rf_length(raw_vec_));
  }
  
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Last chunk handling
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  uint8_t *last_chunk = calloc((size_t)chunksize, 1);
  if (last_chunk == NULL) Rf_error("Couldn't allocate last_chunk");
  int last_chunk_len = len_orig % chunksize;
  if (last_chunk_len == 0) last_chunk_len = chunksize;
  if (verbosity > 0) {
    Rprintf("last_chunk: %i / %i\n", last_chunk_len, chunksize);
  }
  memcpy(last_chunk, raw + (k - 1) * chunksize, last_chunk_len);
  

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Keep track of which block are good
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  bool *good_primary = malloc((size_t)k * sizeof(bool));
  for (int i = 0; i < k; i++) {
    good_primary[i] = false;
  }
  int n_good_primary = 0;
  
  bool *good_repair = malloc((size_t)n_repair_blocks * sizeof(bool));
  for (int i = 0; i < n_repair_blocks; i++) {
    good_repair[i] = false;
  }
  int n_good_repair = 0;
  
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Check current blocks
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (verbosity > 0) Rprintf("Primary blocks:\n");
  for (int i = 0; i < k; i++) {
    uint64_t ref_hash = header8[i];
    uint64_t this_hash;
    if (i == (k - 1)) {
      this_hash = chibihash64(last_chunk, chunksize, 0xdeadbeef);
    } else {
      this_hash = chibihash64(raw + i * chunksize, chunksize, 0xdeadbeef);
    }
    if (verbosity > 0) {
      Rprintf("[%2i] %" PRIx64 " : %" PRIx64 "\n", i, ref_hash, this_hash);
    }
    good_primary[i] = ref_hash == this_hash;
    n_good_primary += good_primary[i];
  }
  
  int n_bad_primary = k - n_good_primary;
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Check repair blocks are OK
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  uint8_t *repair_blocks = header + 
    16 +   // magic + version/m/k + len + chunksize 
    m * 8; // hashes
  if (verbosity > 0) Rprintf("Repair blocks:\n");
  for (int i = 0; i < n_repair_blocks; i++) {
    uint64_t ref_hash = header8[k + i];
    uint64_t this_hash = chibihash64( repair_blocks + i * chunksize, chunksize, 0xdeadbeef);
    if (verbosity > 0) {
      Rprintf("[%2i] %" PRIx64 " : %" PRIx64 "\n", i, ref_hash, this_hash);
    }
    good_repair[i] = ref_hash == this_hash;
    n_good_repair += good_repair[i];
  }
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Repair needed?
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (n_bad_primary == 0) {
    if (verbosity > 0) {
      Rprintf("fec_repair_raw_(): No bad blocks! All good\n");
    }
    free(last_chunk);
    free(good_primary);
    free(good_repair);
    UNPROTECT(nprotect);
    return raw_vec_;
  }
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Repair is needed, but impossible
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (n_bad_primary > n_good_repair) {
    free(last_chunk);
    free(good_primary);
    free(good_repair);
    Rf_error("fec_repair_raw_() Unrepairable.  %i bad blocks. Only %i repair blocks", 
             n_bad_primary, n_good_repair);
  }
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Repair is possible
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (verbosity > 0) {
    Rprintf("Repair possible!  %i bad blocks.  %i repair blocks\n", n_bad_primary, n_good_repair);
  }
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Prepare memory.
  //   * copy good primary blocks into working area
  //   * copy good repair blocks into working area
  //   * copy work_area into 'res'
  //   * track the index of blocks copied
  //   * Run the repair
  //        only blocks needing repair are output to 'res'
  //   * Copy 'res' into the raw vector result to return to R
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  uint8_t *work_area = calloc((unsigned long)(k * chunksize), 1);
  if (work_area == NULL) Rf_error("Couldn't allocate work area");
  
  uint8_t *res = malloc((unsigned long)(k * chunksize));
  if (res == NULL) Rf_error("Couldn't allocate result");
  
  unsigned int *block_idxs = calloc((unsigned long)k, sizeof(unsigned int));
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Prepare work area
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  int repair_idx = 0;
  for (int i = 0; i < k; i++) {
    if (good_primary[i]) {
      if (i == k - 1) {
        memcpy(work_area + i * chunksize, last_chunk, chunksize);
      } else {
        memcpy(work_area + i * chunksize, raw + i * chunksize, chunksize);
      }
      block_idxs[i] = (unsigned int)i; 
    } else {
      while(!good_repair[repair_idx]) {
        repair_idx++;
      }
      memcpy(work_area + i * chunksize, repair_blocks + i * chunksize, chunksize);
      block_idxs[i] = (unsigned int)(k + repair_idx);
      repair_idx++;
    }
  }
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Copy work area into result
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  memcpy(res, work_area, k * chunksize);
  
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Prepare pointers
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  const gf** input = malloc((size_t)k * sizeof(const gf*));
  gf** output      = malloc((size_t)k * sizeof(const gf*));
  
  for (int i = 0; i < k; i++) {
    input[i]  = work_area + i * chunksize;
    output[i] = res       + i * chunksize;
  }
  
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Repair
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  fec_init();
  fec_t* fec = fec_new((unsigned short)k, (unsigned short)m);
  
  fec_decode(fec, input, output, block_idxs, (size_t)chunksize);
  
  fec_free(fec);
  free(output);
  free(input);
  free(work_area);
  free(block_idxs);
  
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Copy result into R vector
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  SEXP res_ = PROTECT(Rf_allocVector(RAWSXP, len_orig)); nprotect++;
  memcpy(RAW(res_), res, len_orig);
  free(res);

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Tidy and return
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  free(last_chunk);
  free(good_primary);
  free(good_repair);
  UNPROTECT(nprotect);
  return res_;
}







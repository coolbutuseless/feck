

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#' Print repair blocks
#' 
#' @param x Repair blocks created by \code{\link{fec_prepare_raw}()}
#' @param ... ignored
#' @return None
#' @examples
#' rblocks <- fec_prepare_raw(as.raw(1:1000))
#' print(rblocks)
#' @export
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
print.fec_blocks <- function(x, ...) {
  
  cat("<fec repair> k/m xx% repair blocks\n")
  invisible(x)
}



#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#' Calculate the 64-bit chibli hash for the given data
#' 
#' @param raw_vec raw vector
#' @param skip number of bytes to skip at start
#' @param len Number of bytes to hash
#' @return String containing the chibli hash 
#' @examples
#' zz <- as.raw(1:100)
#' chiblihash64(zz)
#' chiblihash64(zz)
#' zz[1] <- as.raw(199)
#' chiblihash64(zz)
#' @export
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
chiblihash64 <- function(raw_vec, skip = 0, len = length(raw_vec)) {
  stopifnot(skip >= 0)
  if (skip + len > length(raw_vec)) {
    stop("chiblihash64(): Can't hash past end of data")
  }
  .Call(chiblihash64_, raw_vec, skip, len)
}



#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#' Create recovery blocks 
#' 
#' @param raw_vec raw vector
#' @param k number of chunks to split data into. Larger 'k' will mean the file will
#'        be chunked into more, smaller blocks.  This will help isolate errors,
#'        but can increase computation time.
#' @param n number of repair chunks to create.  Each repair chunk will be 
#'        able to repair one bad chunk in the data.
#' @param verbosity Default: 0
#' @return Raw vector of meta-data and recovery blocks
#' @examples
#' set.seed(1)
#' dat <- as.raw(sample(0:255, 1e3, replace = TRUE))
#' rblocks <- fec_prepare_raw(dat, verbosity = 1)
#' @export
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
fec_prepare_raw <- function(raw_vec, k = 10, n = 2, verbosity = 0L) {
  res <- .Call(fec_prepare_raw_, raw_vec, k, m = k + n, verbosity);
  class(res) <- "fec_blocks"
  res
}



#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#' Recover damaged data in a raw vector
#' 
#' @inheritParams fec_prepare_raw
#' @param blocks raw vector of recovery blocks as created by \code{\link{fec_prepare_raw}()}
#' @return Repaired data, or NULL if repair not possible
#' @examples
#' set.seed(1)
#' dat0 <- dat <- as.raw(sample(0:255, 1e4, replace = TRUE))
#' rblocks <- fec_prepare_raw(dat, verbosity = 1)
#' # simulate damange to data
#' dat[1:100] <- as.raw(0)
#' identical(dat, dat0)
#' dat_repaired <- fec_repair_raw(dat, rblocks, verbosity = 1)
#' identical(dat_repaired, dat0)
#' @export
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
fec_repair_raw <- function(raw_vec, blocks, verbosity = 0L) {
  .Call(fec_repair_raw_, raw_vec, blocks, verbosity)
}



#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#' Create blocks for a file
#' 
#' @inheritParams fec_prepare_raw
#' @param filename filename
#' @param blocks_filename Default: NULL, suffix with ".feck"
#' @return Invisibly return the raw vector of repair blocks
#' @export
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
fec_prepare_file <- function(filename, blocks_filename = NULL, k = 10, n = 2, verbosity = 0L) {
  
  raw_vec <- readBin(filename, raw(), n = file.size(filename))
  blocks  <- fec_prepare_raw(raw_vec, k = k, n = n, verbosity = verbosity)
  if (is.null(blocks_filename)) {
    blocks_filename <- paste0(filename, ".feck")
  }  
  
  attributes(blocks) <- NULL
  writeBin(blocks, blocks_filename)

  invisible(blocks)
}



#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#' Repair file
#' 
#' @inheritParams fec_prepare_raw
#' @param filename filename. Repaired file will have suffix ".repaired"
#' @param blocks_filename Default: NULL, suffix with ".feck"
#' @return Invisibly return a raw vector of the contents of the repaired file.
#'         If repair is not possible, an error will be raised.
#' @export
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
fec_repair_file <- function(filename, blocks_filename = NULL, verbosity = 0L) {

  if (is.null(blocks_filename)) {
    blocks_filename <- paste0(filename, ".feck")
  }  
  
  raw_vec <- readBin(filename, raw(), n = file.size(filename))
  blocks  <- readBin(blocks_filename, raw(), n = file.size(blocks_filename))
  
  repaired <- fec_repair_raw(raw_vec, blocks = blocks, verbosity = verbosity)
  repaired_filename <- paste0(filename, ".repaired")
  writeBin(repaired, repaired_filename)
  
  invisible(repaired)
}



if (FALSE) {
  
  
  tmpfile <- "working/mtcars.rds"
  saveRDS(mtcars, tmpfile)
  restore1 <- readRDS(tmpfile)
  identical(mtcars, restore1)
  
  # Create repair blocks
  fec_prepare_file(tmpfile, k = 20, n = 5)
  
  # Corrupt the RDS file
  x <- readBin(tmpfile, raw(), n = file.size(tmpfile))
  x[1:20] <- as.raw(0)
  writeBin(x, tmpfile)
  
  restore2 <- readRDS(tmpfile)
  identical(mtcars, restore2)

  
  fec_repair_file(tmpfile)
  restore3 <- readRDS(paste0(tmpfile, ".repaired"))
  identical(mtcars, restore3)
    
  
}










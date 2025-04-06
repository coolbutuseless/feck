

test_that("file repair works", {
  
  tmpfile <- tempfile()
  saveRDS(mtcars, tmpfile)
  restore1 <- readRDS(tmpfile)
  expect_identical(mtcars, restore1)
  
  # Create repair blocks
  fec_prepare_file(tmpfile, k = 20, n = 5)
  
  # Corrupt the RDS file
  x <- readBin(tmpfile, raw(), n = file.size(tmpfile))
  x[1:20] <- as.raw(0)
  writeBin(x, tmpfile)
  
  expect_error(
    readRDS(tmpfile)
  )

  fec_repair_file(tmpfile)
  restore3 <- readRDS(paste0(tmpfile, ".repaired"))
  expect_identical(mtcars, restore3)
  
  
})


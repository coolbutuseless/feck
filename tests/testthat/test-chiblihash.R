


test_that("chiblihash works", {
  
  
  zz <- as.raw(1:100)
  ref_hash <- "6c1af424b1a37c7d"
  
  expect_identical(
    chiblihash64(zz),
    ref_hash
  )
  
  expect_identical(
    chiblihash64(zz),
    ref_hash
  )

  # damage the data  
  zz[1] <- as.raw(199)
  expect_true(
    chiblihash64(zz) != ref_hash
  )
})



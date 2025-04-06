

test_that("fec raw works with length a multiple of k", {
  
  # Create test data
  set.seed(1)
  dat0 <- dat <- as.raw(sample(0:255, 1e4, replace = TRUE))
  
  # Create some repair blocks
  rblocks <- fec_prepare_raw(dat, verbosity = 0)
  
  expect_error(fec_repair_raw(dat, NULL))
  expect_error(fec_repair_raw(dat))
  expect_error(fec_repair_raw(dat, c(1, 2, 3)))
  expect_error(fec_repair_raw(dat, as.raw(1:3)))

  # Repair without any damage works
  dat_repaired <- fec_repair_raw(dat, rblocks, verbosity = 0)
  expect_identical(dat_repaired, dat0)
  
    
  # simulate damagge to data
  dat[1] <- as.raw(199)
  expect_false(identical(dat, dat0))
  
  dat_repaired <- fec_repair_raw(dat, rblocks, verbosity = 0)
  expect_identical(dat_repaired, dat0)

  
  # simulate TOO much damange to data. unrepairable
  set.seed(2)
  dat <- as.raw(sample(0:255, 1e4, replace = TRUE))
  expect_false(identical(dat, dat0))
  
  expect_error(
    fec_repair_raw(dat, rblocks, verbosity = 0),
    "Unrepairable"
  )

})


test_that("fec raw works with length NOT multiple of k", {
  
  # Create test data
  set.seed(1)
  dat0 <- dat <- as.raw(sample(0:255, 1e4, replace = TRUE))
  
  # Create some repair blocks
  rblocks <- fec_prepare_raw(dat, k = 11, n = 2,verbosity = 0)
  
  # simulate damagge to data
  dat[1] <- as.raw(199)
  expect_false(identical(dat, dat0))
  
  dat_repaired <- fec_repair_raw(dat, rblocks, verbosity = 0)
  expect_identical(dat_repaired, dat0)
  
})

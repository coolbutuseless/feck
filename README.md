
<!-- README.md is generated from README.Rmd. Please edit that file -->

# feck

<!-- badges: start -->

![](https://img.shields.io/badge/cool-useless-green.svg)
[![CRAN](https://www.r-pkg.org/badges/version/feck)](https://CRAN.R-project.org/package=feck)
[![R-CMD-check](https://github.com/coolbutuseless/feck/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/coolbutuseless/feck/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

`{feck}` is an R package for generating forward error correction codes -
think [RAID5](https://en.wikipedia.org/wiki/Standard_RAID_levels) or
[par2](https://en.wikipedia.org/wiki/Parchive) but for raw vectors.

By pre-calculating a set of *rescue blocks*, if the data is later
corrupted then these blocks can be used to repair the file.

The number of repair blocks generated is configurable depending upon
user needs.

## Installation

<!-- This package can be installed from CRAN -->

<!-- ``` r -->

<!-- install.packages('feck') -->

<!-- ``` -->

You can install the latest development version from
[GitHub](https://github.com/coolbutuseless/feck) with:

``` r
# install.package('remotes')
remotes::install_github('coolbutuseless/feck')
```

<!-- Pre-built source/binary versions can also be installed from -->

<!-- [R-universe](https://r-universe.dev) -->

<!-- ``` r -->

<!-- install.packages('feck', repos = c('https://coolbutuseless.r-universe.dev', 'https://cloud.r-project.org')) -->

<!-- ``` -->

## Example: Forward Error Correction of a raw vector

``` r
library(feck)

# Generate some data
set.seed(1)
dat0 <- dat <- as.raw(sample(0:255, 1e4, replace = TRUE))
length(dat)
```

    #> [1] 10000

``` r
# Partitition the data into 10 chunks, and create 2 repair blocks.
# If errors occur in up to 2 of the chunks of data, then the data can 
# be repaired using these repair blocks
rblocks <- fec_prepare_raw(dat, k = 10, n = 2)
length(rblocks)
```

    #> [1] 2112

``` r
# simulate damage to data
dat[1:100] <- as.raw(0)
identical(dat, dat0)
```

    #> [1] FALSE

``` r
# repair the data using the repair blocks
dat_repaired <- fec_repair_raw(dat, rblocks)
identical(dat_repaired, dat0)
```

    #> [1] TRUE

## Example: Forward Error Correction of a file

``` r
library(feck)

# Same some data to a file
tmpfile <- tempfile()
saveRDS(mtcars, tmpfile)

# When reloaded it should be identical to what was saved
restore1 <- readRDS(tmpfile)
identical(mtcars, restore1)
```

    #> [1] TRUE

``` r
# Create repair blocks. Consider the file split into 20 chunks,
# and create 5 repair blocks to repair errors in up to 5 chunks in the file
# The file is saved as the same as the original filename with a ".feck" suffix
fec_prepare_file(tmpfile, k = 20, n = 5)

# Corrupt the RDS file. Read it in, overwrite some bytes and save it back out
x <- readBin(tmpfile, raw(), n = file.size(tmpfile))
x[1:20] <- as.raw(0)
writeBin(x, tmpfile)

# Data is now unreadable!!
readRDS(tmpfile)
```

    #> Error in readRDS(tmpfile): unknown input format

``` r
# Repair the file and save it with a ".repaired" suffix
fec_repair_file(tmpfile)

# Repaired data is now the same as the original
restore2 <- readRDS(paste0(tmpfile, ".repaired"))
identical(mtcars, restore2)
```

    #> [1] TRUE

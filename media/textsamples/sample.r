# sample.r — R syntax-highlighting sample for the UltraCanvas demo.
# Summarise a small data frame of measurements per group.

measurements <- data.frame(
  group = factor(c("a", "b", "a", "c", "b", "c", "a")),
  value = c(2.5, 3.75, 1.25, NA, 4.0, 6.5, 3.0)
)

describe <- function(x, digits = 2) {
  x <- x[!is.na(x)]
  if (length(x) == 0) return(NULL)
  list(n = length(x), mean = round(mean(x), digits), sd = round(sd(x), digits))
}

summaries <- lapply(split(measurements$value, measurements$group), describe)

for (name in names(summaries)) {
  s <- summaries[[name]]
  if (is.null(s)) next
  cat(sprintf("%s: n=%d mean=%.2f sd=%.2f\n", name, s$n, s$mean, s$sd))
}

above <- subset(measurements, value > 3 & !is.na(value))
print(head(above[order(-above$value), ], 3))

squares <- sapply(1:5, function(i) i^2)
stopifnot(sum(squares) == 55L, all(squares %in% c(1, 4, 9, 16, 25)))

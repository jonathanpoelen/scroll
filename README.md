# Scroll

Limits the number of lines written by a program giving an automatic scroll effect.

(The text `^[[11;1R` is a terminal capture tool issue, it is not visible on normal use)

![scroll sample](./demo.gif "scroll sample")

# Compilation

```sh
g++ -std=c++17 -O3 -DNDEBUG -s scroll.cpp -o scroll
```
<!-- g++ -std=c++17 -O3 -DNDEBUG -s -fuse-ld=lld scroll.cpp -o scroll -->

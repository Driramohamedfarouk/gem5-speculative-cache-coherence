# Speculative Cache Coherence
This repository implements Speculative Cache Coherence in gem5 on top of the standard MSI protocol.
Speculative Cache Coherence consists of speculatively accessing invalidated cache lines before waiting
for a response from the Directory. The main goal of this project is to assess the benefit of speculation on
avoiding false sharing penalty.

## building gem5
`Please see the dependecies required by gem5 on gem5 documentation.`


### Speculative Cache Coherence
```bash
 scons build/X86_SCC/gem5.opt  PROTOCOL=MSI SLICC_HTML=True
```

### Baseline MSI
Since gem5 can only build with one cache coherence protocol at a time. The baseline should be built
separate in another directory.
```bash
git checkout af72b9ba580546ac12ce05bfaac3fd53fa8699f4
scons build/X86/gem5.opt  PROTOCOL=MSI SLICC_HTML=True
```

## building test programs
the Makefile assumes that you're running on an x86 machine. If not, please find an x86 machine and run on.
If this is not possible, install the cross compiling toolchain.

```shell
make
```

### Testing
Inside the source dir, type make tests. This will generate a single binary named scc_test; Run:
```shell
 build/X86_SCC/gem5.opt  configs/learning_gem5/part3/simple_ruby.py ./scc_test
```

The test files mainly contains 3 stress tests that run a loop for some number of iterations 100K an verify and invariant
on that loop.
Simulating the gem4 O3CPU takes much longer time than TimingSimpleCPU, therefore 100K iterations are fine.
Tests usually takes few minutes.

Test 1 — Read‑Only Unpadded: Hot field + read‑only field share a cache line. Detects speculative corruption or false sharing.

Test 2 — Misspeculation Stress Test: Writer/reader hammer a single field to stress abort/retry behavior and catch stale commits.

Test 3 — Branch on Speculative Load: Reader branches on a loaded flag; stale values trigger an “impossible” branch.

You should see `ALL TESTS PASSED`

### Debug traces
To see collected traces, run with `--debug-flags=RubyCache,Commit` for RubyCache and Commit logic traces respectively.
Make sure to pipe through `grep SCC` to see only Speculative Cache Coherence traces.

### Microbechmark

I contended with a single microbenchmark to see results `ro_bench.c`.

```shell
build/X86_SCC/gem5.opt  configs/learning_gem5/part3/simple_ruby.py ./ro_bench_unpadded
```

### Statistics

I added some custom statistics, they are outputted on the stats.txt file and starting with prefix `scc`
like `system.cpu0.commit.sccSpecCorrect`.

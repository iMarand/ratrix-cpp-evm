# libsecp256k1 (vendored)

Source: https://github.com/bitcoin-core/secp256k1 release v0.5.1
Tarball sha256: 081f4730becba2715a6b0fd198fedd9e649a6caaa6a7d6d3cf0f9fa7483f2cf1
Used for: ECDSA transaction signing (EVM recoverable sigs + BTC DER sigs).
Built via add_subdirectory with only the recovery module enabled; tests/benchmarks off.
Removed from the vendored copy: benchmarks, tests, ctime tests, precompute tools.
License: MIT (see COPYING).

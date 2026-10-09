#ifndef RTX_NET_ENDPOINTS_H
#define RTX_NET_ENDPOINTS_H

// Free public RPC endpoints (no API key), one list per network, shared by the
// balance lookups and sending. Callers try them in order and move on to the
// next one whenever an endpoint fails or answers with an error.
//
// Checked live on 2026-10-09: Ankr now requires an API key on every chain, and
// drpc's free plan no longer serves Tron or Solana, so those are left out.

#include <string>
#include <vector>

namespace rtxnet::endpoints {

inline const std::vector<std::string>& eth() {
    static const std::vector<std::string> v = {"https://ethereum-rpc.publicnode.com", "https://eth.drpc.org",
                                               "https://1rpc.io/eth", "https://rpc.mevblocker.io"};
    return v;
}

inline const std::vector<std::string>& bsc() {
    static const std::vector<std::string> v = {"https://bsc-dataseed.bnbchain.org", "https://bsc-rpc.publicnode.com",
                                               "https://bsc-dataseed1.binance.org", "https://1rpc.io/bnb",
                                               "https://bsc.drpc.org"};
    return v;
}

// Tron's Ethereum-compatible JSON-RPC: eth_getBalance / eth_call take the
// 20-byte hex form of a Tron address (see tron::toEvmHex).
inline const std::vector<std::string>& tron() {
    static const std::vector<std::string> v = {"https://api.trongrid.io/jsonrpc",
                                               "https://tron-rpc.publicnode.com/jsonrpc"};
    return v;
}

// The same nodes' native HTTP API (/wallet/...), used for sending.
inline const std::vector<std::string>& tronHttp() {
    static const std::vector<std::string> v = {"https://api.trongrid.io", "https://tron-rpc.publicnode.com"};
    return v;
}

inline const std::vector<std::string>& solana() {
    static const std::vector<std::string> v = {"https://solana-rpc.publicnode.com",
                                               "https://api.mainnet-beta.solana.com"};
    return v;
}

}  // namespace rtxnet::endpoints

#endif  // RTX_NET_ENDPOINTS_H

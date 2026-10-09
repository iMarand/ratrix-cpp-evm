#pragma once

// Read-only Tron balances: TRX and TRC-20 tokens (USDT) for a "T..." address,
// through Tron's Ethereum-compatible JSON-RPC, which takes the address in its
// 20-byte hex form. Same endpoint fallback as the EVM checkers.

#include <string>

#include "../tron.h"
#include "TokenBalance.h"

namespace RTX_BALANCE {

class TronBalanceChecker {
public:
    // TRX balance (6 decimals) as a decimal string, e.g. "125.50".
    std::string getBalance(const std::string& address) {
        return detail::rpcBalance(rtxnet::endpoints::tron(), tron::toEvmHex(address), 6);
    }

    // TRC-20 balance, e.g. getTokenBalance(tokens::tronUSDT(), "T...").
    std::string getTokenBalance(const TokenInfo& token, const std::string& address) {
        return TokenBalanceChecker(token).getBalance(tron::toEvmHex(address));
    }
};

}  // namespace RTX_BALANCE

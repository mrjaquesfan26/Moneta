#pragma once
#include <vector>
#include "wallet/monero_wallet_full.h"
#include "common.h"

bool maybe_run_tui(monero::monero_wallet_full* wallet, bool tui_flag,
                   const std::vector<Contact>& contacts);

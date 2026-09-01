#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "wallet/monero_wallet_full.h"

struct BalanceInfo
{
	uint64_t balance;
	uint64_t unlocked;
};

struct AddressInfo
{
	std::string address;
	std::string label;
};

struct TransferInfo
{
	uint64_t amount;
};

struct TxInfo
{
	std::string hash;
	bool incoming;
	uint64_t fee;
	uint64_t height;
	std::vector<TransferInfo> transfers;
};

struct SendResult
{
	bool success;
	std::string error;
	std::vector<std::string> hashes;
	std::vector<uint64_t> fees;
};

struct Contact
{
	std::string name;
	std::string address;
};

SendResult do_send(monero::monero_wallet_full* wallet, const std::string& address, const std::string& amount, bool relay = true);
double to_monero(uint64_t piconeros);
uint64_t to_piconero(const std::string& xmr_str);
BalanceInfo get_balance(monero::monero_wallet_full* wallet);
std::vector<AddressInfo> get_addresses(monero::monero_wallet_full* wallet, const std::vector<uint32_t>& idx);
std::vector<TxInfo> get_tx_history(monero::monero_wallet_full* wallet, int limit);

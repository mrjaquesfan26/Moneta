#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include "common.h"

#include "tui.h"

#include "wallet/monero_wallet_full.h"
#include "utils/monero_utils.h"

using namespace monero;

static void print_help()
{
	std::cout <<
	"moneta usage:\n"
	"  -w <wallet> <password>     	wallet file\n"
	"  -cw <wallet> <password>    	create wallet\n"
	"  -ca <label>         	  	create subaddress with label\n"
	"  -a <range>          	  	address selection: 1 | 2:4 | 2,4\n"
	"  -la                 	  	list created subaddresses\n"
	"  -b                  	  	show balance\n"
	"  -s <amount> <dest address> 	send/spend monero\n"
	"  -sc <amount> <name>        	send to a saved contact\n"
	"  -lc                 	  	list contacts\n"
	"  -tx <N>             	  	show tx history (0 for all)\n"
	"  -c                  	  	clean output (CSV output)\n"
	"  -d <host:port>      	  	daemon address\n"
	"  -h, --help          	  	show help\n"
	"  -cfg <path>          	  	config file\n"
	"  -tui                	  	launch interactive TUI\n";
}

// Gets balance (unlocked and total) of the wallet
BalanceInfo get_balance(monero_wallet_full* wallet)
{
	BalanceInfo info;
	info.balance = wallet->get_balance();
	info.unlocked = wallet->get_unlocked_balance();
	return info;
}

// Gets the addresses that have been explicitly created for the wallet
std::vector<AddressInfo> get_addresses(monero_wallet_full* wallet, const std::vector<uint32_t>& idx)
{
	std::vector<AddressInfo> result;
	auto subs = wallet->get_subaddresses(0, idx);

	for (const auto& s : subs)
	{
		AddressInfo info;
		info.address = s.m_address ? *s.m_address : "";
		info.label = (s.m_label && !s.m_label->empty()) ? *s.m_label : "-";
		result.push_back(info);
	}

	return result;
}

// Gets transactions for this wallet counting from most recent to limit, unlimited if limit <= 0
std::vector<TxInfo> get_tx_history(monero_wallet_full* wallet, int limit)
{
	std::vector<TxInfo> result;
	auto txs = wallet->get_txs();

	int count = 0;

	for (auto& tx : txs)
	{
		if (limit > 0 && count >= limit)
			break;

		TxInfo info;
		info.hash = tx->m_hash ? *tx->m_hash : "N/A";
		info.fee = tx->m_fee ? *tx->m_fee : 0;
		info.incoming = tx->m_is_incoming ? *tx->m_is_incoming : false;

		info.height = 0;
		auto height_opt = tx->get_height();
		if (height_opt) info.height = *height_opt;

		auto transfers = tx->get_transfers();
		for (auto& t : transfers)
		{
			TransferInfo tr;
			tr.amount = t->m_amount ? *t->m_amount : 0;
			info.transfers.push_back(tr);
		}

		result.push_back(info);
		count++;
	}

	return result;
}

// Converts index for -a (e.g. 2:6={2,3,4,5,6}, 2,4,6={2,4,6} 
static std::vector<uint32_t> parse_indices(const std::string& input)
{
	std::vector<uint32_t> out;

	if (input.find(':') != std::string::npos)
	{
		size_t pos = input.find(':');
		int a = std::stoi(input.substr(0, pos));
		int b = std::stoi(input.substr(pos + 1));

		for (int i = a; i <= b; i++)
			out.push_back(i);
	}
	else if (input.find(',') != std::string::npos)
	{
		std::stringstream ss(input);
		std::string item;

		while (std::getline(ss, item, ','))
			out.push_back(std::stoi(item));
	}
	else
	{
		out.push_back(std::stoi(input));
	}

	return out;
}

// Checks if the file exists & is readable
static bool file_exists(const std::string& path)
{
	std::ifstream f(path);
	return f.good();
}

// Returns correct path or nothing if specified & not existant
static std::string find_config(std::string& config_path)
{
	if (!config_path.empty())
	{
		if (file_exists(config_path))
			return config_path;

		std::cerr << "Specified config file not found or not readable \n";
		return "";
	}

	if (file_exists("moneta.conf"))
		return "moneta.conf";

	const char* home = std::getenv("HOME");
	if (home)
	{
		std::string home_path(home);
		config_path = home_path + "/.config/moneta.conf";
		if (file_exists(config_path))
			return config_path;
		config_path = home_path + "/.config/moneta/moneta.conf";
		if (file_exists(config_path))
			return config_path;
	}
	return "";
}

// Libraries use piconeros, which is monero * 10^12. Using string to prevent floating point errors
uint64_t to_piconero(const std::string& xmr_str)
{
	size_t dot = xmr_str.find('.');

	std::string integer_part;
	std::string decimal_part;

	if (dot == std::string::npos)
	{
		integer_part = xmr_str;
		decimal_part = "";
	}
	else
	{
		integer_part = xmr_str.substr(0, dot);
		decimal_part = xmr_str.substr(dot + 1);
	}

	// Pad or truncate decimal to exactly 12 digits
	if (decimal_part.size() < 12)
		decimal_part.append(12 - decimal_part.size(), '0');
	else
		decimal_part = decimal_part.substr(0, 12);

	return std::stoull(integer_part + decimal_part);
}

// Turns piconeros into monero, usually for display to user
double to_monero(uint64_t piconeros)
{
	return static_cast<double>(piconeros) / 1e12;
}

// Creates a subaddress (any address other than 0, subs always start with 8 rather than 4)
// Label is required because they are usable without being created if one is not needed.
static void create_sub(monero_wallet_full* wallet, const std::string& label)
{
	wallet->create_subaddress(0, label);
	wallet->sync();
	wallet->save();
}


// Formats send result (e.g. tx proof hash)
static void print_send_result(const SendResult& result, bool script)
{
	if (!result.success)
	{
		std::cerr << "Error: " << result.error << "\n";
		return;
	}

	for (size_t i = 0; i < result.hashes.size(); i++)
	{
		if (!script)
		{
			std::cout << "TX: " << result.hashes[i] << "\n";
			std::cout << "Fee: " << to_monero(result.fees[i]) << "\n";
		}
		else
		{
			std::cout << result.hashes[i] << "," << result.fees[i] << "\n";
		}
	}
}


SendResult do_send(monero_wallet_full* wallet, const std::string& address, const std::string& amount, bool relay)
{
    SendResult result;
    result.success = false;
    monero_tx_config config;
    monero_destination dest;
    dest.m_address = address;
    dest.m_amount = to_piconero(amount);
    std::shared_ptr<monero_destination> dest_ptr = std::make_shared<monero_destination>(dest);
    config.m_destinations.push_back(dest_ptr);
    config.m_priority = monero_tx_priority::DEFAULT;
    config.m_relay = relay;
    config.m_account_index = 0;
    try
    {
        auto txs = wallet->create_txs(config);
        for (const auto& tx : txs)
        {
            	//Adds transaction and hash fees, falling back to defaults if not existant
		result.hashes.push_back(tx->m_hash ? *tx->m_hash : "N/A");
            	result.fees.push_back(tx->m_fee ? *tx->m_fee : 0);
        }
        monero_utils::free(txs);
        if (!relay)
            wallet->sync();
        result.success = true;
    }
    catch (const std::exception& e)
    {
        result.error = e.what();
    }
    return result;
}

static void print_balance(const BalanceInfo& info, bool script)
{
	if (script)
	{
		std::cout << info.balance << "," << info.unlocked << "\n";
	}
	else
	{
		std::cout << "Balance: " << to_monero(info.balance) << "\n";
		std::cout << "Unlocked: " << to_monero(info.unlocked) << "\n";
	}
}



static void print_addresses(const std::vector<AddressInfo>& addresses, bool script)
{
	for (const auto& a : addresses)
	{
		if (script)
		{
			std::cout << a.address << "," << a.label << "\n";
		}
		else
		{
			std::cout << a.address << "\n" << a.label << "\n\n";
		}
	}
}


static void print_tx_history(const std::vector<TxInfo>& txs, bool script)
{
	for (const auto& tx : txs)
	{
		if (!script)
		{
			std::cout << "Tx Hash:   " << tx.hash << "\n";
			std::cout << "Direction: " << (tx.incoming ? "IN" : "OUT") << "\n";
			std::cout << "Fee:       " << to_monero(tx.fee) << "\n";
			std::cout << "Height:    " << tx.height << "\n";
		}
		else
		{
			std::cout << tx.hash << "," << tx.incoming << "," << tx.fee << "," << tx.height << ",";
		}

		for (const auto& tr : tx.transfers)
		{
			if (!script)
				std::cout << "\nTransfer: " << to_monero(tr.amount) << "\n\n";
			else
				std::cout << tr.amount << "\n";
		}
	}
}

int main(int argc, char* argv[])
{
	std::cout << std::fixed << std::setprecision(12);
	std::string wallet_file = "";
	std::string password = "";
	std::string dest;
	std::string daemon;
	// Amount as string to prevent floating point errors
	std::string amount;
	bool tui_flag = false;

	bool create_wallet_flag = false;
	bool create_sub_flag = false;
	bool list_all_flag = false;
	bool script_mode = false;
	bool balance_flag = false;
	bool transfer_flag = false;
	bool transfer_contact_flag = false;
	bool list_contacts_flag = false;
	bool tx_flag = false;
	std::string contact_name;
	std::string config_file;

	int tx_limit = 0;

	std::vector<uint32_t> addr_idx;
	std::string ac_label;

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];

		if (arg == "-w" && i + 1 < argc)
		{
			if (argc < i + 2)
			{
				std::cerr << "Enter both a wallet file and password";
			}
			else
			{
				wallet_file = argv[++i];
				password = argv[++i];
			}
		}
		else if (arg == "-cw")
		{
			if (argc < i + 2)
			{
				std::cerr << "Enter both a wallet file and password";
			}
			else
			{
				create_wallet_flag = true;
				wallet_file = argv[++i];
				password = argv[++i];
			}
		}
		else if (arg == "-ca" && i + 1 < argc)
		{
			create_sub_flag = true;
			ac_label = argv[++i];
		}
		else if (arg == "-la")
		{
			list_all_flag = true;
		}
		else if (arg == "-c")
		{
			script_mode = true;
		}
		else if (arg == "-d")
		{
			if (argc > i)
			{
				daemon = argv[++i];
			}
		}
		else if (arg == "-a" && i + 1 < argc)
		{
			addr_idx = parse_indices(argv[++i]);
		}
		else if (arg == "-b")
		{
			balance_flag = true;
		}
		else if (arg == "-s")
		{
			if (i + 2 < argc)
			{
				amount = argv[++i];
				dest = argv[++i];
				transfer_flag=true;
			}
			else std::cerr << "Please enter amount and destination\n";
		}
		else if (arg == "-sc")
		{
			if (i + 2 < argc)
			{
				amount = argv[++i];
				contact_name = argv[++i];
				transfer_contact_flag = true;
			}
			else std::cerr << "Please enter amount and contact name\n";
		}
		else if (arg == "-tx")
		{
			tx_flag = true;
			if (i + 1 < argc)
			{
				std::string next = argv[i + 1];
				// Check that the next is not a flag 
				if (!next.empty() && next[0] != '-')
				{
					try
					{
						tx_limit = std::stoi(next);
						i++; // only advance i if we actually consumed the argument
					}
					catch (const std::exception&)
					{
						tx_limit = 0;
					}
				}
			}
		}
		else if (arg == "-h" || arg == "--help")
		{
			print_help();
			return 0;
		}
		else if (arg == "-cfg")
		{
			if (argc > i)
			{
				config_file = argv[++i];
			}
		}
		else if (arg == "-lc")
		{
			list_contacts_flag = true;
		}
		else if (arg == "-tui")
		{
			tui_flag = true;
		}
	}

	try
	{
		config_file = find_config(config_file);
		monero_wallet_full* wallet = nullptr;
		std::vector<Contact> contacts;

		if (!config_file.empty())
		{
			std::ifstream conf;
			conf.open(config_file);

			if (!conf.is_open())
			{
				std::cerr << "Could not open config file: " << config_file << "\n";
			}
			else
			{
				std::string line;
				while (std::getline(conf, line))
				{
					size_t eq = line.find('=');
					if (eq == std::string::npos)
						continue;

					std::string key = line.substr(0, eq);
					std::string val = line.substr(eq + 1);

					if (key == "wallet" && wallet_file.empty())   wallet_file = val;
					if (key == "password" && password.empty())    password = val;
					if (key == "daemon" && daemon.empty())        daemon = val;

					if (key == "contact")
					{
						size_t sep = val.find(':');
						if (sep != std::string::npos)
						{
							Contact c;
							c.name    = val.substr(0, sep);
							c.address = val.substr(sep + 1);
							if (!c.name.empty() && !c.address.empty())
								contacts.push_back(c);
						}
					}
				}

				conf.close();
			}
		}

		if (password.empty())
			password = "password";
		if (wallet_file.empty())
			wallet_file = "monero_wallet";
		if (daemon.empty())
			daemon = "http://127.0.0.1:18081";

		monero_wallet_config cfg;
		cfg.m_network_type = monero_network_type::MAINNET;
		cfg.m_password = password;
		cfg.m_path = wallet_file;
		cfg.m_server = monero_rpc_connection(daemon);

		if (create_wallet_flag)
			wallet = monero_wallet_full::create_wallet(cfg);
		else
			wallet = monero_wallet_full::open_wallet(wallet_file, password, monero_network_type::MAINNET);

		wallet->set_daemon_connection(cfg.m_server);
		wallet->sync();
		// wallet->rescan_blockchain();
		wallet->save();

		// TUI takes over completely if requested — must be checked before
		// any other flag handling runs, or both would execute.
		if (maybe_run_tui(wallet, tui_flag, contacts))
			return 0;

		if (create_sub_flag)
			create_sub(wallet, ac_label);

		if (list_all_flag)
			print_addresses(get_addresses(wallet, {}), script_mode);

		if (!addr_idx.empty())
			print_addresses(get_addresses(wallet, addr_idx), script_mode);

		if (balance_flag)
			print_balance(get_balance(wallet), script_mode);

		if (tx_flag)
			print_tx_history(get_tx_history(wallet, tx_limit), script_mode);

		if (list_contacts_flag)
		{
			for (const auto& c : contacts)
			{
				if (script_mode)
					std::cout << c.name << "," << c.address << "\n";
				else
					std::cout << c.name << "\n" << c.address << "\n\n";
			}
		}

		if (transfer_flag)
			print_send_result(do_send(wallet, dest, amount), script_mode);

		if (transfer_contact_flag)
		{
			bool found = false;
			for (const auto& c : contacts)
			{
				if (c.name == contact_name)
				{
					print_send_result(do_send(wallet, c.address, amount), script_mode);
					found = true;
					break;
				}
			}
			if (!found)
				std::cerr << "Contact not found: " << contact_name << "\n";
		}

		if (!script_mode)
			std::cout << "\n";

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "\n";
	}

	return 0;
}

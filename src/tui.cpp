#include "tui.h"
#include <iostream>

#ifdef MONETA_TUI

#include <ncurses.h>
#include <vector>
#include <string>
#include "common.h"

enum class TuiScreen
{
	MENU,
	BALANCE,
	ADDRESSES,
	SEND_AMOUNT,
	SEND_CONTACT,
	SEND_ADDRESS,
	SEND_CONFIRM,
	SEND_RESULT
};


enum class ContactPickResult
{
	Pending,     // Still navigating 
	Cancelled,   // q pressed
	ManualEntry, // Manual entry of address
	Selected     // Selected a contact
};

struct ContactPick
{
	ContactPickResult result;
	std::string address; // For if user selected 
};

// Draw Logic
static void draw_menu(int selected)
{
	clear();
	mvprintw(0, 0, "Moneta: press q to quit, arrows to navigate, enter to select");
	const char* items[] = { "Balance", "Addresses", "Send" };
	for (int i = 0; i < 3; i++)
	{
		if (i == selected)
			attron(A_REVERSE);
		mvprintw(2 + i, 2, "%s", items[i]);
		if (i == selected)
			attroff(A_REVERSE);
	}
	refresh();
}

// Shows balance (unlocked and total) of this wallet
static void draw_balance(monero::monero_wallet_full* wallet)
{
	BalanceInfo info = get_balance(wallet);
	clear();
	mvprintw(0, 0, "Balance: press q to go back");
	mvprintw(2, 2, "Balance:  %.12f", to_monero(info.balance));
	mvprintw(3, 2, "Unlocked: %.12f", to_monero(info.unlocked));
	refresh();
}

// Shows addresses of this wallet
static void draw_addresses(monero::monero_wallet_full* wallet)
{
	std::vector<AddressInfo> addresses = get_addresses(wallet, {});
	clear();
	mvprintw(0, 0, "Addresses: press q to go back");
	int row = 2;
	for (const auto& a : addresses)
	{
		mvprintw(row, 2, "%s  [%s]", a.address.c_str(), a.label.c_str());
		row++;
	}
	refresh();
}

// Requests the amount to send
static std::string draw_send_amount(monero::monero_wallet_full* wallet)
{
	BalanceInfo info = get_balance(wallet);
	clear();
	mvprintw(0, 0, "Send XMR: press Ctrl+C to cancel");
	mvprintw(2, 2, "Balance:  %.12f XMR", to_monero(info.balance));
	mvprintw(3, 2, "Unlocked: %.12f XMR", to_monero(info.unlocked));
	mvprintw(5, 2, "Amount to send:");
	refresh();

	echo();
	curs_set(1);
	char buf[64] = {};
	move(5, 18);
	getnstr(buf, sizeof(buf) - 1);
	noecho();
	curs_set(0);
	return std::string(buf);
}

// Choose between contacts or manual entry
static ContactPick draw_send_contact(const std::vector<Contact>& contacts, int& selected)
{
	int total = static_cast<int>(contacts.size()) + 1;
	clear();
	mvprintw(0, 0, "Select recipient: arrows/enter to pick, q to cancel");

	for (int i = 0; i < static_cast<int>(contacts.size()); i++)
	{
		if (i == selected)
			attron(A_REVERSE);
		mvprintw(2 + i, 2, "%-20s  %s",
		         contacts[i].name.c_str(),
		         contacts[i].address.c_str());
		if (i == selected)
			attroff(A_REVERSE);
	}

	int manual_row = 2 + static_cast<int>(contacts.size());
	if (selected == total - 1)
		attron(A_REVERSE);
	mvprintw(manual_row, 2, "[ Enter address manually ]");
	if (selected == total - 1)
		attroff(A_REVERSE);

	refresh();

	int ch = getch();
	if (ch == 'q')
		return { ContactPickResult::Cancelled, "" };

	if (ch == KEY_UP)
		selected = (selected == 0) ? total - 1 : selected - 1;
	else if (ch == KEY_DOWN)
		selected = (selected == total - 1) ? 0 : selected + 1;
	else if (ch == '\n')
	{
		if (selected == total - 1)
			return { ContactPickResult::ManualEntry, "" };
		return { ContactPickResult::Selected, contacts[selected].address };
	}

	return { ContactPickResult::Pending, "" };
}

// Collects address from user
static std::string prompt_address()
{
	clear();
	mvprintw(0, 0, "Enter destination address:");
	refresh();
	echo();
	curs_set(1);
	char buf[256] = {};
	move(2, 2);
	getnstr(buf, sizeof(buf) - 1);
	noecho();
	curs_set(0);
	return std::string(buf);
}

// Checks for confirmation of amount and address
static void draw_send_confirm(const std::string& amount, const std::string& address,
                              const std::vector<Contact>& contacts)
{
	std::string label;
	for (const auto& c : contacts)
	{
		if (c.address == address)
		{
			label = c.name;
			break;
		}
	}

	clear();
	mvprintw(0, 0, "Confirm send: y to confirm, n to cancel");
	mvprintw(2, 2, "Amount:  %s XMR", amount.c_str());
	if (!label.empty())
		mvprintw(3, 2, "To:      %s (%s)", label.c_str(), address.c_str());
	else
		mvprintw(3, 2, "To:      %s", address.c_str());
	refresh();
}

// Provides TXID of transaction 
static void draw_send_result(const SendResult& result)
{
	clear();
	if (!result.success)
	{
		mvprintw(0, 0, "Send failed - press q to return");
		mvprintw(2, 2, "%s", result.error.c_str());
	}
	else
	{
		mvprintw(0, 0, "Send complete - press q to return");
		int row = 2;
		for (size_t i = 0; i < result.hashes.size(); i++)
		{
			mvprintw(row, 2, "TX:  %s", result.hashes[i].c_str());
			row++;
			mvprintw(row, 2, "Fee: %.12f", to_monero(result.fees[i]));
			row += 2;
		}
	}
	refresh();
}

static void run_tui(monero::monero_wallet_full* wallet, const std::vector<Contact>& contacts)
{
	initscr();
	noecho();
	cbreak();
	keypad(stdscr, TRUE);
	curs_set(0);

	TuiScreen screen = TuiScreen::MENU;
	int menu_selected    = 0;
	int contact_selected = 0;
	bool running = true;

	std::string send_amount;
	std::string send_address;
	SendResult  send_result;

	while (running)
	{
		if (screen == TuiScreen::MENU)
		{
			draw_menu(menu_selected);
			int ch = getch();
			if (ch == 'q')
				running = false;
			else if (ch == KEY_UP)
				menu_selected = (menu_selected == 0) ? 2 : menu_selected - 1;
			else if (ch == KEY_DOWN)
				menu_selected = (menu_selected == 2) ? 0 : menu_selected + 1;
			else if (ch == '\n')
			{
				if (menu_selected == 0)      screen = TuiScreen::BALANCE;
				else if (menu_selected == 1) screen = TuiScreen::ADDRESSES;
				else
				{
					send_amount  = "";
					send_address = "";
					screen = TuiScreen::SEND_AMOUNT;
				}
			}
		}
		else if (screen == TuiScreen::BALANCE)
		{
			draw_balance(wallet);
			int ch = getch();
			if (ch == 'q')
				screen = TuiScreen::MENU;
		}
		else if (screen == TuiScreen::ADDRESSES)
		{
			draw_addresses(wallet);
			int ch = getch();
			if (ch == 'q')
				screen = TuiScreen::MENU;
		}
		else if (screen == TuiScreen::SEND_AMOUNT)
		{
			send_amount = draw_send_amount(wallet);
			if (send_amount.empty())
			{
				screen = TuiScreen::MENU;
			}
			else
			{
				contact_selected = 0;
				screen = contacts.empty() ? TuiScreen::SEND_ADDRESS
				                          : TuiScreen::SEND_CONTACT;
			}
		}
		else if (screen == TuiScreen::SEND_CONTACT)
		{
			ContactPick pick = draw_send_contact(contacts, contact_selected);

			if (pick.result == ContactPickResult::Cancelled)
			{
				screen = TuiScreen::MENU;
			}
			else if (pick.result == ContactPickResult::Pending)
			{
				// still navigating — redraw and wait for more input
			}
			else if (pick.result == ContactPickResult::ManualEntry)
			{
				screen = TuiScreen::SEND_ADDRESS;
			}
			else if (pick.result == ContactPickResult::Selected)
			{
				send_address = pick.address;
				screen = TuiScreen::SEND_CONFIRM;
			}
		}
		else if (screen == TuiScreen::SEND_ADDRESS)
		{
			send_address = prompt_address();
			if (send_address.empty())
				screen = TuiScreen::MENU;
			else
				screen = TuiScreen::SEND_CONFIRM;
		}
		else if (screen == TuiScreen::SEND_CONFIRM)
		{
			draw_send_confirm(send_amount, send_address, contacts);
			int ch = getch();
			if (ch == 'y')
			{
				send_result = do_send(wallet, send_address, send_amount);
				screen = TuiScreen::SEND_RESULT;
			}
			else if (ch == 'n')
			{
				screen = TuiScreen::MENU;
			}
		}
		else if (screen == TuiScreen::SEND_RESULT)
		{
			draw_send_result(send_result);
			int ch = getch();
			if (ch == 'q')
				screen = TuiScreen::MENU;
		}
	}

	endwin();
}

bool maybe_run_tui(monero::monero_wallet_full* wallet, bool tui_flag,
                   const std::vector<Contact>& contacts)
{
	if (!tui_flag)
		return false;
	run_tui(wallet, contacts);
	return true;
}

#else

bool maybe_run_tui(monero::monero_wallet_full* /*wallet*/, bool tui_flag,
                   const std::vector<Contact>& /*contacts*/)
{
	if (tui_flag)
		std::cerr << "This build was compiled without TUI support.\n";
	return false;
}

#endif

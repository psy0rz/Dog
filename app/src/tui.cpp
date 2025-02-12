#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctype.h>
#include <thread>
#include <sys/stat.h>

#include <libchess/Position.h>

#include "book.h"
#include "eval.h"
#include "main.h"
#include "max-ascii.h"
#include "nnue.h"
#include "san.h"
#include "search.h"
#include "str.h"
#include "syzygy.h"
#include "tui.h"


void my_printf(const char *const fmt, ...)
{
#if defined(ESP32)
        char *buffer = nullptr;
        va_list ap { };
        va_start(ap, fmt);
        auto buffer_len = vasprintf(&buffer, fmt, ap);
        va_end(ap);

        printf("%s", buffer);
        ESP_ERROR_CHECK(uart_wait_tx_done(uart_num, 100));
        uart_write_bytes(uart_num, buffer, buffer_len);
	if (buffer[buffer_len - 1] == '\n') {
		const char cr = '\r';
		uart_write_bytes(uart_num, &cr, 1);
	}
        free(buffer);
#else
        va_list ap { };
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
#endif
}

uint64_t do_perft(libchess::Position &pos, int depth)
{
	libchess::MoveList move_list = pos.legal_move_list();
	if (depth == 1)
		return move_list.size();

	uint64_t count     = 0;
	for(const libchess::Move & move: move_list) {
		pos.make_move(move);
		count += do_perft(pos, depth - 1);
		pos.unmake_move();
	}

	return count;
}

void perft(libchess::Position &pos, int depth)
{
	my_printf("Perft for fen: %s\n", pos.fen().c_str());

	for(int d=1; d<=depth; d++) {
		uint64_t t_start = esp_timer_get_time();
		uint64_t count   = do_perft(pos, d);
		uint64_t t_end   = esp_timer_get_time();
		double   t_diff  = std::max(uint64_t(1), t_end - t_start) / 1000000.;
		my_printf("%d: %" PRIu64 " (%.3f nps, %.2f seconds)\n", d, count, count / t_diff, t_diff);
	}
}

void display(const libchess::Position & p, const bool large, const bool colors, const std::optional<std::vector<libchess::Move> > & moves, const std::vector<int16_t> & scores)
{
	if (!large) {
		p.display();
		return;
	}

	std::vector<std::string> lines;

	if (colors) {
		std::string line = "\x1b[0m\x1b[43;30m    ";
		for(int x=0; x<8; x++)
			line += "   ";
		line += " \x1b[0m";
		lines.push_back(line);
	}

	for(int y=7; y>=0; y--) {
		std::string line;
		if (colors)
			line = "\x1b[43;30m " + std::to_string(y + 1) + " |";
		else
			line = " " + std::to_string(y + 1) + " |";
		for(int x=0; x<8; x++) {
			const auto piece = p.piece_on(libchess::Square::from(libchess::File(x), libchess::Rank(y)).value());
			if (piece.has_value()) {
				int  c        = piece.value().type().to_char();
				bool is_white = piece.value().color() == libchess::constants::WHITE;
				if (colors) {
					if (is_white)
						line += "\x1b[30;47m ";
					else
						line += "\x1b[40;37m ";
				}
				else {
					line += " ";
				}
				line += char(piece.value().color() == libchess::constants::WHITE ? toupper(c) : c) + std::string(" ");
				if (colors)
					line += "\x1b[43;30m";
			}
			else {
				line += "   ";
			}
		}
		line += " \x1b[0m";
		lines.push_back(line);
	}
	if (colors) {
		std::string line;
		line = "\x1b[43;30m   +";
		for(int x=0; x<8; x++)
			line += "---";
		line += " \x1b[0m";
		lines.push_back(line);
		line = "\x1b[43;30m    ";
		for(int x=0; x<8; x++)
			line += std::string(" ") + char('A' + x) + " ";
		line += " \x1b[0m";
		lines.push_back(line);
	}
	else {
		std::string line = "   +";
		for(int x=0; x<8; x++)
			line += "---";
		lines.push_back(line);
		line = "    ";
		for(int x=0; x<8; x++)
			line += std::string(" ") + char('A' + x) + " ";
		lines.push_back(line);
	}

	if (moves.has_value()) {
		auto & refm  = moves.value();
		size_t nrefm = refm.size();
		size_t skip  = 0;
		size_t half  = (nrefm + 1) / 2;
		if (half > lines.size())
			skip = (half - lines.size()) * 2;
		size_t line_nr = 0;
		for(size_t i=skip; i<nrefm; i += 2, line_nr++) {
			std::string add = "  " + std::to_string(i / 2 + 1) + ". " + refm.at(i + 0).to_str();
			if (nrefm - i >= 2)
				add += " " + refm.at(i + 1).to_str();
			if (add.size())
				add += std::string(17 - add.size(), ' ');
			lines.at(line_nr) += add;
		}
		while(line_nr < lines.size())
			lines.at(line_nr++) += std::string(17, ' ');
	}

	// scores
	int16_t max = -32768;
	int16_t min =  32767;
	for(auto & v: scores) {
		max = std::max(max, v);
		min = std::min(min, v);
	}

	if (max != min) {
		constexpr int width  = 25;
		constexpr int height = 8;
		char matrix[height][width] { };
		memset(matrix, ' ', sizeof matrix);

		int    extent   = max - min;
		size_t n_values = scores.size();
		size_t start    = 0;
		if (n_values > width)
			start = n_values - width;
		for(size_t idx=start; idx<n_values; idx++) {
			int y = (scores[idx] - min) * (height - 1) / extent;
			int x = idx - start;
			matrix[y][x] = '+';
		}
		for(int y=0; y<height; y++)
			lines.at(y) += std::string(matrix[y], width);
	}

	for(auto & line: lines)
		my_printf("%s\n", line.c_str());

	if (p.game_state() != libchess::Position::GameState::IN_PROGRESS)
		my_printf("Game is finished\n");
	else
		my_printf("Move number: %d, color: %s\n", p.fullmoves(), p.side_to_move() == libchess::constants::WHITE ? "white":"black");
}

void emit_pv(const libchess::Position & pos, const libchess::Move & best_move, const bool colors)
{
	std::vector<libchess::Move> pv = get_pv_from_tt(pos, best_move);
	auto start_color = pos.side_to_move();
	auto start_score = nnue_evaluate(pos);

	if (colors) {
		my_printf("\x1b[43;30mPV[%.2f]:\x1b[0m\n    ", start_score / 100.);

		int prev_score = start_score;
		int nr         = 0;
		libchess::Position work(sp.at(0)->pos);
		for(auto & move: pv) {
			if (++nr == 6)
				my_printf("\n    "), nr = 0;
			my_printf(" ");

			work.make_move(move);
			auto cur_color = work.side_to_move();
			int  cur_score = nnue_evaluate(pos);

			if ((start_color == cur_color && cur_score < start_score) || (start_color != cur_color && cur_score > start_score))
				my_printf("\x1b[40;31m%s\x1b[0m", move.to_str().c_str());
			else if (start_score == cur_score)
				my_printf("%s", move.to_str().c_str());
			else
				my_printf("\x1b[40;32m%s\x1b[0m", move.to_str().c_str());
			if (cur_score > prev_score)
				my_printf("\x1b[40;32m▲\x1b[0m");
			else if (cur_score < prev_score)
				my_printf("\x1b[40;31m▼\x1b[0m");
			else
				my_printf("-");
			prev_score = cur_score;
			if (work.side_to_move() != start_color)
				my_printf(" [%.2f] ", -cur_score / 100.);
			else
				my_printf(" [%.2f] ", cur_score / 100.);
		}
	}
	else {
		my_printf("PV:");
		for(auto & move: pv)
			my_printf(" %s", move.to_str().c_str());
	}
}

void show_movelist(const libchess::Position & pos)
{
	bool first = true;
	auto moves = pos.legal_move_list();
	for(auto & move: moves) {
		if (first)
			first = false;
		else
			my_printf(" ");
		my_printf("%s", move.to_str().c_str());
	}
	my_printf("\n");
}

void tt_lookup()
{
	auto te = tti.lookup(sp.at(0)->pos.hash());
	if (te.has_value() == false)
		my_printf("None\n");
	else {
		const char *const flag_names[] = { "invalid", "exact", "lowerbound", "upperbound" };
		my_printf("Score: %.2f (%s)\n", te.value().score / 100., flag_names[te.value().flags]);
		my_printf("Depth: %d\n", te.value().depth);
		std::optional<libchess::Move> tt_move;
		if (te.value().m)
			tt_move = libchess::Move(te.value().m);
		if (tt_move.has_value() && sp.at(0)->pos.is_legal_move(tt_move.value()))
			my_printf("Move: %s\n", tt_move.value().to_str().c_str());
	}
}

void do_syzygy(const libchess::Position & pos)
{
#if defined(linux) || defined(_WIN32) || defined(__ANDROID__) || defined(__APPLE__)
	if (with_syzygy) {
		std::optional<std::pair<libchess::Move, int> > s_root = probe_fathom_root(pos);
		if (s_root.has_value())
			my_printf("Syzygy move + score for current position: %.2f for %s\n", s_root.value().second / 100., s_root.value().first.to_str().c_str());
		else
			my_printf("No Syzygy move + score for current position.\n");

		std::optional<int> s_nonroot = probe_fathom_nonroot(pos);
		if (s_nonroot.has_value())
			my_printf("Syzygy score for current position: %.2f\n", s_nonroot.value() / 100.);
		else
			my_printf("No Syzygy score for current position.\n");
	}
	else
#endif
	{
		my_printf("No syzygy available\n");
	}
}

bool colors        = false;
bool default_trace = false;
int  think_time    = 1000;  // milliseconds
bool do_ponder     = false;

std::optional<std::string> get_cfg_dir()
{
	const char *home = std::getenv("HOME");
	if (!home) {
		fprintf(stderr, "$HOME not found\n");
		return { };
	}

	return std::string(home) + "/.config/Dog";
}

void write_settings()
{
#if defined(ESP32)
	FILE *fh = fopen("/spiffs/settings.dat", "w");
#else
	auto home = get_cfg_dir();
	if (home.has_value() == false)
		return;
#if defined(_WIN32)
	mkdir(home.value().c_str());
#else
	mkdir(home.value().c_str(), 0700);
#endif

	FILE *fh = fopen((home.value() + "/settings.dat").c_str(), "w");
#endif
	if (!fh) {
		fprintf(stderr, "Cannot write settings: %s\n", strerror(errno));
		return;
	}

	fprintf(fh, "%d\n", colors);
	fprintf(fh, "%d\n", default_trace);
	fprintf(fh, "%d\n", think_time);
	fprintf(fh, "%d\n", do_ponder);

	fclose(fh);
}

void load_settings()
{
#if defined(ESP32)
	FILE *fh = fopen("/spiffs/settings.dat", "r");
#else
	auto home = get_cfg_dir();
	if (home.has_value() == false)
		return;
	FILE *fh = fopen((home.value() + "/settings.dat").c_str(), "r");
#endif
	if (!fh)
		return;

	char buffer[16] { };
	fgets(buffer, sizeof buffer, fh);
	colors        = atoi(buffer);
	fgets(buffer, sizeof buffer, fh);
	default_trace = atoi(buffer);
	fgets(buffer, sizeof buffer, fh);
	think_time    = atoi(buffer);
	fgets(buffer, sizeof buffer, fh);
	do_ponder     = atoi(buffer);

	fclose(fh);
}

static void help()
{
	my_printf("quit    stop the tui\n");
	my_printf("new     restart game\n");
	my_printf("player  select player (\"white\" or \"black\")\n");
	my_printf("time    set think time, in seconds\n");
	my_printf("fen     show fen for current position\n");
	my_printf("setfen  set fen\n");
	my_printf("eval    show current evaluation score\n");
	my_printf("moves   show valid moves\n");
	my_printf("syzygy  probe the syzygy ETB\n");
	my_printf("tt      show TT entry for current position\n");
	my_printf("undo    take back last move\n");
	my_printf("auto    auto play until the end\n");
	my_printf("ponder  on/off\n");
	my_printf("trace   on/off\n");
	my_printf("colors  on/off\n");
	my_printf("perft   run \"perft\" for the given depth\n");
	my_printf("...or enter a move (SAN/LAN)\n");
}

void tui()
{
	set_thread_name("TUI");

	load_settings();

	stop_ponder();

	std::optional<libchess::Color> player = sp.at(0)->pos.side_to_move();

	trace_enabled = default_trace;
	i.set_local_echo(true);
	
	std::vector<int16_t>        scores;
	std::vector<libchess::Move> moves_played;

#if defined(ESP32)
	auto *pb = new polyglot_book("/spiffs/dog-book.bin");
#else
	auto *pb = new polyglot_book("dog-book.bin");
#endif

	for(;;) {
		display(sp.at(0)->pos, true, colors, moves_played, scores);

		bool finished = sp.at(0)->pos.game_state() != libchess::Position::GameState::IN_PROGRESS;
		if ((player.has_value() && player.value() == sp.at(0)->pos.side_to_move()) || finished) {
			if (do_ponder)
				start_ponder();

			std::string line;
			my_printf("> ");
			if (!std::getline(is, line))
				break;
			if (line.empty())
				continue;

			stop_ponder();

			auto parts = split(line, " ");
			if (parts[0] == "help")
				help();
			else if (parts[0] == "quit")
				break;
			else if (parts[0] == "auto")
				player.reset();
			else if (parts[0] == "fen")
				my_printf("FEN: %s\n", sp.at(0)->pos.fen().c_str());
			else if (parts[0] == "setfen") {
				if (parts.size() == 6 + 1)
					sp.at(0)->pos = libchess::Position(parts[1] + " " + parts[2] + " " + parts[3] + " " + parts[4] + " " + parts[5] + " " + parts[6]);
				else
					my_printf("Invalid FEN\n");
			}
			else if (parts[0] == "hash")
				my_printf("Polyglot Zobrist hash: %" PRIx64 "\n", sp.at(0)->pos.hash());
			else if (parts[0] == "perft" && parts.size() == 2)
				perft(sp.at(0)->pos, std::stoi(parts.at(1)));
			else if (parts[0] == "new") {
				stop_ponder();
				memset(sp.at(0)->history, 0x00, history_malloc_size);
				tti.reset();
				sp.at(0)->pos = libchess::Position(libchess::constants::STARTPOS_FEN);
				moves_played.clear();
				scores.clear();
			}
			else if (parts[0] == "player" && parts.size() == 2) {
				if (parts[1] == "white" || parts[1] == "w")
					player = libchess::constants::WHITE;
				else
					player = libchess::constants::BLACK;
			}
			else if (parts[0] == "time" && parts.size() == 2) {
				think_time = std::stod(parts[1]) * 1000;
				write_settings();
			}
			else if (parts[0] == "moves")
				show_movelist(sp.at(0)->pos);
			else if (parts[0] == "syzygy")
				do_syzygy(sp.at(0)->pos);
			else if (parts[0] == "trace") {
				if (parts.size() == 2)
					trace_enabled = parts[1] == "on";
				else
					trace_enabled = !trace_enabled;
				default_trace = trace_enabled;
				write_settings();
				my_printf("Tracing is now %senabled\n", trace_enabled ? "":"not ");
			}
			else if (parts[0] == "colors") {
				if (parts.size() == 2)
					colors = parts[1] == "on";
				else
					colors = !colors;
				write_settings();
				my_printf("Colors are now %senabled\n", colors ? "":"not ");
			}
			else if (parts[0] == "ponder") {
				if (parts.size() == 2)
					do_ponder = parts[1] == "on";
				else
					do_ponder = !do_ponder;
				write_settings();
				my_printf("Pondering is now %senabled\n", do_ponder ? "":"not ");
				if (!do_ponder)
					stop_ponder();
			}
			else if (parts[0] == "undo") {
				stop_ponder();
				sp.at(0)->pos.unmake_move();
				player = sp.at(0)->pos.side_to_move();
				moves_played.pop_back();
				scores.pop_back();
			}
			else if (parts[0] == "eval") {
				int nnue_score = nnue_evaluate(sp.at(0)->pos);
				my_printf("evaluation score: %.2f\n", nnue_score / 100.);
			}
			else if (parts[0] == "tt")
				tt_lookup();
			else if (parts[0] == "dog")
				print_max_ascii();
			else {
				bool valid = false;
				std::optional<libchess::Move> move;
				move = libchess::Move::from(parts[0]);
				if (move.has_value() == false)
					move = SAN_to_move(parts[0], sp.at(0)->pos);
				if (move.has_value() == true) {
					if (sp.at(0)->pos.is_legal_move(move.value())) {
						sp.at(0)->pos.make_move(move.value());
						moves_played.push_back(move.value());
						scores.push_back(nnue_evaluate(sp.at(0)->pos));
						valid = true;
					}
				}
				if (!valid)
					my_printf("Not a valid move nor command (enter \"help\" for command list)\n");
			}
		}
		else {
#if !defined(linux) && !defined(_WIN32) && !defined(__ANDROID__) && !defined(__APPLE__)
			stop_blink(led_red_timer, &led_red);
			start_blink(led_green_timer);
#endif

			my_printf("Color: %s\n", sp.at(0)->pos.side_to_move() == libchess::constants::WHITE ? "white":"black");

			auto move = pb->query(sp.at(0)->pos);
			if (move.has_value()) {
				my_printf("Book move: %s\n", move.value().to_str().c_str());

				sp.at(0)->pos.make_move(move.value());
				moves_played.push_back(move.value());
				scores.push_back(nnue_evaluate(sp.at(0)->pos));
			}
			else {
				my_printf("Thinking... (%.3f seconds)\n", think_time / 1000.);
				libchess::Move best_move  { 0 };
				int            best_score { 0 };
				clear_flag(sp.at(0)->stop);
				std::tie(best_move, best_score) = search_it(think_time, true, sp.at(0), -1, { }, true);
				my_printf("Selected move: %s (score: %.2f)\n", best_move.to_str().c_str(), best_score / 100.);
				emit_pv(sp.at(0)->pos, best_move, colors);

				sp.at(0)->pos.make_move(best_move);
				moves_played.push_back(best_move);
				scores.push_back(best_score);
			}

			my_printf("\n");

#if !defined(linux) && !defined(_WIN32) && !defined(__APPLE__)
			stop_blink(led_green_timer, &led_green);
#endif
		}
	}

	delete pb;

	i.set_local_echo(false);
	trace_enabled = true;
}

void run_tui()
{
	// because of ESP32 stack
	auto th = new std::thread{tui};
	th->join();
	delete th;
}
